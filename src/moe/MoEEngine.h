#pragma once
// MoEEngine.h — manages the closecrab-moe-engine binary lifecycle.
// The MoE engine (125B model on consumer GPU) runs as a child process
// communicating over stdio pipes. CloseCrab spawns it invisibly; the user
// only knows they chose "Local" mode.

#include "MoEHardwareProbe.h"
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace closecrab::moe {

// Callback fired for each generated token id (int32_t).
using TokenCallback = std::function<void(int32_t tokenId, bool isEos)>;

struct EngineConfig {
    std::string modelPath;      // path to GGUF model/expert pack directory
    std::string runConfigPath;  // path to moe-<id>.json run config (optional)
    int         contextLen   = 32768;
    int         nParallel    = 1;
    int         gpuIndex     = -1;  // -1 = default (first usable GPU)
    bool        prefillAuto  = true;
    std::string extraArgs;          // appended verbatim to engine CLI
};

class MoEEngine {
public:
    MoEEngine();
    ~MoEEngine();

    // Find the engine binary beside closecrab.exe or in PATH.
    // Returns empty string if not found.
    static std::string findEngineBinary();

    // Load model and start engine process. Returns true on success.
    bool start(const EngineConfig& cfg);

    // Stop the engine process (SIGTERM / TerminateProcess).
    void stop();

    bool isRunning() const { return running_; }

    // Run one generation: feed token IDs, receive generated token IDs via cb.
    // Blocking until EOS or maxTokens.
    bool generate(const std::vector<int32_t>& promptIds,
                  int maxTokens,
                  float temperature,
                  TokenCallback cb);

    // Tokenize text → IDs using the engine's own tokenizer invocation.
    // Returns empty on failure.
    std::vector<int32_t> tokenize(const std::string& text);

    // Detokenize IDs → text.
    std::string detokenize(const std::vector<int32_t>& ids);

    // Get engine version string.
    static std::string engineVersion(const std::string& engineBin);

private:
    bool running_    = false;
    int  childPid_   = 0;
    int  stdinFd_    = -1;
    int  stdoutFd_   = -1;

#if defined(_WIN32)
    void* hProcess_  = nullptr;
    void* hThread_   = nullptr;
    void* hStdinW_   = nullptr;   // write end of stdin pipe
    void* hStdoutR_  = nullptr;   // read end of stdout pipe
#endif

    bool writeToEngine(const void* data, size_t len);
    bool readLine(std::string& out, int timeoutMs = 30000);
};

} // namespace closecrab::moe
