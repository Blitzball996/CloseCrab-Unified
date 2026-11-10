// MoETokenizer.cpp — Qwen BPE tokenizer via llama.cpp vocab API.
#include "MoETokenizer.h"
#include <spdlog/spdlog.h>
#ifdef CLOSECRAB_HAS_LOCAL_LLM
#include <llama.h>
#endif
#include <array>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;
namespace closecrab::moe {

MoETokenizer::MoETokenizer() = default;

#ifdef CLOSECRAB_HAS_LOCAL_LLM
MoETokenizer::~MoETokenizer() {
    if (model_) { llama_model_free(model_); model_ = nullptr; }
}

bool MoETokenizer::load(const std::string& ggufPath) {
    if (!fs::exists(ggufPath)) {
        spdlog::error("MoETokenizer: gguf file not found: {}", ggufPath);
        return false;
    }
    if (model_) { llama_model_free(model_); model_ = nullptr; vocab_ = nullptr; }

    // Load with vocab only — no GPU layers needed for tokenization.
    llama_model_params params = llama_model_default_params();
    params.n_gpu_layers = 0;
    params.vocab_only   = true;

    model_ = llama_model_load_from_file(ggufPath.c_str(), params);
    if (!model_) {
        spdlog::error("MoETokenizer: failed to load vocab from {}", ggufPath);
        return false;
    }
    vocab_ = llama_model_get_vocab(model_);
    if (!vocab_) {
        spdlog::error("MoETokenizer: vocab is null after load");
        llama_model_free(model_); model_ = nullptr;
        return false;
    }
    spdlog::info("MoETokenizer: loaded vocab ({} tokens) from {}",
                 llama_vocab_n_tokens(vocab_), ggufPath);
    return true;
}

int MoETokenizer::vocabSize() const {
    return vocab_ ? llama_vocab_n_tokens(vocab_) : 0;
}

std::vector<int32_t> MoETokenizer::encode(const std::string& text,
                                            bool addSpecial) const {
    if (!vocab_) return {};
    // First pass: get required buffer size (returns negative on overflow).
    int n = llama_tokenize(vocab_, text.c_str(), static_cast<int32_t>(text.size()),
                           nullptr, 0, addSpecial, true);
    if (n == 0) return {};
    if (n < 0) n = -n;  // recover from overflow estimate
    std::vector<int32_t> ids(static_cast<size_t>(n));
    int actual = llama_tokenize(vocab_, text.c_str(),
                                static_cast<int32_t>(text.size()),
                                ids.data(), n, addSpecial, true);
    if (actual < 0) {
        spdlog::warn("MoETokenizer: tokenize buffer overflow, retrying");
        ids.resize(static_cast<size_t>(-actual));
        actual = llama_tokenize(vocab_, text.c_str(),
                                static_cast<int32_t>(text.size()),
                                ids.data(), -actual, addSpecial, true);
    }
    if (actual <= 0) return {};
    ids.resize(static_cast<size_t>(actual));
    return ids;
}

std::string MoETokenizer::tokenToPiece(int32_t tokenId) const {
    if (!vocab_) return "";
    std::array<char, 256> buf{};
    int n = llama_token_to_piece(vocab_, tokenId, buf.data(),
                                 static_cast<int32_t>(buf.size()), 0, true);
    if (n <= 0) return "";
    return std::string(buf.data(), static_cast<size_t>(n));
}

std::string MoETokenizer::decode(const std::vector<int32_t>& ids) const {
    if (!vocab_ || ids.empty()) return "";
    // Use llama_detokenize for whole-sequence decoding.
    // Buffer size: estimate 8 bytes/token, grow if needed.
    size_t bufSize = ids.size() * 8 + 64;
    std::vector<char> buf(bufSize);
    int n = llama_detokenize(vocab_, ids.data(), static_cast<int32_t>(ids.size()),
                             buf.data(), static_cast<int32_t>(buf.size()),
                             false, true);
    if (n < 0) {
        buf.resize(static_cast<size_t>(-n) + 1);
        n = llama_detokenize(vocab_, ids.data(), static_cast<int32_t>(ids.size()),
                             buf.data(), static_cast<int32_t>(buf.size()),
                             false, true);
    }
    if (n <= 0) return "";
    return std::string(buf.data(), static_cast<size_t>(n));
}

#else
MoETokenizer::~MoETokenizer() = default;
bool MoETokenizer::load(const std::string&) {
    spdlog::error("MoE tokenizer requires a build with local LLM support");
    return false;
}
int MoETokenizer::vocabSize() const { return 0; }
std::vector<int32_t> MoETokenizer::encode(const std::string&, bool) const { return {}; }
std::string MoETokenizer::tokenToPiece(int32_t) const { return {}; }
std::string MoETokenizer::decode(const std::vector<int32_t>&) const { return {}; }
#endif

// static
std::string MoETokenizer::buildQwenPrompt(
    const std::string& systemPrompt,
    const std::vector<std::pair<std::string,std::string>>& turns)
{
    // Qwen chat format (same as LLMEngine / LocalLLMClient):
    //   <|im_start|>system\n...\n<|im_end|>\n
    //   <|im_start|>user\n...\n<|im_end|>\n
    //   <|im_start|>assistant\n
    std::ostringstream s;
    if (!systemPrompt.empty())
        s << "<|im_start|>system\n" << systemPrompt << "\n<|im_end|>\n";
    for (auto& [role, content] : turns)
        s << "<|im_start|>" << role << "\n" << content << "\n<|im_end|>\n";
    s << "<|im_start|>assistant\n";
    return s.str();
}

} // namespace closecrab::moe
