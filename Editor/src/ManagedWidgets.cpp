#include "ManagedWidgets.h"
#include "Assets/AssetManager.h"
#include "Core/Application.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace Stardust::ManagedWidgets {
using json = nlohmann::json;

void DrawIcon(ImDrawList* d, const char* icon, ImVec2 c, float s, ImU32 col) {
    const float r = s * 0.5f, t = std::max(1.2f, s / 11.0f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    auto line = [&](float x0, float y0, float x1, float y1, float w = 0) { d->AddLine(P(x0, y0), P(x1, y1), col, w > 0 ? w : t); };
    const std::string_view name(icon);
    if (name == "select") { // arrow cursor
        ImVec2 pts[] = { P(-.55f, -.9f), P(.55f, .25f), P(.05f, .25f), P(.35f, .9f), P(.1f, .95f), P(-.18f, .35f), P(-.55f, .65f) };
        d->AddPolyline(pts, 7, col, ImDrawFlags_Closed, t);
    } else if (name == "brush") { // paint brush: handle and bristles
        line(.85f, -.85f, -.05f, .05f, t * 1.6f);
        ImVec2 tip[] = { P(-.05f, -.15f), P(.15f, .05f), P(-.35f, .75f), P(-.85f, .85f), P(-.75f, .35f) };
        d->AddConvexPolyFilled(tip, 5, col);
    } else if (name == "eraser") {
        ImVec2 body[] = { P(-.95f, .35f), P(.1f, -.7f), P(.85f, .05f), P(-.2f, 1.1f) };
        d->AddPolyline(body, 4, col, ImDrawFlags_Closed, t);
        ImVec2 band[] = { P(-.95f, .35f), P(-.45f, -.15f), P(.3f, .6f), P(-.2f, 1.1f) };
        d->AddConvexPolyFilled(band, 4, col);
    } else if (name == "box") { // dashed rectangle
        for (int i = 0; i < 4; ++i) {
            const float a = -.85f + i * .45f;
            line(a, -.75f, a + .25f, -.75f); line(a, .75f, a + .25f, .75f);
            line(-.85f, a * .85f, -.85f, a * .85f + .2f); line(.85f, a * .85f, .85f, a * .85f + .2f);
        }
        d->AddRectFilled(P(-.4f, -.3f), P(.4f, .3f), col, 1.5f);
    } else if (name == "bucket") { // paint bucket pouring
        ImVec2 can[] = { P(-.7f, -.2f), P(.05f, -.95f), P(.8f, -.2f), P(.05f, .55f) };
        d->AddPolyline(can, 4, col, ImDrawFlags_Closed, t);
        line(-.2f, -.55f, .45f, .1f);
        d->AddCircleFilled(P(.75f, .65f), r * .22f, col, 10);
        line(.8f, -.2f, .82f, .35f);
    } else if (name == "picker") { // eyedropper
        line(-.75f, .75f, .25f, -.25f, t * 1.5f);
        d->AddCircleFilled(P(.5f, -.5f), r * .32f, col, 12);
        line(.2f, -.8f, .8f, -.2f, t * 1.4f);
        d->AddCircleFilled(P(-.8f, .8f), t, col, 6);
    } else if (name == "flipx") { // mirrored triangles left/right
        ImVec2 a[] = { P(-.15f, -.7f), P(-.15f, .7f), P(-.9f, .7f) };
        ImVec2 b[] = { P(.15f, -.7f), P(.15f, .7f), P(.9f, .7f) };
        d->AddConvexPolyFilled(a, 3, col); d->AddPolyline(b, 3, col, ImDrawFlags_Closed, t);
        line(0, -.95f, 0, .95f, t * .7f);
    } else if (name == "flipy") {
        ImVec2 a[] = { P(-.7f, -.15f), P(.7f, -.15f), P(.7f, -.9f) };
        ImVec2 b[] = { P(-.7f, .15f), P(.7f, .15f), P(.7f, .9f) };
        d->AddConvexPolyFilled(a, 3, col); d->AddPolyline(b, 3, col, ImDrawFlags_Closed, t);
        line(-.95f, 0, .95f, 0, t * .7f);
    } else if (name == "rotate") { // circular arrow
        d->PathArcTo(c, r * .7f, -2.6f, 1.9f, 18); d->PathStroke(col, 0, t);
        const ImVec2 end = P(std::cos(1.9f) * .7f, std::sin(1.9f) * .7f);
        d->AddTriangleFilled({ end.x - r * .35f, end.y - r * .05f }, { end.x + r * .3f, end.y - r * .2f }, { end.x - r * .05f, end.y + r * .4f }, col);
    } else if (name == "grid") {
        d->AddRect(P(-.85f, -.85f), P(.85f, .85f), col, 1.5f, 0, t);
        line(-.28f, -.85f, -.28f, .85f); line(.28f, -.85f, .28f, .85f); line(-.85f, -.28f, .85f, -.28f); line(-.85f, .28f, .85f, .28f);
    } else if (name == "palette") {
        d->AddCircle(c, r * .85f, col, 20, t);
        for (int i = 0; i < 4; ++i) { const float a = -2.4f + i * 0.9f; d->AddCircleFilled(P(std::cos(a) * .48f, std::sin(a) * .48f), r * .14f, col, 8); }
    } else if (name == "plus") { line(-.7f, 0, .7f, 0); line(0, -.7f, 0, .7f); }
    else if (name == "minus") line(-.7f, 0, .7f, 0);
    else if (name == "play") d->AddTriangleFilled(P(-.5f, -.75f), P(.75f, 0), P(-.5f, .75f), col);
    else if (name == "eye") {
        d->PathArcTo(P(0, .55f), r * 1.05f, -2.4f, -.74f, 12); d->PathStroke(col, 0, t);
        d->PathArcTo(P(0, -.55f), r * 1.05f, .74f, 2.4f, 12); d->PathStroke(col, 0, t);
        d->AddCircleFilled(c, r * .28f, col, 10);
    } else d->AddRect(P(-.6f, -.6f), P(.6f, .6f), col, 2, 0, t);
}

bool IconButton(const char* icon, bool selected, const char* tooltip) {
    const float ui = Application::Get().UiScale(), size = ImGui::GetFrameHeight() + 4 * ui;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(icon);
    const bool clicked = ImGui::InvisibleButton("##icon", { size, size });
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    auto* d = ImGui::GetWindowDrawList();
    const ImU32 background = selected ? IM_COL32(47, 128, 237, 255) : held ? IM_COL32(70, 74, 82, 255) : hovered ? IM_COL32(58, 62, 69, 255) : IM_COL32(44, 47, 53, 255);
    d->AddRectFilled(p, { p.x + size, p.y + size }, background, 4 * ui);
    DrawIcon(d, icon, { p.x + size * 0.5f, p.y + size * 0.5f }, size * 0.56f, selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(205, 210, 218, 255));
    if (hovered && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

bool Toolbar(int& selected, const std::vector<std::string>& options) {
    if (options.empty()) return false;
    const float ui = Application::Get().UiScale(), gap = 2 * ui, height = ImGui::GetFrameHeight();
    const float width = (ImGui::GetContentRegionAvail().x - gap * (options.size() - 1)) / options.size();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(start, { start.x + ImGui::GetContentRegionAvail().x, start.y + height }, IM_COL32(28, 30, 34, 255), 4 * ui);
    bool changed = false;
    for (size_t i = 0; i < options.size(); ++i) {
        if (i) ImGui::SameLine(0, gap);
        ImGui::PushID((int)i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##segment", { width, height }) && selected != (int)i) { selected = (int)i; changed = true; }
        const bool hovered = ImGui::IsItemHovered(), on = selected == (int)i;
        if (on || hovered) d->AddRectFilled({ p.x + 1, p.y + 1 }, { p.x + width - 1, p.y + height - 1 }, on ? IM_COL32(58, 62, 70, 255) : IM_COL32(40, 43, 48, 255), 3 * ui);
        if (on) d->AddRectFilled({ p.x + 6 * ui, p.y + height - 3 * ui }, { p.x + width - 6 * ui, p.y + height - 1 * ui }, IM_COL32(47, 128, 237, 255), 1);
        const ImVec2 text = ImGui::CalcTextSize(options[i].c_str());
        const ImVec4 clip(p.x, p.y, p.x + width, p.y + height);
        d->AddText(nullptr, 0, { p.x + std::max(4.f, (width - text.x) * 0.5f), p.y + (height - text.y) * 0.5f }, on ? IM_COL32(240, 242, 246, 255) : IM_COL32(165, 170, 178, 255), options[i].c_str(), nullptr, 0, &clip);
        ImGui::PopID();
    }
    return changed;
}

bool TileGrid(const std::string& texturePath, json& value) {
    const float ui = Application::Get().UiScale();
    const int columns = std::max(1, value.value("columns", 1)), rows = std::max(1, value.value("rows", 1));
    const float tileWidth = value.value("tileWidth", 16.f), tileHeight = value.value("tileHeight", 16.f);
    const float margin = value.value("margin", 0.f), spacing = value.value("spacing", 0.f);
    auto texture = AssetManager::GetTexture(texturePath, true);
    std::vector<int> selected, highlighted;
    for (auto& v : value.value("selected", json::array())) if (v.is_number_integer()) selected.push_back(v.get<int>());
    for (auto& v : value.value("highlighted", json::array())) if (v.is_number_integer()) highlighted.push_back(v.get<int>());

    // Cells keep the texture's layout; they shrink to fit the width, never below 14 px
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = 1.0f, wanted = std::max(8.f, value.value("size", 40.f)) * ui;
    const float cell = std::max(14.f * ui, std::min(wanted, (avail - gap * (columns + 1) - 2) / columns));
    const float aspect = tileHeight / std::max(1.f, tileWidth), cellH = cell * aspect;
    const float gridW = columns * (cell + gap) + gap, gridH = rows * (cellH + gap) + gap;
    const float maxHeight = value.value("height", 0.f) * ui;
    const float viewH = maxHeight > 0 ? std::min(gridH, maxHeight) : gridH;

    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(22, 23, 26, 255));
    ImGui::BeginChild("##tileGrid", { std::min(avail, gridW + (gridH > viewH ? ImGui::GetStyle().ScrollbarSize : 0)), viewH }, ImGuiChildFlags_None,
                      gridW > avail ? ImGuiWindowFlags_HorizontalScrollbar : 0);
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##grid", { gridW, gridH });
    const bool hovered = ImGui::IsItemHovered();
    auto cellAt = [&](ImVec2 m) -> int {
        const int cx = (int)((m.x - origin.x - gap) / (cell + gap)), cy = (int)((m.y - origin.y - gap) / (cellH + gap));
        return cx < 0 || cy < 0 || cx >= columns || cy >= rows ? -1 : cy * columns + cx;
    };
    auto cellRect = [&](int tile, ImVec2& a, ImVec2& b) {
        a = { origin.x + gap + (tile % columns) * (cell + gap), origin.y + gap + (tile / columns) * (cellH + gap) };
        b = { a.x + cell, a.y + cellH };
    };
    // Drag state lives in ImGui storage, per grid
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID dragKey = ImGui::GetID("##dragStart");
    int dragStart = storage->GetInt(dragKey, -1);
    const int under = hovered ? cellAt(ImGui::GetMousePos()) : -1;
    if (ImGui::IsItemActivated() && under >= 0) { dragStart = under; storage->SetInt(dragKey, under); }
    const bool dragging = ImGui::IsItemActive() && dragStart >= 0;
    const int dragEnd = dragging ? std::max(0, cellAt(ImGui::GetMousePos())) : -1;

    // Checkerboard under transparent pixels, then the tiles
    const float w = texture ? (float)texture->Desc().width : 1, h = texture ? (float)texture->Desc().height : 1;
    const ImTextureID id = texture ? (ImTextureID)Application::Get().GetDevice().ImGuiTexture(texture.get()) : (ImTextureID)0;
    const ImVec2 clipMin = ImGui::GetWindowPos(), clipMax = { clipMin.x + ImGui::GetWindowWidth(), clipMin.y + ImGui::GetWindowHeight() };
    for (int tile = 0; tile < columns * rows; ++tile) {
        ImVec2 a, b;
        cellRect(tile, a, b);
        if (b.y < clipMin.y || a.y > clipMax.y) continue;
        const float half = cell * 0.5f;
        d->AddRectFilled(a, b, IM_COL32(52, 54, 58, 255));
        d->AddRectFilled(a, { a.x + half, a.y + cellH * 0.5f }, IM_COL32(64, 66, 71, 255));
        d->AddRectFilled({ a.x + half, a.y + cellH * 0.5f }, b, IM_COL32(64, 66, 71, 255));
        if (id) {
            const float x = margin + (tile % columns) * (tileWidth + spacing), y = margin + (tile / columns) * (tileHeight + spacing);
            d->AddImage(id, a, b, { x / w, y / h }, { (x + tileWidth) / w, (y + tileHeight) / h });
        }
    }
    auto outline = [&](int tile, ImU32 color, float thickness, bool fill) {
        if (tile < 0 || tile >= columns * rows) return;
        ImVec2 a, b;
        cellRect(tile, a, b);
        if (fill) d->AddRectFilled(a, b, (color & 0x00FFFFFF) | 0x38000000);
        d->AddRect({ a.x - 0.5f, a.y - 0.5f }, { b.x + 0.5f, b.y + 0.5f }, color, 0, 0, thickness);
    };
    for (int tile : highlighted) outline(tile, IM_COL32(240, 180, 60, 255), 1.5f * ui, false);
    for (int tile : selected) outline(tile, IM_COL32(70, 150, 255, 255), 2.f * ui, true);
    if (dragging && dragEnd >= 0 && dragEnd != dragStart) {
        // Rectangle being dragged out
        const int x0 = std::min(dragStart % columns, dragEnd % columns), x1 = std::max(dragStart % columns, dragEnd % columns);
        const int y0 = std::min(dragStart / columns, dragEnd / columns), y1 = std::max(dragStart / columns, dragEnd / columns);
        ImVec2 a, b, c, e;
        cellRect(y0 * columns + x0, a, b); cellRect(y1 * columns + x1, c, e);
        d->AddRectFilled(a, e, IM_COL32(70, 150, 255, 50));
        d->AddRect(a, e, IM_COL32(120, 180, 255, 255), 0, 0, 2 * ui);
    } else if (under >= 0) {
        outline(under, IM_COL32(255, 255, 255, 220), 1.5f * ui, false);
        ImGui::SetTooltip("Tile %d", under);
    }
    if (ImGui::IsItemDeactivated() && dragStart >= 0) {
        const int end = std::max(0, cellAt(ImGui::GetMousePos()) < 0 ? dragStart : cellAt(ImGui::GetMousePos()));
        const ImGuiIO& io = ImGui::GetIO();
        json event = { { "clicked", end == dragStart ? dragStart : -1 }, { "shift", io.KeyShift }, { "ctrl", io.KeyCtrl },
                       { "rectStart", end != dragStart ? dragStart : -1 }, { "rectEnd", end != dragStart ? end : -1 } };
        value["event"] = event;
        storage->SetInt(dragKey, -1);
        changed = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return changed;
}

} // namespace Stardust::ManagedWidgets
