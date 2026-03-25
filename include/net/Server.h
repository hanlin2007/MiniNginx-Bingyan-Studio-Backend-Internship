#ifndef SERVER_H
#define SERVER_H

#include "http/Request.h"
#include "http/Response.h"
#include "config/Config.h"

#include <string>
#include <filesystem>
#include <unordered_map>
#include <chrono>

class Server {
public:
    explicit Server(const std::string& config_path);
    ~Server();

    bool start();
    void stop();
    bool isRunning() const { return running_; }

private:
    struct ClientState {
        std::string recv_buffer;
    };

    std::string config_path_;
    GlobalConfig config_;

    int listen_port_ = 8080;
    int server_fd_ = -1;
    int epoll_fd_ = -1;
    bool running_ = false;

    std::unordered_map<int, ClientState> clients_;

    std::filesystem::file_time_type config_last_write_time_{};
    std::chrono::steady_clock::time_point last_reload_check_{};

    bool loadConfig();
    bool initializeSocket();

    void eventLoop();
    void acceptClient();
    void handleClientReadable(int client_fd);

    Response routeRequest(const Request& request);
    void sendResponseAndClose(int client_fd, const Response& response);

    void maybeReloadConfig();

    static bool setNonBlocking(int fd);
    static void closeFd(int fd);
};

#endif
