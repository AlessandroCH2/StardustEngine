// Project Hub, Build Settings, Project Settings, player builds and undo/redo
#include "Assets/AssetMeta.h"
#include "Renderer/Quality.h"
#include "EditorApp.h"
#include "EditorIcons.h"
#include "Project/BuildCooker.h"
#include "AssetUI.h"
#include "FileDialogs.h"
#include "Project/Project.h"
#include "Project/PackageManager.h"
#include "Assets/AssetManager.h"
#include "Assets/Pak.h"
#include <random>
#include "UI/CanvasRenderer.h"
#include "Scripting/ScriptEngine.h"
#include "Scene/SceneSerializer.h"
#include "Core/FileSystem.h"
#include "Core/Process.h"
#include "Core/Log.h"
#include "Version.h"
#include <chrono>
#include <format>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <imgui_stdlib.h>
#include <thread>
#include <fstream>
#include <map>
#include <set>

namespace Stardust {

namespace fs = std::filesystem;

// ------------------------------------------------------------------------------------------------ project hub

namespace {
// "5 minutes ago", "3 days ago"... for the project list
std::string AgeText(const fs::path& file) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    if (ec) return "";
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - written).count();
    auto plural = [](long long n, const char* unit) { return std::format("{} {}{} ago", n, unit, n == 1 ? "" : "s"); };
    if (seconds < 60) return "Just now";
    if (seconds < 3600) return plural(seconds / 60, "minute");
    if (seconds < 86400) return plural(seconds / 3600, "hour");
    if (seconds < 86400 * 30) return plural(seconds / 86400, "day");
    if (seconds < 86400 * 365) return plural(seconds / (86400 * 30), "month");
    return plural(seconds / (86400 * 365), "year");
}
// Rounded tile with the project's initial, its color picked from the name
void ProjectTile(ImDrawList* d, ImVec2 min, float size, const std::string& name) {
    static const ImU32 colors[] = { IM_COL32(47, 128, 237, 255), IM_COL32(52, 168, 120, 255), IM_COL32(200, 120, 60, 255),
                                    IM_COL32(150, 95, 210, 255), IM_COL32(200, 80, 110, 255), IM_COL32(60, 160, 185, 255) };
    const ImU32 color = colors[std::hash<std::string>{}(name) % std::size(colors)];
    d->AddRectFilled(min, { min.x + size, min.y + size }, color, size * 0.22f);
    const char initial[2] = { name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0 };
    ImFont* bold = Application::BoldFont();
    ImFont* font = bold ? bold : ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * 1.15f;
    const ImVec2 t = font->CalcTextSizeA(fontSize, FLT_MAX, 0, initial);
    d->AddText(font, fontSize, { min.x + (size - t.x) * 0.5f, min.y + (size - t.y) * 0.5f }, IM_COL32(255, 255, 255, 255), initial);
}
}

namespace {
// What the Hub shows about a project, read from its .sdproject and refreshed when the file changes
struct HubProjectInfo { bool exists = false; fs::file_time_type modified{}; std::string engineVersion, platform; };
const HubProjectInfo& ProjectInfo(const fs::path& file) {
    static std::map<fs::path, HubProjectInfo> cache;
    HubProjectInfo& info = cache[file];
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec) { info = {}; return info; }
    if (info.exists && info.modified == time) return info;
    info.exists = true; info.modified = time;
    const auto j = nlohmann::json::parse(FileSystem::ReadText(file), nullptr, false);
    info.engineVersion = j.is_object() ? j.value("engineVersion", std::string()) : std::string();
    info.platform = "Windows";
    if (j.is_object() && j.contains("player") && j["player"].is_object()) info.platform = j["player"].value("target", info.platform);
    return info;
}
// -1, 0 or 1 comparing "0.6.2"-style versions number by number; empty counts as oldest
int CompareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v) { std::vector<int> out; int n = 0; bool any = false; for (char c : v) { if (std::isdigit((unsigned char)c)) { n = n * 10 + (c - '0'); any = true; } else if (c == '.') { out.push_back(n); n = 0; } else break; } if (any || !out.empty()) out.push_back(n); return out; };
    const auto x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        const int u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
        if (u != v) return u < v ? -1 : 1;
    }
    return x.empty() == y.empty() ? 0 : x.empty() ? -1 : 1;
}
// Favorite projects, kept in %APPDATA%/Stardust/hub.json next to the recent list
fs::path HubFile() { const char* appdata = std::getenv("APPDATA"); return fs::path(appdata ? appdata : ".") / "Stardust" / "hub.json"; }
std::set<std::string>& HubFavoritesStorage() {
    static std::set<std::string> favorites;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        const auto j = nlohmann::json::parse(FileSystem::ReadText(HubFile()), nullptr, false);
        if (j.is_object()) for (auto& f : j.value("favorites", nlohmann::json::array())) if (f.is_string()) favorites.insert(f.get<std::string>());
    }
    return favorites;
}
const std::set<std::string>& HubFavorites() { return HubFavoritesStorage(); }
void SetHubFavorite(const fs::path& project, bool favorite) {
    auto& favorites = HubFavoritesStorage();
    const std::string key = FileSystem::Utf8(project);
    if (favorite ? !favorites.insert(key).second : !favorites.erase(key)) return;
    FileSystem::WriteText(HubFile(), nlohmann::json({ { "favorites", favorites } }).dump(2));
}
void DrawStar(ImDrawList* d, ImVec2 c, float r, bool filled, ImU32 color) {
    ImVec2 points[10];
    for (int i = 0; i < 10; ++i) {
        const float a = -1.5707963f + i * 0.6283185f, radius = i % 2 ? r * 0.45f : r;
        points[i] = { c.x + std::cos(a) * radius, c.y + std::sin(a) * radius };
    }
    if (filled) { for (int i = 0; i < 10; i += 2) d->AddTriangleFilled(c, points[i], points[(i + 1) % 10], color), d->AddTriangleFilled(c, points[i], points[(i + 9) % 10], color); }
    else d->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.4f);
}
void DrawWarning(ImDrawList* d, ImVec2 c, float r, ImU32 color) {
    d->AddTriangleFilled({ c.x, c.y - r }, { c.x + r * 1.1f, c.y + r * .85f }, { c.x - r * 1.1f, c.y + r * .85f }, color);
    d->AddLine({ c.x, c.y - r * .35f }, { c.x, c.y + r * .3f }, IM_COL32(25, 26, 29, 255), std::max(1.5f, r * .2f));
    d->AddCircleFilled({ c.x, c.y + r * .58f }, std::max(1.f, r * .13f), IM_COL32(25, 26, 29, 255));
}
}

// Project Hub in the editor's style: a sidebar, a Projects page with a searchable list, and a New Project dialog
void EditorApp::DrawProjectHub() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ui = UiScale();
    ImFont* bold = Application::BoldFont();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(35, 37, 41, 255));
    ImGui::Begin("##hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Sidebar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 25, 29, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * ui, 16 * ui));
    ImGui::BeginChild("##hubSidebar", ImVec2(230 * ui, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float iconSize = 30 * ui;
        if (void* icon = EngineIconTexture()) d->AddImage((ImTextureID)icon, p, { p.x + iconSize, p.y + iconSize });
        d->AddText(bold ? bold : ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, { p.x + iconSize + 10 * ui, p.y + (iconSize - ImGui::GetFontSize() * 1.2f) * 0.5f },
                   IM_COL32(232, 234, 238, 255), "Stardust Hub");
        ImGui::Dummy({ 1, iconSize + 22 * ui });
        // Navigation (only Projects for now)
        const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + ImGui::GetContentRegionAvail().x, a.y + 34 * ui);
        d->AddRectFilled(a, b, IM_COL32(44, 72, 115, 255), 4);
        d->AddRectFilled(a, { a.x + 3 * ui, b.y }, IM_COL32(47, 128, 237, 255), 2);
        // Small cube icon
        const ImVec2 c(a.x + 20 * ui, (a.y + b.y) * 0.5f);
        const float s = 6.5f * ui;
        ImVec2 top[] = { { c.x, c.y - s }, { c.x + s, c.y - s * .5f }, { c.x, c.y }, { c.x - s, c.y - s * .5f } };
        d->AddConvexPolyFilled(top, 4, IM_COL32(220, 225, 232, 255));
        ImVec2 left[] = { { c.x - s, c.y - s * .35f }, { c.x - s * .08f, c.y + s * .12f }, { c.x - s * .08f, c.y + s }, { c.x - s, c.y + s * .5f } };
        d->AddConvexPolyFilled(left, 4, IM_COL32(160, 168, 180, 255));
        ImVec2 right[] = { { c.x + s * .08f, c.y + s * .12f }, { c.x + s, c.y - s * .35f }, { c.x + s, c.y + s * .5f }, { c.x + s * .08f, c.y + s } };
        d->AddConvexPolyFilled(right, 4, IM_COL32(120, 128, 140, 255));
        d->AddText({ a.x + 36 * ui, (a.y + b.y - ImGui::GetFontSize()) * 0.5f }, IM_COL32(235, 237, 241, 255), "Projects");
        ImGui::Dummy({ 1, b.y - a.y });
        // Version at the bottom
        const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 16 * ui - ImGui::GetFontSize();
        d->AddText({ a.x, bottom }, IM_COL32(110, 114, 122, 255), "Stardust " STARDUST_VERSION_FULL);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 0);

    // Projects page
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * ui, 22 * ui));
    ImGui::BeginChild("##hubMain", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    static std::string search;
    static fs::path selected, pendingOpen;
    static int sortColumn = 2; // 0 name, 1 engine version, 2 modified
    static bool sortAscending = false;
    fs::path openNow;          // opened after the Hub window ends
    bool askVersion = false;
    const auto& favorites = HubFavorites();
    // Opens a project, asking first when it was last saved by another engine version
    auto requestOpen = [&](const fs::path& project) {
        const HubProjectInfo& info = ProjectInfo(project);
        if (!info.engineVersion.empty() && info.engineVersion != STARDUST_VERSION) { pendingOpen = project; askVersion = true; }
        else openNow = project;
    };
    {
        if (bold) ImGui::PushFont(bold);
        ImGui::SetWindowFontScale(1.45f);
        ImGui::TextUnformatted("Projects");
        ImGui::SetWindowFontScale(1);
        if (bold) ImGui::PopFont();
        // Search, Add and New project on the right of the heading, like Unity Hub
        const float buttonHeight = 32 * ui, addWidth = 80 * ui, newWidth = 130 * ui, searchWidth = std::min(300 * ui, ImGui::GetWindowWidth() * 0.3f);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - searchWidth - addWidth - newWidth - 16 * ui);
        const float rowY = ImGui::GetCursorPosY() - 4 * ui;
        ImGui::SetCursorPosY(rowY);
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::SetNextItemWidth(searchWidth);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(26 * ui, (buttonHeight - ImGui::GetFontSize()) * 0.5f));
            ImGui::InputTextWithHint("##hubSearch", "Search", &search);
            ImGui::PopStyleVar();
            auto* d = ImGui::GetWindowDrawList();
            const ImVec2 c(p.x + 13 * ui, p.y + buttonHeight * 0.5f - 1);
            d->AddCircle(c, 4.5f * ui, IM_COL32(150, 154, 162, 255), 12, 1.5f);
            d->AddLine({ c.x + 3.2f * ui, c.y + 3.2f * ui }, { c.x + 6.5f * ui, c.y + 6.5f * ui }, IM_COL32(150, 154, 162, 255), 1.8f);
        }
        ImGui::SameLine(0, 8 * ui);
        ImGui::SetCursorPosY(rowY);
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Button("Add      ##hubAdd", { addWidth, buttonHeight })) ImGui::OpenPopup("HubAdd");
            const float cx = p.x + addWidth - 18 * ui, cy = p.y + buttonHeight * 0.5f;
            ImGui::GetWindowDrawList()->AddTriangleFilled({ cx - 4 * ui, cy - 2 * ui }, { cx + 4 * ui, cy - 2 * ui }, { cx, cy + 3 * ui }, IM_COL32(210, 214, 220, 255));
            ImGui::SetNextWindowPos({ p.x, p.y + buttonHeight + 4 * ui });
            if (ImGui::BeginPopup("HubAdd")) {
                if (ImGui::MenuItem("Add project from disk"))
                    if (auto file = FileDialogs::OpenFile(GetWindow().NativeHandle(), L"Stardust Project (*.sdproject)\0*.sdproject\0", {})) { Project::AddRecent(*file); selected = fs::absolute(*file); }
                if (ImGui::MenuItem("Open project from disk"))
                    if (auto file = FileDialogs::OpenFile(GetWindow().NativeHandle(), L"Stardust Project (*.sdproject)\0*.sdproject\0", {})) requestOpen(*file);
                ImGui::EndPopup();
            }
        }
        ImGui::SameLine(0, 8 * ui);
        ImGui::SetCursorPosY(rowY);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(47, 128, 237, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(82, 150, 245, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(36, 108, 210, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        if (ImGui::Button("+  New project", { newWidth, buttonHeight })) ImGui::OpenPopup("New Project");
        ImGui::PopStyleColor(4);
    }
    ImGui::Dummy({ 1, 10 * ui });

    // Projects matching the search; favorites first, then the chosen sort
    auto recent = Project::RecentProjects();
    auto lower = [](std::string text) { std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return text; };
    const std::string needle = lower(search);
    std::erase_if(recent, [&](const fs::path& p) { return !needle.empty() && lower(FileSystem::Utf8(p)).find(needle) == std::string::npos; });
    std::stable_sort(recent.begin(), recent.end(), [&](const fs::path& x, const fs::path& y) {
        const bool fx = favorites.count(FileSystem::Utf8(x)) > 0, fy = favorites.count(FileSystem::Utf8(y)) > 0;
        if (fx != fy) return fx;
        int order = 0;
        if (sortColumn == 0) order = lower(FileSystem::Utf8(x.stem())).compare(lower(FileSystem::Utf8(y.stem())));
        else if (sortColumn == 1) order = CompareVersions(ProjectInfo(x).engineVersion, ProjectInfo(y).engineVersion);
        else { const auto tx = ProjectInfo(x).modified, ty = ProjectInfo(y).modified; order = tx < ty ? -1 : tx > ty ? 1 : 0; }
        return sortAscending ? order < 0 : order > 0;
    });

    // Column layout shared by the header and the rows
    const float width = ImGui::GetContentRegionAvail().x;
    const float nameX = 92 * ui, platformX = std::max(330 * ui, width * 0.5f), versionX = platformX + 150 * ui, modifiedX = width - 190 * ui;
    {
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = 30 * ui, ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
        d->AddRectFilled(p, { p.x + width, p.y + h }, IM_COL32(29, 31, 35, 255), 5, ImDrawFlags_RoundCornersTop);
        d->AddLine({ p.x, p.y + h }, { p.x + width, p.y + h }, IM_COL32(52, 55, 61, 255));
        const ImU32 dim = IM_COL32(150, 154, 162, 255);
        DrawStar(d, { p.x + 22 * ui, p.y + h * 0.5f }, 6.5f * ui, false, dim);
        // Sortable headers: click to sort, again to reverse
        auto header = [&](const char* label, float x, int column) {
            const ImVec2 size = ImGui::CalcTextSize(label);
            ImGui::SetCursorScreenPos({ p.x + x - 4 * ui, p.y });
            ImGui::PushID(column);
            if (ImGui::InvisibleButton("##sort", { size.x + 26 * ui, h })) { if (sortColumn == column) sortAscending = !sortAscending; else { sortColumn = column; sortAscending = column == 0; } }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            d->AddText({ p.x + x, ty }, hovered || sortColumn == column ? IM_COL32(225, 228, 233, 255) : dim, label);
            if (sortColumn == column) {
                const float ax = p.x + x + size.x + 10 * ui, ay = p.y + h * 0.5f, r = 3.5f * ui;
                if (sortAscending) d->AddTriangleFilled({ ax - r, ay + r * .6f }, { ax + r, ay + r * .6f }, { ax, ay - r * .7f }, IM_COL32(225, 228, 233, 255));
                else d->AddTriangleFilled({ ax - r, ay - r * .6f }, { ax + r, ay - r * .6f }, { ax, ay + r * .7f }, IM_COL32(225, 228, 233, 255));
            }
        };
        header("Name", nameX, 0);
        d->AddText({ p.x + platformX, ty }, dim, "Platform");
        header("Engine version", versionX, 1);
        header("Modified", modifiedX, 2);
        ImGui::SetCursorScreenPos({ p.x, p.y + h + 4 * ui });
        ImGui::Dummy({ width, 0 });
    }

    ImGui::BeginChild("##hubList", ImVec2(0, 0));
    if (recent.empty()) {
        const char* empty = search.empty() ? "No projects yet. Create one with New project." : "No project matches the search.";
        ImGui::Dummy({ 1, 30 * ui });
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(empty).x) * 0.5f));
        ImGui::TextDisabled("%s", empty);
    }
    const float rowHeight = 58 * ui, tile = 36 * ui, lineH = ImGui::GetFontSize();
    for (const fs::path& p : recent) {
        ImGui::PushID(FileSystem::Utf8(p).c_str());
        const HubProjectInfo& info = ProjectInfo(p);
        const bool exists = info.exists, favorite = favorites.count(FileSystem::Utf8(p)) > 0;
        const std::string name = FileSystem::Utf8(p.stem()), folder = FileSystem::Utf8(p.parent_path());
        const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + ImGui::GetContentRegionAvail().x, a.y + rowHeight);
        const bool clicked = ImGui::InvisibleButton("##project", { b.x - a.x, rowHeight }, ImGuiButtonFlags_PressedOnClick);
        const bool hovered = ImGui::IsItemHovered(), isSelected = selected == p;
        const ImVec2 star(a.x + 22 * ui, (a.y + b.y) * 0.5f), dots(b.x - 18 * ui, (a.y + b.y) * 0.5f);
        const bool overStar = hovered && ImGui::IsMouseHoveringRect({ star.x - 12 * ui, a.y }, { star.x + 12 * ui, b.y });
        const bool overDots = hovered && ImGui::IsMouseHoveringRect({ dots.x - 12 * ui, a.y }, { dots.x + 12 * ui, b.y });
        if (clicked) {
            selected = p;
            if (overStar) SetHubFavorite(p, !favorite);
            else if (overDots) ImGui::OpenPopup("ProjectMenu");
        }
        if (hovered && !overStar && !overDots && ImGui::IsMouseDoubleClicked(0) && exists) requestOpen(p);
        if (ImGui::IsItemClicked(1)) { selected = p; ImGui::OpenPopup("ProjectMenu"); }

        auto* d = ImGui::GetWindowDrawList();
        if (isSelected || hovered) d->AddRectFilled(a, b, isSelected ? IM_COL32(44, 72, 115, 255) : IM_COL32(43, 46, 51, 255), 5);
        else d->AddLine({ a.x + 8 * ui, b.y }, { b.x - 8 * ui, b.y }, IM_COL32(42, 44, 49, 255));
        // Favorite star
        if (favorite || hovered) DrawStar(d, star, 7 * ui, favorite, favorite ? IM_COL32(245, 196, 70, 255) : overStar ? IM_COL32(230, 232, 236, 255) : IM_COL32(130, 134, 142, 255));
        // Tile, name and path
        if (exists) ProjectTile(d, { a.x + 44 * ui, a.y + (rowHeight - tile) * 0.5f }, tile, name);
        else DrawWarning(d, { a.x + 44 * ui + tile * 0.5f, (a.y + b.y) * 0.5f }, 11 * ui, IM_COL32(240, 190, 60, 255));
        const ImVec4 nameClip(a.x, a.y, a.x + platformX - 16 * ui, b.y);
        const float nameY = a.y + rowHeight * 0.5f - lineH - 1;
        ImFont* nameFont = bold ? bold : ImGui::GetFont();
        d->AddText(nameFont, lineH, { a.x + nameX, nameY }, exists ? IM_COL32(235, 237, 241, 255) : IM_COL32(170, 174, 182, 255), name.c_str(), nullptr, 0, &nameClip);
        if (!exists) { // "Project not found" pill after the name
            const float x = a.x + nameX + nameFont->CalcTextSizeA(lineH, FLT_MAX, 0, name.c_str()).x + 8 * ui;
            const char* badge = "Project not found";
            const ImVec2 t = ImGui::CalcTextSize(badge);
            d->AddRectFilled({ x, nameY - 1 }, { x + t.x + 14 * ui, nameY + lineH + 1 }, IM_COL32(64, 67, 74, 255), lineH);
            d->AddText({ x + 7 * ui, nameY }, IM_COL32(225, 228, 233, 255), badge);
        }
        d->AddText(nullptr, 0, { a.x + nameX, a.y + rowHeight * 0.5f + 2 }, IM_COL32(135, 139, 147, 255), folder.c_str(), nullptr, 0, &nameClip);
        // Platform, engine version (warning when it is not this editor's), modified
        const float midY = a.y + (rowHeight - lineH) * 0.5f;
        d->AddText({ a.x + platformX, midY }, exists ? IM_COL32(200, 204, 211, 255) : IM_COL32(120, 124, 132, 255), exists ? info.platform.c_str() : "-");
        if (!exists || info.engineVersion.empty()) {
            d->AddText({ a.x + versionX, midY }, IM_COL32(120, 124, 132, 255), exists ? "Unknown" : "-");
            if (exists && ImGui::IsMouseHoveringRect({ a.x + versionX, a.y }, { a.x + modifiedX - 10 * ui, b.y }))
                ImGui::SetTooltip("Saved before engine versions were recorded.\nIt is stamped the next time the project opens.");
        } else {
            const int compare = CompareVersions(info.engineVersion, STARDUST_VERSION);
            float x = a.x + versionX;
            if (compare != 0) {
                DrawWarning(d, { x + 7 * ui, midY + lineH * 0.5f }, 7 * ui, compare > 0 ? IM_COL32(235, 85, 75, 255) : IM_COL32(240, 190, 60, 255));
                x += 20 * ui;
                if (ImGui::IsMouseHoveringRect({ a.x + versionX, a.y }, { a.x + modifiedX - 10 * ui, b.y }))
                    ImGui::SetTooltip(compare > 0 ? "Last saved with a newer engine (%s).\nThis editor is %s: some data may not load." : "Last saved with an older engine (%s).\nOpening it in %s upgrades the project.",
                                      info.engineVersion.c_str(), STARDUST_VERSION);
            }
            d->AddText({ x, midY }, IM_COL32(200, 204, 211, 255), info.engineVersion.c_str());
        }
        d->AddText({ a.x + modifiedX, midY }, IM_COL32(150, 154, 162, 255), exists ? AgeText(p).c_str() : "-");
        // ⋮ menu
        if (hovered || isSelected) {
            if (overDots) d->AddRectFilled({ dots.x - 11 * ui, dots.y - 13 * ui }, { dots.x + 11 * ui, dots.y + 13 * ui }, IM_COL32(64, 68, 76, 255), 4);
            for (int i = -1; i <= 1; ++i) d->AddCircleFilled({ dots.x, dots.y + i * 5 * ui }, 1.8f * ui, IM_COL32(200, 204, 211, 255));
        }
        if (overStar) ImGui::SetTooltip("%s", favorite ? "Remove from favorites" : "Add to favorites");
        else if (hovered && !overDots && !ImGui::IsMouseHoveringRect({ a.x + versionX, a.y }, { a.x + modifiedX - 10 * ui, b.y })) ImGui::SetTooltip("%s", exists ? "Double-click to open" : "The project file was moved or deleted");
        if (ImGui::BeginPopup("ProjectMenu")) {
            if (ImGui::MenuItem("Open", nullptr, false, exists)) requestOpen(p);
            if (ImGui::MenuItem("Show in Explorer", nullptr, false, exists)) ShellExecuteFile(p.parent_path());
            if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(folder.c_str());
            if (ImGui::MenuItem(favorite ? "Remove from favorites" : "Add to favorites")) SetHubFavorite(p, !favorite);
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from list")) { Project::RemoveRecent(p); SetHubFavorite(p, false); }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Opening a project saved by another engine version asks first, like Unity Hub
    if (askVersion) ImGui::OpenPopup("Change engine version?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 480 * ui, 0 });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * ui, 18 * ui));
    if (ImGui::BeginPopupModal("Change engine version?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const std::string version = ProjectInfo(pendingOpen).engineVersion;
        const bool newer = CompareVersions(version, STARDUST_VERSION) > 0;
        if (bold) ImGui::PushFont(bold);
        ImGui::TextUnformatted(FileSystem::Utf8(pendingOpen.stem()).c_str());
        if (bold) ImGui::PopFont();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::Text("This project was last saved with Stardust %s. It will open in Stardust %s.", version.c_str(), STARDUST_VERSION);
        if (newer) ImGui::TextColored({ 1, .5f, .45f, 1 }, "That version is newer than this editor: data it added may be lost when the project is saved.");
        else ImGui::TextDisabled("Files are upgraded when saved and may not open in the older version again. Back up the project if needed.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy({ 1, 6 * ui }); ImGui::Separator(); ImGui::Dummy({ 1, 6 * ui });
        const float buttonWidth = 130 * ui;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - buttonWidth * 2 - 8 * ui);
        if (ImGui::Button("Cancel", { buttonWidth, 30 * ui }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { pendingOpen.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(0, 8 * ui);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(47, 128, 237, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(82, 150, 245, 255));
        if (ImGui::Button("Open anyway", { buttonWidth, 30 * ui })) { openNow = pendingOpen; pendingOpen.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::PopStyleColor(2);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // New Project dialog
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 520 * ui, 0 });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * ui, 18 * ui));
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        static std::string name = "My Game";
        static std::string location;
        if (location.empty()) location = (fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : std::getenv("HOME") ? std::getenv("HOME") : "/") / "Documents" / "Stardust Projects").string();
        ImGui::TextDisabled("Name");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##name", &name);
        ImGui::Spacing();
        ImGui::TextDisabled("Location");
        ImGui::SetNextItemWidth(-80 * ui);
        ImGui::InputText("##loc", &location);
        ImGui::SameLine();
        if (ImGui::Button("Browse", { -1, 0 })) if (auto f = FileDialogs::PickFolder(GetWindow().NativeHandle(), location)) location = f->string();
        const fs::path dir = fs::path(location) / name;
        const bool taken = !name.empty() && fs::exists(dir);
        ImGui::Spacing();
        if (taken) ImGui::TextColored({ 1, .55f, .45f, 1 }, "A folder with this name already exists.");
        else ImGui::TextDisabled("Folder: %s", dir.string().c_str());
        ImGui::Dummy({ 1, 6 * ui }); ImGui::Separator(); ImGui::Dummy({ 1, 6 * ui });
        const float buttonWidth = 110 * ui;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - buttonWidth * 2 - 8 * ui);
        if (ImGui::Button("Cancel", { buttonWidth, 30 * ui }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, 8 * ui);
        ImGui::BeginDisabled(name.empty() || taken);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(47, 128, 237, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(82, 150, 245, 255));
        const bool create = ImGui::Button("Create", { buttonWidth, 30 * ui });
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if (create && Project::Create(dir, name)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup(); ImGui::PopStyleVar(); ImGui::EndChild(); ImGui::End();
            OpenProject(Project::File(), true);
            return;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::End();
    if (!openNow.empty()) OpenProject(openNow);
}

void EditorApp::OpenProject(const fs::path& file, bool isNew) {
    StopAnimationPreview();
    if(!m_PrefabAsset.empty())ExitPrefabStage();
    if (m_State != PlayState::Edit) Stop();
    if (Project::IsLoaded() && m_EditorScene) SaveProjectState();
    if (!Project::Load(file)) return;
    Project::Save(); // stamps the engine version the Hub shows
    Pak::SetPassphrase(Project::Config().pakKey);
    AssetMeta::MigrateFolder(Project::AssetsDir());
    PackageManager::EnsureDefault();
    Project::WriteEditorScriptProject();
    Project::AddRecent(file);
    m_ProjectRoot = Project::AssetsDir();
    m_ProjectCurrentDir = m_ProjectRoot;
    m_SelectedAsset.clear();
    CanvasRenderer::Clear();
    AssetManager::Shutdown();
    AssetManager::Init(GetDevice(), m_ProjectRoot);
    TerrainData::ClearCache();

    if (!ScriptEngine::IsReady()) ScriptEngine::Init();
    else ScriptEngine::ReloadGameAssembly();
    ScriptEngine::CheckForSourceChange();
    if (ScriptEngine::IsReady() && ScriptEngine::NeedsBuild()) { SD_INFO("Compiling project scripts..."); ScriptEngine::BuildAsync(); }

    // Last scene, else the first scene in build, else a new default scene
    std::string scene = Project::Config().lastOpenedScene;
    if (scene.empty() || !fs::exists(m_ProjectRoot / fs::u8path(scene))) scene = Project::EnabledScenes().empty() ? "" : Project::EnabledScenes()[0];
    if (!scene.empty() && fs::exists(m_ProjectRoot / fs::u8path(scene))) OpenScene(m_ProjectRoot / fs::u8path(scene));
    else {
        NewScene();
        SaveSceneAs(m_ProjectRoot / "Scenes" / "Main.scene");
        auto& cfg = Project::Config();
        if (cfg.scenes.empty()) { cfg.scenes.push_back("Scenes/Main.scene"); cfg.scenesEnabled.push_back(true); }
        Project::Save();
    }
    m_Undo.clear(); m_Redo.clear(); m_UndoSnapshot.clear();
    m_ShowHub = false;
    UpdateTitle();
    (void)isNew;
}

void EditorApp::SaveProjectState() {
    if (!Project::IsLoaded()) return;
    if (!m_ScenePath.empty()) Project::Config().lastOpenedScene = AssetManager::ToRelative(m_ScenePath);
    Project::Save();
}

// ------------------------------------------------------------------------------------------------ build settings

void EditorApp::DrawBuildSettings() {
    if (!m_ShowBuildSettings) return;
    ImGui::SetNextWindowSize(ImVec2(620*UiScale(), 560*UiScale()), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Build Settings", &m_ShowBuildSettings,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    auto& cfg = Project::Config();
    cfg.scenesEnabled.resize(cfg.scenes.size(), true);

    static int page=0;const char* pages[]={"Scenes","Content Paks","Player","Export"};EditorIcons::SettingsTabs("BuildTabs",page,pages,4);ImGui::BeginChild("BuildSettingsBody",{0,0});
    if(page==0){
    ImGui::SeparatorText("Scenes In Build");
    ImGui::TextDisabled("Index 0 is loaded first. Drag .scene files here from the Project panel, drag rows to reorder.");
    ImGui::BeginChild("##scenes", ImVec2(0, 200*UiScale()), ImGuiChildFlags_Borders);
    int moveFrom = -1, moveTo = -1, removeAt = -1;
    int index = 0;
    for (size_t i = 0; i < cfg.scenes.size(); ++i) {
        ImGui::PushID((int)i);
        bool en = cfg.scenesEnabled[i];
        if (ImGui::Checkbox("##en", &en)) { cfg.scenesEnabled[i] = en; Project::Save(); }
        ImGui::SameLine();
        ImGui::Selectable(std::format("{}   {}", en ? std::to_string(index) : std::string(" "), cfg.scenes[i]).c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 30, 0));
        if (en) ++index;
        if (ImGui::BeginDragDropSource()) {
            int src = (int)i;
            ImGui::SetDragDropPayload("STARDUST_SCENE_ROW", &src, sizeof(int));
            ImGui::Text("%s", cfg.scenes[i].c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STARDUST_SCENE_ROW")) { moveFrom = *(const int*)p->Data; moveTo = (int)i; }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = (int)i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    // dropping assets on the list
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)) {
            std::string path((const char*)p->Data, p->DataSize - 1);
            if (path.ends_with(".scene") && std::find(cfg.scenes.begin(), cfg.scenes.end(), path) == cfg.scenes.end()) {
                cfg.scenes.push_back(path); cfg.scenesEnabled.push_back(true); Project::Save();
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
        std::string s = cfg.scenes[moveFrom]; bool e = cfg.scenesEnabled[moveFrom];
        cfg.scenes.erase(cfg.scenes.begin() + moveFrom); cfg.scenesEnabled.erase(cfg.scenesEnabled.begin() + moveFrom);
        cfg.scenes.insert(cfg.scenes.begin() + moveTo, s); cfg.scenesEnabled.insert(cfg.scenesEnabled.begin() + moveTo, e);
        Project::Save();
    }
    if (removeAt >= 0) { cfg.scenes.erase(cfg.scenes.begin() + removeAt); cfg.scenesEnabled.erase(cfg.scenesEnabled.begin() + removeAt); Project::Save(); }
    if (ImGui::Button("Add Open Scene") && !m_ScenePath.empty()) {
        std::string rel = AssetManager::ToRelative(m_ScenePath);
        if (std::find(cfg.scenes.begin(), cfg.scenes.end(), rel) == cfg.scenes.end()) { cfg.scenes.push_back(rel); cfg.scenesEnabled.push_back(true); Project::Save(); }
    }

    }
    if(page==1){
    ImGui::SeparatorText("Optional Content Paks");ImGui::TextWrapped("Folders are relative to Assets. Scene dependencies always go in game.pak. Remaining assets and their dependencies go in Data/Optional and are mounted by script.");
    int removePack=-1;for(int i=0;i<(int)cfg.contentPacks.size();++i){ImGui::PushID(i);bool change=ImGui::InputText("Pak name",&cfg.contentPacks[i].name);change|=ImGui::InputText("Asset folder",&cfg.contentPacks[i].folder);if(ImGui::SmallButton("Remove"))removePack=i;if(change)Project::Save();ImGui::PopID();}
    if(removePack>=0){cfg.contentPacks.erase(cfg.contentPacks.begin()+removePack);Project::Save();}if(ImGui::Button("+ Add Optional Pak")){cfg.contentPacks.push_back({"content"+std::to_string(cfg.contentPacks.size()+1),"DLC"});Project::Save();}
    }
    if(page==2){
    ImGui::SeparatorText("Player");
    bool changed = false;
    int platform=cfg.buildTarget=="Linux"?1:0;if(ImGui::Combo("Target Platform",&platform,"Windows x64\0Linux x64\0")){cfg.buildTarget=platform?"Linux":"Windows";cfg.lastBuildFolder=(Project::Dir()/"Build"/cfg.buildTarget).string();changed=true;}
    changed |= ImGui::InputText("Product Name", &cfg.name);
    changed |= ImGui::InputText("Company", &cfg.company);
    changed |= ImGui::InputText("Version", &cfg.version);
    changed |= AssetUI::AssetField("Game Icon",cfg.icon,{".png",".jpg",".jpeg",".bmp",".tga"},"Stardust (default)");
    ImGui::TextDisabled("Used for the game window, taskbar and exported .exe. Transparent square PNG recommended.");
    int res[2] = { (int)cfg.windowWidth, (int)cfg.windowHeight };
    if (ImGui::InputInt2("Resolution", res)) { cfg.windowWidth = (u32)std::max(320, res[0]); cfg.windowHeight = (u32)std::max(240, res[1]); changed = true; }
    changed |= ImGui::Checkbox("Fullscreen", &cfg.fullscreen);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("VSync", &cfg.vsync);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Graphics Quality", &cfg.quality, "Low (older GPUs)\0Medium\0High\0")) { GraphicsQuality::Apply(cfg.quality); changed = true; }
    {   // Pak encryption: the key goes into the exported player, never into the paks
        static bool showKey = false;
        ImGui::SetNextItemWidth(260);
        if (ImGui::InputTextWithHint("Pak Encryption Key", "empty = not encrypted", &cfg.pakKey, showKey ? 0 : ImGuiInputTextFlags_Password)) { Pak::SetPassphrase(cfg.pakKey); changed = true; }
        ImGui::SameLine(); ImGui::Checkbox("Show##pakKey", &showKey);
        ImGui::SameLine(); if (ImGui::SmallButton("Generate")) { std::random_device random; static const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789"; cfg.pakKey.clear(); for (int i = 0; i < 32; ++i) cfg.pakKey += chars[random() % 57]; Pak::SetPassphrase(cfg.pakKey); changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paks built from now on are encrypted with this key. Keep it: old builds need theirs.");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Low and Medium render the Game view and the built player at a lower 3D resolution,\nwith smaller shadow maps, cheaper clouds and sparser grass.");
    if (changed) Project::Save();

    }
    if(page==3){
    ImGui::SeparatorText("Build");
    ImGui::Text("Target: %s",cfg.buildTarget.c_str());
    if (cfg.lastBuildFolder.empty()) cfg.lastBuildFolder = (Project::Dir() / "Build").string();
    ImGui::SetNextItemWidth(-80);
    ImGui::InputText("##out", &cfg.lastBuildFolder);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) if (auto f = FileDialogs::PickFolder(GetWindow().NativeHandle(), cfg.lastBuildFolder)) { cfg.lastBuildFolder = f->string(); Project::Save(); }
    ImGui::BeginDisabled(m_Building || m_State != PlayState::Edit || Project::EnabledScenes().empty());
    if (ImGui::Button("Build", ImVec2(140*UiScale(), 30*UiScale()))) BuildPlayer(false);
    ImGui::SameLine();
    if (ImGui::Button("Build And Run", ImVec2(140*UiScale(), 30*UiScale()))) BuildPlayer(true);
    ImGui::EndDisabled();
    if (m_Building) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Building... (see Console)"); }
    if (Project::EnabledScenes().empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Add at least one scene to build.");
    ImGui::TextDisabled("The build needs the .NET 8 runtime on the target PC (C# scripts).");
    }
    ImGui::EndChild();
    ImGui::End();
}

// Copies the player executable, runtime files, compiled scripts and the project assets into a standalone folder
void EditorApp::BuildPlayer(bool run) {
    if(!m_PrefabAsset.empty())ExitPrefabStage();
    if (m_Building) return;
    if (m_PlayerBuildThread.joinable()) m_PlayerBuildThread.join();
    SaveScene();
    if (m_ScenePath.empty() || m_SceneDirty) { SD_ERROR("Save the scene before building the player"); return; }
    SaveProjectState();
    m_Building = true;
    ProjectConfig cfg = Project::Config();
    fs::path out = fs::absolute(cfg.lastBuildFolder).lexically_normal();
    fs::path exeDir = FileSystem::ExecutableDir();
    fs::path projectDir = Project::Dir(), assets = Project::AssetsDir(), csproj = Project::ScriptProjectFile(), projectFile = Project::File();
    // Reject layouts where replacing Data/Assets could touch project sources or recurse into the build.
    auto within = [](const fs::path& child, const fs::path& parent) {
        auto rel = fs::weakly_canonical(child).lexically_relative(fs::weakly_canonical(parent));
        return !rel.empty() && *rel.begin() != "..";
    };
    if (within(out, assets) || within(assets, out / "Data" / "Assets") || within(exeDir, out)) {
        SD_ERROR("Choose a build folder outside Assets and the engine binaries"); m_Building = false; return;
    }
    for (const auto& scene : Project::EnabledScenes()) {
        if (!fs::is_regular_file(assets / fs::u8path(scene))) { SD_ERROR("Build scene is missing: {}", scene); m_Building = false; return; }
    }
    m_PlayerBuildThread = std::thread([this, cfg, out, exeDir, projectDir, assets, csproj, projectFile, run, within] {
        auto fail = [&](const std::string& why) { SD_ERROR("Build failed: {}", why); m_Building = false; };
        try {
        std::error_code ec;
        SD_INFO("Building player '{}' into {}", cfg.name, out.string());
        fs::create_directories(out / "Data" / "Managed", ec);
        if (ec) return fail(ec.message());

        // 1. C# scripts (Release)
        if (fs::exists(csproj)) {
            std::wstring cmd = L"dotnet build \"" + csproj.wstring() + L"\" -c Release -o \"" + (out / "Data" / "Managed").wstring() +
                               L"\" -p:StardustBin=\"" + exeDir.wstring() + L"\" --nologo -v:q -clp:NoSummary";
            int code = RunProcess(cmd, projectDir, [](const std::string& l) {
                if (l.find(": error") != std::string::npos) SD_ERROR("{}", l); else SD_INFO("{}", l);
            });
            if (code != 0) return fail("script compilation failed");
        }
        // 2. runtime binaries
        std::string exeName = cfg.name;
        std::erase_if(exeName, [](char c) { return std::string("\\/:*?\"<>|").find(c) != std::string::npos; });
        if (exeName.empty()) exeName = "Game";
        bool linuxTarget=cfg.buildTarget=="Linux";
#ifdef _WIN32
        auto runtimeDir=linuxTarget?exeDir/"Linux":exeDir;
#else
        auto runtimeDir=exeDir; // a Linux editor can only export for Linux, with the player next to it
        if(!linuxTarget)return fail("Windows players can only be built from the Windows editor.");
#endif

        if(linuxTarget&&!fs::is_regular_file(runtimeDir/"StardustPlayer"))return fail("Linux runtime missing. Run Tools/BuildLinuxRuntime.sh on Linux and copy its output to build/bin/Release/Linux.");
        if(linuxTarget){std::ifstream runtime(runtimeDir/"StardustPlayer",std::ios::binary);unsigned char header[20]{};runtime.read((char*)header,sizeof(header));if(runtime.gcount()!=sizeof(header)||header[0]!=0x7f||header[1]!='E'||header[2]!='L'||header[3]!='F'||header[4]!=2||header[18]!=62||header[19]!=0)return fail("Invalid Linux runtime: expected a native ELF x86-64 executable.");}
        auto playerFile=out/(linuxTarget?"Game.x86_64":exeName+".exe");
        fs::copy_file(runtimeDir / (linuxTarget?"StardustPlayer":"StardustPlayer.exe"), playerFile, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("StardustPlayer.exe not found next to the editor (" + ec.message() + ")");
        for (const char* f : { "Stardust.ScriptCore.dll", "Stardust.ScriptCore.runtimeconfig.json", "Stardust.ScriptCore.deps.json" })
        {
            fs::copy_file(exeDir / f, out / f, fs::copy_options::overwrite_existing, ec);
            if (ec) return fail(std::string("cannot copy ") + f + ": " + ec.message());
        }
        fs::copy_file(runtimeDir/(linuxTarget?"libnethost.so":"nethost.dll"),out/(linuxTarget?"libnethost.so":"nethost.dll"),fs::copy_options::overwrite_existing,ec);if(ec)return fail("Missing native .NET host: "+ec.message());
        if(linuxTarget){
            fs::permissions(playerFile,fs::perms::owner_exec|fs::perms::group_exec|fs::perms::others_exec,fs::perm_options::add,ec);
            if(fs::exists(runtimeDir/"engineIcon.png"))fs::copy_file(runtimeDir/"engineIcon.png",out/"engineIcon.png",fs::copy_options::overwrite_existing);
            FileSystem::WriteText(out/"Launch.sh",R"launch(#!/bin/sh
cd -- "$(dirname -- "$0")"
exec ./Game.x86_64 "$@"
)launch");
        }
        fs::copy(exeDir / "shaders", out / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("cannot copy shaders: " + ec.message());
        // 3. assets (without C# sources) and project settings
        if(!BuildCooker::Cook(projectDir,out,cfg))return fail("Asset dependency cooking failed (see Console)");
        auto loose=out/"Data/Assets";if(fs::exists(loose)){if(!within(loose,out/"Data"))return fail("Old loose asset folder is outside export");fs::remove_all(loose,ec);if(ec)return fail("Cannot remove old loose assets");}
        auto legacy=out/"Data/game.sdproject";if(fs::is_regular_file(legacy))fs::remove(legacy,ec);
        auto license=FileSystem::EngineRoot()/"docs/THIRD_PARTY_UFBX.txt";
        if (fs::exists(license)) fs::copy_file(license,out/"THIRD_PARTY_UFBX.txt",fs::copy_options::overwrite_existing);
        auto videoLicense=FileSystem::EngineRoot()/"docs/THIRD_PARTY_PL_MPEG.txt";if(fs::exists(videoLicense))fs::copy_file(videoLicense,out/"THIRD_PARTY_PL_MPEG.txt",fs::copy_options::overwrite_existing);
        SD_INFO("Build complete: {} (Data/game.pak)", playerFile.string());
        if (run&&!linuxTarget) LaunchProcess(playerFile, out);
        else if(run&&linuxTarget) SD_INFO("Linux build ready; run Launch.sh on Linux.");
        else ShellExecuteFile(out);
        m_Building = false;
        } catch (const std::exception& e) { fail(e.what()); }
    });
}

// ------------------------------------------------------------------------------------------------ project settings

void EditorApp::DrawProjectSettings() {
    if (!m_ShowProjectSettings) return;
    ImGui::SetNextWindowSize(ImVec2(480*UiScale(), 420*UiScale()), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Settings", &m_ShowProjectSettings,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    auto& cfg = Project::Config();
    bool changed = false;
    static int section=0;const char* categories[]={"General","Packages","Input","Layers","Tags"};EditorIcons::SettingsTabs("ProjectTabs",section,categories,5);ImGui::BeginChild("ProjectSettingsBody",{0,0});
    if(section==0){
    ImGui::SeparatorText("Project");
    changed |= ImGui::InputText("Name", &cfg.name);
    changed |= ImGui::InputText("Company", &cfg.company);
    changed |= ImGui::InputText("Version", &cfg.version);
    ImGui::TextDisabled("Scripts assembly: %s", cfg.assemblyName.c_str());
    ImGui::TextDisabled("Folder: %s", Project::Dir().string().c_str());
    }
    if(section==1){
    ImGui::TextWrapped("Install, update and remove packages, import their samples, or add packages from disk or from a git repository in the Package Manager.");
    ImGui::Spacing();
    if (ImGui::Button("Open Package Manager", { 220 * UiScale(), 30 * UiScale() })) m_ShowPackageManager = true;
    ImGui::SeparatorText("Installed");
    for (const auto& package : PackageManager::Installed())
        ImGui::BulletText("%s  %s%s", package.displayName.c_str(), package.version.c_str(), package.engine ? "  (Engine)" : "");
    }
    if(section==2){
    ImGui::SeparatorText("Input Manager");
        auto file=Project::AssetsDir()/"Settings/Input.json";static std::string loadedInput;static nlohmann::json input;
        if(loadedInput!=file.string()){loadedInput=file.string();input=nlohmann::json::parse(FileSystem::ReadText(file),nullptr,false);if(!input.is_object())input={{"Horizontal",{{"Positive",68},{"Negative",65},{"AlternatePositive",262},{"AlternateNegative",263},{"GamepadAxis",0}}},{"Vertical",{{"Positive",87},{"Negative",83},{"AlternatePositive",265},{"AlternateNegative",264},{"GamepadAxis",1}}},{"Jump",{{"Positive",32},{"GamepadButton",0}}},{"Submit",{{"Positive",257},{"GamepadButton",0}}},{"Cancel",{{"Positive",256},{"GamepadButton",1}}}};}
        bool save=false;static std::string newAction;ImGui::InputText("Action Name",&newAction);ImGui::SameLine();if(ImGui::Button("Add Action")&&!newAction.empty()){input[newAction]={{"Positive",32},{"Negative",0},{"GamepadAxis",-1},{"GamepadButton",-1},{"Deadzone",.15f},{"Invert",false}};save=true;}
        std::string removeInput,renameFrom,renameTo;for(auto& [name,action]:input.items())if(ImGui::TreeNode(name.c_str())){
            auto renamed=name;if(ImGui::InputText("Name",&renamed,ImGuiInputTextFlags_EnterReturnsTrue)&&!renamed.empty()&&renamed!=name&&!input.contains(renamed)){renameFrom=name;renameTo=renamed;}
            for(const char* key:{"Positive","Negative","AlternatePositive","AlternateNegative"}){int value=action.value(key,0);if(ImGui::InputInt(key,&value)){action[key]=std::clamp(value,0,348);save=true;}}
            int controller=action.value("GamepadIndex",0);if(ImGui::SliderInt("Controller",&controller,0,15)){action["GamepadIndex"]=controller;save=true;}
            int axis=action.value("GamepadAxis",-1)+1;if(ImGui::Combo("Gamepad Axis",&axis,"None\0Left X\0Left Y\0Right X\0Right Y\0Left Trigger\0Right Trigger\0")){action["GamepadAxis"]=axis-1;save=true;}
            int button=action.value("GamepadButton",-1);if(ImGui::SliderInt("Gamepad Button",&button,-1,14)){action["GamepadButton"]=button;save=true;}
            float deadzone=action.value("Deadzone",.15f);if(ImGui::SliderFloat("Deadzone",&deadzone,0,.95f)){action["Deadzone"]=deadzone;save=true;}bool invert=action.value("Invert",false);if(ImGui::Checkbox("Invert",&invert)){action["Invert"]=invert;save=true;}
            float sensitivity=action.value("Sensitivity",3.f);if(ImGui::DragFloat("Sensitivity",&sensitivity,.05f,0,100)){action["Sensitivity"]=sensitivity;save=true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("How fast the keyboard value reaches 1 (per second). 0 = instant");
            float gravity=action.value("Gravity",3.f);if(ImGui::DragFloat("Gravity",&gravity,.05f,0,100)){action["Gravity"]=gravity;save=true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("How fast it returns to 0 once released (per second). 0 = instant");
            bool snap=action.value("Snap",true);if(ImGui::Checkbox("Snap",&snap)){action["Snap"]=snap;save=true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("Pressing the opposite key jumps through 0");
            ImGui::TextDisabled("Keyboard codes: Space 32, A-Z 65-90, Enter 257, arrows 262-265.");if(ImGui::Button("Remove Action"))removeInput=name;ImGui::TreePop();}
        if(!renameFrom.empty()){input[renameTo]=input[renameFrom];input.erase(renameFrom);save=true;}
        if(!removeInput.empty()){input.erase(removeInput);save=true;}if(save)FileSystem::WriteText(file,input.dump(2));
    }
    if(section==3){cfg.layers.resize(32);for(int i=0;i<32;++i){ImGui::PushID(i);if(ImGui::InputText(std::format("Layer {}",i).c_str(),&cfg.layers[i]))changed=true;ImGui::PopID();}}
    if(section==4){
    ImGui::SeparatorText("Tags");
    int remove = -1;
    for (size_t i = 0; i < cfg.tags.size(); ++i) {
        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(-40);
        bool fixed = i == 0; // "Untagged"
        ImGui::BeginDisabled(fixed);
        changed |= ImGui::InputText("##tag", &cfg.tags[i]);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = (int)i;
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (remove > 0) { cfg.tags.erase(cfg.tags.begin() + remove); changed = true; }
    if (ImGui::Button("Add Tag")) { cfg.tags.push_back("NewTag"); changed = true; }
    }
    if (changed) Project::Save();
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------ undo / redo
// The scene is snapshotted as JSON whenever it settles after a change (no mouse button held, no active widget).
// This catches every kind of edit (inspector, gizmo, hierarchy, drag & drop) without instrumenting each widget.

void EditorApp::UpdateUndo() {
    if (m_State != PlayState::Edit || !m_EditorScene) return;
    // Drawing a freshly restored or selected object can tidy its data (material slots...). That is not an edit.
    if (m_UndoRebase > 0) {
        --m_UndoRebase;
        m_UndoSnapshot = SceneSerializer::ToJson(*m_EditorScene);
        return;
    }
    const bool busy = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || m_Stroking;
    const bool gestureEnded = m_UndoWasBusy && !busy;
    const bool selectionChanged = m_UndoSelected != m_Selected;
    m_UndoWasBusy = busy;
    m_UndoTimer += DeltaTime();
    if (busy || (!gestureEnded && !selectionChanged && m_UndoTimer < 0.25f)) return;
    m_UndoTimer = 0.0f;
    m_UndoSelected = m_Selected;
    std::string now = SceneSerializer::ToJson(*m_EditorScene);
    if (m_UndoSnapshot.empty()) { m_UndoSnapshot = std::move(now); m_UndoRebase = selectionChanged ? 2 : 0; return; }
    if (now != m_UndoSnapshot) {
        m_Undo.push_back(std::move(m_UndoSnapshot));
        if (m_Undo.size() > 100) m_Undo.erase(m_Undo.begin());
        m_Redo.clear();
        m_UndoSnapshot = std::move(now);
        m_SceneDirty = true;
        if (m_AnimationPreviewScene && !m_AnimationRecording) RefreshAnimationPreview();
    }
    if (selectionChanged) m_UndoRebase = 2;
}

// Previews run on a copy of the scene: rebuild the copy so they show the current values
void EditorApp::RefreshAnimationPreview() {
    if (!m_AnimationPreviewScene || !m_EditorScene) return;
    const u64 uuid = m_AnimationPreviewScene->Reg().Valid(m_AnimationPreviewRoot) ? m_AnimationPreviewScene->Reg().Get<IDComponent>(m_AnimationPreviewRoot).uuid : 0;
    const std::string asset = m_AnimationPreviewAsset, clip = m_AnimationPreviewJson, state = m_AnimationPreviewState;
    const float time = m_AnimationPreviewTime, speed = m_AnimationPreviewSpeed;
    const bool playing = m_AnimationPreviewPlaying, recording = m_AnimationRecording;
    EntityID root = uuid ? m_EditorScene->FindByUUID(uuid) : kNullEntity;
    StopAnimationPreview();
    if (!root) return;
    StartAnimationPreview(root, asset);
    m_AnimationPreviewJson = clip;
    m_AnimationPreviewState = state;
    m_AnimationPreviewTime = time;
    m_AnimationPreviewSpeed = speed;
    m_AnimationPreviewPlaying = playing;
    m_AnimationRecording = recording;
}

void EditorApp::RestoreSnapshot(const std::string& json) {
    auto& oldRegistry = m_EditorScene->Reg();
    u64 selectedUuid = oldRegistry.Valid(m_Selected) ? oldRegistry.Get<IDComponent>(m_Selected).uuid : 0;
    std::vector<u64> selection;
    for (EntityID e : m_EntitySelection) if (oldRegistry.Valid(e)) selection.push_back(oldRegistry.Get<IDComponent>(e).uuid);
    std::string name = m_EditorScene->Name();
    auto scene = SceneSerializer::FromJson(json);
    if (!scene) return;
    scene->SetName(name);
    m_EditorScene = scene;
    m_ActiveScene = scene;
    m_Selected = selectedUuid ? scene->FindByUUID(selectedUuid) : kNullEntity;
    m_EntitySelection.clear();
    for (u64 uuid : selection) if (EntityID e = scene->FindByUUID(uuid)) m_EntitySelection.push_back(e);
    m_UndoSelected = m_Selected;
    ScriptEngine::SetEditorScene(scene.get());
    // The reloaded scene can serialize slightly differently; compare against what it writes from now on
    m_UndoSnapshot = SceneSerializer::ToJson(*scene);
    m_UndoRebase = 2;
    m_UndoWasBusy = false;
    m_SceneDirty = true;
    RefreshAnimationPreview();
}

void EditorApp::Undo() {
    if ((m_AnimationWindowFocused || m_AnimatorWindowFocused || m_AnimationRecording) && StepAnimationHistory(false)) return;
    if (m_State != PlayState::Edit) return;
    std::string now = SceneSerializer::ToJson(*m_EditorScene);
    if (!m_UndoSnapshot.empty() && now != m_UndoSnapshot) { // commit an edit newer than the last check
        m_Undo.push_back(m_UndoSnapshot);
        m_Redo.clear();
        m_UndoSnapshot = std::move(now);
    }
    if (m_Undo.empty()) return;
    std::string current = m_UndoSnapshot;
    std::string previous = std::move(m_Undo.back());
    m_Undo.pop_back();
    m_Redo.push_back(std::move(current));
    RestoreSnapshot(previous);
}

void EditorApp::Redo() {
    if ((m_AnimationWindowFocused || m_AnimatorWindowFocused || m_AnimationRecording) && StepAnimationHistory(true)) return;
    if (m_State != PlayState::Edit || m_Redo.empty()) return;
    m_Undo.push_back(m_UndoSnapshot);
    std::string next = std::move(m_Redo.back());
    m_Redo.pop_back();
    RestoreSnapshot(next);
}

} // namespace Stardust

