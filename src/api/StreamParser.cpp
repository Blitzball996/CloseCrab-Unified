#include "StreamParser.h"

namespace closecrab {

StreamParser::StreamParser(EventCallback callback) : callback_(std::move(callback)) {}

void StreamParser::feed(const std::string& chunk) {
    buffer_ += chunk;

    // Process complete lines
    size_t pos = 0;
    while (pos < buffer_.size()) {
        size_t nl = buffer_.find('\n', pos);
        if (nl == std::string::npos) break;

        std::string line = buffer_.substr(pos, nl - pos);
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        processLine(line);
        pos = nl + 1;
    }
    buffer_ = buffer_.substr(pos);
}

void StreamParser::finish() {
    if (!buffer_.empty()) {
        processLine(buffer_);
        buffer_.clear();
    }
    if (!currentData_.empty()) {
        dispatchEvent();
    }
}

void StreamParser::processLine(const std::string& line) {
    if (line.empty()) {
        // Empty line = end of event
        dispatchEvent();
        return;
    }

    if (line[0] == ':') {
        // Comment, ignore
        return;
    }

    size_t colon = line.find(':');
    if (colon == std::string::npos) {
        // Field with no value
        return;
    }

    std::string field = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    // Remove leading space after colon
    if (!value.empty() && value[0] == ' ') value = value.substr(1);

    if (field == "event") {
        currentEvent_ = value;
    } else if (field == "data") {
        if (!currentData_.empty()) currentData_ += "\n";
        currentData_ += value;
    }
}

void StreamParser::dispatchEvent() {
    if (currentData_.empty() && currentEvent_.empty()) return;

    SSEEvent event;
    event.event = currentEvent_.empty() ? "message" : currentEvent_;
    event.data = currentData_;

    callback_(event);

    currentEvent_.clear();
    currentData_.clear();
}

} // namespace closecrab
