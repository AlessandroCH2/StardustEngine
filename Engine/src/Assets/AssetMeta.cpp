#include "Assets/AssetMeta.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Base.h"
#include <cstring>
#include "Core/Log.h"
#include <chrono>
#include <mutex>
#include <random>
#include <unordered_map>

namespace Stardust::AssetMeta {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
struct Legacy { const char* suffix; const char* section; };
constexpr Legacy kLegacy[] = { { ".import.json", "texture" }, { ".rig.json", "model" } };

struct Cached { fs::file_time_type stamp; json data; };
std::unordered_map<std::string, Cached> s_Cache;
std::mutex s_Mutex;
// Assets without any .meta: checked again at most once a second, sprites ask every frame
std::unordered_map<std::string, std::chrono::steady_clock::time_point> s_Missing;

fs::path WithSuffix(const fs::path& file, const char* suffix) { fs::path p = file; p += suffix; return p; }

std::string NewGuid() {
    static std::mt19937_64 random(std::random_device{}() ^ (u64)std::chrono::steady_clock::now().time_since_epoch().count());
    static const char* hex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 2; ++i) { u64 v = random(); for (int k = 0; k < 16; ++k) { id += hex[v & 15]; v >>= 4; } }
    return id;
}

// The parsed .meta, or null when the asset has none
json Load(const fs::path& meta) {
    const std::string key = FileSystem::Utf8(meta);
    std::error_code ec;
    auto stamp = fs::last_write_time(meta, ec);
    auto found = s_Cache.find(key);
    if (ec) {
        if (!FileSystem::Exists(meta)) { if (found != s_Cache.end()) s_Cache.erase(found); return nullptr; }
        if (found != s_Cache.end()) return found->second.data; // inside a pak: it never changes
    } else if (found != s_Cache.end() && found->second.stamp == stamp) return found->second.data;
    json data = json::parse(FileSystem::ReadText(meta), nullptr, false);
    if (!data.is_object()) data = json::object();
    s_Cache[key] = { stamp, data };
    return data;
}

bool Store(const fs::path& meta, const json& data) {
    if (!FileSystem::WriteText(meta, data.dump(2))) return false;
    s_Cache.erase(FileSystem::Utf8(meta));
    std::string asset = FileSystem::Utf8(meta); asset.resize(asset.size() - 5);
    s_Missing.erase(asset);
    return true;
}

// Folds the old side files of one asset into its .meta and removes them
json Migrate(const fs::path& asset) {
    json meta = json::object();
    std::vector<fs::path> old;
    for (const Legacy& legacy : kLegacy) {
        fs::path file = WithSuffix(asset, legacy.suffix);
        if (!FileSystem::Exists(file)) continue;
        json settings = json::parse(FileSystem::ReadText(file), nullptr, false);
        if (settings.is_object()) meta[legacy.section] = settings;
        old.push_back(file);
    }
    if (old.empty()) return nullptr;
    meta["fileFormatVersion"] = 1;
    meta["guid"] = NewGuid();
    // Inside a pak nothing can be written: the old data still works, it just stays where it is
    if (!Store(WithSuffix(asset, ".meta"), meta)) return meta;
    std::error_code ec;
    for (auto& file : old) fs::remove(file, ec);
    return meta;
}

json LoadOrMigrate(const fs::path& asset) {
    const std::string key = FileSystem::Utf8(asset);
    const auto now = std::chrono::steady_clock::now();
    if (auto missing = s_Missing.find(key); missing != s_Missing.end()) {
        if (now - missing->second < std::chrono::seconds(1)) return nullptr;
        s_Missing.erase(missing);
    }
    json meta = Load(WithSuffix(asset, ".meta"));
    if (!meta.is_object()) meta = Migrate(asset);
    if (!meta.is_object()) s_Missing[key] = now;
    return meta;
}
} // namespace

fs::path PathFor(const std::string& asset) { return WithSuffix(AssetManager::ToAbsolute(asset), ".meta"); }

json Read(const std::string& asset, const char* section) {
    if (asset.empty()) return json::object();
    std::lock_guard lock(s_Mutex);
    json meta = LoadOrMigrate(AssetManager::ToAbsolute(asset));
    if (!meta.is_object()) return json::object();
    auto found = meta.find(section);
    return found != meta.end() && found->is_object() ? *found : json::object();
}

bool Write(const std::string& asset, const char* section, const json& settings) {
    if (asset.empty()) return false;
    std::lock_guard lock(s_Mutex);
    const fs::path absolute = AssetManager::ToAbsolute(asset);
    json meta = LoadOrMigrate(absolute);
    if (!meta.is_object()) meta = { { "fileFormatVersion", 1 }, { "guid", NewGuid() } };
    meta[section] = settings;
    return Store(WithSuffix(absolute, ".meta"), meta);
}

int MigrateFolder(const fs::path& folder) {
    std::lock_guard lock(s_Mutex);
    std::vector<fs::path> assets;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = FileSystem::Utf8(it->path().filename());
        for (const Legacy& legacy : kLegacy)
            if (name.size() > strlen(legacy.suffix) && name.ends_with(legacy.suffix)) {
                fs::path asset = it->path(); asset.replace_filename(fs::u8path(name.substr(0, name.size() - strlen(legacy.suffix))));
                assets.push_back(asset);
            }
    }
    int changed = 0;
    for (auto& asset : assets) {
        // A .meta that already exists wins; the old file is merged only into sections it lacks
        json meta = Load(WithSuffix(asset, ".meta"));
        if (!meta.is_object()) { if (Migrate(asset).is_object()) ++changed; continue; }
        bool merged = false;
        for (const Legacy& legacy : kLegacy) {
            fs::path file = WithSuffix(asset, legacy.suffix);
            if (!fs::exists(file, ec)) continue;
            if (!meta.contains(legacy.section)) { json settings = json::parse(FileSystem::ReadText(file), nullptr, false); if (settings.is_object()) { meta[legacy.section] = settings; merged = true; } }
            if (merged ? Store(WithSuffix(asset, ".meta"), meta) : true) fs::remove(file, ec);
        }
        if (merged) ++changed;
    }
    if (changed) SD_INFO("Import settings of {} assets moved to .meta files", changed);
    return changed;
}

void NewIdentity(const fs::path& metaFile) {
    std::lock_guard lock(s_Mutex);
    json meta = Load(metaFile);
    if (!meta.is_object()) return;
    meta["guid"] = NewGuid();
    Store(metaFile, meta);
}

bool IsSideFile(const fs::path& file) {
    const std::string name = FileSystem::Utf8(file.filename());
    if (name.ends_with(".meta")) return true;
    // Compiled .shader / .shadergraph output: the source asset stands for it
    for (const char* generated : { ".shader.spv", ".shadergraph.spv", ".shader.spv.properties.json", ".shadergraph.spv.properties.json", ".spv.tmp" })
        if (name.ends_with(generated)) return true;
    for (const Legacy& legacy : kLegacy) if (name.ends_with(legacy.suffix)) return true;
    return false;
}

} // namespace Stardust::AssetMeta
