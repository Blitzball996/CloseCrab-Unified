// MCP transport implementations: stdio (local subprocess), SSE and Streamable
// HTTP (remote). See MCPTransport.h for the interface contract.
#include "MCPTransport.h"
#include <spdlog/spdlog.h>
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>

#ifdef _WIN32
// winsock2.h MUST be included before windows.h, otherwise windows.h pulls in
// the legacy winsock.h and httplib.h's later winsock2.h include triggers
// C2375 "redefinition; different linkage". WIN32_LEAN_AND_MEAN further keeps
// windows.h from dragging in winsock.h on its own.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

#include <httplib.h>

namespace closecrab {

// ---------------------------------------------------------------------------
// stdio transport: spawn the server as a child process, exchange newline-
// delimited JSON over its stdin/stdout.
// ---------------------------------------------------------------------------
class StdioTransport : public MCPTransport {
public:
    StdioTransport(const std::string& command,
                   const std::vector<std::string>& args,
                   const std::map<std::string, std::string>& env)
        : command_(command), args_(args), env_(env) {}

    ~StdioTransport() override { stop(); }

    bool start() override;
    void stop() override;
    bool send(const nlohmann::json& message) override;
    bool receive(nlohmann::json& out, int timeoutMs) override;
    bool isOpen() const override { return open_; }

private:
    std::string readLine();
    void writeLine(const std::string& line);

    std::string command_;
    std::vector<std::string> args_;
    std::map<std::string, std::string> env_;
    std::atomic<bool> open_{false};

#ifdef _WIN32
    void* processHandle_ = nullptr;
    void* stdinWrite_ = nullptr;
    void* stdoutRead_ = nullptr;
#else
    int pid_ = -1;
    int stdinFd_ = -1;
    int stdoutFd_ = -1;
#endif
};

#ifdef _WIN32
bool StdioTransport::start() {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childStdinRead, childStdinWrite, childStdoutRead, childStdoutWrite;
    if (!CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0)) return false;
    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) {
        CloseHandle(childStdinRead); CloseHandle(childStdinWrite);
        return false;
    }
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStdoutWrite;
    si.dwFlags = STARTF_USESTDHANDLES;

    std::string cmdLine = command_;
    for (const auto& arg : args_) cmdLine += " " + arg;

    // Build child environment block (current env + per-server overrides).
    std::string envBlock;
    bool haveEnv = !env_.empty();
    if (haveEnv) {
        for (auto& [k, v] : env_) { envBlock += k + "=" + v; envBlock.push_back('\0'); }
        if (LPCH parentEnv = GetEnvironmentStringsA()) {
            for (LPCH p = parentEnv; *p; ) {
                std::string entry(p);
                envBlock += entry; envBlock.push_back('\0');
                p += entry.size() + 1;
            }
            FreeEnvironmentStringsA(parentEnv);
        }
        envBlock.push_back('\0');
    }

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(nullptr, &cmdLine[0], nullptr, nullptr, TRUE,
                             0, haveEnv ? (LPVOID)envBlock.data() : nullptr,
                             nullptr, &si, &pi);
    if (!ok) {
        spdlog::error("MCP stdio: failed to start process: {}", cmdLine);
        CloseHandle(childStdinRead); CloseHandle(childStdinWrite);
        CloseHandle(childStdoutRead); CloseHandle(childStdoutWrite);
        return false;
    }
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    CloseHandle(pi.hThread);
    processHandle_ = pi.hProcess;
    stdinWrite_ = childStdinWrite;
    stdoutRead_ = childStdoutRead;
    open_ = true;
    return true;
}

void StdioTransport::stop() {
    if (!open_) return;
    open_ = false;
    if (stdinWrite_) { CloseHandle(stdinWrite_); stdinWrite_ = nullptr; }
    if (stdoutRead_) { CloseHandle(stdoutRead_); stdoutRead_ = nullptr; }
    if (processHandle_) {
        TerminateProcess(processHandle_, 0);
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
}
std::string StdioTransport::readLine() {
    std::string line;
    char c; DWORD read;
    while (open_ && ReadFile((HANDLE)stdoutRead_, &c, 1, &read, nullptr) && read > 0) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

void StdioTransport::writeLine(const std::string& line) {
    std::string data = line + "\n";
    DWORD written;
    WriteFile((HANDLE)stdinWrite_, data.c_str(), (DWORD)data.size(), &written, nullptr);
    FlushFileBuffers((HANDLE)stdinWrite_);
}

#else // POSIX

bool StdioTransport::start() {
    int stdinPipe[2], stdoutPipe[2];
    if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) return false;
    pid_ = fork();
    if (pid_ < 0) return false;
    if (pid_ == 0) {
        close(stdinPipe[1]); close(stdoutPipe[0]);
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stdoutPipe[1], STDERR_FILENO);
        close(stdinPipe[0]); close(stdoutPipe[1]);
        for (auto& [k, v] : env_) setenv(k.c_str(), v.c_str(), 1);
        std::vector<const char*> argv;
        argv.push_back(command_.c_str());
        for (const auto& a : args_) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execvp(command_.c_str(), const_cast<char**>(argv.data()));
        _exit(1);
    }
    close(stdinPipe[0]); close(stdoutPipe[1]);
    stdinFd_ = stdinPipe[1];
    stdoutFd_ = stdoutPipe[0];
    open_ = true;
    return true;
}

void StdioTransport::stop() {
    if (!open_) return;
    open_ = false;
    if (stdinFd_ >= 0) { close(stdinFd_); stdinFd_ = -1; }
    if (stdoutFd_ >= 0) { close(stdoutFd_); stdoutFd_ = -1; }
    if (pid_ > 0) { kill(pid_, SIGTERM); waitpid(pid_, nullptr, 0); pid_ = -1; }
}

std::string StdioTransport::readLine() {
    std::string line;
    char c;
    while (open_ && read(stdoutFd_, &c, 1) == 1) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

void StdioTransport::writeLine(const std::string& line) {
    std::string data = line + "\n";
    ssize_t n = ::write(stdinFd_, data.c_str(), data.size());
    (void)n;
}
#endif

bool StdioTransport::send(const nlohmann::json& message) {
    if (!open_) return false;
    try { writeLine(message.dump()); return true; }
    catch (...) { return false; }
}

bool StdioTransport::receive(nlohmann::json& out, int /*timeoutMs*/) {
    while (open_) {
        std::string line = readLine();
        if (line.empty()) { if (!open_) return false; continue; }
        try { out = nlohmann::json::parse(line); return true; }
        catch (...) { /* skip non-JSON log lines from the server */ }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Shared helpers for remote (HTTP-based) transports.
// ---------------------------------------------------------------------------

// Split a URL like "https://host:443/mcp/path" into scheme+host (for the
// httplib::Client base, e.g. "https://host:443") and the path ("/mcp/path").
struct ParsedUrl {
    std::string base;   // scheme://host[:port]
    std::string path;   // /path (defaults to "/")
    bool valid = false;
};

static ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl r;
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return r;
    auto hostStart = schemeEnd + 3;
    auto pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos) {
        r.base = url;
        r.path = "/";
    } else {
        r.base = url.substr(0, pathStart);
        r.path = url.substr(pathStart);
    }
    r.valid = true;
    return r;
}

static httplib::Headers buildHeaders(const std::map<std::string, std::string>& extra,
                                     const std::string& bearer) {
    httplib::Headers h;
    for (auto& [k, v] : extra) h.emplace(k, v);
    if (!bearer.empty()) h.emplace("Authorization", "Bearer " + bearer);
    return h;
}

// ---------------------------------------------------------------------------
// Streamable HTTP transport (MCP "streamable-http"): the client POSTs each
// JSON-RPC message to a single endpoint. The server replies with either a
// single JSON body (Content-Type: application/json) or an SSE stream
// (text/event-stream) carrying one or more messages. A session id returned in
// the "Mcp-Session-Id" header is echoed back on subsequent requests.
// ---------------------------------------------------------------------------
class StreamableHttpTransport : public MCPTransport {
public:
    StreamableHttpTransport(const std::string& url,
                            const std::map<std::string, std::string>& headers,
                            const std::string& bearer)
        : url_(url), headers_(headers), bearer_(bearer) {}

    ~StreamableHttpTransport() override { stop(); }

    bool start() override {
        auto p = parseUrl(url_);
        if (!p.valid) { spdlog::error("MCP http: bad url {}", url_); return false; }
        parsed_ = p;
        open_ = true;
        return true;
    }

    void stop() override { open_ = false; }
    bool isOpen() const override { return open_; }

    bool send(const nlohmann::json& message) override {
        if (!open_) return false;
        httplib::Client cli(parsed_.base.c_str());
        cli.set_connection_timeout(10);
        cli.set_read_timeout(120);
        auto headers = buildHeaders(headers_, bearer_);
        headers.emplace("Accept", "application/json, text/event-stream");
        if (!sessionId_.empty()) headers.emplace("Mcp-Session-Id", sessionId_);
        auto res = cli.Post(parsed_.path.c_str(), headers, message.dump(), "application/json");
        if (!res) { spdlog::warn("MCP http: POST failed to {}", url_); return false; }
        // Capture session id from the first response.
        if (res->has_header("Mcp-Session-Id")) sessionId_ = res->get_header_value("Mcp-Session-Id");
        // Parse the body now and queue any JSON-RPC messages for receive().
        std::string ctype = res->get_header_value("Content-Type");
        if (ctype.find("text/event-stream") != std::string::npos) {
            queueSseMessages(res->body);
        } else if (!res->body.empty()) {
            try {
                auto j = nlohmann::json::parse(res->body);
                std::lock_guard<std::mutex> lk(mtx_);
                inbox_.push_back(std::move(j));
            } catch (...) { /* notification with empty/non-JSON body */ }
        }
        return true;
    }

    bool receive(nlohmann::json& out, int timeoutMs) override {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        std::unique_lock<std::mutex> lk(mtx_);
        while (inbox_.empty()) {
            if (!open_) return false;
            if (cv_.wait_until(lk, deadline) == std::cv_status::timeout && inbox_.empty())
                return false;
        }
        out = std::move(inbox_.front());
        inbox_.pop_front();
        return true;
    }

private:
    // Extract JSON payloads from an SSE body ("data: {...}\n\n" frames).
    void queueSseMessages(const std::string& body) {
        std::istringstream ss(body);
        std::string line, data;
        std::lock_guard<std::mutex> lk(mtx_);
        auto flush = [&]() {
            if (data.empty()) return;
            try { inbox_.push_back(nlohmann::json::parse(data)); } catch (...) {}
            data.clear();
        };
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data:", 0) == 0) {
                std::string chunk = line.substr(5);
                if (!chunk.empty() && chunk.front() == ' ') chunk.erase(0, 1);
                data += chunk;
            } else if (line.empty()) {
                flush();
            }
        }
        flush();
        cv_.notify_all();
    }

    std::string url_;
    std::map<std::string, std::string> headers_;
    std::string bearer_;
    ParsedUrl parsed_;
    std::string sessionId_;
    std::atomic<bool> open_{false};
    std::deque<nlohmann::json> inbox_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

// ---------------------------------------------------------------------------
// SSE transport (MCP "sse", the older HTTP+SSE variant): the client opens a
// long-lived GET to the SSE endpoint. The server's first SSE event ("endpoint")
// tells the client which URL to POST JSON-RPC requests to; subsequent SSE
// "message" events carry server->client JSON-RPC messages.
// ---------------------------------------------------------------------------
class SseTransport : public MCPTransport {
public:
    SseTransport(const std::string& url,
                 const std::map<std::string, std::string>& headers,
                 const std::string& bearer)
        : url_(url), headers_(headers), bearer_(bearer) {}

    ~SseTransport() override { stop(); }

    bool start() override {
        auto p = parseUrl(url_);
        if (!p.valid) { spdlog::error("MCP sse: bad url {}", url_); return false; }
        parsed_ = p;
        open_ = true;
        // Background thread holds the SSE stream open and feeds the inbox.
        reader_ = std::thread([this]() { readLoop(); });
        // Wait (briefly) for the server to announce the POST endpoint.
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, std::chrono::seconds(10), [this]() { return !postPath_.empty() || !open_; });
        return open_ && !postPath_.empty();
    }

    void stop() override {
        open_ = false;
        if (reader_.joinable()) reader_.join();
    }
    bool isOpen() const override { return open_; }

    bool send(const nlohmann::json& message) override {
        if (!open_ || postPath_.empty()) return false;
        httplib::Client cli(parsed_.base.c_str());
        cli.set_connection_timeout(10);
        cli.set_read_timeout(120);
        auto headers = buildHeaders(headers_, bearer_);
        auto res = cli.Post(postPath_.c_str(), headers, message.dump(), "application/json");
        return res && res->status >= 200 && res->status < 300;
    }

    bool receive(nlohmann::json& out, int timeoutMs) override {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        std::unique_lock<std::mutex> lk(mtx_);
        while (inbox_.empty()) {
            if (!open_) return false;
            if (cv_.wait_until(lk, deadline) == std::cv_status::timeout && inbox_.empty())
                return false;
        }
        out = std::move(inbox_.front());
        inbox_.pop_front();
        return true;
    }

private:
    void readLoop() {
        httplib::Client cli(parsed_.base.c_str());
        cli.set_connection_timeout(10);
        cli.set_read_timeout(3600);  // long-lived stream
        auto headers = buildHeaders(headers_, bearer_);
        headers.emplace("Accept", "text/event-stream");
        std::string event, data;
        auto handleFrame = [&]() {
            if (data.empty()) { event.clear(); return; }
            if (event == "endpoint") {
                // data is the relative POST URL
                std::lock_guard<std::mutex> lk(mtx_);
                postPath_ = data;
                if (!postPath_.empty() && postPath_[0] != '/') postPath_ = "/" + postPath_;
                cv_.notify_all();
            } else { // "message" (or default)
                try {
                    auto j = nlohmann::json::parse(data);
                    std::lock_guard<std::mutex> lk(mtx_);
                    inbox_.push_back(std::move(j));
                    cv_.notify_all();
                } catch (...) {}
            }
            event.clear(); data.clear();
        };
        // httplib streams the body to this sink; we parse SSE frames incrementally.
        std::string buf;
        auto res = cli.Get(parsed_.path.c_str(), headers,
            [&](const char* chunk, size_t n) -> bool {
                buf.append(chunk, n);
                size_t pos;
                while ((pos = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, pos);
                    buf.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) { handleFrame(); }
                    else if (line.rfind("event:", 0) == 0) {
                        event = line.substr(6);
                        if (!event.empty() && event.front() == ' ') event.erase(0, 1);
                    } else if (line.rfind("data:", 0) == 0) {
                        std::string d = line.substr(5);
                        if (!d.empty() && d.front() == ' ') d.erase(0, 1);
                        data += d;
                    }
                }
                return open_.load();
            });
        open_ = false;
        cv_.notify_all();
    }

    std::string url_;
    std::map<std::string, std::string> headers_;
    std::string bearer_;
    ParsedUrl parsed_;
    std::string postPath_;
    std::atomic<bool> open_{false};
    std::thread reader_;
    std::deque<nlohmann::json> inbox_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

// Factory: build the right transport for a server config.
std::unique_ptr<MCPTransport> makeTransport(
        const std::string& transport,
        const std::string& command,
        const std::vector<std::string>& args,
        const std::map<std::string, std::string>& env,
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        const std::string& bearer) {
    if (transport == "sse")
        return std::make_unique<SseTransport>(url, headers, bearer);
    if (transport == "http" || transport == "streamable-http" || transport == "streamableHttp")
        return std::make_unique<StreamableHttpTransport>(url, headers, bearer);
    return std::make_unique<StdioTransport>(command, args, env);
}

} // namespace closecrab
