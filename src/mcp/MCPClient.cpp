#include "MCPClient.h"
#include <spdlog/spdlog.h>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

namespace closecrab {

MCPClient::MCPClient(const MCPServerConfig& config) : config_(config) {}

MCPClient::~MCPClient() { disconnect(); }

#ifdef _WIN32
bool MCPClient::connect() {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childStdinRead, childStdinWrite, childStdoutRead, childStdoutWrite;

    CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0);
    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0);
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStdoutWrite;
    si.dwFlags = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};
    std::string cmdLine = config_.command;
    for (const auto& arg : config_.args) cmdLine += " " + arg;

    if (!CreateProcessA(nullptr, &cmdLine[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        spdlog::error("MCP: Failed to start process: {}", cmdLine);
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
    connected_ = true;

    // Send initialize
    MCPRequest init;
    init.method = "initialize";
    init.params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {{"name", "CloseCrab-Unified"}, {"version", "0.1.0"}}}
    };
    auto resp = sendRequest(init);
    if (resp.isError()) {
        spdlog::error("MCP init failed: {}", resp.error.dump());
        disconnect();
        return false;
    }

    // Send initialized notification
    writeLine(nlohmann::json({{"jsonrpc","2.0"},{"method","notifications/initialized"}}).dump());

    spdlog::info("MCP: Connected to {}", config_.name);
    return true;
}

void MCPClient::disconnect() {
    if (!connected_) return;
    connected_ = false;
    if (stdinWrite_) { CloseHandle(stdinWrite_); stdinWrite_ = nullptr; }
    if (stdoutRead_) { CloseHandle(stdoutRead_); stdoutRead_ = nullptr; }
    if (processHandle_) {
        TerminateProcess(processHandle_, 0);
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
}

std::string MCPClient::readLine() {
    std::string line;
    char c;
    DWORD read;
    while (ReadFile(stdoutRead_, &c, 1, &read, nullptr) && read > 0) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

void MCPClient::writeLine(const std::string& line) {
    std::string data = line + "\n";
    DWORD written;
    WriteFile(stdinWrite_, data.c_str(), (DWORD)data.size(), &written, nullptr);
    FlushFileBuffers(stdinWrite_);
}

#else // POSIX

bool MCPClient::connect() {
    int stdinPipe[2], stdoutPipe[2];
    if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) return false;

    pid_ = fork();
    if (pid_ < 0) return false;

    if (pid_ == 0) {
        // Child
        close(stdinPipe[1]); close(stdoutPipe[0]);
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stdoutPipe[1], STDERR_FILENO);
        close(stdinPipe[0]); close(stdoutPipe[1]);

        std::vector<const char*> argv;
        argv.push_back(config_.command.c_str());
        for (const auto& a : config_.args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execvp(config_.command.c_str(), const_cast<char**>(argv.data()));
        _exit(1);
    }

    // Parent
    close(stdinPipe[0]); close(stdoutPipe[1]);
    stdinFd_ = stdinPipe[1];
    stdoutFd_ = stdoutPipe[0];
    connected_ = true;

    MCPRequest init;
    init.method = "initialize";
    init.params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {{"name", "CloseCrab-Unified"}, {"version", "0.1.0"}}}
    };
    auto resp = sendRequest(init);
    if (resp.isError()) { disconnect(); return false; }

    writeLine(nlohmann::json({{"jsonrpc","2.0"},{"method","notifications/initialized"}}).dump());
    spdlog::info("MCP: Connected to {}", config_.name);
    return true;
}

void MCPClient::disconnect() {
    if (!connected_) return;
    connected_ = false;
    if (stdinFd_ >= 0) { close(stdinFd_); stdinFd_ = -1; }
    if (stdoutFd_ >= 0) { close(stdoutFd_); stdoutFd_ = -1; }
    if (pid_ > 0) { kill(pid_, SIGTERM); waitpid(pid_, nullptr, 0); pid_ = -1; }
}

std::string MCPClient::readLine() {
    std::string line;
    char c;
    while (read(stdoutFd_, &c, 1) == 1) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

void MCPClient::writeLine(const std::string& line) {
    std::string data = line + "\n";
    ::write(stdinFd_, data.c_str(), data.size());
}
#endif

MCPResponse MCPClient::sendRequest(const MCPRequest& req) {
    MCPRequest r = req;
    r.id = nextId_++;
    writeLine(r.toJson().dump());

    // Read response (skip notifications)
    for (int attempts = 0; attempts < 100; attempts++) {
        std::string line = readLine();
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            if (j.contains("id") && j["id"].get<int>() == r.id) {
                MCPResponse resp;
                resp.id = r.id;
                resp.result = j.value("result", nlohmann::json());
                resp.error = j.value("error", nlohmann::json());
                return resp;
            }
        } catch (...) {}
    }
    return {req.id, {}, {{"code", -1}, {"message", "Timeout waiting for response"}}};
}

std::vector<MCPToolDef> MCPClient::listTools() {
    auto resp = sendRequest({"tools/list", {}, 0});
    std::vector<MCPToolDef> tools;
    if (!resp.isError() && resp.result.contains("tools")) {
        for (const auto& t : resp.result["tools"]) {
            tools.push_back({
                t.value("name", ""),
                t.value("description", ""),
                t.value("inputSchema", nlohmann::json::object())
            });
        }
    }
    return tools;
}

nlohmann::json MCPClient::callTool(const std::string& name, const nlohmann::json& args) {
    auto resp = sendRequest({"tools/call", {{"name", name}, {"arguments", args}}, 0});
    if (resp.isError()) return {{"error", resp.error}};
    return resp.result;
}

std::vector<MCPResource> MCPClient::listResources() {
    auto resp = sendRequest({"resources/list", {}, 0});
    std::vector<MCPResource> resources;
    if (!resp.isError() && resp.result.contains("resources")) {
        for (const auto& r : resp.result["resources"]) {
            resources.push_back({
                r.value("uri", ""), r.value("name", ""),
                r.value("mimeType", ""), r.value("description", "")
            });
        }
    }
    return resources;
}

std::string MCPClient::readResource(const std::string& uri) {
    auto resp = sendRequest({"resources/read", {{"uri", uri}}, 0});
    if (resp.isError()) return "[Error: " + resp.error.dump() + "]";
    if (resp.result.contains("contents") && resp.result["contents"].is_array() &&
        !resp.result["contents"].empty()) {
        return resp.result["contents"][0].value("text", "");
    }
    return resp.result.dump();
}

// ---- MCPServerManager ----

MCPServerManager& MCPServerManager::getInstance() {
    static MCPServerManager instance;
    return instance;
}

bool MCPServerManager::addServer(const MCPServerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto client = std::make_unique<MCPClient>(config);
    if (!client->connect()) return false;
    clients_[config.name] = std::move(client);
    return true;
}

void MCPServerManager::removeServer(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(name);
    if (it != clients_.end()) {
        it->second->disconnect();
        clients_.erase(it);
    }
}

MCPClient* MCPServerManager::getClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(name);
    return (it != clients_.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> MCPServerManager::getServerNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [n, _] : clients_) names.push_back(n);
    return names;
}

std::vector<std::pair<std::string, MCPToolDef>> MCPServerManager::getAllTools() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, MCPToolDef>> all;
    for (auto& [name, client] : clients_) {
        if (!client->isConnected()) continue;
        auto tools = client->listTools();
        for (auto& t : tools) all.push_back({name, std::move(t)});
    }
    return all;
}

void MCPServerManager::loadFromSettings(const nlohmann::json& mcpServers) {
    for (auto& [name, cfg] : mcpServers.items()) {
        MCPServerConfig config;
        config.name = name;
        config.command = cfg.value("command", "");
        if (cfg.contains("args") && cfg["args"].is_array()) {
            for (const auto& a : cfg["args"]) config.args.push_back(a.get<std::string>());
        }
        if (cfg.contains("env") && cfg["env"].is_object()) {
            for (auto& [k, v] : cfg["env"].items()) config.env[k] = v.get<std::string>();
        }
        config.transport = cfg.value("transport", "stdio");
        config.url = cfg.value("url", "");

        spdlog::info("MCP: Loading server '{}'...", name);
        if (!addServer(config)) {
            spdlog::warn("MCP: Failed to connect to '{}'", name);
        }
    }
}

void MCPServerManager::disconnectAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [_, client] : clients_) client->disconnect();
    clients_.clear();
}

} // namespace closecrab
