#pragma once
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string_view>

// Cross-platform signal handling
#include <csignal>
#ifdef _WIN32
  #include <windows.h>
#else
  // backtrace only on Linux/macOS
  #if defined(__linux__) || defined(__APPLE__)
    #include <execinfo.h>
    #define HAS_BACKTRACE 1
  #endif
#endif

namespace Log {

enum class Level { INFO, WARN, ERR };

inline FILE*      g_file  = nullptr;
inline std::mutex g_mutex;

inline void init(const char* logPath = "aetheris.log") {
    g_file = fopen(logPath, "w");
}

inline void shutdown() {
    if (g_file) { fflush(g_file); fclose(g_file); g_file = nullptr; }
}

inline void write(Level level, std::string_view msg) {
    const char* tag = level == Level::INFO ? "INFO"
                    : level == Level::WARN ? "WARN"
                                           : "ERR ";
    std::time_t t = std::time(nullptr);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", std::localtime(&t));

    std::lock_guard lock(g_mutex);
    fprintf(stdout, "[%s][%s] %.*s\n", timebuf, tag, (int)msg.size(), msg.data());
    if (g_file)
        fprintf(g_file, "[%s][%s] %.*s\n", timebuf, tag, (int)msg.size(), msg.data());
}

inline void info(std::string_view msg) { write(Level::INFO, msg); }
inline void warn(std::string_view msg) { write(Level::WARN, msg); }
inline void err (std::string_view msg) { write(Level::ERR,  msg); }

inline void printBacktrace() {
#if defined(HAS_BACKTRACE)
    void* frames[32];
    int   count = backtrace(frames, 32);
    char** syms = backtrace_symbols(frames, count);
    for (int i = 0; i < count; i++)
        err(syms ? syms[i] : "??");
    free(syms);
#elif defined(_WIN32)
    err("(backtrace not available on Windows build — attach debugger for stack trace)");
#endif
}

inline void crashHandler(int sig) {
    const char* name = sig == SIGSEGV ? "SIGSEGV"
                     : sig == SIGABRT ? "SIGABRT"
                     : sig == SIGFPE  ? "SIGFPE"
                                      : "SIGNAL";
    err(std::string("Caught ") + name + " — flushing log");
    printBacktrace();
    shutdown();
    // Re-raise so the OS generates a core dump / Windows error report
    signal(sig, SIG_DFL);
    raise(sig);
}

inline void installCrashHandlers() {
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE,  crashHandler);
}

} // namespace Log
