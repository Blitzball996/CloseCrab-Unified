#pragma once
#include <string>

namespace closecrab {

// Information about an available update fetched from GitHub Releases.
struct UpdateInfo {
    std::string currentVersion;  // CLOSECRAB_VERSION, e.g. "0.4.2"
    std::string latestVersion;   // latest tag without 'v', e.g. "0.4.3"
    std::string downloadUrl;     // direct browser_download_url for this platform
    std::string assetName;       // e.g. "CloseCrab-Linux-x86_64.tar.gz"
    bool hasUpdate = false;
    std::string errorMsg;        // non-empty if check failed
};

// Query GitHub Releases API for the latest version and pick the right asset
// for the current platform/arch. Returns in <1s on a good connection (timeout=8s).
UpdateInfo checkForUpdate();

// Download and apply the update in-place.
//   - Linux/macOS: replaces the running binary atomically; user must restart.
//   - Windows: downloads .new file, writes a helper bat, user must restart.
// Returns true if the binary was replaced (or staged for replacement on restart).
// On failure sets info.errorMsg.
bool installUpdate(UpdateInfo& info);

} // namespace closecrab
