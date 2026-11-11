#include "Core/Profiler.h"
#include <cstring>
#include <mutex>

namespace Stardust {

static std::vector<Profiler::Section> s_Sections;
static std::mutex s_Mutex;

void Profiler::Add(const char* name, double ms) {
    std::lock_guard lock(s_Mutex);
    for (auto& s : s_Sections) if (s.name == name) { s.ms += ms; return; }
    if (s_Sections.size() < 64) s_Sections.push_back({ name, ms, 0.0 });
}

void Profiler::EndFrame() {
    std::lock_guard lock(s_Mutex);
    for (auto& s : s_Sections) { s.average += (s.ms - s.average) * 0.05; s.ms = 0.0; }
}

const std::vector<Profiler::Section>& Profiler::Sections() { return s_Sections; }

} // namespace Stardust
