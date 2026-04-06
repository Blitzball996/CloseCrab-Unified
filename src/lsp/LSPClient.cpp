#include "LSPClient.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace closecrab {

#ifdef _WIN32
bool LSPClient::start(const std::string& command, const std::vector<std::string>& args) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE childStdinRead, childStdinWrite, childStdoutRead, childStdoutWrite;
    CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0);
    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0);
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {}; si.cb = sizeof(si);
    si.hStdInput = childStdinRead; si.hStdOutput = childStdoutWrite;
    si.hStdError = childStdoutWrite; si.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi = {};

    std::string cmd = command;
    for (const auto& a : args) cmd += " " + a;

    if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(childStdinRead); CloseHandle(childStdoutWrite); CloseHandle(pi.hThread);
    process_ = pi.hProcess; stdinWrite_ = childStdinWrite; stdoutRead_ = childStdoutRead;
    running_ = true;
    return true;
}

void LSPClient::stop() {
    if (!running_) return;
    running_ = false;
    if (stdinWrite_) { CloseHandle(stdinWrite_); stdinWrite_ = nullptr; }
    if (stdoutRead_) { CloseHandle(stdoutRead_); stdoutRead_ = nullptr; }
    if (process_) { TerminateProcess(process_, 0); CloseHandle(process_); process_ = nullptr; }
}

void LSPClient::writeMessage(const nlohmann::json& msg) {
    std::string body = msg.dump();
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    std::string full = header + body;
    DWORD written;
    WriteFile(stdinWrite_, full.c_str(), (DWORD)full.size(), &written, nullptr);
}

nlohmann::json LSPClient::readMessage() {
    // Read Content-Length header
    std::string header;
    char c;
    DWORD read;
    while (ReadFile(stdoutRead_, &c, 1, &read, nullptr) && read > 0) {
        header += c;
        if (header.size() >= 4 && header.substr(header.size()-4) == "\r\n\r\n") break;
    }
    int contentLength = 0;
    auto pos = header.find("Content-Length: ");
    if (pos != std::string::npos) contentLength = std::stoi(header.substr(pos + 16));
    if (contentLength <= 0) return {};

    std::string body(contentLength, 0);
    DWORD totalRead = 0;
    while (totalRead < (DWORD)contentLength) {
        ReadFile(stdoutRead_, &body[totalRead], contentLength - totalRead, &read, nullptr);
        totalRead += read;
    }
    try { return nlohmann::json::parse(body); } catch (...) { return {}; }
}
#else
bool LSPClient::start(const std::string& command, const std::vector<std::string>& args) {
    int stdinPipe[2], stdoutPipe[2];
    if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) return false;
    pid_ = fork();
    if (pid_ == 0) {
        close(stdinPipe[1]); close(stdoutPipe[0]);
        dup2(stdinPipe[0], 0); dup2(stdoutPipe[1], 1);
        close(stdinPipe[0]); close(stdoutPipe[1]);
        std::vector<const char*> argv = {command.c_str()};
        for (const auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execvp(command.c_str(), const_cast<char**>(argv.data()));
        _exit(1);
    }
    close(stdinPipe[0]); close(stdoutPipe[1]);
    stdinFd_ = stdinPipe[1]; stdoutFd_ = stdoutPipe[0];
    running_ = true;
    return true;
}
void LSPClient::stop() {
    if (!running_) return; running_ = false;
    if (stdinFd_ >= 0) close(stdinFd_);
    if (stdoutFd_ >= 0) close(stdoutFd_);
    if (pid_ > 0) { kill(pid_, SIGTERM); waitpid(pid_, nullptr, 0); }
}
void LSPClient::writeMessage(const nlohmann::json& msg) {
    std::string body = msg.dump();
    std::string full = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    ::write(stdinFd_, full.c_str(), full.size());
}
nlohmann::json LSPClient::readMessage() {
    std::string header; char c;
    while (::read(stdoutFd_, &c, 1) == 1) {
        header += c;
        if (header.size() >= 4 && header.substr(header.size()-4) == "\r\n\r\n") break;
    }
    int len = 0;
    auto pos = header.find("Content-Length: ");
    if (pos != std::string::npos) len = std::stoi(header.substr(pos + 16));
    if (len <= 0) return {};
    std::string body(len, 0);
    int total = 0;
    while (total < len) { int r = ::read(stdoutFd_, &body[total], len - total); if (r <= 0) break; total += r; }
    try { return nlohmann::json::parse(body); } catch (...) { return {}; }
}
#endif

nlohmann::json LSPClient::sendRequest(const std::string& method, const nlohmann::json& params) {
    nlohmann::json req = {{"jsonrpc","2.0"},{"id",nextId_++},{"method",method},{"params",params}};
    writeMessage(req);
    // Read responses until we get our ID
    for (int i = 0; i < 50; i++) {
        auto resp = readMessage();
        if (resp.contains("id") && resp["id"].get<int>() == req["id"].get<int>()) {
            return resp.value("result", nlohmann::json());
        }
    }
    return {};
}

void LSPClient::sendNotification(const std::string& method, const nlohmann::json& params) {
    writeMessage({{"jsonrpc","2.0"},{"method",method},{"params",params}});
}

nlohmann::json LSPClient::initialize(const std::string& rootUri) {
    return sendRequest("initialize", {
        {"processId", nullptr},
        {"rootUri", rootUri},
        {"capabilities", nlohmann::json::object()}
    });
}

nlohmann::json LSPClient::hover(const std::string& uri, int line, int character) {
    return sendRequest("textDocument/hover", {
        {"textDocument", {{"uri", uri}}},
        {"position", {{"line", line}, {"character", character}}}
    });
}

nlohmann::json LSPClient::definition(const std::string& uri, int line, int character) {
    return sendRequest("textDocument/definition", {
        {"textDocument", {{"uri", uri}}},
        {"position", {{"line", line}, {"character", character}}}
    });
}

nlohmann::json LSPClient::references(const std::string& uri, int line, int character) {
    return sendRequest("textDocument/references", {
        {"textDocument", {{"uri", uri}}},
        {"position", {{"line", line}, {"character", character}}},
        {"context", {{"includeDeclaration", true}}}
    });
}

nlohmann::json LSPClient::diagnostics(const std::string& uri) {
    return sendRequest("textDocument/diagnostic", {{"textDocument", {{"uri", uri}}}});
}

void LSPClient::didOpen(const std::string& uri, const std::string& languageId, const std::string& text) {
    sendNotification("textDocument/didOpen", {
        {"textDocument", {{"uri", uri}, {"languageId", languageId}, {"version", 1}, {"text", text}}}
    });
}

void LSPClient::didChange(const std::string& uri, const std::string& text) {
    sendNotification("textDocument/didChange", {
        {"textDocument", {{"uri", uri}, {"version", 2}}},
        {"contentChanges", {{{"text", text}}}}
    });
}

void LSPClient::didSave(const std::string& uri) {
    sendNotification("textDocument/didSave", {{"textDocument", {{"uri", uri}}}});
}

} // namespace closecrab
