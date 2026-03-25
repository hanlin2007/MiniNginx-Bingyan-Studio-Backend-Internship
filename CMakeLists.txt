cmake_minimum_required(VERSION 3.10)
project(MiniNginx)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -pedantic)

include_directories(include)

set(SOURCES
    src/main.cpp
    src/net/Server.cpp
    src/http/Request.cpp
    src/http/Response.cpp
    src/handlers/StaticHandler.cpp
    src/handlers/ProxyHandler.cpp
    src/config/Config.cpp
)

add_executable(MiniNginx ${SOURCES})

set_target_properties(MiniNginx PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
