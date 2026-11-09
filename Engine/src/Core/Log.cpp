#include "Core/Log.h"
#include <chrono>
#include <cstdio>

namespace Stardust {

static double NowSeconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void Log::Write(LogLevel level, std::string msg) {
    static const char* tags[] = { "TRACE", "INFO ", "WARN ", "ERROR" };
    static const char* colors[] = { "\x1b[90m", "\x1b[37m", "\x1b[33m", "\x1b[31m" };
    std::lock_guard lock(s_Mutex);
    std::printf("%s[%s] %s\x1b[0m\n", colors[(int)level], tags[(int)level], msg.c_str());
    std::fflush(stdout);
    s_Entries.push_back({ level, std::move(msg), NowSeconds() });
    if (s_Entries.size() > kMaxEntries) s_Entries.pop_front();
    ++s_Version;
}

std::vector<LogEntry> Log::Snapshot() {
    std::lock_guard lock(s_Mutex);
    return { s_Entries.begin(), s_Entries.end() };
}

void Log::Clear() {
    std::lock_guard lock(s_Mutex);
    s_Entries.clear();
    ++s_Version;
}

u64 Log::Version() {
    std::lock_guard lock(s_Mutex);
    return s_Version;
}

} // namespace Stardust
