#include "config/Config.h"

#include <fstream>
#include <sstream>
#include <cctype>

std::string ConfigParser::trim(const std::string& s) const {
    size_t l = 0;
    while (l < s.size() && std::isspace(static_cast<unsigned char>(s[l]))) {
        ++l;
    }
    size_t r = s.size();
    while (r > l && std::isspace(static_cast<unsigned char>(s[r - 1]))) {
        --r;
    }
    return s.substr(l, r - l);
}

std::vector<std::string> ConfigParser::tokenize(const std::string& content) const {
    std::vector<std::string> tokens;
    std::string current;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];

        if (c == '#') {
            while (i < content.size() && content[i] != '\n') {
                ++i;
            }
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        if (c == '{' || c == '}' || c == ';') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.emplace_back(1, c);
            continue;
        }

        current.push_back(c);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

bool ConfigParser::parseProxyPass(const std::string& value, std::string& host, int& port) const {
    // supported: http://127.0.0.1:7000
    const std::string prefix = "http://";
    if (value.rfind(prefix, 0) != 0) {
        return false;
    }

    std::string host_port = value.substr(prefix.size());
    size_t colon = host_port.rfind(':');
    if (colon == std::string::npos) {
        return false;
    }

    host = host_port.substr(0, colon);
    std::string port_str = host_port.substr(colon + 1);
    if (host.empty() || port_str.empty()) {
        return false;
    }

    try {
        port = std::stoi(port_str);
    } catch (...) {
        return false;
    }

    return port > 0 && port <= 65535;
}

bool ConfigParser::parseFile(const std::string& path, GlobalConfig& out_config, std::string& err_msg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        err_msg = "cannot open config file: " + path;
        return false;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    std::vector<std::string> tk = tokenize(oss.str());

    out_config = GlobalConfig{};

    size_t i = 0;

    auto requireToken = [&](const std::string& expected) -> bool {
        if (i >= tk.size() || tk[i] != expected) {
            err_msg = "expected token '" + expected + "'";
            return false;
        }
        ++i;
        return true;
    };

    while (i < tk.size()) {
        if (tk[i] == "worker_processes") {
            ++i;
            if (i >= tk.size()) {
                err_msg = "worker_processes needs a value";
                return false;
            }

            if (tk[i] == "auto") {
                out_config.worker_processes = 0;
                ++i;
            } else {
                try {
                    out_config.worker_processes = std::stoi(tk[i]);
                } catch (...) {
                    err_msg = "invalid worker_processes";
                    return false;
                }
                ++i;
            }

            if (!requireToken(";")) return false;
            continue;
        }

        if (tk[i] == "http") {
            ++i;
            if (!requireToken("{")) return false;

            while (i < tk.size() && tk[i] != "}") {
                if (tk[i] != "server") {
                    err_msg = "only 'server' allowed in http block";
                    return false;
                }
                ++i;
                if (!requireToken("{")) return false;

                ServerConfig server;

                while (i < tk.size() && tk[i] != "}") {
                    if (tk[i] == "listen") {
                        ++i;
                        if (i >= tk.size()) {
                            err_msg = "listen needs value";
                            return false;
                        }
                        try {
                            server.listen = std::stoi(tk[i]);
                        } catch (...) {
                            err_msg = "invalid listen";
                            return false;
                        }
                        ++i;
                        if (!requireToken(";")) return false;
                        continue;
                    }

                    if (tk[i] == "server_name") {
                        ++i;
                        if (i >= tk.size()) {
                            err_msg = "server_name needs value";
                            return false;
                        }
                        server.server_name = tk[i++];
                        if (!requireToken(";")) return false;
                        continue;
                    }

                    if (tk[i] == "location") {
                        ++i;

                        if (i >= tk.size()) {
                            err_msg = "location needs match mode or path";
                            return false;
                        }

                        // allow optional '^~'
                        if (tk[i] == "^~") {
                            ++i;
                        }

                        if (i >= tk.size()) {
                            err_msg = "location needs path prefix";
                            return false;
                        }

                        LocationConfig loc;
                        loc.path_prefix = tk[i++];

                        if (!requireToken("{")) return false;

                        while (i < tk.size() && tk[i] != "}") {
                            if (tk[i] == "proxy_set_header") {
                                ++i;
                                if (i + 1 >= tk.size()) {
                                    err_msg = "proxy_set_header needs key and value";
                                    return false;
                                }
                                std::string key = tk[i++];
                                std::string val = tk[i++];
                                if (key == "Host") {
                                    loc.proxy_set_header_host = val;
                                }
                                if (!requireToken(";")) return false;
                                continue;
                            }

                            if (tk[i] == "proxy_pass") {
                                ++i;
                                if (i >= tk.size()) {
                                    err_msg = "proxy_pass needs value";
                                    return false;
                                }
                                if (!parseProxyPass(tk[i], loc.proxy_pass_host, loc.proxy_pass_port)) {
                                    err_msg = "invalid proxy_pass format";
                                    return false;
                                }
                                loc.is_proxy = true;
                                ++i;
                                if (!requireToken(";")) return false;
                                continue;
                            }

                            if (tk[i] == "root") {
                                ++i;
                                if (i >= tk.size()) {
                                    err_msg = "root needs value";
                                    return false;
                                }
                                loc.root = tk[i++];
                                if (!requireToken(";")) return false;
                                continue;
                            }

                            if (tk[i] == "index") {
                                ++i;
                                if (i >= tk.size()) {
                                    err_msg = "index needs value";
                                    return false;
                                }
                                loc.index = tk[i++];
                                if (!requireToken(";")) return false;
                                continue;
                            }

                            err_msg = "unknown directive in location: " + tk[i];
                            return false;
                        }

                        if (!requireToken("}")) return false;
                        server.locations.push_back(loc);
                        continue;
                    }

                    // ignore logs and other directives
                    if (tk[i] == "error_log" || tk[i] == "access_log") {
                        ++i;
                        if (i >= tk.size()) {
                            err_msg = "log directive needs value";
                            return false;
                        }
                        ++i;
                        if (!requireToken(";")) return false;
                        continue;
                    }

                    err_msg = "unknown server directive: " + tk[i];
                    return false;
                }

                if (!requireToken("}")) return false;
                out_config.servers.push_back(server);
            }

            if (!requireToken("}")) return false;
            continue;
        }

        err_msg = "unknown top-level token: " + tk[i];
        return false;
    }

    if (out_config.servers.empty()) {
        err_msg = "no server block configured";
        return false;
    }

    return true;
}
