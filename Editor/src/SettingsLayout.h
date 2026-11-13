#pragma once
// Settings window: a search field at the top, the list of pages on the left, the selected page on the
// right under its title, with label / value rows (Insp widgets). Used by Project Settings and Build Settings.
#include "InspectorWidgets.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Stardust::SettingsUI {

inline float Scale() { return ImGui::GetFontSize() / 14.0f; }
inline std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return text;
}

// A full-width list row: blue when selected
inline bool ListRow(const char* label, bool selected, float indent = 10) {
    const float ui = Scale(), h = ImGui::GetFontSize() + 7 * ui;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##row", { std::max(1.0f, width), h });
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    auto* d = ImGui::GetWindowDrawList();
    if (selected) d->AddRectFilled(p, { p.x + width, p.y + h }, IM_COL32(44, 93, 160, 255));
    else if (hovered) d->AddRectFilled(p, { p.x + width, p.y + h }, IM_COL32(48, 51, 57, 255));
    d->AddText({ p.x + indent * ui, p.y + (h - ImGui::GetFontSize()) * 0.5f }, selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(206, 209, 214, 255), label);
    return clicked;
}

// The search row across the top of the window
inline void SearchBar(std::string& search) {
    const float ui = Scale(), avail = ImGui::GetContentRegionAvail().x, width = std::min(300 * ui, avail * 0.5f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight() + 8 * ui;
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled({ ImGui::GetWindowPos().x, start.y }, { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), start.y + h }, IM_COL32(34, 36, 40, 255));
    d->AddLine({ ImGui::GetWindowPos().x, start.y + h - 1 }, { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), start.y + h - 1 }, IM_COL32(18, 19, 22, 255));
    ImGui::SetCursorScreenPos({ start.x + avail - width, start.y + 4 * ui });
    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("##settingsSearch", "Search", &search);
    ImGui::SetCursorScreenPos({ start.x, start.y + h });
}

// Begins the layout; always pair with End(). The pages are segmented tabs (the Tilemap inspector's style) under the
// search field; keywords[i] lists the setting names of page i, so the search keeps only the pages that hold a match
// and jumps to the first one when the current page is hidden.
inline void Begin(const char* id, int& page, const char* const* names, const char* const* keywords, int count, std::string& search) {
    ImGui::PushID(id);
    const float ui = Scale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
    SearchBar(search);
    ImGui::PopStyleVar();
    const std::string needle = Lower(search);
    std::vector<int> shown;
    for (int i = 0; i < count; ++i)
        if (needle.empty() || Lower(names[i]).find(needle) != std::string::npos || (keywords && keywords[i] && Lower(keywords[i]).find(needle) != std::string::npos))
            shown.push_back(i);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 12 * ui, 8 * ui });
    ImGui::BeginChild("##page", { 0, 0 }, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    if (shown.empty()) { ImGui::TextDisabled("No settings match \"%s\"", search.c_str()); page = -1; }
    else {
        if (std::find(shown.begin(), shown.end(), page) == shown.end()) page = shown.front();
        std::vector<const char*> labels;
        int selected = 0;
        for (int k = 0; k < (int)shown.size(); ++k) { labels.push_back(names[shown[k]]); if (shown[k] == page) selected = k; }
        EditorIcons::SettingsTabs("##pages", selected, labels.data(), (int)labels.size());
        page = shown[selected];
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 6 * ui, 4 * ui });
}
inline void End() { ImGui::PopStyleVar(); ImGui::EndChild(); ImGui::PopID(); }

// A group of settings inside a page: a title and a thin rule
inline void Section(const char* title) {
    const float ui = Scale();
    ImGui::Dummy({ 0, 6 * ui });
    ImGui::TextColored({ 0.86f, 0.88f, 0.92f, 1 }, "%s", title);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({ p.x, p.y + 1 }, { p.x + ImGui::GetContentRegionAvail().x, p.y + 1 }, IM_COL32(60, 63, 70, 255));
    ImGui::Dummy({ 0, 4 * ui });
}
inline void Note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
// A read-only row: label on the left, value on the right
inline void ValueRow(const char* label, const std::string& value) {
    Insp::LabelCell(label);
    ImGui::TextDisabled("%s", value.c_str());
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(value.c_str()).x > ImGui::GetContentRegionAvail().x) ImGui::SetTooltip("%s", value.c_str());
}

} // namespace Stardust::SettingsUI
