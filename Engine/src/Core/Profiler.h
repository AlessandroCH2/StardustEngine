#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace Stardust {

// Lightweight CPU timings per frame: SD_PROFILE("Physics") measures the rest of the enclosing scope.
// Sections with the same name add up within a frame; EndFrame() publishes a smoothed average.
class Profiler {
public:
    struct Section { std::string name; double ms = 0.0; double average = 0.0; double peak = 0.0; double windowPeak = 0.0; };
    static void Add(const char* name, double ms);
    static void EndFrame();
    static const std::vector<Section>& Sections();   // in first-seen order: average over ~0.5 s, peak = slowest frame of the last second
};

struct ProfileScope {
    const char* name;
    std::chrono::steady_clock::time_point start;
    explicit ProfileScope(const char* n) : name(n), start(std::chrono::steady_clock::now()) {}
    ~ProfileScope() { Profiler::Add(name, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()); }
};

} // namespace Stardust

#define SD_PROFILE_CONCAT2(a, b) a##b
#define SD_PROFILE_CONCAT(a, b) SD_PROFILE_CONCAT2(a, b)
#define SD_PROFILE(name) ::Stardust::ProfileScope SD_PROFILE_CONCAT(sdProfile, __LINE__)(name)
