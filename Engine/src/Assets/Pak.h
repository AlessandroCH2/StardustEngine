#pragma once
#include "Core/Base.h"
#include <filesystem>

namespace Stardust {
// Indexed archives: mount reads metadata only; assets are read on demand without extraction.
class Pak {
public:
    static bool Build(const std::filesystem::path& source, const std::filesystem::path& output);
    static bool Mount(const std::filesystem::path& archive, const std::filesystem::path& virtualRoot);
    static bool Unmount(const std::filesystem::path& archive);
    static void UnmountAll();
    static std::filesystem::path Resolve(const std::filesystem::path& path);
    static bool Contains(const std::filesystem::path& path);
    static bool Locate(const std::filesystem::path& path,std::filesystem::path& archive,u64& offset,u64& size);
    static u64 Size(const std::filesystem::path& path);
    static u64 ReadRange(const std::filesystem::path& path,u64 offset,void* output,u64 count);
    static bool ReadBinary(const std::filesystem::path& path,std::vector<u8>& data);
    static u64 Generation();
};
}
