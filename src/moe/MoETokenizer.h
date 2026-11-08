#pragma once
// MoETokenizer.h — Qwen BPE tokenizer using llama.cpp's vocab API.
// Loads the tokenizer.gguf file from the model pack directory.
// Used by MoELLMClient to convert text <-> token ids before/after
// sending requests to closecrab-moe-engine.

#include <string>
#include <vector>
#include <cstdint>

struct llama_model;
struct llama_vocab;

namespace closecrab::moe {

class MoETokenizer {
public:
    MoETokenizer();
    ~MoETokenizer();

    // Load tokenizer from a GGUF file (tokenizer.gguf or any model shard).
    // Returns true on success. Safe to call multiple times (reloads).
    bool load(const std::string& ggufPath);

    // True after a successful load().
    bool isLoaded() const { return model_ != nullptr; }

    // Encode text -> token ids. add_special=true inserts BOS/EOS per model config.
    std::vector<int32_t> encode(const std::string& text, bool addSpecial = false) const;

    // Decode token ids -> text.
    std::string decode(const std::vector<int32_t>& ids) const;

    // Single token id -> piece string.
    std::string tokenToPiece(int32_t tokenId) const;

    // Number of tokens in vocabulary.
    int vocabSize() const;

    // Qwen chat format: build the full prompt string from messages.
    // Then call encode() on it for the token ids to feed the engine.
    static std::string buildQwenPrompt(
        const std::string& systemPrompt,
        const std::vector<std::pair<std::string,std::string>>& turns);

private:
    llama_model*       model_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
};

} // namespace closecrab::moe
