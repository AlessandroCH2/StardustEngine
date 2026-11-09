#pragma once
#include "Core/Base.h"
#include <filesystem>

namespace Stardust::FileSystem {

namespace fs = std::filesystem;

inline std::string Utf8(const fs::path& path) { auto s=path.generic_u8string();return {s.begin(),s.end()}; }
bool Exists(const fs::path& path);
fs::path ExecutableDir();
fs::path EngineRoot();                       // source root (Assets/, ScriptCore/, ...)
std::vector<u8> ReadBinary(const fs::path& path);
std::string ReadText(const fs::path& path);
bool WriteText(const fs::path& path, const std::string& text);

} // namespace Stardust::FileSystem
