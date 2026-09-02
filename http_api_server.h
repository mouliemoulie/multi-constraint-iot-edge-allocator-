#ifndef HTTP_API_SERVER_H
#define HTTP_API_SERVER_H

#include "api/live_runtime.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

class HttpApiServer {
public:
    explicit HttpApiServer(std::shared_ptr<LiveRuntime> runtime);
    ~HttpApiServer();

    bool start(uint16_t port);
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    std::shared_ptr<LiveRuntime> runtime_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    std::atomic<int> server_fd_{-1};

    void serve(uint16_t port);
    void handleClient(int client_fd);
    static std::string makeResponse(int status_code, const std::string& status_text,
                                    const std::string& content_type, const std::string& body);
    static std::string makeJsonResponse(int status_code, const nlohmann::json& body);
    static std::string statusText(int status_code);
};

#endif
