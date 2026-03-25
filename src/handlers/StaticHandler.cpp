#include "handlers/StaticHandler.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>

Response StaticHandler::handle(const Request& request) {
    std::string file_path = buildFilePath(request.path);
    std::string normalized_path = normalizePath(file_path);

    if (!isPathWithinRoot(normalized_path, root_directory)) {
        return Response::error(403, "Forbidden: Path traversal not allowed");
    }

    std::error_code ec;
    if (!std::filesystem::exists(normalized_path, ec) || ec) {
        return Response::error(404, "File not found: " + request.path);
    }

    if (!std::filesystem::is_regular_file(normalized_path, ec) || ec) {
        return Response::error(403, "Forbidden: Not a regular file");
    }

    std::string content = readFile(normalized_path);
    if (content.empty()) {
        // allow empty file response
        content = "";
    }

    Response response = Response::success(content, getMimeType(request.getFilename()));
    return response;
}

std::string StaticHandler::getMimeType(const std::string& filename) const {
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos) return "application/octet-stream";

    std::string extension = filename.substr(dot_pos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == "html" || extension == "htm") return "text/html";
    if (extension == "css") return "text/css";
    if (extension == "js") return "application/javascript";
    if (extension == "png") return "image/png";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "gif") return "image/gif";
    if (extension == "txt") return "text/plain";
    if (extension == "json") return "application/json";

    return "application/octet-stream";
}

std::string StaticHandler::readFile(const std::string& filepath) const {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string StaticHandler::buildFilePath(const std::string& url_path) const {
    // expected mapping: /static/a/b -> <root>/a/b
    std::string relative_path = url_path;
    const std::string prefix = "/static/";
    if (relative_path.rfind(prefix, 0) == 0) {
        relative_path = relative_path.substr(prefix.size());
    }

    if (!relative_path.empty() && (relative_path[0] == '/' || relative_path[0] == '\\')) {
        relative_path = relative_path.substr(1);
    }

    std::filesystem::path full = std::filesystem::path(root_directory) / relative_path;
    return full.lexically_normal().string();
}

std::string StaticHandler::normalizePath(const std::string& path) const {
    return std::filesystem::path(path).lexically_normal().string();
}

bool StaticHandler::isPathWithinRoot(const std::string& path, const std::string& root) const {
    std::filesystem::path p = std::filesystem::path(path).lexically_normal();
    std::filesystem::path r = std::filesystem::path(root).lexically_normal();

    auto pit = p.begin();
    auto rit = r.begin();
    for (; rit != r.end(); ++rit, ++pit) {
        if (pit == p.end() || *pit != *rit) {
            return false;
        }
    }

    return true;
}
