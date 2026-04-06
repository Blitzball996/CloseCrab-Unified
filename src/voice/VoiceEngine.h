#pragma once

#include <string>
#include <functional>
#include <atomic>

namespace closecrab {

// Voice engine — audio capture + STT + TTS
// Windows: WASAPI for capture, SAPI for TTS, Whisper.cpp for STT
class VoiceEngine {
public:
    static VoiceEngine& getInstance() {
        static VoiceEngine instance;
        return instance;
    }

    bool isAvailable() const;
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool v) { enabled_ = v; }

    // Start listening for voice input
    bool startListening(std::function<void(const std::string& text)> onTranscript);
    void stopListening();

    // Speak text aloud
    void speak(const std::string& text);
    void stopSpeaking();

    // Initialize with model paths
    bool init(const std::string& whisperModelPath = "");

private:
    VoiceEngine() = default;
    std::atomic<bool> enabled_{false};
    std::atomic<bool> listening_{false};
    std::string whisperModelPath_;
};

// Stub implementations (full implementation requires platform-specific audio APIs)
inline bool VoiceEngine::isAvailable() const {
#ifdef _WIN32
    return true; // SAPI available on Windows
#else
    return false;
#endif
}

inline bool VoiceEngine::init(const std::string& whisperModelPath) {
    whisperModelPath_ = whisperModelPath;
    return isAvailable();
}

inline bool VoiceEngine::startListening(std::function<void(const std::string&)> onTranscript) {
    if (!isAvailable()) return false;
    listening_ = true;
    // Full implementation: WASAPI capture → Whisper.cpp STT → callback
    return true;
}

inline void VoiceEngine::stopListening() { listening_ = false; }

inline void VoiceEngine::speak(const std::string& text) {
#ifdef _WIN32
    // Full implementation would use Windows SAPI
    // For now, just log
#endif
}

inline void VoiceEngine::stopSpeaking() {}

} // namespace closecrab
