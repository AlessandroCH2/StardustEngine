#include "Project/PackageManager.h"
#include "Project/Project.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include "Version.h"
#include "Core/Process.h"
#include <algorithm>
#include <chrono>
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
        if (!fs::exists(it->path() / "package.json")) continue;
        auto info = Read(it->path());
        if (!info.name.empty()) result.push_back(info);
    }
    // Packages that came from git remember their URL in the manifest
    auto manifest = json::parse(FileSystem::ReadText(Project::Dir() / "Packages" / "manifest.json"), nullptr, false);
    if (manifest.is_object() && manifest.contains("sources") && manifest["sources"].is_object())
        for (auto& p : result) p.gitUrl = manifest["sources"].value(p.name, std::string());
    return result;
}
PackageInfo PackageManager::Read(const fs::path& folder) {
    PackageInfo info;
    auto j = json::parse(FileSystem::ReadText(folder / "package.json"), nullptr, false);
    if (!j.is_object()) return info;
    info.name = j.value("name", FileSystem::Utf8(folder.filename()));
    info.version = j.value("version", "0.0.0");
    info.displayName = j.value("displayName", info.name);
    info.description = j.value("description", "");
    info.directory = folder;
    info.engine = j.value("engine", false);
    if (j.contains("author")) info.author = j["author"].is_string() ? j["author"].get<std::string>() : j["author"].is_object() ? j["author"].value("name", "") : "";
    if (j.contains("dependencies") && j["dependencies"].is_object())
        for (auto& [name, version] : j["dependencies"].items()) info.dependencies.push_back({ name, version.is_string() ? version.get<std::string>() : version.dump() });
    std::error_code ec;
    const fs::path samples = folder / "Samples";
    if (fs::is_directory(samples, ec)) {
        for (auto& entry : fs::directory_iterator(samples, ec)) if (entry.is_directory()) info.samples.push_back(FileSystem::Utf8(entry.path().filename()));
        if (info.samples.empty()) info.samples.push_back("Samples");
        std::sort(info.samples.begin(), info.samples.end());
    }
    return info;
}
fs::path PackageManager::ImportSample(const PackageInfo& package, const std::string& sample) {
    try {
        const fs::path source = sample == "Samples" ? package.directory / "Samples" : package.directory / "Samples" / fs::u8path(sample);
        const fs::path target = Project::AssetsDir() / "Samples" / fs::u8path(package.displayName) / fs::u8path(sample);
        if (!fs::is_directory(source)) return {};
        fs::create_directories(target);
        fs::copy(source, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        SD_INFO("Sample imported: {} -> {}", sample, FileSystem::Utf8(target));
        return target;
    } catch (const std::exception& e) { SD_ERROR("Sample import failed: {}", e.what()); return {}; }
}
bool PackageManager::InstallFromGit(const std::string& input, std::string& error, std::string* installedName) {
    std::string url = input, ref, sub;
    if (auto query = url.find("?path="); query != std::string::npos) { sub = url.substr(query + 6); url.erase(query); }
    if (auto hash = url.find('#'); hash != std::string::npos) { ref = url.substr(hash + 1); url.erase(hash); }
    while (!url.empty() && std::isspace((unsigned char)url.back())) url.pop_back();
    if (url.empty() || url.find_first_of("\"\n\r") != std::string::npos || ref.find_first_of(" \"") != std::string::npos) { error = "Not a valid git URL"; return false; }
    const fs::path temp = Project::Dir() / "Temp" / ("git-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(temp.parent_path(), ec);
    std::string log;
    std::string command = "git clone --depth 1 " + (ref.empty() ? std::string() : "--branch \"" + ref + "\" ") + "\"" + url + "\" \"" + FileSystem::Utf8(temp) + "\"";
    const int code = RunProcess(fs::u8path(command).wstring(), Project::Dir(), [&](const std::string& line) { log += line + "\n"; });
    auto cleanup = [&] {
        // git marks its object files read-only: clear that before deleting
        for (auto it = fs::recursive_directory_iterator(temp, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add, ec);
        fs::remove_all(temp, ec);
    };
    if (code != 0) { error = code < 0 ? "git could not be started: install Git and make sure it is on the PATH" : "git clone failed:\n" + log; cleanup(); return false; }
    const fs::path folder = sub.empty() ? temp : temp / fs::u8path(sub);
    if (!fs::exists(folder / "package.json")) { error = "No package.json in " + (sub.empty() ? std::string("the repository root") : sub); cleanup(); return false; }
    fs::remove_all(folder / ".git", ec);
    const PackageInfo info = Read(folder);
    const bool exists = fs::exists(Project::Dir() / "Packages" / info.name);
    const bool ok = exists ? Update(info.name, folder) : InstallLocal(folder);
    if (ok) {
        auto file = Project::Dir() / "Packages" / "manifest.json";
        auto manifest = json::parse(FileSystem::ReadText(file), nullptr, false);
        if (!manifest.is_object()) manifest = json::object();
        manifest["sources"][info.name] = input;
        FileSystem::WriteText(file, manifest.dump(2));
        if (installedName) *installedName = info.name;
    } else error = "The package could not be installed (see the Console)";
    cleanup();
    return ok;
}
std::vector<PackageInfo> PackageManager::Official() {
    std::vector<PackageInfo> result;
    // The source tree while developing, a Packages folder next to the editor in a distribution
    for (const fs::path& root : { FileSystem::EngineRoot() / "Packages", FileSystem::ExecutableDir() / "Packages" }) {
        std::error_code ec;
        for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            auto j = json::parse(FileSystem::ReadText(it->path() / "package.json"), nullptr, false);
            if (!j.is_object() || j.value("engine", false) || j.value("name", "") == "com.stardust.core") continue;
            const std::string name = j.value("name", "");
            if (name.empty() || std::any_of(result.begin(), result.end(), [&](const PackageInfo& p) { return p.name == name; })) continue;
            result.push_back(Read(it->path()));
        }
    }
    std::sort(result.begin(), result.end(), [](const PackageInfo& a, const PackageInfo& b) { return a.displayName < b.displayName; });
    return result;
}
bool PackageManager::Remove(const std::string& name) {
    try {
        if (name.empty() || name == "com.stardust.core" || name.find_first_of("/\\") != std::string::npos || name == "." || name == "..") return false;
        auto folder = Project::Dir() / "Packages" / name;
        if (!fs::exists(folder)) return false;
        auto trash = Project::Dir() / "Trash" / (name + "-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(trash.parent_path());
        fs::rename(folder, trash);
        auto file = Project::Dir() / "Packages" / "manifest.json";
        auto manifest = json::parse(FileSystem::ReadText(file), nullptr, false);
        if (manifest.is_object()) {
            if (manifest.contains("dependencies") && manifest["dependencies"].is_object()) manifest["dependencies"].erase(name);
            if (manifest.contains("sources") && manifest["sources"].is_object()) manifest["sources"].erase(name);
            FileSystem::WriteText(file, manifest.dump(2));
        }
        SD_INFO("Package removed: {} (moved to {})", name, FileSystem::Utf8(trash));
        return true;
    } catch (const std::exception& e) { SD_ERROR("Package removal failed: {}", e.what()); return false; }
}
bool PackageManager::Update(const std::string& name, const fs::path& folder) {
    try {
        auto target = Project::Dir() / "Packages" / name;
        if (!fs::exists(target)) return InstallLocal(folder);
        fs::copy(folder, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        auto j = json::parse(FileSystem::ReadText(folder / "package.json"));
        auto file = Project::Dir() / "Packages" / "manifest.json";
        auto manifest = json::parse(FileSystem::ReadText(file), nullptr, false);
        if (!manifest.is_object()) manifest = json::object();
        manifest["dependencies"][name] = j.value("version", "0.0.0");
        SD_INFO("Package updated: {} {}", name, j.value("version", "0.0.0"));
        return FileSystem::WriteText(file, manifest.dump(2));
    } catch (const std::exception& e) { SD_ERROR("Package update failed: {}", e.what()); return false; }
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
