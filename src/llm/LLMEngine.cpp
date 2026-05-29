#include "LLMEngine.h"
#include <spdlog/spdlog.h>
#ifdef CLOSECRAB_HAS_LOCAL_LLM
#include <llama.h>
#include <ggml.h>
#endif
#include <cstring>
#include <random>
#include <algorithm>
#include <vector>
#include "../ssd/SSDExpertStreamer.h"
#include <fstream>

#ifndef CLOSECRAB_HAS_LOCAL_LLM
// Stub implementations when local LLM is disabled
LLMEngine::LLMEngine(const std::string&, int nParallel, int cpuMoeLayers)
    : m_cpuMoeLayers(cpuMoeLayers), m_nParallel(nParallel) {
    spdlog::warn("LLMEngine: built without local LLM support");
}
LLMEngine::~LLMEngine() = default;
std::string LLMEngine::generate(const std::string&, const std::string&, int, float) { return ""; }
void LLMEngine::generateStreaming(const std::string&, const std::string&, int, float,
    std::function<void(const std::string&)>, std::function<void()>) {}
std::string LLMEngine::generateRaw(const std::string&, int, float) { return ""; }
void LLMEngine::generateRaw(const std::string&, int, float,
    std::function<void(const std::string&)>, std::function<void()>) {}
int LLMEngine::acquireSequenceSlot() { return -1; }
void LLMEngine::releaseSequenceSlot(int) {}
int LLMEngine::activeSlots() const { return 0; }
void LLMEngine::generateForSequence(int, const std::string&, int, float,
    std::function<void(const std::string&)>, std::function<void()>) {}
std::string LLMEngine::getModelInfo() const { return "Local LLM not available in this build"; }
int LLMEngine::countTokens(const std::string& text) const { return static_cast<int>(text.size() / 4); }
bool LLMEngine::initSSDStreaming(const std::string&, size_t, size_t) { return false; }
std::string LLMEngine::getSSDStreamerStatus() const { return "disabled"; }
bool LLMEngine::isSSDStreamingEnabled() const { return false; }
std::vector<int> LLMEngine::stringToTokens(const std::string&) const { return {}; }
std::string LLMEngine::tokensToString(const std::vector<int>&) const { return ""; }
void LLMEngine::generateTokensStreaming(const std::vector<int>&, int, float,
    std::function<void(const std::string&)>) {}
#else
#include <thread>


//extern bool g_llama_log_enabled;

LLMEngine::LLMEngine(const std::string& modelPath, int nParallel, int cpuMoeLayers)
    : m_cpuMoeLayers(cpuMoeLayers), m_nParallel(std::max(1, nParallel)),
      m_slotInUse(std::max(1, nParallel), false) {
    // ��ʼ�� llama ���
    llama_backend_init();

    llama_log_set([](enum ggml_log_level level, const char* text, void*) {
        if (level >= GGML_LOG_LEVEL_WARN) {
            fprintf(stderr, "%s", text);
        }
        }, nullptr);

    // ����ģ�Ͳ���
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99;  // �Զ���� GPU
    model_params.use_mmap = true;
    model_params.use_mlock = false;

    // ���� n_cpu_moe
    if (m_cpuMoeLayers > 0) {
        static std::vector<llama_model_tensor_buft_override> overrides;
        static std::vector<std::string> patterns;

        overrides.clear();
        patterns.clear();
        patterns.reserve(m_cpuMoeLayers);
        overrides.reserve(m_cpuMoeLayers + 1);

        for (int i = 0; i < m_cpuMoeLayers; ++i) {
            // ƥ��� i ��ר�ҿ�������������������ʵ��ģ�͵�����
            std::string pattern = "blk\\." + std::to_string(i) + "\\.ffn_(gate|up|down)";
            patterns.push_back(pattern);
            overrides.push_back({ patterns.back().c_str(), ggml_backend_cpu_buffer_type() });
        }
        overrides.push_back({ nullptr, nullptr });
        model_params.tensor_buft_overrides = overrides.data();
    }

    spdlog::info("Loading model from: {}", modelPath);
    spdlog::info("GPU layers: auto (-1)");

    spdlog::info("CPU MoE layers: {}", m_cpuMoeLayers);

    // ����ģ��
    model = llama_load_model_from_file(modelPath.c_str(), model_params);
    if (!model) {
        spdlog::error("Failed to load model from: {}", modelPath);
        return;
    }
    spdlog::info("Model loaded successfully");

    // ��ȡ vocab
    vocab = llama_model_get_vocab(model);
    if (!vocab) {
        spdlog::error("Failed to get vocab from model");
        llama_free_model(model);
        model = nullptr;
        return;
    }

    // ���������Ĳ���
    llama_context_params ctx_params = llama_context_default_params();
    size_t modelSizeMB = llama_model_size(model) / 1024 / 1024;
    if (modelSizeMB > 50000) {
        // ����ģ�ͣ�>50GB��������������
        ctx_params.n_ctx = 32768;
    }
    else if (modelSizeMB > 10000) {
        // ��ģ�ͣ�10-50GB��
        ctx_params.n_ctx = 65536;
    }
    else {
        // Сģ�ͣ����Ը���������
        ctx_params.n_ctx = 131072;
    }
    ctx_params.n_batch = 8192;          // ���� batch ���� prefill
    ctx_params.n_threads = std::thread::hardware_concurrency();
    ctx_params.n_seq_max = m_nParallel;

    // ====== KV Cache �������ڴ���룬������������ ======
    ctx_params.type_k = GGML_TYPE_Q8_0;   // Key �� FP16 ѹ�� Q8
    ctx_params.type_v = GGML_TYPE_Q8_0;   // Value �� FP16 ѹ�� Q8
    spdlog::info("KV cache quantization: Q8_0 (memory ~50% of FP16)");

    // ====== Flash Attention������ע���������ڴ��ֵ ======
    ctx_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
    spdlog::info("Flash Attention: enabled");

    // ����������
    ctx = llama_new_context_with_model(model, ctx_params);
    if (!ctx) {
        spdlog::error("Failed to create context for model");
        llama_free_model(model);
        model = nullptr;
        return;
    }

    spdlog::info("LLMEngine initialized successfully");
    spdlog::info("  - Context size: {}", llama_n_ctx(ctx));
    spdlog::info("  - Model size: {} MB", llama_model_size(model) / 1024 / 1024);
    spdlog::info("  - Vocabulary size: {}", llama_n_vocab(vocab));
    spdlog::info("  - CPU threads: {}", ctx_params.n_threads);

    // GPU usage info
    if (model_params.n_gpu_layers > 0) {
        spdlog::info("  - GPU acceleration: ENABLED ({} layers on GPU)", model_params.n_gpu_layers);
    }
    else {
        spdlog::info("  - GPU acceleration: DISABLED (CPU only)");
    }
}

LLMEngine::~LLMEngine() {
    if (ctx) {
        llama_free(ctx);
        ctx = nullptr;
    }
    if (model) {
        llama_free_model(model);
        model = nullptr;
    }
    llama_backend_free();
    spdlog::info("LLMEngine destroyed");
}

int LLMEngine::acquireSequenceSlot() {
    std::lock_guard<std::mutex> lock(m_slotMutex);
    for (int i = 0; i < m_nParallel; ++i) {
        if (!m_slotInUse[i]) {
            m_slotInUse[i] = true;
            return i;
        }
    }
    return -1;
}

void LLMEngine::releaseSequenceSlot(int slotId) {
    std::lock_guard<std::mutex> lock(m_slotMutex);
    if (slotId >= 0 && slotId < m_nParallel) {
        m_slotInUse[slotId] = false;
    }
}

int LLMEngine::activeSlots() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_slotMutex));
    int count = 0;
    for (bool used : m_slotInUse) {
        if (used) ++count;
    }
    return count;
}

void LLMEngine::generateForSequence(int seqId,
    const std::string& fullPrompt,
    int maxTokens,
    float temperature,
    std::function<void(const std::string&)> onToken,
    std::function<void()> onComplete) {
    // For now, delegate to generateRaw (single-threaded decode).
    // True parallel batch decode requires llama_batch with seq_id routing,
    // which will be implemented when the basic multi-client flow is validated.
    generateRaw(fullPrompt, maxTokens, temperature, onToken, onComplete);
}

std::vector<int> LLMEngine::stringToTokens(const std::string& text) const {
    const int maxTokens = static_cast<int>(text.length()) + 10;
    std::vector<int> tokens(maxTokens);

    // �� vocab
    int n = llama_tokenize(vocab, text.c_str(), static_cast<int>(text.length()),
        tokens.data(), maxTokens, true, true);
    if (n < 0) {
        spdlog::error("Failed to tokenize: {}", text.substr(0, 50));
        return {};
    }
    tokens.resize(n);
    return tokens;
}

std::string LLMEngine::tokensToString(const std::vector<int>& tokens) const {
    std::string result;
    for (int token : tokens) {
        std::string piece;
        piece.resize(128);
        // �� vocab
        int n = llama_token_to_piece(vocab, token, piece.data(), static_cast<int>(piece.size()), 0, true);
        if (n > 0) {
            piece.resize(n);
            result += piece;
        }
    }
    return result;
}

void LLMEngine::generateTokensStreaming(const std::vector<int>& inputTokens,
    int maxTokens,
    float temperature,
    std::function<void(const std::string&)> onToken) {

    // ====== �޸����� llama_kv_cache_clear ���������ؽ� context ======
    // ԭ����������llama_free(ctx) + llama_new_context(...)  ���� ������KV cache �˷�
    // ���ڵ�������ֻ��� KV cache��context �������� ���� �� 1000 ��
    llama_memory_clear(llama_get_memory(ctx), true);

    const int n_vocab = llama_n_vocab(vocab);
    const int n_batch = 2048;  // ÿ����ദ���� token ��

    // ====== ����ضϱ��� ======
    std::vector<int> tokens = inputTokens;
    int maxCtx = (int)llama_n_ctx(ctx);
    if ((int)tokens.size() > maxCtx - maxTokens - 4) {
        spdlog::warn("Input too long ({} tokens), truncating", tokens.size());
        int keepFront = 200;
        int keepBack = maxCtx - keepFront - maxTokens - 4;
        if (keepBack > 0 && (int)tokens.size() > keepFront + keepBack) {
            std::vector<int> truncated;
            truncated.insert(truncated.end(), tokens.begin(), tokens.begin() + keepFront);
            truncated.insert(truncated.end(), tokens.end() - keepBack, tokens.end());
            tokens = std::move(truncated);
        }
        else {
            tokens.resize(maxCtx - maxTokens - 4);
        }
        spdlog::info("Truncated to {} tokens", tokens.size());
    }

    // ====== Prefill �׶Σ���������ȫ������ token ======
    // �ѳ� prompt �ֳɶ�� batch ���룬���ⳬ�� n_batch ����
    for (int pos = 0; pos < (int)tokens.size(); pos += n_batch) {
        int batchSize = std::min(n_batch, (int)tokens.size() - pos);
        llama_batch batch = llama_batch_get_one(tokens.data() + pos, batchSize);
        int ret = llama_decode(ctx, batch);
        if (ret != 0) {
            spdlog::error("Prefill failed at pos {}/{}", pos, (int)tokens.size());
            return;
        }
    }

    // ====== Decode �׶Σ��� token ���� ======
    std::random_device rd;
    std::mt19937 gen(rd());

    for (int i = 0; i < maxTokens; ++i) {
        const float* logits = llama_get_logits(ctx);

        int nextToken = -1;

        if (temperature <= 0.0f) {
            // ̰�Ĳ���
            nextToken = (int)(std::max_element(logits, logits + n_vocab) - logits);
        }
        else {
            // �¶Ȳ���
            std::vector<float> probs(n_vocab);
            float maxLogit = *std::max_element(logits, logits + n_vocab);
            float sum = 0.0f;
            for (int j = 0; j < n_vocab; ++j) {
                probs[j] = expf((logits[j] - maxLogit) / temperature);
                sum += probs[j];
            }
            if (sum > 0.0f) {
                for (int j = 0; j < n_vocab; ++j) {
                    probs[j] /= sum;
                }
            }

            std::discrete_distribution<int> dist(probs.begin(), probs.end());
            nextToken = dist(gen);
        }

        if (nextToken == llama_token_eos(vocab)) {
            break;
        }

        // ��� token
        std::string piece;
        piece.resize(128);
        int n = llama_token_to_piece(vocab, nextToken, piece.data(), (int)piece.size(), 0, true);
        if (n > 0) {
            piece.resize(n);
            onToken(piece);
        }

        // ====== �ؼ���ֻ�� 1 ���� token ======
        // KV cache �Ѿ���ס��֮ǰ���� token������Ҫ��������
        // ԭ���Ĵ���ÿ�ֶ���ȫ�� token������û�� KV cache
        llama_batch batch = llama_batch_get_one(&nextToken, 1);
        int ret = llama_decode(ctx, batch);
        if (ret != 0) {
            spdlog::error("Decode failed at token {}", i);
            break;
        }
    }
}

std::string LLMEngine::generate(const std::string& prompt,
    const std::string& system,
    int maxTokens,
    float temperature) {
    if (!isLoaded()) {
        spdlog::error("LLMEngine not loaded");
        return "";
    }

    std::string fullPrompt;
    if (!system.empty()) {
        fullPrompt = "<|im_start|>system\n" + system + "<|im_end|>\n";
    }
    fullPrompt += "<|im_start|>user\n" + prompt + "<|im_end|>\n";
    fullPrompt += "<|im_start|>assistant\n";

    std::vector<int> inputTokens = stringToTokens(fullPrompt);
    if (inputTokens.empty()) {
        return "";
    }

    // ====== ��������ֹ token �����������Ĵ��ڵ��±��� ======
    int maxCtx = (int)llama_n_ctx(ctx);
    if ((int)inputTokens.size() > maxCtx - 4) {
        spdlog::warn("Input too long: {} tokens (max context: {}), truncating",
            inputTokens.size(), maxCtx);
        // ������ͷ��ϵͳ prompt��Լ 200 token��+ ��ȡĩβ
        int keepFront = 200;
        int keepBack = maxCtx - keepFront - 4;  // �� 4 �� token ������
        if (keepBack > 0 && (int)inputTokens.size() > keepFront + keepBack) {
            std::vector<int> truncated;
            truncated.insert(truncated.end(),
                inputTokens.begin(),
                inputTokens.begin() + keepFront);
            truncated.insert(truncated.end(),
                inputTokens.end() - keepBack,
                inputTokens.end());
            inputTokens = std::move(truncated);
            spdlog::info("Truncated to {} tokens", inputTokens.size());
        }
        else {
            inputTokens.resize(maxCtx - 4);
        }
    }
    // ====== �ضϱ������� ======

    //generateTokensStreaming(inputTokens, maxTokens, temperature, onToken);

    std::vector<int> allTokens = inputTokens;
    std::random_device rd;
    std::mt19937 gen(rd());

    // �� vocab
    const int n_vocab = llama_n_vocab(vocab);

    for (int i = 0; i < maxTokens; ++i) {
        llama_batch batch;
        if (i == 0) {
            // ��һ�Σ�����ȫ������ token��prefill��
            batch = llama_batch_get_one(allTokens.data(), (int)allTokens.size());
        }
        else {
            // ������ֻ�����һ���� token��decode��
            batch = llama_batch_get_one(&allTokens.back(), 1);
        }

        int n_eval = llama_decode(ctx, batch);
        if (n_eval != 0) {
            spdlog::error("Failed to evaluate model");
            break;
        }

        const float* logits = llama_get_logits(ctx);

        int nextToken = -1;

        if (temperature <= 0.0f) {
            nextToken = std::max_element(logits, logits + n_vocab) - logits;
        }
        else {
            std::vector<float> probs(n_vocab);
            float sum = 0.0f;
            for (int j = 0; j < n_vocab; ++j) {
                probs[j] = expf(logits[j] / temperature);
                sum += probs[j];
            }
            if (sum > 0.0f) {
                for (int j = 0; j < n_vocab; ++j) {
                    probs[j] /= sum;
                }
            }
            std::discrete_distribution<int> dist(probs.begin(), probs.end());
            nextToken = dist(gen);
        }

        if (nextToken == llama_token_eos(vocab)) {
            break;
        }

        allTokens.push_back(nextToken);
    }

    if (allTokens.size() > inputTokens.size()) {
        std::vector<int> responseTokens(allTokens.begin() + inputTokens.size(), allTokens.end());
        return tokensToString(responseTokens);
    }

    return "";
}

// ============================================
// ������ֱ��ʹ������ prompt �����ɷ������������κθ�ʽ��
// ============================================

void LLMEngine::generateRaw(const std::string& fullPrompt,
    int maxTokens,
    float temperature,
    std::function<void(const std::string&)> onToken,
    std::function<void()> onComplete) {
    if (!isLoaded()) {
        spdlog::error("LLMEngine not loaded");
        if (onComplete) onComplete();
        return;
    }

    std::vector<int> inputTokens = stringToTokens(fullPrompt);
    if (inputTokens.empty()) {
        spdlog::error("Failed to tokenize prompt");
        if (onComplete) onComplete();
        return;
    }

    // ֱ�ӵ��ã����������ؽ� context
    // generateTokensStreaming �ڲ��� llama_kv_cache_clear
    generateTokensStreaming(inputTokens, maxTokens, temperature, onToken);

    if (onComplete) onComplete();
}

std::string LLMEngine::generateRaw(const std::string& fullPrompt,
    int maxTokens,
    float temperature) {
    if (!isLoaded()) {
        spdlog::error("LLMEngine not loaded");
        return "";
    }

    std::vector<int> inputTokens = stringToTokens(fullPrompt);
    if (inputTokens.empty()) {
        return "";
    }

    std::vector<int> allTokens = inputTokens;
    std::random_device rd;
    std::mt19937 gen(rd());

    const int n_vocab = llama_n_vocab(vocab);

    for (int i = 0; i < maxTokens; ++i) {
        llama_batch batch = llama_batch_get_one(allTokens.data(), allTokens.size());
        int n_eval = llama_decode(ctx, batch);
        if (n_eval != 0) {
            spdlog::error("Failed to evaluate model");
            break;
        }

        const float* logits = llama_get_logits(ctx);

        int nextToken = -1;

        if (temperature <= 0.0f) {
            nextToken = std::max_element(logits, logits + n_vocab) - logits;
        }
        else {
            std::vector<float> probs(n_vocab);
            float sum = 0.0f;
            for (int j = 0; j < n_vocab; ++j) {
                probs[j] = expf(logits[j] / temperature);
                sum += probs[j];
            }
            if (sum > 0.0f) {
                for (int j = 0; j < n_vocab; ++j) {
                    probs[j] /= sum;
                }
            }
            std::discrete_distribution<int> dist(probs.begin(), probs.end());
            nextToken = dist(gen);
        }

        if (nextToken == llama_token_eos(vocab)) {
            break;
        }

        allTokens.push_back(nextToken);
    }

    if (allTokens.size() > inputTokens.size()) {
        std::vector<int> responseTokens(allTokens.begin() + inputTokens.size(), allTokens.end());
        return tokensToString(responseTokens);
    }

    return "";
}

void LLMEngine::generateStreaming(const std::string& prompt,
    const std::string& system,
    int maxTokens,
    float temperature,
    std::function<void(const std::string&)> onToken,
    std::function<void()> onComplete) {
    if (!isLoaded()) {
        spdlog::error("LLMEngine not loaded");
        if (onComplete) onComplete();
        return;
    }

    std::string fullPrompt;
    if (!system.empty()) {
        fullPrompt = "<|im_start|>system\n" + system + "<|im_end|>\n";
    }
    fullPrompt += "<|im_start|>user\n" + prompt + "<|im_end|>\n";
    fullPrompt += "<|im_start|>assistant\n";

    std::vector<int> inputTokens = stringToTokens(fullPrompt);
    if (inputTokens.empty()) {
        if (onComplete) onComplete();
        return;
    }

    generateTokensStreaming(inputTokens, maxTokens, temperature, onToken);

    if (onComplete) onComplete();
}

std::string LLMEngine::getModelInfo() const {
    if (!model) return "No model loaded";

    std::string info;
    info += "Model size: " + std::to_string(llama_model_size(model) / 1024 / 1024) + " MB\n";
    info += "Context size: " + std::to_string(llama_n_ctx(ctx)) + "\n";
    info += "Vocabulary size: " + std::to_string(llama_n_vocab(vocab));
    return info;
}

int LLMEngine::countTokens(const std::string& text) const {
    if (!isLoaded()) return 0;

    // ʹ����ʱ batch ����ȡ token ����
    std::vector<int> tokens = stringToTokens(text);
    return static_cast<int>(tokens.size());
}

bool LLMEngine::initSSDStreaming(const std::string& expertDir,
    size_t cacheSizeMB,
    size_t gpuCacheSizeMB) {
    if (!isLoaded()) {
        spdlog::error("Model must be loaded before initializing SSD streaming");
        return false;
    }

    SSDStreamerConfig config;
    config.expertDir = expertDir;
    config.cacheSizeMB = cacheSizeMB;
    config.gpuCacheSizeMB = gpuCacheSizeMB;
    config.useMemoryMap = true;
    config.ioThreads = 4;
    config.enablePrefetch = true;
    config.prefetchDepth = 1;

    // ���Դ� manifest.json ��ȡģ�Ͳ���
    std::string manifestPath = expertDir + "/manifest.json";
    std::ifstream mf(manifestPath);
    if (mf.is_open()) {
        spdlog::info("Found expert manifest: {}", manifestPath);
        // ������Խ��� JSON ���Զ����ò���
        // �򻯰棺ʹ��Ĭ�ϵ� Qwen3.5-397B ����
        mf.close();
    }

    // Qwen3.5-397B-A17B �Ĳ���
    // ������ģ�Ͳ�ͬ�����޸���Щֵ
    config.numLayers = 60;
    config.numExperts = 128;     // ÿ���·��ר����
    config.activeExperts = 4;    // ÿ�� token ���� K=4 ��
    config.sharedExperts = 1;    // ����ר��
    config.hiddenDim = 4096;
    config.quantBits = 4;        // Q4 ����
    config.groupSize = 128;

    m_ssdStreamer = std::make_unique<SSDExpertStreamer>();
    if (!m_ssdStreamer->init(config)) {
        spdlog::error("Failed to initialize SSD Expert Streamer");
        m_ssdStreamer.reset();
        return false;
    }

    spdlog::info("SSD Expert Streaming initialized successfully!");
    spdlog::info("{}", m_ssdStreamer->getStatusString());
    return true;
}

std::string LLMEngine::getSSDStreamerStatus() const {
    if (!m_ssdStreamer) return "SSD Streaming: not initialized";
    return m_ssdStreamer->getStatusString();
}

bool LLMEngine::isSSDStreamingEnabled() const {
    return m_ssdStreamer != nullptr && m_ssdStreamer->isInitialized();
}
#endif  // CLOSECRAB_HAS_LOCAL_LLM