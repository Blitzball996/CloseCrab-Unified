#pragma once
// StrataManager — lifecycle and hardware-probe bridge for the Strata local
// inference engine (github.com/Niko1221/Strata, MIT).
//
// Usage:
//   auto& mgr = StrataManager::getInstance();
//   auto probe = mgr.probe();          // runs strata_probe.py, returns hw + model list
//   if (mgr.start(probe.recommendedId)) {
//       // Strata is up at http://127.0.0.1:<mgr.port()>
//   }
//   // ... use RemoteAPIClient pointed at mgr.baseUrl() ...
//   mgr.stop();   // called automatically on destruction

#include <string>
#include <vector>
#include <optional>

namespace closecrab {

// One entry from the model table produced by strata_probe.py.
struct StrataModel {
    std::string family;         // "qwen", "deepseek41", etc.
    std::string familyTitle;    // "Qwen3.8-Flash-Next"
    std::string model;          // "IQ3_XXS"
    std::string id;             // run-config tag used to start the server
    std::string about;          // human description
    double      downloadGb   = 0;
    double      needVramGb   = 0;
    double      needRamGb    = 0;
    double      spillGb      = 0;
    std::string verdict;        // "fits" | "tight" | "no"
    std::string words;          // plain-words verdict
    bool        recommended  = false;
    bool        installed    = false;
    bool        experimental = false;
};

// Hardware summary from strata_probe.py.
struct StrataHardware {
    std::string gpuName;
    double      vramGb      = 0;
    int         gpuCount    = 0;
    double      ramGb       = 0;
    double      diskFreeGb  = 0;
    std::string summary;    // e.g. "RTX 3060 12 GB · 32 GB RAM · 480 GB free"
    std::string backend;    // "cuda", "hip", "cpu", …
    std::string os;
};

// Result of a probe() call.
struct StrataProbeResult {
    bool                      ok = false;
    std::string               error;
    StrataHardware            hardware;
    std::optional<StrataModel> recommended;
    std::vector<StrataModel>  models;
};

class StrataManager {
public:
    static StrataManager& getInstance();

    // Run strata_probe.py and return hardware + model list.
    // Safe to call before start(); does not require the server to be running.
    StrataProbeResult probe(const std::string& strataRoot = "");

    // Start the Strata inference server for the given model id (from StrataModel::id).
    // Finds a free port, launches `python -m serve.server --engine strata
    // --config <id>.json --port <port>`, then polls /health until ready (up to
    // timeoutSec seconds). Returns true when the server is healthy.
    bool start(const std::string& modelId,
               const std::string& strataRoot = "",
               int  timeoutSec = 120,
               int  port       = 0);    // 0 = pick a free port

    // Stop the running Strata server (sends SIGTERM / TerminateProcess).
    void stop();

    // True after a successful start().
    bool isRunning() const;

    // Base URL of the running server, e.g. "http://127.0.0.1:8080".
    std::string baseUrl() const;

    // Port the server is listening on (0 if not started).
    int port() const { return port_; }

    // Path to the vendored Strata source tree
    // (default: <exe_dir>/../external/strata, or STRATA_ROOT env var).
    static std::string defaultStrataRoot();

    // Find a Python interpreter (checks STRATA_PYTHON env, venv, system).
    static std::string findPython();

    ~StrataManager();

private:
    StrataManager() = default;
    StrataManager(const StrataManager&) = delete;
    StrataManager& operator=(const StrataManager&) = delete;

    bool pollHealth(int timeoutSec) const;

    int         port_       = 0;
    bool        running_    = false;

#if defined(_WIN32)
    void*       processHandle_  = nullptr;   // HANDLE
    void*       processThread_  = nullptr;   // HANDLE (needed for CloseHandle)
#else
    int         childPid_   = 0;
#endif
};

} // namespace closecrab
