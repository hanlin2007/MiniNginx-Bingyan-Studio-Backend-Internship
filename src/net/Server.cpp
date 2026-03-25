#include "net/Server.h"

#include "handlers/StaticHandler.h"
#include "handlers/ProxyHandler.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

Server::Server(const std::string& config_path) : config_path_(config_path) {}

Server::~Server() {
    stop();
}

bool Server::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void Server::closeFd(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

bool Server::loadConfig() {
    ConfigParser parser;
    std::string err;
    GlobalConfig parsed;

    if (!parser.parseFile(config_path_, parsed, err)) {
        std::cerr << "Config parse failed: " << err << std::endl;
        return false;
    }

    if (parsed.servers.empty()) {
        std::cerr << "Config has no server block" << std::endl;
        return false;
    }

    listen_port_ = parsed.servers.front().listen;
    config_ = parsed;

    try {
        config_last_write_time_ = std::filesystem::last_write_time(config_path_);
    } catch (...) {
        // ignore
    }

    std::cout << "Config loaded. listen=" << listen_port_
              << ", worker_processes=" << config_.worker_processes
              << ", servers=" << config_.servers.size() << std::endl;
    return true;
}

bool Server::initializeSocket() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket, errno=" << errno << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (!setNonBlocking(server_fd_)) {
        std::cerr << "Failed to set non-blocking listen socket" << std::endl;
        closeFd(server_fd_);
        server_fd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(listen_port_));

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Bind failed on port " << listen_port_ << ", errno=" << errno << std::endl;
        closeFd(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (listen(server_fd_, 512) < 0) {
        std::cerr << "Listen failed, errno=" << errno << std::endl;
        closeFd(server_fd_);
        server_fd_ = -1;
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::cerr << "epoll_create1 failed, errno=" << errno << std::endl;
        closeFd(server_fd_);
        server_fd_ = -1;
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev) < 0) {
        std::cerr << "epoll_ctl add listen fd failed, errno=" << errno << std::endl;
        closeFd(epoll_fd_);
        epoll_fd_ = -1;
        closeFd(server_fd_);
        server_fd_ = -1;
        return false;
    }

    return true;
}

bool Server::start() {
    if (!loadConfig()) {
        return false;
    }

    if (!initializeSocket()) {
        return false;
    }

    running_ = true;
    last_reload_check_ = std::chrono::steady_clock::now();

    std::cout << "==========================================" << std::endl;
    std::cout << "MiniNginx running on http://0.0.0.0:" << listen_port_ << std::endl;
    std::cout << "Config file: " << config_path_ << std::endl;
    std::cout << "Linux mode: epoll-based IO multiplexing" << std::endl;
    std::cout << "==========================================" << std::endl;

    eventLoop();
    return true;
}

void Server::stop() {
    if (!running_ && server_fd_ < 0 && epoll_fd_ < 0) {
        return;
    }

    running_ = false;

    for (auto& kv : clients_) {
        closeFd(kv.first);
    }
    clients_.clear();

    closeFd(server_fd_);
    server_fd_ = -1;

    closeFd(epoll_fd_);
    epoll_fd_ = -1;
}

void Server::eventLoop() {
    const int MAX_EVENTS = 256;
    epoll_event events[MAX_EVENTS];

    while (running_) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "epoll_wait failed, errno=" << errno << std::endl;
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == server_fd_) {
                acceptClient();
                continue;
            }

            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                closeFd(fd);
                clients_.erase(fd);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                handleClientReadable(fd);
            }
        }

        maybeReloadConfig();
    }
}

void Server::acceptClient() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);

        int client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cerr << "accept failed, errno=" << errno << std::endl;
            break;
        }

        if (!setNonBlocking(client_fd)) {
            closeFd(client_fd);
            continue;
        }

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            closeFd(client_fd);
            continue;
        }

        clients_[client_fd] = ClientState{};
    }
}

void Server::handleClientReadable(int client_fd) {
    char buffer[4096];

    while (true) {
        int n = recv(client_fd, buffer, sizeof(buffer), 0);

        if (n == 0) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
            closeFd(client_fd);
            clients_.erase(client_fd);
            return;
        }

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
            closeFd(client_fd);
            clients_.erase(client_fd);
            return;
        }

        clients_[client_fd].recv_buffer.append(buffer, n);

        // extremely simple HTTP framing: parse when header finished
        if (clients_[client_fd].recv_buffer.find("\r\n\r\n") != std::string::npos) {
            Request req;
            if (!req.parse(clients_[client_fd].recv_buffer)) {
                sendResponseAndClose(client_fd, Response::error(400, "Bad Request"));
                clients_.erase(client_fd);
                return;
            }

            Response resp = routeRequest(req);
            sendResponseAndClose(client_fd, resp);
            clients_.erase(client_fd);
            return;
        }
    }
}

Response Server::routeRequest(const Request& request) {
    std::string host = request.getHeader("Host");
    if (host.empty()) {
        host = "localhost";
    }

    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }

    const ServerConfig* server = nullptr;
    for (const auto& s : config_.servers) {
        if (s.server_name == host || s.server_name == "_") {
            server = &s;
            break;
        }
    }
    if (!server) {
        server = &config_.servers.front();
    }

    const LocationConfig* best = nullptr;
    size_t best_len = 0;
    for (const auto& loc : server->locations) {
        const std::string& prefix = loc.path_prefix;
        bool matched = false;

        if (request.path.rfind(prefix, 0) == 0) {
            matched = true;
        } else if (!prefix.empty() && prefix.back() == '/' && request.path == prefix.substr(0, prefix.size() - 1)) {
            // allow '/static' to match location '/static/'
            matched = true;
        }

        if (matched && prefix.size() > best_len) {
            best = &loc;
            best_len = prefix.size();
        }
    }

    if (!best) {
        if (request.path == "/") {
            std::string body =
                "<!DOCTYPE html>"
                "<html lang='zh-CN'>"
                "<head>"
                "<meta charset='UTF-8'/>"
                "<meta name='viewport' content='width=device-width,initial-scale=1.0'/>"
                "<title>MiniNginx Home</title>"
                "<style>"
                ":root{--text:#2b2b2b;--muted:#5d5d5d;--card:#ffffffcc;--line:#ffffff88;--btn:#2f2f2f;--btnText:#fff;}"
                "*{box-sizing:border-box}"
                "body{margin:0;min-height:100vh;font-family:Inter,Segoe UI,Microsoft YaHei,sans-serif;color:var(--text);"
                "background:radial-gradient(1200px 800px at 10% 20%,#ff86b699,transparent 55%),"
                "radial-gradient(1200px 700px at 50% -10%,#ffe28aaa,transparent 50%),"
                "radial-gradient(1000px 700px at 95% 25%,#c9b8ffaa,transparent 55%),"
                "radial-gradient(1100px 900px at 85% 90%,#b7d4ff99,transparent 60%),"
                "linear-gradient(130deg,#ffd6e7,#ffe8b8,#e8d4ff,#cfe1ff);"
                "display:flex;align-items:center;justify-content:center;padding:24px;}"
                ".card{width:min(760px,92vw);background:var(--card);border:1px solid var(--line);border-radius:28px;padding:30px;backdrop-filter:blur(10px);box-shadow:0 20px 60px #00000020;}"
                "h1{margin:0;font-size:48px;line-height:1.1;}"
                "p{margin:10px 0 0;color:var(--muted);font-size:24px;}"
                ".actions{display:flex;gap:12px;flex-wrap:wrap;margin-top:22px;}"
                "a{display:inline-block;padding:10px 16px;border-radius:999px;text-decoration:none;font-weight:600;transition:all .2s ease;}"
                ".primary{background:#ffffffc2;color:#2f2f2f;border:1px solid #ffffff;}"
                ".primary:hover{background:#141414;color:#fff;border-color:#141414;}"
                ".ghost{background:#ffffffa0;color:#2f2f2f;border:1px solid #ffffff;}"
                ".ghost:hover{background:#1e1e1e;color:#ffffff;border-color:#1e1e1e;}"
                "</style>"
                "</head>"
                "<body>"
                "<section class='card'>"
                "<h1>MiniNginx</h1>"
                "<p>一个基于 C++ 与 socket 的轻量级反向代理和静态资源服务器</p>"
                "<div class='actions'>"
                "<a class='primary' href='/static/index.html'>查看完整欢迎页</a>"
                "<a class='ghost' href='/static/images/test.txt'>静态文件测试</a>"
                "<a class='ghost' href='/api/v1/healthy'>API 代理测试</a>"
                "</div>"
                "</section>"
                "</body></html>";
            return Response::success(body, "text/html");
        }
        return Response::error(404, "No location matched");
    }

    if (best->is_proxy) {
        std::string host_header = best->proxy_set_header_host;
        if (host_header == "$host") {
            host_header = request.getHeader("Host");
        }
        ProxyHandler proxy(best->proxy_pass_host, best->proxy_pass_port, host_header);
        return proxy.handle(request);
    }

    if (!best->root.empty()) {
        std::filesystem::path effective_root = best->root;
        if (effective_root.is_relative()) {
            const std::filesystem::path cwd = std::filesystem::current_path();
            const std::filesystem::path conf_dir = std::filesystem::path(config_path_).parent_path();

            std::vector<std::filesystem::path> candidates = {
                (cwd / effective_root).lexically_normal(),
                (conf_dir / effective_root).lexically_normal(),
                (conf_dir / ".." / effective_root).lexically_normal()
            };

            for (const auto& p : candidates) {
                if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                    effective_root = p;
                    break;
                }
            }

            if (effective_root.is_relative()) {
                effective_root = (cwd / effective_root).lexically_normal();
            }
        }

        if (request.path == best->path_prefix || request.path == best->path_prefix + "/") {
            Request rewritten = request;
            std::string slash = (best->path_prefix.empty() || best->path_prefix.back() == '/') ? "" : "/";
            rewritten.path = best->path_prefix + slash + best->index;
            StaticHandler st(effective_root.string());
            return st.handle(rewritten);
        }

        StaticHandler st(effective_root.string());
        return st.handle(request);
    }

    return Response::error(404, "Invalid location config");
}

void Server::sendResponseAndClose(int client_fd, const Response& response) {
    std::string out = response.build();
    size_t sent = 0;

    while (sent < out.size()) {
        ssize_t n = send(client_fd, out.data() + sent, out.size() - sent, 0);
        if (n <= 0) {
            break;
        }
        sent += static_cast<size_t>(n);
    }

    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    closeFd(client_fd);
}

void Server::maybeReloadConfig() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_reload_check_ < std::chrono::seconds(1)) {
        return;
    }
    last_reload_check_ = now;

    try {
        auto current = std::filesystem::last_write_time(config_path_);
        if (current != config_last_write_time_) {
            std::cout << "Config changed, reloading..." << std::endl;
            GlobalConfig new_cfg;
            std::string err;
            ConfigParser parser;
            if (parser.parseFile(config_path_, new_cfg, err) && !new_cfg.servers.empty()) {
                config_ = new_cfg;
                config_last_write_time_ = current;
                std::cout << "Config reload success" << std::endl;
            } else {
                std::cerr << "Config reload failed: " << err << std::endl;
            }
        }
    } catch (...) {
        // ignore filesystem exceptions
    }
}
