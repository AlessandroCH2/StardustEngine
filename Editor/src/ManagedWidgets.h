#pragma once
// Widgets for C# editor scripts (EditorGUILayout / GUILayout): icon buttons, segmented bars and the tile grid
#include <imgui.h>
#include <nlohmann/json.hpp>

namespace Stardust::ManagedWidgets {

// Square button with a built-in line icon ("select", "brush", "eraser", "box", "bucket", "picker", "flipx", "flipy",
// "rotate", "grid", "palette", "plus", "minus", "play", "eye"); filled in blue while selected
bool IconButton(const char* icon, bool selected, const char* tooltip);
// Row of equal buttons filling the width, one selected; returns true when another one is clicked
bool Toolbar(int& selected, const std::vector<std::string>& options);
// A texture cut into a grid of tiles, laid out as in the texture. value: texture, columns, rows, tileWidth, tileHeight,
// margin, spacing, size (cell pixels), height (largest height before scrolling), selected / highlighted (tile lists).
// Returns true with value["event"] = { clicked, shift, ctrl, rectStart, rectEnd } after a click or a drag.
bool TileGrid(const std::string& texture, nlohmann::json& value);
void DrawIcon(ImDrawList* draw, const char* icon, ImVec2 center, float size, ImU32 color);

} // namespace Stardust::ManagedWidgets
