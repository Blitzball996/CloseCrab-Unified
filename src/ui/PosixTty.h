#pragma once
// POSIX terminal raw-mode + key reading. The Windows build uses ReadConsoleInputW
// / _getch for the per-turn Esc-watcher and the permission selector; mac/Linux had
// NO equivalent, so (1) Esc could not interrupt a turn and (2) the permission
// prompt auto-returned the default (Allow). This module provides the missing
// pieces, mirroring the Windows behavior.
#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <atomic>

namespace closecrab {

// Per-turn raw mode (mirrors Windows enterTurnInputMode/leaveTurnInputMode).
// Raw = no echo, no line buffering → the watcher reads each key immediately and
// typed characters during generation are consumed silently instead of echoing /
// piling up in the buffer. Restored to cooked mode between turns so the normal
// std::getline prompt works. Single shared instance; only the main thread calls
// enter/leave (around the turn), so no locking needed for the termios swap.
class PosixTty {
public:
    static PosixTty& instance() { static PosixTty t; return t; }

    void enterRaw() {
        if (rawActive_.load()) return;
        if (!isatty(STDIN_FILENO)) return;          // piped stdin: leave alone
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        struct termios raw = saved_;
        raw.c_lflag &= ~(ICANON | ECHO);            // char-at-a-time, no echo
        raw.c_cc[VMIN] = 0;                          // non-blocking read()
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            haveSaved_ = true;
            rawActive_ = true;
        }
    }

    void leaveRaw() {
        if (!rawActive_.load()) return;
        tcflush(STDIN_FILENO, TCIFLUSH);             // drop anything typed mid-turn
        if (haveSaved_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        rawActive_ = false;
    }

    bool isRaw() const { return rawActive_.load(); }

    // Temporarily restore cooked mode (for the selector's free-text input) and
    // give back a handle to re-enter raw afterward.
    void toCookedTemporarily() {
        if (haveSaved_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    }
    void backToRaw() {
        if (!isatty(STDIN_FILENO)) return;
        struct termios raw = saved_;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

private:
    struct termios saved_{};
    bool haveSaved_ = false;
    std::atomic<bool> rawActive_{false};
};

// Read one byte with a timeout. Returns the byte (0..255), or -1 on timeout.
inline int posixReadByteTimeout(int timeoutMs) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int r = ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (r <= 0) return -1;
    unsigned char c;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n != 1) return -1;
    return (int)c;
}

// Key codes returned by posixReadKey().
enum PosixKey {
    PK_NONE  = -1,
    PK_ENTER = 1000,
    PK_ESC   = 1001,   // a LONE Esc (not the prefix of an arrow sequence)
    PK_UP    = 1002,
    PK_DOWN  = 1003,
    PK_OTHER = 1004,   // some other key (caller may inspect lastChar)
};

// Read a logical key, disambiguating a lone Esc from an arrow escape sequence
// (Esc '[' 'A'/'B'). `blockMs` is how long to wait for the first byte; on the Esc
// byte we then peek ~40ms for a following '[' to decide arrow-vs-Esc. lastChar
// receives the raw byte for PK_OTHER (e.g. 'y'/'n'/'a').
inline int posixReadKey(int blockMs, int& lastChar) {
    lastChar = 0;
    int c = posixReadByteTimeout(blockMs);
    if (c < 0) return PK_NONE;
    if (c == '\r' || c == '\n') return PK_ENTER;
    if (c == 0x1b) {
        // Could be a lone Esc or an arrow sequence. Peek briefly for '['.
        int n1 = posixReadByteTimeout(40);
        if (n1 < 0) return PK_ESC;            // nothing followed → real Esc
        if (n1 == '[' || n1 == 'O') {
            int n2 = posixReadByteTimeout(40);
            if (n2 == 'A') return PK_UP;
            if (n2 == 'B') return PK_DOWN;
            return PK_OTHER;                  // some other CSI seq → ignore-ish
        }
        return PK_ESC;                        // Esc followed by a normal key
    }
    lastChar = c;
    return PK_OTHER;
}

} // namespace closecrab
#endif
