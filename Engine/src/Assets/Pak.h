#pragma once
#include "Core/Base.h"
#include <filesystem>
#include <string>

namespace Stardust {
// Indexed archives: mount reads metadata only; assets are read on demand without extraction.
class Pak {
public:
    // encrypt uses the key set with SetPassphrase / SetKey
    static bool Build(const std::filesystem::path& source, const std::filesystem::path& output, bool encrypt = false);
    static bool Mount(const std::filesystem::path& archive, const std::filesystem::path& virtualRoot);
    static bool Unmount(const std::filesystem::path& archive);
    static void UnmountAll();
    static std::filesystem::path Resolve(const std::filesystem::path& path);
    static bool Contains(const std::filesystem::path& path);
    // Where an entry lives inside its archive. Without cipher an encrypted entry is refused; with it, pass it to Decrypt.
    static bool Locate(const std::filesystem::path& path,std::filesystem::path& archive,u64& offset,u64& size,u64* cipher=nullptr);
    static void Decrypt(u64 cipher,u64 offsetInFile,void* data,u64 count);
    // Encryption key for building and mounting paks: a passphrase (editor) or its 32-byte digest (exported players)
    static void SetPassphrase(const std::string& passphrase);
    static void SetKey(const u8 key[32]);
    static bool HasKey();
    static void KeyFromPassphrase(const std::string& passphrase,u8 out[32]);
    // Writes the key into an exported player executable, at the slot the player reads at startup
    static bool EmbedKey(const std::filesystem::path& executable,const std::string& passphrase);
    static u64 Size(const std::filesystem::path& path);
    static u64 ReadRange(const std::filesystem::path& path,u64 offset,void* output,u64 count);
    static bool ReadBinary(const std::filesystem::path& path,std::vector<u8>& data);
    static u64 Generation();
};
}
