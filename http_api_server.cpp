#include "api/http_api_server.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>

namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::string urlDecode(std::string value) {
    for (std::size_t i = 0U; i + 2U < value.size(); ++i) {
        if (value[i] == '%') {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(value[i + 1U]);
            const int lo = hex(value[i + 2U]);
            if (hi >= 0 && lo >= 0) {
                value[i] = static_cast<char>((hi << 4) | lo);
                value.erase(i + 1U, 2U);
            }
        }
    }
    return value;
}

}  // namespace

HttpApiServer::HttpApiServer(std::shared_ptr<LiveRuntime> runtime)
    : runtime_(std::move(runtime)) {
}

HttpApiServer::~HttpApiServer() {
    stop();
}

bool HttpApiServer::start(uint16_t port) {
    if (running_.exchange(true)) return true;
    server_thread_ = std::thread(&HttpApiServer::serve, this, port);
    // The socket is created in the server thread. Give it a short window to
    // bind so start() can report obvious failures to the caller.
    for (int i = 0; i < 50 && server_fd_.load() < 0 && running_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return server_fd_.load() >= 0;
}

void HttpApiServer::stop() {
    running_.exchange(false);
    if (server_fd_.load() >= 0) {
        const int fd = server_fd_.exchange(-1);
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    if (server_thread_.joinable()) server_thread_.join();
}

void HttpApiServer::serve(uint16_t port) {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_.load() < 0) {
        running_ = false;
        return;
    }
    int yes = 1;
    setsockopt(server_fd_.load(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(server_fd_.load(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(server_fd_.load(), 32) < 0) {
        const int fd = server_fd_.exchange(-1);
        close(fd);
        running_ = false;
        return;
    }

    while (running_) {
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);
        const int client_fd = accept(server_fd_.load(), reinterpret_cast<sockaddr*>(&client), &client_len);
        if (client_fd < 0) {
            if (running_) continue;
            break;
        }
        std::thread(&HttpApiServer::handleClient, this, client_fd).detach();
    }
}

void HttpApiServer::handleClient(int client_fd) {
    std::string request;
    std::vector<char> buffer(8192);
    std::size_t expected_body = 0U;

    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 1024U * 1024U) {
        const ssize_t received = recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            close(client_fd);
            return;
        }
        request.append(buffer.data(), static_cast<std::size_t>(received));
    }

    const std::size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        const std::string response = makeResponse(400, statusText(400), "text/plain", "Malformed HTTP request");
        send(client_fd, response.data(), response.size(), 0);
        close(client_fd);
        return;
    }

    const std::string header_block = request.substr(0, header_end);
    std::string body = request.substr(header_end + 4U);
    std::istringstream headers(header_block);
    std::string request_line;
    std::getline(headers, request_line);
    request_line = trim(request_line);
    std::istringstream request_parts(request_line);
    std::string method;
    std::string target;
    std::string version;
    request_parts >> method >> target >> version;

    std::string header_line;
    while (std::getline(headers, header_line)) {
        const auto colon = header_line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = trim(header_line.substr(0, colon));
        const std::string value = trim(header_line.substr(colon + 1U));
        if (name == "Content-Length" || name == "content-length") {
            try { expected_body = static_cast<std::size_t>(std::stoull(value)); } catch (...) { expected_body = 0U; }
        }
    }

    while (body.size() < expected_body && body.size() < 1024U * 1024U) {
        const ssize_t received = recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) break;
        body.append(buffer.data(), static_cast<std::size_t>(received));
    }
    if (body.size() > expected_body) body.resize(expected_body);

    if (method == "OPTIONS") {
        const std::string response = makeResponse(204, statusText(204), "text/plain", "");
        send(client_fd, response.data(), response.size(), 0);
        close(client_fd);
        return;
    }

    try {
        std::string path = target;
        const auto query = path.find('?');
        if (query != std::string::npos) path = path.substr(0, query);
        path = urlDecode(path);

        nlohmann::json response;
        int status = 200;

        if (method == "GET" && path == "/api/health") {
            response = { {"ok", true}, {"backend", "cpp"} };
        } else if (method == "GET" && path == "/api/status") {
            response = runtime_->status();
        } else if (method == "GET" && path == "/api/configuration") {
            response = runtime_->configuration();
        } else if (method == "PUT" && path == "/api/configuration") {
            const auto input = nlohmann::json::parse(body);
            const bool automatic = input.value("automatic", false);
            const std::string allocation = input.value<std::string>("allocation_strategy", "multi_constraint");
            const std::string scheduling = input.value<std::string>("scheduling_algorithm", "edf");
            std::string error;
            if (!runtime_->applyConfiguration(automatic, allocation, scheduling, error)) {
                status = 400;
                response = { {"success", false}, {"error", error} };
            } else {
                response = { {"success", true}, {"configuration", runtime_->configuration()} };
            }
        } else if (method == "GET" && path == "/api/devices") {
            response = runtime_->devices();
        } else if (method == "GET" && path == "/api/edge-nodes") {
            response = runtime_->edgeNodes();
        } else if (method == "GET" && path == "/api/tasks") {
            response = runtime_->tasks();
        } else if (method == "GET" && path == "/api/tasks/active") {
            response = runtime_->activeTasks();
        } else if (method == "GET" && path.rfind("/api/tasks/", 0U) == 0U && path.size() > 11U) {
            response = runtime_->taskLifecycle(path.substr(11U));
            if (!response.value("found", false)) status = 404;
        } else if (method == "GET" && path == "/api/allocations") {
            response = runtime_->allocations();
        } else if (method == "GET" && path == "/api/metrics") {
            response = runtime_->metrics();
        } else if (method == "GET" && path == "/api/activities") {
            response = runtime_->activities();
        } else if (method == "GET" && path == "/api/alerts") {
            response = runtime_->alerts();
        } else if (method == "GET" && path == "/api/resource-history") {
            response = runtime_->resourceHistory();
        } else {
            status = 404;
            response = { {"error", "Endpoint not found"} };
        }

        const std::string serialized = response.dump();
        const std::string http_response = makeJsonResponse(status, response);
        (void)serialized;
        send(client_fd, http_response.data(), http_response.size(), 0);
    } catch (const std::exception& ex) {
        const nlohmann::json response = { {"error", ex.what()} };
        const std::string http_response = makeJsonResponse(400, response);
        send(client_fd, http_response.data(), http_response.size(), 0);
    }

    close(client_fd);
}

std::string HttpApiServer::makeResponse(int status_code, const std::string& status_text,
                                        const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, PUT, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    return response.str();
}

std::string HttpApiServer::makeJsonResponse(int status_code, const nlohmann::json& body) {
    return makeResponse(status_code, statusText(status_code), "application/json; charset=utf-8", body.dump());
}

std::string HttpApiServer::statusText(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "Response";
    }
}
