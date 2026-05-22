#pragma once
#include <atomic>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

namespace closecrab {

class CtrlBHandler {
public:
    static CtrlBHandler& getInstance() {
        static CtrlBHandler instance;
        return instance;
    }

    void startListening() {
        if (listening_) return;
        listening_ = true;
        triggered_ = false;
#ifdef _WIN32
        listenerThread_ = std::thread([this]() {
            while (listening_) {
                if (_kbhit()) {
                    int ch = _getch();
                    if (ch == 2) { // Ctrl+B = ASCII 2
                        triggered_ = true;
                        listening_ = false;
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
#endif
    }

    void stopListening() {
        listening_ = false;
#ifdef _WIN32
        if (listenerThread_.joinable()) listenerThread_.join();
#endif
    }

    bool wasTriggered() {
        bool val = triggered_.load();
        triggered_ = false;
        return val;
    }

    bool isListening() const { return listening_; }

private:
    CtrlBHandler() = default;
    ~CtrlBHandler() { stopListening(); }

    std::atomic<bool> listening_{false};
    std::atomic<bool> triggered_{false};
    std::thread listenerThread_;
};

} // namespace closecrab
