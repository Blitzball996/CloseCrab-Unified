#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace closecrab {

// Minimal LSP client (JSON-RPC 2.0 over stdio)
class LSPClient {
public:
    bool start(const std::string& command, const std::vector<std::string>& args = {});
    void stop();
    bool isRunning() const { return running_; }

    // LSP methods
    nlohmann::json initialize(const std::string& rootUri);
    nlohmann::json hover(const std::string& uri, int line, int character);
    nlohmann::json definition(const std::string& uri, int line, int character);
    nlohmann::json references(const std::string& uri, int line, int character);
    nlohmann::json diagnostics(const std::string& uri);
    void didOpen(const std::string& uri, const std::string& languageId, const std::string& text);
    void didChange(const std::string& uri, const std::string& text);
    void didSave(const std::string& uri);

private:
    nlohmann::json sendRequest(const std::string& method, const nlohmann::json& params);
    void sendNotification(const std::string& method, const nlohmann::json& params);
    void writeMessage(const nlohmann::json& msg);
    nlohmann::json readMessage();

    bool running_ = false;
    int nextId_ = 1;

#ifdef _WIN32
    HANDLE process_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
#else
    int pid_ = -1;
    int stdinFd_ = -1;
    int stdoutFd_ = -1;
#endif
};

} // namespace closecrab
