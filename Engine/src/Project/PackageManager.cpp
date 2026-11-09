#include "Project/PackageManager.h"
#include "Project/Project.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
namespace Stardust {
namespace fs = std::filesystem;
using json = nlohmann::json;
bool PackageManager::EnsureDefault() {
    if (!Project::IsLoaded()) return false;
    try {
        auto packages = Project::Dir() / "Packages";
        fs::create_directories(packages);
        auto core = packages / "com.stardust.core";
        if (!fs::exists(core / "package.json")) {
            auto source = FileSystem::EngineRoot() / "Packages" / "com.stardust.core";
            if (fs::exists(source)) fs::copy(source, core, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            else FileSystem::WriteText(core / "package.json", R"({"name":"com.stardust.core","version":"0.1.0","displayName":"Stardust Engine","engine":true})");
        }
        if (!fs::exists(core / "Shaders") && fs::exists(FileSystem::EngineRoot()/"Assets/Shaders"))
            fs::copy(FileSystem::EngineRoot()/"Assets/Shaders",core/"Shaders",fs::copy_options::recursive);
        auto shaderLibrary=FileSystem::EngineRoot()/"Assets/Shaders";if(fs::is_directory(shaderLibrary))for(auto& entry:fs::directory_iterator(shaderLibrary))if(entry.is_regular_file()&&!fs::exists(core/"Shaders"/entry.path().filename()))fs::copy_file(entry.path(),core/"Shaders"/entry.path().filename());
        auto source=FileSystem::EngineRoot()/"Packages/com.stardust.core";
        if(fs::is_directory(source))for(auto& entry:fs::recursive_directory_iterator(source))if(entry.is_regular_file()){auto target=core/fs::relative(entry.path(),source);if(!fs::exists(target)){fs::create_directories(target.parent_path());fs::copy_file(entry.path(),target);}}
        auto manifestFile = packages / "manifest.json";
        auto manifest = json::parse(FileSystem::ReadText(manifestFile), nullptr, false);
        if (!manifest.is_object()) manifest = json::object();
        manifest["dependencies"]["com.stardust.core"] = "0.1.0";
        return FileSystem::WriteText(manifestFile, manifest.dump(2));
    } catch (const std::exception& e) { SD_ERROR("Package setup failed: {}", e.what()); return false; }
}
std::vector<PackageInfo> PackageManager::Installed() {
    std::vector<PackageInfo> result;
    std::error_code ec;
    for (auto it = fs::directory_iterator(Project::Dir() / "Packages", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        auto j = json::parse(FileSystem::ReadText(it->path() / "package.json"), nullptr, false);
        if (!j.is_object()) continue;
        result.push_back({j.value("name", it->path().filename().string()), j.value("version", "0.0.0"),
            j.value("displayName", j.value("name", "Package")), it->path(), j.value("engine", false)});
    }
    return result;
}
bool PackageManager::InstallLocal(const fs::path& folder) {
    try {
        auto j = json::parse(FileSystem::ReadText(folder / "package.json"));
        std::string name = j.at("name"), version = j.at("version");
        if (name.empty() || name == "." || name == ".." || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-_") != std::string::npos || name == "com.stardust.core") return false;
        auto installed=Installed();
        auto dependencies=j.value("dependencies",json::object());
        for (auto& [dependency,required] : dependencies.items()) {
            auto found=std::find_if(installed.begin(),installed.end(),[&](const auto& package){return package.name==dependency && (required=="*" || required==package.version);});
            if (found==installed.end()) { SD_ERROR("Install dependency {} {} before {}",dependency,required.dump(),name); return false; }
        }
        auto target = Project::Dir() / "Packages" / name;
        if (fs::exists(target)) { SD_ERROR("Package already installed: {}", name); return false; }
        fs::copy(folder, target, fs::copy_options::recursive);
        auto file = Project::Dir() / "Packages" / "manifest.json";
        auto manifest = json::parse(FileSystem::ReadText(file));
        manifest["dependencies"][name] = version;
        return FileSystem::WriteText(file, manifest.dump(2));
    } catch (const std::exception& e) { SD_ERROR("Package installation failed: {}", e.what()); return false; }
}
}
