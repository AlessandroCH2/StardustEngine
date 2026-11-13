#pragma once
#include <string>

namespace Stardust::ShaderGraphEditor {
// Opens a .shadergraph (asset-relative path) in the Shader Graph window
void Open(const std::string& asset);
bool IsOpen();
void Draw();
}
