#pragma once
#include <string>
#include <vector>
#include <chrono>

namespace closecrab {

class Buddy {
public:
    enum class State { IDLE, THINKING, WORKING, HAPPY, ERROR, SLEEPING };

    static Buddy& getInstance() {
        static Buddy instance;
        return instance;
    }

    void setState(State s) { state_ = s; frame_ = 0; }
    State getState() const { return state_; }
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }

    std::string getFrame() {
        if (!enabled_) return "";
        frame_++;
        const auto& frames = getFrames(state_);
        if (frames.empty()) return "";
        return frames[frame_ % frames.size()];
    }

private:
    Buddy() = default;
    State state_ = State::IDLE;
    int frame_ = 0;
    bool enabled_ = false;

    static const std::vector<std::string>& getFrames(State state) {
        static const std::vector<std::string> idle = {
            "  (o_o)  ", "  (o_o)  ", "  (o_O)  ", "  (O_o)  "
        };
        static const std::vector<std::string> thinking = {
            "  (o_o)? ", "  (o_o)?.", "  (o_o)?..", "  (o_o)?..."
        };
        static const std::vector<std::string> working = {
            "  (>_<)/ ", "  (>_<)| ", "  (>_<)\\ ", "  (>_<)| "
        };
        static const std::vector<std::string> happy = {
            "  (^_^)  ", "  (^_^)/ ", "  (^_^)  ", "  (^_^)\\ "
        };
        static const std::vector<std::string> error = {
            "  (x_x)  ", "  (X_X)  ", "  (x_x)  ", "  (X_X)  "
        };
        static const std::vector<std::string> sleeping = {
            "  (-_-)z ", "  (-_-)zz", "  (-_-)zzz", "  (-_-)zz"
        };

        switch (state) {
            case State::IDLE: return idle;
            case State::THINKING: return thinking;
            case State::WORKING: return working;
            case State::HAPPY: return happy;
            case State::ERROR: return error;
            case State::SLEEPING: return sleeping;
        }
        return idle;
    }
};

} // namespace closecrab
