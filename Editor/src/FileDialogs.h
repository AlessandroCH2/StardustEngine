#pragma once
#include <filesystem>
#include <optional>

namespace Stardust::FileDialogs {

// Native Windows dialogs. filter example: L"Stardust Scene (*.scene)\0*.scene\0"
std::optional<std::filesystem::path> OpenFile(void* ownerHwnd, const wchar_t* filter, const std::filesystem::path& initialDir);
std::optional<std::filesystem::path> PickFolder(void* ownerHwnd, const std::filesystem::path& initialDir);
std::optional<std::filesystem::path> SaveFile(void* ownerHwnd, const wchar_t* filter, const wchar_t* defaultExt,
                                              const std::filesystem::path& initialDir);

} // namespace Stardust::FileDialogs
