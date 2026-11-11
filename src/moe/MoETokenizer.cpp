// MoETokenizer.cpp — Qwen BPE tokenizer.
// Primary path: HF JSON format (vocab.json + merges.txt) from iq_pack.py output.
// Fallback path: llama.cpp GGUF vocab API.
#include "MoETokenizer.h"
#include <spdlog/spdlog.h>
#ifdef CLOSECRAB_HAS_LOCAL_LLM
#include <llama.h>
#include "../../external/llama.cpp/src/unicode.h"
#endif
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>

namespace fs = std::filesystem;
namespace closecrab::moe {

MoETokenizer::MoETokenizer() = default;

// ---- HF JSON BPE path ------------------------------------------------------

// GPT-2 byte-to-unicode mapping (same as tiktoken/HF)
static std::string byteToUnicodeChar(unsigned char b) {
    // GPT-2 maps bytes 33-126, 161-172, 174-255 to themselves (as UTF-8),
    // and the rest to U+0100 onwards.
    static const std::string table = []() {
        std::string t(256, 0);
        // Fill direct-map range
        int n = 0;
        for (int b = 0; b < 256; ++b) {
            if ((b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255)) {
                // encode as UTF-8 of the codepoint = b
                t[b] = (char)b; // placeholder; we'll use full encoding below
            }
        }
        return t;
    }();
    // Build the full table on first call
    static std::unordered_map<unsigned char, std::string> btou = []() {
        std::unordered_map<unsigned char, std::string> m;
        int extra = 256; // start of extension codepoints
        for (int b = 0; b < 256; ++b) {
            if ((b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255)) {
                // UTF-8 encode the byte value as a unicode codepoint
                if (b < 0x80) {
                    m[(unsigned char)b] = std::string(1, (char)b);
                } else {
                    // 2-byte UTF-8: 0xC0 | (b>>6), 0x80 | (b&0x3F)
                    m[(unsigned char)b] = std::string{(char)(0xC0 | (b >> 6)), (char)(0x80 | (b & 0x3F))};
                }
            } else {
                // Map to codepoint extra++ (U+0100+)
                int cp = extra++;
                // UTF-8 encode cp
                if (cp < 0x800) {
                    m[(unsigned char)b] = std::string{(char)(0xC0 | (cp >> 6)), (char)(0x80 | (cp & 0x3F))};
                } else {
                    m[(unsigned char)b] = std::string{(char)(0xE0 | (cp >> 12)),
                                                       (char)(0x80 | ((cp >> 6) & 0x3F)),
                                                       (char)(0x80 | (cp & 0x3F))};
                }
            }
        }
        return m;
    }();
    auto it = btou.find(b);
    return it != btou.end() ? it->second : std::string(1, (char)b);
}

bool MoETokenizer::loadFromJson(const std::string& dir) {
    fs::path vocabPath = fs::path(dir) / "vocab.json";
    fs::path mergesPath = fs::path(dir) / "merges.txt";
    fs::path tokJsonPath = fs::path(dir) / "tokenizer.json";
    if (!fs::exists(vocabPath) || !fs::exists(mergesPath)) {
        spdlog::error("MoETokenizer::loadFromJson: vocab.json or merges.txt not found in {}", dir);
        return false;
    }
    // Load vocab.json: {"token_str": id, ...}
    try {
        std::ifstream vf(vocabPath);
        nlohmann::json vj;
        vf >> vj;
        vocab_.clear();
        idToToken_.clear();
        int maxId = 0;
        for (auto& [k, v] : vj.items()) {
            int32_t id = v.get<int32_t>();
            vocab_[k] = id;
            if (id > maxId) maxId = id;
        }
        idToToken_.resize((size_t)(maxId + 1));
        for (auto& [k, v] : vj.items())
            idToToken_[(size_t)v.get<int32_t>()] = k;
        spdlog::info("MoETokenizer: loaded {} vocab entries from {}", vocab_.size(), vocabPath.string());
    } catch (std::exception& e) {
        spdlog::error("MoETokenizer: failed to parse vocab.json: {}", e.what());
        return false;
    }
    // Load merges.txt: each line "a b" gives merge rank
    mergeRanks_.clear();
    try {
        std::ifstream mf(mergesPath);
        std::string line;
        int rank = 0;
        while (std::getline(mf, line)) {
            if (line.empty() || line.rfind("#version:", 0) == 0) continue;
            mergeRanks_[line] = rank++;
        }
        spdlog::info("MoETokenizer: loaded {} BPE merges", mergeRanks_.size());
    } catch (std::exception& e) {
        spdlog::error("MoETokenizer: failed to parse merges.txt: {}", e.what());
        return false;
    }
    // Load special token IDs from tokenizer.json if present
    bosId_ = 248044; eosId_ = 248046; // Qwen defaults
    if (fs::exists(tokJsonPath)) {
        try {
            std::ifstream tf(tokJsonPath);
            nlohmann::json tj; tf >> tj;
            if (tj.contains("special_ids")) {
                auto& s = tj["special_ids"];
                if (s.contains("tokenizer.ggml.bos_token_id")) bosId_ = s["tokenizer.ggml.bos_token_id"].get<int32_t>();
                if (s.contains("tokenizer.ggml.eos_token_id")) eosId_ = s["tokenizer.ggml.eos_token_id"].get<int32_t>();
            }
        } catch (...) {}
    }
    return true;
}


bool MoETokenizer::load(const std::string& ggufPath) {
    if (!fs::exists(ggufPath)) {
        spdlog::error("MoETokenizer: gguf file not found: {}", ggufPath);
        return false;
    }
    if (model_) { llama_model_free(model_); model_ = nullptr; llamaVocab_ = nullptr; }

    llama_model_params params = llama_model_default_params();
    params.n_gpu_layers = 0;
    params.vocab_only   = true;

    model_ = llama_model_load_from_file(ggufPath.c_str(), params);
    if (!model_) {
        spdlog::error("MoETokenizer: failed to load vocab from {}", ggufPath);
        return false;
    }
    llamaVocab_ = llama_model_get_vocab(model_);
    if (!llamaVocab_) {
        spdlog::error("MoETokenizer: vocab is null after load");
        llama_model_free(model_); model_ = nullptr;
        return false;
    }
    spdlog::info("MoETokenizer: loaded vocab ({} tokens) from {}",
                 llama_vocab_n_tokens(llamaVocab_), ggufPath);
    return true;
}

// ---- Unconditional: destructor, vocabSize, BPE helpers ----------------------

MoETokenizer::~MoETokenizer() {
#ifdef CLOSECRAB_HAS_LOCAL_LLM
    if (model_) { llama_model_free(model_); model_ = nullptr; }
#endif
}

int MoETokenizer::vocabSize() const {
#ifdef CLOSECRAB_HAS_LOCAL_LLM
    if (llamaVocab_) return llama_vocab_n_tokens(llamaVocab_);
#endif
    return (int)idToToken_.size();
}

// ---- BPE encode (GPT-2 byte-level) -----------------------------------------

// Decode a UTF-8 BPE token string back to raw bytes.
// GPT-2 maps bytes through a unicode table; we invert that here.
static std::string bpeTokenToBytes(const std::string& token) {
    // Build inverse map once
    static const std::unordered_map<std::string, unsigned char> utob = []() {
        std::unordered_map<std::string, unsigned char> m;
        int extra = 256;
        for (int b = 0; b < 256; ++b) {
            std::string enc;
            if ((b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255)) {
                if (b < 0x80) enc = std::string(1, (char)b);
                else enc = std::string{(char)(0xC0|(b>>6)), (char)(0x80|(b&0x3F))};
            } else {
                int cp = extra++;
                if (cp < 0x800) enc = std::string{(char)(0xC0|(cp>>6)), (char)(0x80|(cp&0x3F))};
                else enc = std::string{(char)(0xE0|(cp>>12)), (char)(0x80|((cp>>6)&0x3F)), (char)(0x80|(cp&0x3F))};
            }
            m[enc] = (unsigned char)b;
        }
        return m;
    }();
    // Decode token char-by-char (each char is 1 or 2 UTF-8 bytes in the mapping)
    std::string out;
    size_t i = 0;
    while (i < token.size()) {
        // Try 3-byte, 2-byte, 1-byte
        for (int len = 3; len >= 1; --len) {
            if (i + len > token.size()) continue;
            std::string sub = token.substr(i, len);
            auto it = utob.find(sub);
            if (it != utob.end()) { out += (char)it->second; i += len; goto next; }
        }
        out += token[i++]; // fallback: pass through
        next:;
    }
    return out;
}

std::vector<std::string> MoETokenizer::bpeEncode(const std::string& word) const {
    if (word.empty()) return {};
    // Convert each byte to its BPE unicode representation
    std::vector<std::string> chars;
    for (unsigned char b : word)
        chars.push_back(byteToUnicodeChar(b));

    // BPE merges
    while (chars.size() > 1) {
        int bestRank = std::numeric_limits<int>::max();
        int bestIdx = -1;
        for (int i = 0; i + 1 < (int)chars.size(); ++i) {
            std::string pair = chars[i] + " " + chars[i+1];
            auto it = mergeRanks_.find(pair);
            if (it != mergeRanks_.end() && it->second < bestRank) {
                bestRank = it->second;
                bestIdx = i;
            }
        }
        if (bestIdx < 0) break;
        std::string merged = chars[bestIdx] + chars[bestIdx+1];
        chars.erase(chars.begin() + bestIdx, chars.begin() + bestIdx + 2);
        chars.insert(chars.begin() + bestIdx, merged);
    }
    return chars;
}

std::vector<int32_t> MoETokenizer::encode(const std::string& text, bool addSpecial) const {
    if (llamaVocab_) {
        int n = llama_tokenize(llamaVocab_, text.c_str(), (int32_t)text.size(),
                               nullptr, 0, addSpecial, true);
        if (n == 0) return {};
        if (n < 0) n = -n;
        std::vector<int32_t> ids((size_t)n);
        int actual = llama_tokenize(llamaVocab_, text.c_str(), (int32_t)text.size(),
                                    ids.data(), n, addSpecial, true);
        if (actual < 0) {
            ids.resize((size_t)(-actual));
            actual = llama_tokenize(llamaVocab_, text.c_str(), (int32_t)text.size(),
                                    ids.data(), -actual, addSpecial, true);
        }
        if (actual <= 0) return {};
        ids.resize((size_t)actual);
        return ids;
    }
    if (vocab_.empty()) return {};

    std::vector<int32_t> ids;
    if (addSpecial) ids.push_back(bosId_);

    // Qwen35 pre-tokenization, identical to Strata's reference and llama.cpp.
    static const std::vector<std::string> patterns = {
        R"((?:'[sS]|'[tT]|'[rR][eE]|'[vV][eE]|'[mM]|'[lL][lL]|'[dD])|[^\r\n\p{L}\p{N}]?[\p{L}\p{M}]+|\p{N}| ?[^\s\p{L}\p{M}\p{N}]+[\r\n]*|\s*[\r\n]+|\s+(?!\S)|\s+)"
    };
    std::string cur;
    auto flushPiece = [&]() {
        for (const auto& encoded : unicode_regex_split(cur, patterns)) {
            // unicode_regex_split already maps bytes to GPT-2 characters.
            for (const auto& piece : bpeEncode(bpeTokenToBytes(encoded))) {
                ids.push_back(vocab_.at(piece));
            }
        }
        cur.clear();
    };
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] == '<') {
            auto end = text.find('>', i);
            if (end != std::string::npos) {
                auto token = text.substr(i, end - i + 1);
                auto it = vocab_.find(token);
                if (it != vocab_.end() && (token.rfind("<|", 0) == 0 ||
                    token == "<tool_call>" || token == "</tool_call>" ||
                    token == "<tool_response>" || token == "</tool_response>" ||
                    token == "<think>" || token == "</think>")) {
                    flushPiece();
                    ids.push_back(it->second);
                    i = end;
                    continue;
                }
            }
        }
        if (i == text.size()) flushPiece();
        else cur += text[i];
    }

    if (addSpecial) ids.push_back(eosId_);
    return ids;
}

std::string MoETokenizer::tokenToPiece(int32_t tokenId) const {
    if (llamaVocab_) {
        std::array<char, 256> buf{};
        int n = llama_token_to_piece(llamaVocab_, tokenId, buf.data(), (int32_t)buf.size(), 0, true);
        if (n <= 0) return "";
        return std::string(buf.data(), (size_t)n);
    }
    if (tokenId >= 0 && (size_t)tokenId < idToToken_.size())
        return bpeTokenToBytes(idToToken_[(size_t)tokenId]);
    return "";
}

std::string MoETokenizer::decode(const std::vector<int32_t>& ids) const {
    if (llamaVocab_) {
        if (ids.empty()) return "";
        size_t bufSize = ids.size() * 8 + 64;
        std::vector<char> buf(bufSize);
        int n = llama_detokenize(llamaVocab_, ids.data(), (int32_t)ids.size(),
                                 buf.data(), (int32_t)buf.size(), false, true);
        if (n < 0) {
            buf.resize((size_t)(-n) + 1);
            n = llama_detokenize(llamaVocab_, ids.data(), (int32_t)ids.size(),
                                 buf.data(), (int32_t)buf.size(), false, true);
        }
        if (n <= 0) return "";
        return std::string(buf.data(), (size_t)n);
    }
    // JSON path: decode each token piece and concatenate
    std::string out;
    for (int32_t id : ids) {
        if (id == eosId_ || id == bosId_) continue;
        out += tokenToPiece(id);
    }
    return out;
}


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
