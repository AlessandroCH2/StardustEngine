#pragma once
#include "Core/Base.h"
#include <format>
#include <mutex>
#include <deque>

namespace Stardust {

enum class LogLevel : u8 { Trace, Info, Warn, Error };

struct LogEntry {
    LogLevel level;
    std::string message;
    double time;
};

// Thread-safe logger: prints to stdout and keeps a ring buffer that the editor Console panel reads.
class Log {
public:
    static void Write(LogLevel level, std::string msg);
    static std::vector<LogEntry> Snapshot();
    static void Clear();
    static u64 Version(); // increments on every new entry (lets the console auto-scroll)

private:
    static inline std::mutex s_Mutex;
    static inline std::deque<LogEntry> s_Entries;
    static inline u64 s_Version = 0;
    static constexpr size_t kMaxEntries = 2000;
};

} // namespace Stardust

#define SD_TRACE(...) ::Stardust::Log::Write(::Stardust::LogLevel::Trace, std::format(__VA_ARGS__))
#define SD_INFO(...)  ::Stardust::Log::Write(::Stardust::LogLevel::Info,  std::format(__VA_ARGS__))
#define SD_WARN(...)  ::Stardust::Log::Write(::Stardust::LogLevel::Warn,  std::format(__VA_ARGS__))
#define SD_ERROR(...) ::Stardust::Log::Write(::Stardust::LogLevel::Error, std::format(__VA_ARGS__))

#define SD_ASSERT(cond, ...) do { if (!(cond)) { SD_ERROR("Assertion failed: " #cond " ({}:{})", __FILE__, __LINE__); __debugbreak(); } } while (0)
#define SD_VERIFY(cond, msg) do { if (!(cond)) { SD_ERROR("{}", msg); throw std::runtime_error(msg); } } while (0)
