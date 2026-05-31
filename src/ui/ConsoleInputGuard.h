#pragma once
// ConsoleInputGuard — single source of truth for "a thread is reading the
// console RIGHT NOW".
//
// CloseCrab runs a persistent key-watcher thread during every AI turn that
// calls _getch() to catch Esc (abort) and to swallow stray keystrokes so
// type-ahead can't corrupt the stream. The PROBLEM: some tools (the permission
// prompt, AskUserQuestion) ALSO read the console mid-turn via KeyboardSelector.
// If the watcher and a tool both call _getch()/ReadConsole on the same console
// at the same time, the two reads race and the process hard-crashes (闪退).
//
// Rule: any code path that reads the console while a turn is in flight MUST
// hold a ConsoleInputGuard. The watcher checks consoleInputBusy() and yields
// (stops reading) for as long as the guard is held. KeyboardSelector::select()
// takes this guard internally, so every selector-based prompt is covered
// automatically — callers don't have to remember.
//
// inline variable (C++17) → exactly one shared instance across all TUs.

#include <atomic>

namespace closecrab {

inline std::atomic<int>& consoleInputBusyCounter() {
    static std::atomic<int> counter{0};
    return counter;
}

// True while at least one ConsoleInputGuard is alive (a thread owns the console).
inline bool consoleInputBusy() {
    return consoleInputBusyCounter().load() > 0;
}

// RAII: increments the busy counter for its lifetime. Reentrant/nestable
// (counter, not bool) so overlapping guards on one thread are safe.
struct ConsoleInputGuard {
    ConsoleInputGuard() { consoleInputBusyCounter().fetch_add(1); }
    ~ConsoleInputGuard() { consoleInputBusyCounter().fetch_sub(1); }
    ConsoleInputGuard(const ConsoleInputGuard&) = delete;
    ConsoleInputGuard& operator=(const ConsoleInputGuard&) = delete;
};

} // namespace closecrab
