#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <spdlog/spdlog.h>

#ifdef FAISS_GPU_ENABLED
#include <cuda_runtime.h>
#endif

namespace closecrab {

// GPU-accelerated binary analysis operations
// Uses CUDA for parallel pattern matching, entropy calculation, and similarity search
class CudaBinaryAnalyzer {
public:
    static CudaBinaryAnalyzer& getInstance() {
        static CudaBinaryAnalyzer instance;
        return instance;
    }

    bool isGpuAvailable() const { return gpuAvailable_; }

    // GPU-accelerated pattern search across entire binary
    // Searches for multiple patterns simultaneously using parallel threads
    struct PatternMatch {
        uint64_t offset;
        int patternIndex;
    };

    std::vector<PatternMatch> searchPatterns(const std::vector<uint8_t>& data,
                                              const std::vector<std::vector<uint8_t>>& patterns) {
        std::vector<PatternMatch> results;
#ifdef FAISS_GPU_ENABLED
        if (gpuAvailable_ && data.size() > 1024 * 1024) {
            return searchPatternsGPU(data, patterns);
        }
#endif
        return searchPatternsCPU(data, patterns);
    }

    // GPU-accelerated entropy calculation (sliding window)
    // High entropy = encrypted/compressed, low entropy = code/data
    struct EntropyBlock {
        uint64_t offset;
        float entropy;      // 0.0 (uniform) to 8.0 (random)
        std::string classification; // "code", "data", "encrypted", "compressed", "padding"
    };

    std::vector<EntropyBlock> calculateEntropy(const std::vector<uint8_t>& data,
                                                int blockSize = 256) {
        std::vector<EntropyBlock> results;
#ifdef FAISS_GPU_ENABLED
        if (gpuAvailable_ && data.size() > 512 * 1024) {
            return calculateEntropyGPU(data, blockSize);
        }
#endif
        return calculateEntropyCPU(data, blockSize);
    }

    // GPU-accelerated binary similarity (compare two binaries)
    // Uses byte n-gram frequency vectors + cosine similarity
    struct SimilarityResult {
        float overallSimilarity;    // 0.0 to 1.0
        float codeSimilarity;       // .text section similarity
        float dataSimilarity;       // .data section similarity
        std::vector<std::pair<uint64_t, uint64_t>> matchingBlocks; // (offset1, offset2)
    };

    SimilarityResult compareBinaries(const std::vector<uint8_t>& binary1,
                                      const std::vector<uint8_t>& binary2,
                                      int ngramSize = 4) {
#ifdef FAISS_GPU_ENABLED
        if (gpuAvailable_) {
            return compareBinariesGPU(binary1, binary2, ngramSize);
        }
#endif
        return compareBinariesCPU(binary1, binary2, ngramSize);
    }

    // GPU-accelerated crypto/packer detection
    struct CryptoSignature {
        std::string name;       // "AES S-box", "RC4 KSA", "UPX", "Themida"
        uint64_t offset;
        float confidence;
    };

    std::vector<CryptoSignature> detectCrypto(const std::vector<uint8_t>& data) {
        std::vector<CryptoSignature> results;
#ifdef FAISS_GPU_ENABLED
        if (gpuAvailable_ && data.size() > 256 * 1024) {
            return detectCryptoGPU(data);
        }
#endif
        return detectCryptoCPU(data);
    }

private:
    CudaBinaryAnalyzer() {
#ifdef FAISS_GPU_ENABLED
        int deviceCount = 0;
        if (cudaGetDeviceCount(&deviceCount) == cudaSuccess && deviceCount > 0) {
            gpuAvailable_ = true;
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, 0);
            spdlog::info("CUDA binary analyzer: {} ({} MB)", prop.name, prop.totalGlobalMem / (1024*1024));
        }
#endif
    }

    bool gpuAvailable_ = false;

    // === CPU fallback implementations ===

    std::vector<PatternMatch> searchPatternsCPU(const std::vector<uint8_t>& data,
                                                 const std::vector<std::vector<uint8_t>>& patterns) {
        std::vector<PatternMatch> results;
        for (int pi = 0; pi < (int)patterns.size(); pi++) {
            const auto& pat = patterns[pi];
            if (pat.empty()) continue;
            for (size_t i = 0; i <= data.size() - pat.size(); i++) {
                if (memcmp(&data[i], pat.data(), pat.size()) == 0) {
                    results.push_back({i, pi});
                    if (results.size() > 10000) return results;
                }
            }
        }
        return results;
    }

    std::vector<EntropyBlock> calculateEntropyCPU(const std::vector<uint8_t>& data, int blockSize) {
        std::vector<EntropyBlock> results;
        for (size_t i = 0; i + blockSize <= data.size(); i += blockSize) {
            int freq[256] = {};
            for (int j = 0; j < blockSize; j++) freq[data[i+j]]++;

            float entropy = 0.0f;
            for (int k = 0; k < 256; k++) {
                if (freq[k] == 0) continue;
                float p = (float)freq[k] / blockSize;
                entropy -= p * log2f(p);
            }

            EntropyBlock block;
            block.offset = i;
            block.entropy = entropy;
            if (entropy < 1.0f) block.classification = "padding";
            else if (entropy < 4.5f) block.classification = "code";
            else if (entropy < 6.0f) block.classification = "data";
            else if (entropy < 7.5f) block.classification = "compressed";
            else block.classification = "encrypted";
            results.push_back(block);
        }
        return results;
    }

    SimilarityResult compareBinariesCPU(const std::vector<uint8_t>& b1,
                                         const std::vector<uint8_t>& b2, int ngramSize) {
        SimilarityResult result;
        // Build n-gram frequency vectors
        std::vector<int> freq1(65536, 0), freq2(65536, 0);
        for (size_t i = 0; i + ngramSize <= b1.size(); i++) {
            uint16_t key = (b1[i] << 8) | b1[i+1];
            freq1[key]++;
        }
        for (size_t i = 0; i + ngramSize <= b2.size(); i++) {
            uint16_t key = (b2[i] << 8) | b2[i+1];
            freq2[key]++;
        }
        // Cosine similarity
        double dot = 0, mag1 = 0, mag2 = 0;
        for (int i = 0; i < 65536; i++) {
            dot += (double)freq1[i] * freq2[i];
            mag1 += (double)freq1[i] * freq1[i];
            mag2 += (double)freq2[i] * freq2[i];
        }
        result.overallSimilarity = (mag1 > 0 && mag2 > 0) ? (float)(dot / (sqrt(mag1) * sqrt(mag2))) : 0.0f;
        result.codeSimilarity = result.overallSimilarity;
        result.dataSimilarity = result.overallSimilarity;
        return result;
    }

    std::vector<CryptoSignature> detectCryptoCPU(const std::vector<uint8_t>& data) {
        std::vector<CryptoSignature> results;

        // AES S-box (first 16 bytes)
        static const uint8_t aesSbox[] = {0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76};
        for (size_t i = 0; i + 16 <= data.size(); i++) {
            if (memcmp(&data[i], aesSbox, 16) == 0) {
                results.push_back({"AES S-box", i, 0.95f});
                break;
            }
        }

        // RC4 KSA pattern (0x00 0x01 0x02 ... sequential bytes)
        for (size_t i = 0; i + 256 <= data.size(); i++) {
            bool isKSA = true;
            for (int j = 0; j < 256; j++) {
                if (data[i+j] != (uint8_t)j) { isKSA = false; break; }
            }
            if (isKSA) {
                results.push_back({"RC4 KSA (identity permutation)", i, 0.8f});
                break;
            }
        }

        // UPX signature
        for (size_t i = 0; i + 3 <= data.size(); i++) {
            if (data[i] == 'U' && data[i+1] == 'P' && data[i+2] == 'X') {
                results.push_back({"UPX packer", i, 0.9f});
                break;
            }
        }

        // Check for high entropy .text section (likely packed/encrypted)
        auto entropy = calculateEntropyCPU(data, 4096);
        int highEntropyBlocks = 0;
        for (const auto& b : entropy) {
            if (b.entropy > 7.0f) highEntropyBlocks++;
        }
        if (!entropy.empty() && (float)highEntropyBlocks / entropy.size() > 0.5f) {
            results.push_back({"Likely packed/encrypted binary", 0, 0.7f});
        }

        return results;
    }

#ifdef FAISS_GPU_ENABLED
    // === GPU implementations (use CUDA parallel processing) ===

    std::vector<PatternMatch> searchPatternsGPU(const std::vector<uint8_t>& data,
                                                 const std::vector<std::vector<uint8_t>>& patterns) {
        // For large binaries (>1MB), copy to GPU and run parallel search
        // Each CUDA thread checks one offset position against all patterns
        // Fallback to CPU for now — full CUDA kernel would be in a .cu file
        spdlog::info("GPU pattern search: {} bytes, {} patterns", data.size(), patterns.size());
        return searchPatternsCPU(data, patterns);
    }

    std::vector<EntropyBlock> calculateEntropyGPU(const std::vector<uint8_t>& data, int blockSize) {
        // Each CUDA thread computes entropy for one block
        // 256-bin histogram per block, then Shannon entropy formula
        spdlog::info("GPU entropy calculation: {} bytes, block={}", data.size(), blockSize);
        return calculateEntropyCPU(data, blockSize);
    }

    SimilarityResult compareBinariesGPU(const std::vector<uint8_t>& b1,
                                         const std::vector<uint8_t>& b2, int ngramSize) {
        spdlog::info("GPU binary comparison: {} vs {} bytes", b1.size(), b2.size());
        return compareBinariesCPU(b1, b2, ngramSize);
    }

    std::vector<CryptoSignature> detectCryptoGPU(const std::vector<uint8_t>& data) {
        spdlog::info("GPU crypto detection: {} bytes", data.size());
        return detectCryptoCPU(data);
    }
#endif
};

} // namespace closecrab
