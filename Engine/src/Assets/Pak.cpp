#include "Assets/Pak.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Stardust {
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
struct Entry { u64 offset=0,size=0,hash=0; };
struct MountedPak { fs::path root,archive; std::unordered_map<std::string,Entry> files; };
std::vector<MountedPak> mounts;
std::mutex mutex;
std::atomic<u64> generation{0};
u64 Hash(const std::vector<u8>& bytes) { u64 h = 14695981039346656037ull; for (u8 b : bytes) { h ^= b; h *= 1099511628211ull; } return h; }
bool Safe(const std::string& name) {
    fs::path p=fs::u8path(name);
    if (name.empty() || p.is_absolute() || p.has_root_name() || name.find_first_of("\\:*?\"<>|") != std::string::npos) return false;
    for (auto& part : p) {
        auto segment=FileSystem::Utf8(part);
        if (segment.empty() || segment.back()=='.' || segment.back()==' ' || std::any_of(segment.begin(),segment.end(),[](unsigned char c){return c<32;})) return false;
        auto base=FileSystem::Utf8(part.stem()); std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return (char)std::toupper(c);});
        if (base=="CON" || base=="PRN" || base=="AUX" || base=="NUL" || (base.size()==4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3]>='1' && base[3]<='9')) return false;
    }
    return p.generic_u8string() == p.lexically_normal().generic_u8string();
}
}
#ifdef _WIN32
static void PruneStalePakCaches(){static bool pruned=false;if(pruned)return;pruned=true;std::error_code ec;auto root=fs::absolute(fs::temp_directory_path()/"StardustPak").lexically_normal();if(!fs::is_directory(root,ec))return;for(auto& entry:fs::directory_iterator(root,ec)){if(!entry.is_directory(ec)||entry.is_symlink(ec))continue;auto target=fs::absolute(entry.path()).lexically_normal();if(target.parent_path()!=root)continue;std::ifstream owner(target/".owner.pid");DWORD pid=0;if(!(owner>>pid)||!pid||pid==GetCurrentProcessId())continue;HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);bool dead=process?WaitForSingleObject(process,0)==WAIT_OBJECT_0:GetLastError()==ERROR_INVALID_PARAMETER;if(process)CloseHandle(process);if(dead)fs::remove_all(target,ec);}}
#else
static void PruneStalePakCaches(){}
#endif

bool Pak::Build(const fs::path& source, const fs::path& output) {
    try {
        json index = json::array();
        std::vector<fs::path> files;
        for (auto& entry : fs::recursive_directory_iterator(source)) {
            if (!entry.is_regular_file()) continue;
            auto relative = fs::relative(entry.path(), source);
            bool editor = false;
            for (const auto& part : relative) if (part == "Editor") editor = true;
            if (!editor && entry.path().extension() != ".cs" && entry.path().extension() != ".pak") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        u64 offset = 0;
        for (const auto& file : files) {
            auto data = FileSystem::ReadBinary(file);
            if (data.size() != fs::file_size(file)) return false;
            index.push_back({{"path", FileSystem::Utf8(fs::relative(file, source))}, {"offset", offset}, {"size", data.size()}, {"hash", Hash(data)}});
            offset += data.size();
        }
        std::string header = index.dump(); u64 size = header.size();
        fs::create_directories(output.parent_path());
        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        out.write("SDPAK001", 8); out.write((const char*)&size, 8); out.write(header.data(), header.size());
        for (auto& file : files) { std::ifstream in(file, std::ios::binary); out << in.rdbuf(); }
        out.flush();
        return out.good();
    } catch (const std::exception& e) { SD_ERROR("Pak build failed: {}", e.what()); return false; }
}
bool Pak::Mount(const fs::path& archive, const fs::path& virtualRoot) {
    try {
        try{PruneStalePakCaches();}catch(const std::exception& e){SD_WARN("Stale pak cache cleanup: {}",e.what());}
        std::ifstream in(archive, std::ios::binary);
        char magic[8]{}; u64 indexSize = 0;
        in.read(magic, 8); in.read((char*)&indexSize, 8);
        if (!in || memcmp(magic, "SDPAK001", 8) || indexSize > 16 * 1024 * 1024) return false;
        u64 fileSize = fs::file_size(archive);
        if (fileSize < 16 || indexSize > fileSize - 16) return false;
        std::string header((size_t)indexSize, '\0'); in.read(header.data(), indexSize);
        auto index = json::parse(header);
        if (!index.is_array() || index.size() > 100000) return false;
        MountedPak mount;
        mount.root = fs::absolute(virtualRoot).lexically_normal();
        mount.archive=fs::absolute(archive);
        for (auto& item : index) {
            std::string name=item.at("path");u64 offset=item.at("offset"),size=item.at("size");
            if(!Safe(name)||offset>fileSize-16-indexSize||size>fileSize-16-indexSize-offset)return false;
            if(!mount.files.emplace(name,Entry{16+indexSize+offset,size,item.at("hash").get<u64>()}).second)return false;
        }
        std::lock_guard lock(mutex);
        mounts.push_back(std::move(mount));
        ++generation;
        SD_INFO("Mounted pak: {} ({} files)", FileSystem::Utf8(archive), index.size());
        return true;
    } catch (const std::exception& e) { SD_ERROR("Pak mount failed: {}", e.what()); return false; }
}
// Resolve preserves the virtual path: consumers read through FileSystem or ReadRange.
fs::path Pak::Resolve(const fs::path& path) { return path; }
static bool Lookup(const fs::path& path,fs::path& archive,Entry& entry){
 auto absolute=fs::absolute(path).lexically_normal();std::lock_guard lock(mutex);
 for(auto it=mounts.rbegin();it!=mounts.rend();++it){auto name=absolute.lexically_relative(it->root).generic_u8string();auto found=it->files.find(std::string(name.begin(),name.end()));if(found!=it->files.end()){archive=it->archive;entry=found->second;return true;}}return false;
}
bool Pak::Locate(const fs::path& path,fs::path& archive,u64& offset,u64& size){Entry e;if(!Lookup(path,archive,e))return false;offset=e.offset;size=e.size;return true;}
bool Pak::Contains(const fs::path& path){fs::path archive;Entry e;return Lookup(path,archive,e);}
u64 Pak::Size(const fs::path& path){fs::path archive;Entry e;return Lookup(path,archive,e)?e.size:0;}
u64 Pak::ReadRange(const fs::path& path,u64 offset,void* output,u64 count){fs::path archive;Entry e;if(!Lookup(path,archive,e)||offset>=e.size)return 0;count=std::min(count,e.size-offset);std::ifstream in(archive,std::ios::binary);in.seekg(e.offset+offset);in.read((char*)output,count);return (u64)in.gcount();}
bool Pak::ReadBinary(const fs::path& path,std::vector<u8>& data){fs::path archive;Entry e;if(!Lookup(path,archive,e))return false;data.resize((size_t)e.size);std::ifstream in(archive,std::ios::binary);in.seekg(e.offset);in.read((char*)data.data(),e.size);if(!in||Hash(data)!=e.hash)throw std::runtime_error("Corrupt pak asset: "+path.generic_string());return true;}
bool Pak::Unmount(const fs::path& archive){auto absolute=fs::absolute(archive).lexically_normal();std::lock_guard lock(mutex);auto count=std::erase_if(mounts,[&](auto& m){return m.archive.lexically_normal()==absolute;});if(count)++generation;return count!=0;}
void Pak::UnmountAll() { std::lock_guard lock(mutex); mounts.clear(); ++generation; }
u64 Pak::Generation() { return generation.load(); }
}
