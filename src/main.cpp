#include <iostream>
#include "net/Server.h"

int main(int argc, char* argv[]) {
    std::string config_path = "./conf/mininginx.conf";
    if (argc > 1) {
        config_path = argv[1];
    }

    std::cout << "Starting MiniNginx..." << std::endl;
    std::cout << "Using config: " << config_path << std::endl;

    Server server(config_path);

    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    return 0;
}
