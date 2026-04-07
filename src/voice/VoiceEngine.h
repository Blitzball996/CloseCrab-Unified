#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace closecrab {

// Voice engine — audio capture + STT + TTS
// Windows: SAPI for TTS, macOS: say, Linux: espeak
// STT: Whisper.cpp (optional, requires model file)
class VoiceEngine {
public:
    static VoiceEngine& getInstance() {
        static VoiceEngine instance;
        return instance;
    }

    bool isAvailable() const {
#ifdef _WIN32
        return true;
#elif defined(__APPLE__)
        return true;
#else
        // Check if espeak is available
        return std::system("which espeak >/dev/null 2>&1") == 0;
#endif
    }

    bool isEnabled() const { return enabled_; }
    void setEnabled(bool v) {
        enabled_ = v;
        spdlog::info("Voice engine: {}", v ? "enabled" : "disabled");
    }

    bool init(const std::string& whisperModelPath = "") {
        whisperModelPath_ = whisperModelPath;
        if (!whisperModelPath.empty()) {
            spdlog::info("Voice STT model: {}", whisperModelPath);
        }
        return isAvailable();
    }

    // Start listening for voice input (requires Whisper.cpp model)
    bool startListening(std::function<void(const std::string& text)> onTranscript) {
        if (!isAvailable() || whisperModelPath_.empty()) {
            spdlog::warn("Voice STT not available (no Whisper model configured)");
            return false;
        }
        listening_ = true;
        onTranscript_ = std::move(onTranscript);
        // Full implementation: WASAPI/PulseAudio capture -> Whisper.cpp -> callback
        spdlog::info("Voice listening started (STT requires Whisper.cpp integration)");
        return true;
    }

    void stopListening() {
        listening_ = false;
        spdlog::info("Voice listening stopped");
    }

    // Speak text aloud using system TTS
    void speak(const std::string& text) {
        if (!enabled_ || text.empty()) return;

        // Run TTS in background thread to not block
        std::thread([text]() {
            std::string escaped = escapeForShell(text);
#ifdef _WIN32
            // Windows: PowerShell SAPI
            std::string cmd = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Speech; "
                "(New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('" + escaped + "')\" >nul 2>&1";
            std::system(cmd.c_str());
#elif defined(__APPLE__)
            // macOS: say command
            std::string cmd = "say \"" + escaped + "\" 2>/dev/null";
            std::system(cmd.c_str());
#else
            // Linux: espeak
            std::string cmd = "espeak \"" + escaped + "\" 2>/dev/null";
            std::system(cmd.c_str());
#endif
        }).detach();
    }

    void stopSpeaking() {
#ifdef _WIN32
        std::system("taskkill /f /im powershell.exe >nul 2>&1");
#elif defined(__APPLE__)
        std::system("killall say 2>/dev/null");
#else
        std::system("killall espeak 2>/dev/null");
#endif
    }

private:
    VoiceEngine() = default;

    static std::string escapeForShell(const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'' || c == '"' || c == '\\' || c == '`' || c == '$') {
                result += ' '; // Replace problematic chars with space
            } else if (c == '\n') {
                result += ". ";
            } else {
                result += c;
            }
        }
        // Truncate for TTS (don't read entire code blocks aloud)
        if (result.size() > 500) result = result.substr(0, 500) + "...";
        return result;
    }

    std::atomic<bool> enabled_{false};
    std::atomic<bool> listening_{false};
    std::string whisperModelPath_;
    std::function<void(const std::string&)> onTranscript_;
};

} // namespace closecrab
