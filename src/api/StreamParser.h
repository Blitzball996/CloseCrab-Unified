#pragma once

#include <string>
#include <functional>

namespace closecrab {

// SSE (Server-Sent Events) stream parser
// Parses "event: xxx\ndata: yyy\n\n" format
class StreamParser {
public:
    struct SSEEvent {
        std::string event;  // event type
        std::string data;   // event data (may be multi-line)
    };

    using EventCallback = std::function<void(const SSEEvent&)>;

    explicit StreamParser(EventCallback callback);

    // Feed raw bytes from HTTP response
    void feed(const std::string& chunk);

    // Signal end of stream
    void finish();

private:
    void processLine(const std::string& line);
    void dispatchEvent();

    EventCallback callback_;
    std::string buffer_;
    std::string currentEvent_;
    std::string currentData_;
};

} // namespace closecrab
