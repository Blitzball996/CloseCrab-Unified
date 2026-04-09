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

        captureThread_ = std::thread([this]() {
            spdlog::info("Voice listening started");
            while (listening_) {
                std::string transcript = captureAndTranscribe();
                if (!transcript.empty() && onTranscript_) {
                    onTranscript_(transcript);
                }
            }
            spdlog::info("Voice listening stopped");
        });
        captureThread_.detach();
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
    std::thread captureThread_;

    // Capture audio and transcribe using whisper.cpp CLI or platform STT
    std::string captureAndTranscribe() {
        // Record a short audio clip, then run whisper.cpp on it
        std::string tempWav;
#ifdef _WIN32
        tempWav = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\closecrab_stt.wav";
        // Use PowerShell to record 5 seconds of audio via WASAPI
        std::string recordCmd =
            "powershell -NoProfile -Command \""
            "Add-Type -AssemblyName System.Speech;"
            "$r = New-Object System.Speech.Recognition.SpeechRecognitionEngine;"
            "$r.SetInputToDefaultAudioDevice();"
            "$r.LoadGrammar((New-Object System.Speech.Recognition.DictationGrammar));"
            "$result = $r.Recognize((New-Object TimeSpan(0,0,5)));"
            "if($result){$result.Text}\"";
        // PowerShell Speech Recognition returns text directly
        FILE* pipe = _popen(recordCmd.c_str(), "r");
        if (!pipe) return "";
        char buf[4096];
        std::string result;
        while (fgets(buf, sizeof(buf), pipe)) result += buf;
        _pclose(pipe);
        // Trim whitespace
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
#else
        tempWav = "/tmp/closecrab_stt.wav";
        // Record 5 seconds using arecord (Linux) or sox
        std::string recordCmd;
#ifdef __APPLE__
        recordCmd = "rec -q -r 16000 -c 1 -b 16 " + tempWav + " trim 0 5 2>/dev/null";
#else
        recordCmd = "arecord -q -f S16_LE -r 16000 -c 1 -d 5 " + tempWav + " 2>/dev/null";
#endif
        int ret = std::system(recordCmd.c_str());
        if (ret != 0) {
            spdlog::warn("Audio recording failed (exit code {})", ret);
            return "";
        }

        // Run whisper.cpp CLI on the recorded audio
        std::string whisperCmd = "whisper-cpp -m " + whisperModelPath_
                               + " -f " + tempWav + " --no-timestamps -nt 2>/dev/null";
        FILE* pipe = popen(whisperCmd.c_str(), "r");
        if (!pipe) {
            spdlog::warn("Failed to run whisper-cpp");
            return "";
        }
        char buf[4096];
        std::string result;
        while (fgets(buf, sizeof(buf), pipe)) result += buf;
        pclose(pipe);

        // Clean up temp file
        std::remove(tempWav.c_str());

        // Trim whitespace
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
#endif
    }
};

} // namespace closecrab
