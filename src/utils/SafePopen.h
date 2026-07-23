#pragma once

// _popen/_pclose serialization for Windows.
//
// ROOT CAUSE (crash dump closecrab.exe.68284.dmp, 2026-07-07):
//   ucrtbase!common_popen<char> -> _close -> _invalid_parameter -> invoke_watson
//   Exception 0xC0000409, subcode 0x5 FAST_FAIL_INVALID_ARG.
//
// ucrtbase's common_popen temporarily dup()s and reassigns the PROCESS-WIDE
// standard handles while spawning the child. It is NOT thread-safe: two
// threads calling _popen concurrently race on those handles, and the loser
// passes an invalid fd (-1) to _close, which trips the CRT invalid-parameter
// check and __fastfail()s the whole process — bypassing every SEH/VEH/
// terminate handler (hence the silent 闪退 with no crash.log entry).
//
// This process calls _popen from 12+ places (GrepTool, BashTool, git, hooks,
// PDF, voice...) across SessionRouter workers + main thread + mobile remote,
// so concurrent spawns are routine. Serialize the spawn itself with a global
// mutex; reading the pipe afterwards is safe concurrently, so only the brief
// process-creation window is locked.
//
// ALWAYS use safePopen/safePclose instead of raw _popen/_pclose/popen/pclose.

#include <cstdio>
#include <mutex>

namespace closecrab {

inline std::mutex& popenMutex() {
    static std::mutex m;
    return m;
}

inline FILE* safePopen(const char* cmd, const char* mode) {
    std::lock_guard<std::mutex> lock(popenMutex());
#ifdef _WIN32
    return _popen(cmd, mode);
#else
    return popen(cmd, mode);
#endif
}

// _pclose only closes the pipe's own fd, but serialize it too: it runs
// while another thread's _popen may be mid-handle-swap, and the lock is
// held only for the close/wait, which is cheap relative to correctness.
inline int safePclose(FILE* f) {
    std::lock_guard<std::mutex> lock(popenMutex());
#ifdef _WIN32
    return _pclose(f);
#else
    return pclose(f);
#endif
}

} // namespace closecrab
