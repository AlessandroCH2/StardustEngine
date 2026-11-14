#include "Core/FileSystem.h"
#include "Assets/Pak.h"
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace Stardust::FileSystem {

fs::path ExecutableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
#else
    char buf[PATH_MAX];auto size=readlink("/proc/self/exe",buf,sizeof(buf));return size>0?fs::path(std::string(buf,(size_t)size)).parent_path():fs::current_path();
#endif
}

// The engine folders (Packages, Assets/Shaders, docs...) are copied next to the executable at build time
// (cmake/RuntimeFiles.cmake), so every build folder and every distribution works on its own
static fs::path s_EngineRootOverride;
fs::path EngineRoot() { return s_EngineRootOverride.empty() ? ExecutableDir() : s_EngineRootOverride; }
void SetEngineRoot(const fs::path& root) { s_EngineRootOverride = root; }

bool Exists(const fs::path& path) { return Pak::Contains(path)||fs::exists(path); }

std::vector<u8> ReadBinary(const fs::path& path) {
    std::vector<u8> packed;if(Pak::ReadBinary(path,packed))return packed;
    std::ifstream f(Pak::Resolve(path), std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::vector<u8> data((size_t)f.tellg());
    f.seekg(0);
    f.read((char*)data.data(), (std::streamsize)data.size());
    return data;
}

std::string ReadText(const fs::path& path) {
    auto bytes=ReadBinary(path);return std::string(bytes.begin(),bytes.end());
}

bool WriteText(const fs::path& path, const std::string& text) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    // Binary: the text is written exactly as given. Text mode would turn every LF into CRLF on Windows
    // (and CRLF into CR CR LF, which shows as an extra empty line).
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << text;
    return true;
}

} // namespace Stardust::FileSystem
