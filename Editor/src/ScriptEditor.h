#pragma once
#include <filesystem>

namespace Stardust::ScriptEditor {
// Built-in code editor for .cs, .shader, .hlsl/.hlsli and text assets: tabs, syntax highlighting, autocompletion
void Open(const std::filesystem::path& file, int line = 0);
bool IsOpen();
void Draw();
// True when the file is open with unsaved changes
bool HasUnsavedChanges(const std::filesystem::path& file);
}
