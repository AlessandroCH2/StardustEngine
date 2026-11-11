#pragma once
#include "Core/Base.h"
#include <filesystem>
namespace Stardust {
struct PackageInfo {
    std::string name, version, displayName, description, author;
    std::filesystem::path directory;
    bool engine = false;
    std::string gitUrl;                                             // set for packages installed from a git repository
    std::vector<std::pair<std::string, std::string>> dependencies;  // name, version ("*" = any)
    std::vector<std::string> samples;                               // folders under Samples/ (or "Samples" itself)
};
class PackageManager {
public:
    static bool EnsureDefault();
    static std::vector<PackageInfo> Installed();
    static bool InstallLocal(const std::filesystem::path& folder);
    // Official packages shipped in the engine's Packages folder (not the core, which every project has)
    static std::vector<PackageInfo> Official();
    // Replaces an installed package with another copy of it (an official package's newer version)
    static bool Update(const std::string& name, const std::filesystem::path& folder);
    // Moves an installed package to the project's Trash folder and drops it from the manifest
    static bool Remove(const std::string& name);
    // Clones a git repository (needs git on the PATH) and installs or updates the package in it. The URL may end with
    // "#branch-or-tag" and "?path=sub/folder" when the package is not at the root. Blocking: call it from a worker thread.
    static bool InstallFromGit(const std::string& url, std::string& error, std::string* installedName = nullptr);
    // Copies a package sample into Assets/Samples/<package>/<sample>; returns the folder it went to (empty on failure)
    static std::filesystem::path ImportSample(const PackageInfo& package, const std::string& sample);
    static PackageInfo Read(const std::filesystem::path& folder);
};
}
