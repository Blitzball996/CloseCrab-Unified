#pragma once
// MoETokenizer.h — Qwen BPE tokenizer.
// Primary path: HF JSON format (vocab.json + merges.txt) from iq_pack.py output.
// Fallback path: llama.cpp GGUF vocab API (requires a tokenizer.gguf).

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

struct llama_model;
struct llama_vocab;

namespace closecrab::moe {

class MoETokenizer {
public:
    MoETokenizer();
    ~MoETokenizer();

    // Load tokenizer from a GGUF file (tokenizer.gguf or any model shard).
    // Returns true on success.
    bool load(const std::string& ggufPath);

    // Load tokenizer from HF JSON files (vocab.json + merges.txt in a directory).
    // This is the format produced by iq_pack.py's tokenizer/ output.
    // Returns true on success.
    bool loadFromJson(const std::string& tokenizerDir);

    // True after a successful load() or loadFromJson().
    bool isLoaded() const { return model_ != nullptr || !vocab_.empty(); }

    // Encode text -> token ids.
    std::vector<int32_t> encode(const std::string& text, bool addSpecial = false) const;

    // Decode token ids -> text.
    std::string decode(const std::vector<int32_t>& ids) const;

    // Single token id -> piece string.
    std::string tokenToPiece(int32_t tokenId) const;

    // Number of tokens in vocabulary.
    int vocabSize() const;

    // Qwen chat format: build the full prompt string from messages.
    static std::string buildQwenPrompt(
        const std::string& systemPrompt,
        const std::vector<std::pair<std::string,std::string>>& turns);

private:
    // llama.cpp GGUF path
    llama_model*       model_ = nullptr;
    const llama_vocab* llamaVocab_ = nullptr;

    // HF JSON BPE path
    std::unordered_map<std::string, int32_t> vocab_;     // token_str -> id
    std::vector<std::string>                 idToToken_;  // id -> token_str
    std::unordered_map<std::string, int32_t> mergeRanks_; // "a b" -> rank
    int32_t bosId_ = 0, eosId_ = 0;

    // BPE helpers
    std::vector<std::string> bpeEncode(const std::string& word) const;
    std::string bytesToUnicode(unsigned char b) const;
    std::string unicodeToBytes(const std::string& s) const;
};

} // namespace closecrab::moe
