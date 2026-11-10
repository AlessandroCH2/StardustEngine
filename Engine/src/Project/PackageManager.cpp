#include "Project/PackageManager.h"
#include "Project/Project.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include "Version.h"
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
            else FileSystem::WriteText(core / "package.json", R"({"name":"com.stardust.core","displayName":"Stardust Engine","engine":true})");
        }
        if (!fs::exists(core / "Shaders") && fs::exists(FileSystem::EngineRoot()/"Assets/Shaders"))
            fs::copy(FileSystem::EngineRoot()/"Assets/Shaders",core/"Shaders",fs::copy_options::recursive);
        // The core package belongs to the engine: files the engine has a newer copy of are refreshed,
        // so opening a project with a newer engine updates its shaders and built-in assets
        int refreshed=0;
        auto refresh=[&](const fs::path& from,const fs::path& to){std::error_code ec;if(fs::exists(to,ec)&&fs::last_write_time(to,ec)>=fs::last_write_time(from,ec))return;fs::create_directories(to.parent_path(),ec);if(fs::copy_file(from,to,fs::copy_options::overwrite_existing,ec))++refreshed;};
        auto shaderLibrary=FileSystem::EngineRoot()/"Assets/Shaders";if(fs::is_directory(shaderLibrary))for(auto& entry:fs::directory_iterator(shaderLibrary))if(entry.is_regular_file())refresh(entry.path(),core/"Shaders"/entry.path().filename());
        auto source=FileSystem::EngineRoot()/"Packages/com.stardust.core";
        if(fs::is_directory(source))for(auto& entry:fs::recursive_directory_iterator(source))if(entry.is_regular_file()&&entry.path().filename()!="package.json")refresh(entry.path(),core/fs::relative(entry.path(),source));
        // The package version is the engine version
        auto package=json::parse(FileSystem::ReadText(core/"package.json"),nullptr,false);if(!package.is_object())package=json::object();
        auto engineSide=json::parse(FileSystem::ReadText(source/"package.json"),nullptr,false);if(engineSide.is_object())for(auto& [key,value]:engineSide.items())package[key]=value;
        package["name"]="com.stardust.core";package["version"]=STARDUST_VERSION;package["engine"]=true;
        if(FileSystem::ReadText(core/"package.json")!=package.dump(2))FileSystem::WriteText(core/"package.json",package.dump(2));
        if(refreshed)SD_INFO("Stardust Engine package updated to {} ({} files)",STARDUST_VERSION,refreshed);
        auto manifestFile = packages / "manifest.json";
        auto manifest = json::parse(FileSystem::ReadText(manifestFile), nullptr, false);
        if (!manifest.is_object()) manifest = json::object();
        if (!manifest.contains("dependencies") || !manifest["dependencies"].is_object()) manifest["dependencies"] = json::object();
        else if (manifest["dependencies"].value("com.stardust.core", std::string()) == STARDUST_VERSION) return true;
        manifest["dependencies"]["com.stardust.core"] = STARDUST_VERSION;
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
