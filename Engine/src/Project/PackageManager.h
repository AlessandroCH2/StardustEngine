#pragma once
#include "Core/Base.h"
#include <filesystem>
namespace Stardust {
struct PackageInfo { std::string name, version, displayName; std::filesystem::path directory; bool engine = false; };
class PackageManager {
public:
    static bool EnsureDefault();
    static std::vector<PackageInfo> Installed();
    static bool InstallLocal(const std::filesystem::path& folder);
};
}
