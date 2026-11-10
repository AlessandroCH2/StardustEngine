#include "Assets/Pak.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <atomic>
#include <cstring>
#include <random>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Stardust {
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
struct Entry { u64 offset=0,size=0,hash=0,nonce=0; }; // nonce 0: stored as is
struct MountedPak { fs::path root,archive; std::unordered_map<std::string,Entry> files; };
std::vector<MountedPak> mounts;
std::mutex mutex;
std::atomic<u64> generation{0};
// Integrity check of every entry, eight bytes per step
u64 FastHash(const std::vector<u8>& bytes) {
    u64 h = 14695981039346656037ull ^ (bytes.size() * 0x9E3779B97F4A7C15ull);
    const size_t words = bytes.size() / 8;
    for (size_t i = 0; i < words; ++i) { u64 w; std::memcpy(&w, bytes.data() + i * 8, 8); h ^= w * 0xC2B2AE3D27D4EB4Full; h = (h << 31 | h >> 33) * 0x9E3779B185EBCA87ull; }
    for (size_t i = words * 8; i < bytes.size(); ++i) { h ^= bytes[i]; h *= 1099511628211ull; }
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33;
    return h;
}
// ---------------------------------------------------------------- encryption
// ChaCha12 stream cipher: any byte range decrypts on its own, so audio and video keep streaming from inside the pak.
// The key is 32 bytes, made from the project's passphrase with SHA-256.
struct PakKey { u32 words[8]{}; bool enabled = false; };
PakKey s_Key;

inline u32 Rotl(u32 v, int c) { return (v << c) | (v >> (32 - c)); }
void ChaChaBlock(const u32 key[8], u64 nonce, u64 counter, u8 out[64]) {
    const u32 s[16] = { 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
                        (u32)counter, (u32)(counter >> 32), (u32)nonce, (u32)(nonce >> 32) };
    u32 x[16]; std::memcpy(x, s, sizeof(x));
#define SD_QR(a, b, c, d) x[a] += x[b]; x[d] = Rotl(x[d] ^ x[a], 16); x[c] += x[d]; x[b] = Rotl(x[b] ^ x[c], 12); x[a] += x[b]; x[d] = Rotl(x[d] ^ x[a], 8); x[c] += x[d]; x[b] = Rotl(x[b] ^ x[c], 7);
    for (int round = 0; round < 6; ++round) {
        SD_QR(0, 4, 8, 12) SD_QR(1, 5, 9, 13) SD_QR(2, 6, 10, 14) SD_QR(3, 7, 11, 15)
        SD_QR(0, 5, 10, 15) SD_QR(1, 6, 11, 12) SD_QR(2, 7, 8, 13) SD_QR(3, 4, 9, 14)
    }
#undef SD_QR
    for (int i = 0; i < 16; ++i) { u32 v = x[i] + s[i]; out[i * 4] = (u8)v; out[i * 4 + 1] = (u8)(v >> 8); out[i * 4 + 2] = (u8)(v >> 16); out[i * 4 + 3] = (u8)(v >> 24); }
}
void Crypt(const PakKey& key, u64 nonce, u64 offset, u8* data, u64 count) {
    u64 block = offset / 64; u32 skip = (u32)(offset % 64); u8 stream[64];
    while (count) {
        ChaChaBlock(key.words, nonce, block++, stream);
        const u64 n = std::min<u64>(64 - skip, count);
        for (u64 i = 0; i < n; ++i) data[i] ^= stream[skip + i];
        data += n; count -= n; skip = 0;
    }
}
u64 EntryNonce(u64 salt, u64 index) { u64 v = salt ^ ((index + 1) * 0x9E3779B97F4A7C15ull); return v ? v : 1; }
u64 IndexNonce(u64 salt) { return ~salt ? ~salt : 1; }
// Detects a wrong key before anything is read: the first keystream word of a nonce nothing else uses
u64 KeyCheck(const PakKey& key, u64 salt) { u8 block[64]; ChaChaBlock(key.words, salt * 0xD6E8FEB86659FD93ull + 0x5851F42D4C957F2Dull, ~0ull, block); u64 v; std::memcpy(&v, block, 8); return v; }

void Sha256(const u8* data, size_t size, u8 out[32]) {
    static const u32 k[64] = { 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    u32 h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    std::vector<u8> m(data, data + size); m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    const u64 bits = (u64)size * 8; for (int i = 7; i >= 0; --i) m.push_back((u8)(bits >> (i * 8)));
    auto rotr = [](u32 v, int c) { return (v >> c) | (v << (32 - c)); };
    for (size_t chunk = 0; chunk < m.size(); chunk += 64) {
        u32 w[64];
        for (int i = 0; i < 16; ++i) w[i] = (u32)m[chunk + i * 4] << 24 | (u32)m[chunk + i * 4 + 1] << 16 | (u32)m[chunk + i * 4 + 2] << 8 | m[chunk + i * 4 + 3];
        for (int i = 16; i < 64; ++i) { u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
        u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            u32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            u32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) { out[i * 4] = (u8)(h[i] >> 24); out[i * 4 + 1] = (u8)(h[i] >> 16); out[i * 4 + 2] = (u8)(h[i] >> 8); out[i * 4 + 3] = (u8)h[i]; }
}

// ---------------------------------------------------------------- binary index
void PutVarint(std::string& out, u64 v) { while (v >= 0x80) { out += (char)(v | 0x80); v >>= 7; } out += (char)v; }
bool GetVarint(const std::string& in, size_t& at, u64& v) {
    v = 0;
    for (int shift = 0; shift < 64 && at < in.size(); shift += 7) { u8 b = (u8)in[at++]; v |= (u64)(b & 0x7F) << shift; if (!(b & 0x80)) return true; }
    return false;
}
constexpr char kMagicV2[8] = { 'S','D','P','A','K','0','0','2' };
constexpr u64 kHeaderV2 = 8 + 4 + 4 + 8 + 8 + 8; // magic, flags, count, salt, key check, index size

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

bool Pak::Build(const fs::path& source, const fs::path& output, bool encrypt) {
    try {
        if (encrypt && !s_Key.enabled) { SD_ERROR("Pak build: encryption asked but no key is set"); return false; }
        std::vector<fs::path> files;
        for (auto& entry : fs::recursive_directory_iterator(source)) {
            if (!entry.is_regular_file()) continue;
            auto relative = fs::relative(entry.path(), source);
            bool editor = false;
            for (const auto& part : relative) if (part == "Editor") editor = true;
            if (!editor && entry.path().extension() != ".cs" && entry.path().extension() != ".pak") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        u64 salt = 0; { std::random_device random; salt = ((u64)random() << 32) ^ random() ^ (u64)std::chrono::steady_clock::now().time_since_epoch().count(); }
        // Index: sorted paths share their beginning, so each stores only how much it reuses and what is new
        std::string index, previous;
        u64 offset = 0;
        for (const auto& file : files) {
            const u64 size = fs::file_size(file);
            auto name = FileSystem::Utf8(fs::relative(file, source));
            size_t shared = 0; while (shared < name.size() && shared < previous.size() && shared < 65535 && name[shared] == previous[shared]) ++shared;
            auto data = FileSystem::ReadBinary(file);
            if (data.size() != size) return false;
            PutVarint(index, shared); PutVarint(index, name.size() - shared); index.append(name, shared, std::string::npos);
            PutVarint(index, offset); PutVarint(index, size);
            const u64 hash = FastHash(data); index.append((const char*)&hash, 8);
            previous = name; offset += size;
        }
        const u32 flags = encrypt ? 1u : 0u, count = (u32)files.size();
        const u64 check = encrypt ? KeyCheck(s_Key, salt) : 0, indexSize = index.size();
        if (encrypt) Crypt(s_Key, IndexNonce(salt), 0, (u8*)index.data(), index.size());
        fs::create_directories(output.parent_path());
        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        out.write(kMagicV2, 8); out.write((const char*)&flags, 4); out.write((const char*)&count, 4);
        out.write((const char*)&salt, 8); out.write((const char*)&check, 8); out.write((const char*)&indexSize, 8);
        out.write(index.data(), index.size());
        for (size_t i = 0; i < files.size(); ++i) {
            auto data = FileSystem::ReadBinary(files[i]);
            if (encrypt) Crypt(s_Key, EntryNonce(salt, i), 0, data.data(), data.size());
            out.write((const char*)data.data(), data.size());
        }
        out.flush();
        return out.good();
    } catch (const std::exception& e) { SD_ERROR("Pak build failed: {}", e.what()); return false; }
}
bool Pak::Mount(const fs::path& archive, const fs::path& virtualRoot) {
    try {
        try{PruneStalePakCaches();}catch(const std::exception& e){SD_WARN("Stale pak cache cleanup: {}",e.what());}
        std::ifstream in(archive, std::ios::binary);
        char magic[8]{}; u64 indexSize = 0;
        in.read(magic, 8);
        if (in && !memcmp(magic, kMagicV2, 8)) {
            u32 flags = 0, count = 0; u64 salt = 0, check = 0;
            in.read((char*)&flags, 4); in.read((char*)&count, 4); in.read((char*)&salt, 8); in.read((char*)&check, 8); in.read((char*)&indexSize, 8);
            const u64 fileSize = fs::file_size(archive);
            if (!in || count > 1000000 || indexSize > fileSize - kHeaderV2) { SD_ERROR("Pak mount failed: {} is damaged", FileSystem::Utf8(archive)); return false; }
            const bool encrypted = flags & 1;
            if (encrypted && !s_Key.enabled) { SD_ERROR("Pak mount failed: {} is encrypted and no key is set", FileSystem::Utf8(archive)); return false; }
            if (encrypted && KeyCheck(s_Key, salt) != check) { SD_ERROR("Pak mount failed: wrong key for {}", FileSystem::Utf8(archive)); return false; }
            std::string index((size_t)indexSize, '\0'); in.read(index.data(), indexSize);
            if (!in) return false;
            if (encrypted) Crypt(s_Key, IndexNonce(salt), 0, (u8*)index.data(), index.size());
            MountedPak mount;
            mount.root = fs::absolute(virtualRoot).lexically_normal();
            mount.archive = fs::absolute(archive);
            const u64 dataStart = kHeaderV2 + indexSize, dataSize = fileSize - dataStart;
            size_t at = 0; std::string name;
            for (u32 i = 0; i < count; ++i) {
                u64 shared, added, offset, size, hash;
                if (!GetVarint(index, at, shared) || !GetVarint(index, at, added) || shared > name.size() || added > index.size() - at) return false;
                name.resize((size_t)shared); name.append(index, at, (size_t)added); at += (size_t)added;
                if (!GetVarint(index, at, offset) || !GetVarint(index, at, size) || at + 8 > index.size()) return false;
                std::memcpy(&hash, index.data() + at, 8); at += 8;
                if (!Safe(name) || offset > dataSize || size > dataSize - offset) return false;
                if (!mount.files.emplace(name, Entry{ dataStart + offset, size, hash, encrypted ? EntryNonce(salt, i) : 0 }).second) return false;
            }
            std::lock_guard lock(mutex);
            mounts.push_back(std::move(mount));
            ++generation;
            SD_INFO("Mounted pak: {} ({} files{})", FileSystem::Utf8(archive), count, encrypted ? ", encrypted" : "");
            return true;
        }
        SD_ERROR("Pak mount failed: {} is not a Stardust pak or was built by an older engine; rebuild it", FileSystem::Utf8(archive));
        return false;
    } catch (const std::exception& e) { SD_ERROR("Pak mount failed: {}", e.what()); return false; }
}
// Resolve preserves the virtual path: consumers read through FileSystem or ReadRange.
fs::path Pak::Resolve(const fs::path& path) { return path; }
static bool Lookup(const fs::path& path,fs::path& archive,Entry& entry){
 auto absolute=fs::absolute(path).lexically_normal();std::lock_guard lock(mutex);
 for(auto it=mounts.rbegin();it!=mounts.rend();++it){auto name=absolute.lexically_relative(it->root).generic_u8string();auto found=it->files.find(std::string(name.begin(),name.end()));if(found!=it->files.end()){archive=it->archive;entry=found->second;return true;}}return false;
}
bool Pak::Locate(const fs::path& path,fs::path& archive,u64& offset,u64& size,u64* cipher){Entry e;if(!Lookup(path,archive,e))return false;offset=e.offset;size=e.size;if(cipher)*cipher=e.nonce;else if(e.nonce)return false;return true;}
void Pak::Decrypt(u64 cipher,u64 offsetInFile,void* data,u64 count){if(cipher&&s_Key.enabled)Crypt(s_Key,cipher,offsetInFile,(u8*)data,count);}
void Pak::SetPassphrase(const std::string& passphrase){if(passphrase.empty()){s_Key=PakKey{};return;}u8 digest[32];Sha256((const u8*)passphrase.data(),passphrase.size(),digest);SetKey(digest);}
void Pak::SetKey(const u8 key[32]){PakKey k;for(int i=0;i<8;++i)k.words[i]=(u32)key[i*4]|(u32)key[i*4+1]<<8|(u32)key[i*4+2]<<16|(u32)key[i*4+3]<<24;k.enabled=true;s_Key=k;}
bool Pak::HasKey(){return s_Key.enabled;}
void Pak::KeyFromPassphrase(const std::string& passphrase,u8 out[32]){Sha256((const u8*)passphrase.data(),passphrase.size(),out);}
bool Pak::Contains(const fs::path& path){fs::path archive;Entry e;return Lookup(path,archive,e);}
u64 Pak::Size(const fs::path& path){fs::path archive;Entry e;return Lookup(path,archive,e)?e.size:0;}
u64 Pak::ReadRange(const fs::path& path,u64 offset,void* output,u64 count){fs::path archive;Entry e;if(!Lookup(path,archive,e)||offset>=e.size)return 0;count=std::min(count,e.size-offset);std::ifstream in(archive,std::ios::binary);in.seekg(e.offset+offset);in.read((char*)output,count);const u64 got=(u64)in.gcount();if(e.nonce)Crypt(s_Key,e.nonce,offset,(u8*)output,got);return got;}
bool Pak::ReadBinary(const fs::path& path,std::vector<u8>& data){fs::path archive;Entry e;if(!Lookup(path,archive,e))return false;data.resize((size_t)e.size);std::ifstream in(archive,std::ios::binary);in.seekg(e.offset);in.read((char*)data.data(),e.size);if(in&&e.nonce)Crypt(s_Key,e.nonce,0,data.data(),data.size());if(!in||FastHash(data)!=e.hash)throw std::runtime_error("Corrupt pak asset: "+path.generic_string());return true;}
bool Pak::Unmount(const fs::path& archive){auto absolute=fs::absolute(archive).lexically_normal();std::lock_guard lock(mutex);auto count=std::erase_if(mounts,[&](auto& m){return m.archive.lexically_normal()==absolute;});if(count)++generation;return count!=0;}
void Pak::UnmountAll() { std::lock_guard lock(mutex); mounts.clear(); ++generation; }
u64 Pak::Generation() { return generation.load(); }

// The player reserves 48 bytes: an 8-byte tag, 8 bytes of state and the 32-byte key (see Player/src/main.cpp).
// The tag is assembled backwards here so this function never finds its own text.
bool Pak::EmbedKey(const fs::path& executable, const std::string& passphrase) {
    const char reversed[] = "!!REKRAM10YEKPDS";
    std::string tag(reversed, 16); std::reverse(tag.begin(), tag.end());
    auto bytes = FileSystem::ReadBinary(executable);
    std::string_view view((const char*)bytes.data(), bytes.size());
    const size_t at = view.find(tag);
    if (at == std::string_view::npos || view.find(tag, at + 1) != std::string_view::npos || at + 48 > bytes.size()) {
        SD_ERROR("Pak key: no key slot in {}", FileSystem::Utf8(executable));
        return false;
    }
    std::memcpy(bytes.data() + at + 8, "ENABLED!", 8);
    KeyFromPassphrase(passphrase, bytes.data() + at + 16);
    std::ofstream out(executable, std::ios::binary | std::ios::trunc);
    out.write((const char*)bytes.data(), bytes.size());
    return out.good();
}
}
