#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

struct LocationConfig {
    std::string path_prefix;
    bool is_proxy = false;

    std::string proxy_pass_host;
    int proxy_pass_port = 0;
    std::string proxy_set_header_host;

    std::string root;
    std::string index = "index.html";
};

struct ServerConfig {
    int listen = 8080;
    std::string server_name = "localhost";
    std::vector<LocationConfig> locations;
};

struct GlobalConfig {
    int worker_processes = 1; // auto -> hardware_concurrency
    std::vector<ServerConfig> servers;
};

class ConfigParser {
public:
    bool parseFile(const std::string& path, GlobalConfig& out_config, std::string& err_msg);

private:
    std::string trim(const std::string& s) const;
    std::vector<std::string> tokenize(const std::string& content) const;
    bool parseProxyPass(const std::string& value, std::string& host, int& port) const;
};

#endif