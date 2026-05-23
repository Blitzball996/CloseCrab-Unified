#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace closecrab {

struct FileChangeEvent {
    std::string path;
    std::string action; // "created", "modified", "deleted", "renamed"
};

class FileWatcher {
public:
    using Callback = std::function<void(const FileChangeEvent&)>;

    static FileWatcher& getInstance() {
        static FileWatcher instance;
        return instance;
    }

    void watch(const std::string& directory, Callback callback) {
        if (watching_) stop();
        directory_ = directory;
        callback_ = callback;
        watching_ = true;

#ifdef _WIN32
        watchThread_ = std::thread([this]() {
            HANDLE hDir = CreateFileA(directory_.c_str(), FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (hDir == INVALID_HANDLE_VALUE) {
                spdlog::warn("FileWatcher: cannot open directory {}", directory_);
                watching_ = false;
                return;
            }

            char buffer[4096];
            DWORD bytesReturned;
            while (watching_) {
                if (ReadDirectoryChangesW(hDir, buffer, sizeof(buffer), TRUE,
                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                    &bytesReturned, nullptr, nullptr)) {

                    FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buffer;
                    do {
                        std::wstring wname(info->FileName, info->FileNameLength / sizeof(WCHAR));
                        std::string name(wname.begin(), wname.end());

                        FileChangeEvent event;
                        event.path = directory_ + "/" + name;
                        switch (info->Action) {
                            case FILE_ACTION_ADDED: event.action = "created"; break;
                            case FILE_ACTION_REMOVED: event.action = "deleted"; break;
                            case FILE_ACTION_MODIFIED: event.action = "modified"; break;
                            case FILE_ACTION_RENAMED_OLD_NAME: event.action = "renamed"; break;
                            default: event.action = "unknown"; break;
                        }
                        if (callback_) callback_(event);

                        if (info->NextEntryOffset == 0) break;
                        info = (FILE_NOTIFY_INFORMATION*)((char*)info + info->NextEntryOffset);
                    } while (true);
                }
            }
            CloseHandle(hDir);
        });
#endif
    }

    void stop() {
        watching_ = false;
        if (watchThread_.joinable()) watchThread_.join();
    }

    bool isWatching() const { return watching_; }
    const std::string& getDirectory() const { return directory_; }

private:
    FileWatcher() = default;
    ~FileWatcher() { stop(); }
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    std::atomic<bool> watching_{false};
    std::string directory_;
    Callback callback_;
    std::thread watchThread_;
};

} // namespace closecrab
