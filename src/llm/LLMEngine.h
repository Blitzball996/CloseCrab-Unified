#pragma once
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <cstddef>
#include <functional>

// ǰ������
struct llama_model;
struct llama_context;
struct llama_vocab;
class SSDExpertStreamer;
struct SSDStreamerConfig;

class LLMEngine {
public:
    LLMEngine(const std::string& modelPath, int nParallel = 1, int cpuMoeLayers = 0);
    ~LLMEngine();

    std::string generate(const std::string& prompt,
        const std::string& system = "",
        int maxTokens = 512,
        float temperature = 0.7f);

    void generateStreaming(const std::string& prompt,
        const std::string& system,
        int maxTokens,
        float temperature,
        std::function<void(const std::string&)> onToken,
        std::function<void()> onComplete = nullptr);

    std::string generateRaw(const std::string& fullPrompt,
        int maxTokens = 512,
        float temperature = 0.7f);

    void generateRaw(const std::string& fullPrompt,
        int maxTokens,
        float temperature,
        std::function<void(const std::string&)> onToken,
        std::function<void()> onComplete = nullptr);

    // === Parallel sequence support (Team Mode) ===
    int acquireSequenceSlot();
    void releaseSequenceSlot(int slotId);
    int maxParallel() const { return m_nParallel; }
    int activeSlots() const;

    void generateForSequence(int seqId,
        const std::string& fullPrompt,
        int maxTokens,
        float temperature,
        std::function<void(const std::string&)> onToken,
        std::function<void()> onComplete = nullptr);

    bool isLoaded() const { return model != nullptr && ctx != nullptr; }
    std::string getModelInfo() const;
    int countTokens(const std::string& text) const;

    bool initSSDStreaming(const std::string& expertDir,
        size_t cacheSizeMB = 4096,
        size_t gpuCacheSizeMB = 1024);

    std::string getSSDStreamerStatus() const;
    bool isSSDStreamingEnabled() const;

private:
    struct llama_model* model = nullptr;
    struct llama_context* ctx = nullptr;
    const struct llama_vocab* vocab = nullptr;
    int m_cpuMoeLayers = 0;
    int m_nParallel = 1;

    // Sequence slot management
    std::vector<bool> m_slotInUse;
    std::mutex m_slotMutex;

    std::vector<int> stringToTokens(const std::string& text) const;
    std::string tokensToString(const std::vector<int>& tokens) const;
    void generateTokensStreaming(const std::vector<int>& inputTokens,
        int maxTokens,
        float temperature,
        std::function<void(const std::string&)> onToken);

    std::unique_ptr<SSDExpertStreamer> m_ssdStreamer;
};