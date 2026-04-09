#pragma once
#include "../core/TokenEstimator.h"
#include <mutex>
#include <spdlog/spdlog.h>

namespace closecrab {

class TokenEstimationService {
public:
    static TokenEstimationService& getInstance() {
        static TokenEstimationService instance;
        return instance;
    }

    int estimate(const std::string& text) const {
        int raw = TokenEstimator::estimate(text);
        return static_cast<int>(raw * calibrationFactor_);
    }

    int estimateMessages(const std::vector<Message>& messages) const {
        int raw = TokenEstimator::estimateMessages(messages);
        return static_cast<int>(raw * calibrationFactor_);
    }

    // Calibrate against actual API token counts
    void calibrate(const std::string& text, int actualTokens) {
        std::lock_guard<std::mutex> lock(mutex_);
        int estimated = TokenEstimator::estimate(text);
        if (estimated > 0 && actualTokens > 0) {
            double ratio = static_cast<double>(actualTokens) / estimated;
            // Exponential moving average
            calibrationFactor_ = calibrationFactor_ * 0.9 + ratio * 0.1;
            spdlog::debug("Token calibration: estimated={}, actual={}, factor={:.3f}",
                          estimated, actualTokens, calibrationFactor_);
        }
    }

    double getCalibrationFactor() const { return calibrationFactor_; }

private:
    TokenEstimationService() = default;
    double calibrationFactor_ = 1.0;
    mutable std::mutex mutex_;
};

} // namespace closecrab
