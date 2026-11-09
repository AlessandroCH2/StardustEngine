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

fs::path EngineRoot() {
    // A packaged build runs away from its source tree, with the engine folders next to the executable
    static const fs::path root = fs::is_directory(fs::path(STARDUST_ROOT_DIR)) ? fs::path(STARDUST_ROOT_DIR) : ExecutableDir();
    return root;
}

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
    std::ofstream f(path);
    if (!f) return false;
    f << text;
    return true;
}

} // namespace Stardust::FileSystem
