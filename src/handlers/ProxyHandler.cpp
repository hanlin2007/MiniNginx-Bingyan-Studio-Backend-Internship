#include "handlers/ProxyHandler.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <sstream>
#include <iostream>

Response ProxyHandler::handle(const Request& request) {
    std::string forward_request = buildForwardRequest(request);
    std::string target_response = sendToTarget(forward_request);

    if (target_response.empty()) {
        return Response::error(502, "Bad Gateway: Cannot connect to target server");
    }

    return parseTargetResponse(target_response);
}

std::string ProxyHandler::buildForwardRequest(const Request& request) const {
    std::ostringstream forward_request;

    forward_request << request.method << " " << request.path << " " << request.version << "\r\n";

    bool has_host = false;
    for (const auto& header : request.headers) {
        std::string key = header.first;
        std::string value = header.second;

        if (key == "Host") {
            has_host = true;
            if (!custom_host_header.empty()) {
                value = custom_host_header;
            }
        }

        forward_request << key << ": " << value << "\r\n";
    }

    if (!has_host) {
        std::string host_v = custom_host_header.empty() ? target_host : custom_host_header;
        forward_request << "Host: " << host_v << "\r\n";
    }

    forward_request << "\r\n";

    if (!request.body.empty()) {
        forward_request << request.body;
    }

    return forward_request.str();
}

std::string ProxyHandler::sendToTarget(const std::string& forward_request) const {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Proxy: Cannot create socket" << std::endl;
        return "";
    }

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target_port);

    if (inet_pton(AF_INET, target_host.c_str(), &target_addr.sin_addr) <= 0) {
        std::cerr << "Proxy: Invalid target address" << std::endl;
        close(sockfd);
        return "";
    }

    if (connect(sockfd, reinterpret_cast<sockaddr*>(&target_addr), sizeof(target_addr)) < 0) {
        std::cerr << "Proxy: Cannot connect to target server" << std::endl;
        close(sockfd);
        return "";
    }

    ssize_t sent = send(sockfd, forward_request.c_str(), forward_request.length(), 0);
    if (sent <= 0) {
        close(sockfd);
        return "";
    }

    char buffer[4096];
    std::string response;
    ssize_t bytes_received;

    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, bytes_received);
    }

    close(sockfd);
    return response;
}

Response ProxyHandler::parseTargetResponse(const std::string& raw_response) const {
    Response response;

    size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        response.status_code = 502;
        response.status_text = "Bad Gateway";
        response.body = "Invalid response from target server";
        response.setContentType("text/plain");
        response.setContentLength(response.body.length());
        return response;
    }

    std::string headers_part = raw_response.substr(0, header_end);
    response.body = raw_response.substr(header_end + 4);

    size_t first_line_end = headers_part.find("\r\n");
    std::string status_line = (first_line_end == std::string::npos) ? headers_part : headers_part.substr(0, first_line_end);

    std::istringstream sl(status_line);
    std::string http_ver;
    sl >> http_ver >> response.status_code;
    std::getline(sl, response.status_text);
    if (!response.status_text.empty() && response.status_text[0] == ' ') {
        response.status_text.erase(0, 1);
    }
    if (response.status_text.empty()) {
        response.status_text = "OK";
    }

    response.setContentLength(response.body.length());
    return response;
}
