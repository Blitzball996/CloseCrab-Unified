#pragma once
#include "LSPClient.h"
#include <map>
#include <memory>
#include <mutex>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {

struct LSPServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::vector<std::string> fileExtensions; // e.g., {".ts", ".tsx", ".js"}
    std::string rootUri;
};

class LSPServerManager {
public:
    static LSPServerManager& getInstance() {
        static LSPServerManager instance;
        return instance;
    }

    bool addServer(const LSPServerConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (clients_.count(config.name)) {
            spdlog::warn("LSP server '{}' already registered", config.name);
            return false;
        }
        auto client = std::make_unique<LSPClient>();
        std::string cmdLine = config.command;
        for (const auto& arg : config.args) cmdLine += " " + arg;

        if (!client->start(cmdLine)) {
            spdlog::error("Failed to start LSP server '{}'", config.name);
            return false;
        }
        if (!client->initialize(config.rootUri)) {
            spdlog::error("Failed to initialize LSP server '{}'", config.name);
            client->stop();
            return false;
        }

        for (const auto& ext : config.fileExtensions) {
            extensionToServer_[ext] = config.name;
        }
        configs_[config.name] = config;
        clients_[config.name] = std::move(client);
        spdlog::info("LSP server '{}' started for extensions: {}", config.name,
                     [&]{ std::string s; for(auto& e:config.fileExtensions) s+=e+" "; return s; }());
        return true;
    }

    void removeServer(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(name);
        if (it != clients_.end()) {
            it->second->stop();
            clients_.erase(it);
        }
        // Remove extension mappings
        for (auto eit = extensionToServer_.begin(); eit != extensionToServer_.end(); ) {
            if (eit->second == name) eit = extensionToServer_.erase(eit);
            else ++eit;
        }
        configs_.erase(name);
        spdlog::info("LSP server '{}' removed", name);
    }

    LSPClient* getClientForFile(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string ext = getExtension(filePath);
        auto it = extensionToServer_.find(ext);
        if (it == extensionToServer_.end()) return nullptr;
        auto cit = clients_.find(it->second);
        return cit != clients_.end() ? cit->second.get() : nullptr;
    }

    LSPClient* getClient(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(name);
        return it != clients_.end() ? it->second.get() : nullptr;
    }

    std::vector<std::string> getServerNames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, _] : clients_) names.push_back(name);
        return names;
    }

    void loadFromSettings(const nlohmann::json& lspServers) {
        if (!lspServers.is_object()) return;
        for (auto& [name, cfg] : lspServers.items()) {
            LSPServerConfig sc;
            sc.name = name;
            sc.command = cfg.value("command", "");
            if (cfg.contains("args") && cfg["args"].is_array()) {
                for (const auto& a : cfg["args"]) sc.args.push_back(a.get<std::string>());
            }
            if (cfg.contains("fileExtensions") && cfg["fileExtensions"].is_array()) {
                for (const auto& e : cfg["fileExtensions"]) sc.fileExtensions.push_back(e.get<std::string>());
            }
            sc.rootUri = cfg.value("rootUri", "");
            if (!sc.command.empty()) addServer(sc);
        }
    }

    void disconnectAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, client] : clients_) {
            client->stop();
            spdlog::info("LSP server '{}' disconnected", name);
        }
        clients_.clear();
        extensionToServer_.clear();
        configs_.clear();
    }

private:
    LSPServerManager() = default;

    std::string getExtension(const std::string& path) const {
        auto pos = path.rfind('.');
        return pos != std::string::npos ? path.substr(pos) : "";
    }

    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<LSPClient>> clients_;
    std::map<std::string, LSPServerConfig> configs_;
    std::map<std::string, std::string> extensionToServer_;
};

} // namespace closecrab
