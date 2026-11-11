#pragma once
#include "APIClient.h"
#include <mutex>
#include <stdexcept>

namespace closecrab {
// Consumers keep this stable address; each request pins its implementation.
class SwitchableAPIClient : public APIClient {
    mutable std::mutex mutex_;
    std::shared_ptr<APIClient> current_;
public:
    explicit SwitchableAPIClient(std::shared_ptr<APIClient> client):current_(std::move(client)){}
    std::shared_ptr<APIClient> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_;
    }
    void replace(std::shared_ptr<APIClient> client) {
        if(!client)throw std::invalid_argument("Missing client");
        std::lock_guard<std::mutex> lock(mutex_);
        current_.swap(client);
    }
    void streamChat(const std::vector<Message>& messages,const std::string& prompt,const ModelConfig& config,StreamCallback callback) override {
        snapshot()->streamChat(messages,prompt,config,std::move(callback));
    }
    int countTokens(const std::string& text) const override{return snapshot()->countTokens(text);}
    std::string getModelName() const override{return snapshot()->getModelName();}
    bool isLocal() const override{return snapshot()->isLocal();}
    bool supportsToolUse() const override{return snapshot()->supportsToolUse();}
    bool supportsThinking() const override{return snapshot()->supportsThinking();}
    bool supportsStreaming() const override{return snapshot()->supportsStreaming();}
};
}
