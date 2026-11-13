// Package Manager window: a list of packages on the left, details on the right,
// install/update/remove, samples, and packages added from disk or from a git URL
#include "EditorApp.h"
#include "FileDialogs.h"
#include "Core/Application.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Assets/AssetManager.h"
#include "Project/PackageManager.h"
#include "Project/Project.h"
#include "Scripting/ScriptEngine.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <future>
#include <map>

namespace Stardust {
namespace fs = std::filesystem;

namespace {
struct ListedPackage {
    PackageInfo info;                 // the installed copy when installed, else the engine's
    std::optional<PackageInfo> official;
    bool installed = false;
    std::string source;               // Engine, Stardust, Git, Local
    bool updatable() const { return installed && official && official->version != info.version; }
};

void Pill(ImDrawList* d, ImVec2 at, const char* text, ImU32 fill, ImU32 ink) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float padX = 6, padY = 1;
    d->AddRectFilled(at, { at.x + size.x + padX * 2, at.y + size.y + padY * 2 }, fill, size.y);
    d->AddText({ at.x + padX, at.y + padY }, ink, text);
}
ImU32 SourceColor(const std::string& source) {
    return source == "Engine" ? IM_COL32(110, 80, 170, 255) : source == "Stardust" ? IM_COL32(40, 110, 200, 255) : source == "Git" ? IM_COL32(200, 100, 40, 255) : IM_COL32(80, 86, 96, 255);
}
} // namespace

void EditorApp::DrawPackageManager() {
    if (!m_ShowPackageManager) return;
    const float ui = UiScale();
    static int filter = 0; // In Project, Stardust Packages, All
    static std::string search, selectedName, gitUrl, gitError, status;
    static std::vector<ListedPackage> packages;
    static double listedAt = -100;
    static std::future<std::pair<bool, std::string>> gitJob;
    static std::string gitJobUrl;
    auto rebuild = [&] { Project::WriteScriptProject(Project::ScriptProjectFile(), Project::Config().assemblyName); Project::WriteEditorScriptProject(); ScriptEngine::BuildAsync(); listedAt = -100; };

    // A git install running in the background
    const bool busy = gitJob.valid();
    if (busy && gitJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto [ok, result] = gitJob.get();
        if (ok) { selectedName = result; status = "Installed from git: " + result; rebuild(); }
        else { gitError = result; status = "Git install failed: " + result.substr(0, result.find('\n')); SD_ERROR("Package from git: {}", result); }
    }

    // The list, refreshed every couple of seconds
    if (ImGui::GetTime() - listedAt > 2.0) {
        listedAt = ImGui::GetTime();
        std::map<std::string, ListedPackage> byName;
        for (auto& p : PackageManager::Installed()) {
            ListedPackage l; l.info = p; l.installed = true;
            l.source = p.engine ? "Engine" : !p.gitUrl.empty() ? "Git" : "Local";
            byName[p.name] = l;
        }
        for (auto& p : PackageManager::Official()) {
            auto& l = byName[p.name];
            if (!l.installed) { l.info = p; l.source = "Stardust"; }
            else if (l.source == "Local") l.source = "Stardust";
            l.official = p;
        }
        packages.clear();
        for (auto& [name, l] : byName) packages.push_back(l);
        std::sort(packages.begin(), packages.end(), [](const ListedPackage& a, const ListedPackage& b) {
            auto rank = [](const std::string& s) { return s == "Engine" ? 0 : s == "Stardust" ? 1 : s == "Git" ? 2 : 3; };
            return rank(a.source) != rank(b.source) ? rank(a.source) < rank(b.source) : a.info.displayName < b.info.displayName;
        });
    }

    ImGui::SetNextWindowSize({ 940 * ui, 580 * ui }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Package Manager", &m_ShowPackageManager)) { ImGui::End(); return; }

    // ---------------------------------------------------------------- top bar: add, filter, search
    {
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos(), size(30 * ui, ImGui::GetFrameHeight());
        if (ImGui::InvisibleButton("##add", size)) ImGui::OpenPopup("AddPackage");
        const bool hovered = ImGui::IsItemHovered();
        d->AddRectFilled(p, { p.x + size.x, p.y + size.y }, hovered ? IM_COL32(68, 72, 79, 255) : IM_COL32(48, 51, 57, 255), 3);
        const ImVec2 c(p.x + 11 * ui, p.y + size.y * 0.5f);
        d->AddLine({ c.x - 5 * ui, c.y }, { c.x + 5 * ui, c.y }, IM_COL32(220, 223, 228, 255), 1.6f);
        d->AddLine({ c.x, c.y - 5 * ui }, { c.x, c.y + 5 * ui }, IM_COL32(220, 223, 228, 255), 1.6f);
        d->AddTriangleFilled({ p.x + 20 * ui, c.y - 1.5f * ui }, { p.x + 26 * ui, c.y - 1.5f * ui }, { p.x + 23 * ui, c.y + 2 * ui }, IM_COL32(180, 184, 191, 255));
        if (hovered) ImGui::SetTooltip("Add a package");
        bool openGit = false;
        if (ImGui::BeginPopup("AddPackage")) {
            if (ImGui::MenuItem("Add package from disk...")) {
                if (auto folder = FileDialogs::PickFolder(GetWindow().NativeHandle(), Project::Dir())) {
                    const auto info = PackageManager::Read(*folder);
                    if (PackageManager::InstallLocal(*folder)) { selectedName = info.name; status = "Installed: " + info.displayName; rebuild(); }
                    else status = "Install failed (see the Console)";
                }
            }
            if (ImGui::MenuItem("Add package from git URL...", nullptr, false, !busy)) openGit = true;
            ImGui::EndPopup();
        }
        if (openGit) { gitError.clear(); ImGui::OpenPopup("Add package from git URL"); }

        ImGui::SameLine(0, 10 * ui);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Packages:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170 * ui);
        const char* filters[] = { "In Project", "Stardust Packages", "All" };
        ImGui::Combo("##filter", &filter, filters, 3);
        const float searchWidth = std::min(260 * ui, ImGui::GetContentRegionAvail().x * 0.4f);
        ImGui::SameLine(ImGui::GetWindowWidth() - searchWidth - ImGui::GetStyle().WindowPadding.x);
        const ImVec2 s = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24 * ui, ImGui::GetStyle().FramePadding.y));
        ImGui::InputTextWithHint("##search", "Search", &search);
        ImGui::PopStyleVar();
        const ImVec2 mc(s.x + 12 * ui, s.y + ImGui::GetFrameHeight() * 0.5f - 1);
        d->AddCircle(mc, 4.5f * ui, IM_COL32(150, 154, 162, 255), 12, 1.5f);
        d->AddLine({ mc.x + 3.2f * ui, mc.y + 3.2f * ui }, { mc.x + 6.5f * ui, mc.y + 6.5f * ui }, IM_COL32(150, 154, 162, 255), 1.8f);
    }

    // Git URL dialog
    ImGui::SetNextWindowSize({ 560 * ui, 0 });
    if (ImGui::BeginPopupModal("Add package from git URL", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextDisabled("Repository URL");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputTextWithHint("##url", "https://github.com/user/repo.git", &gitUrl, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Add #tag or #branch to pick a version, and ?path=folder when the package is not at the root, e.g. https://github.com/user/repo.git?path=Packages/com.user.tool#v1.2. Git must be installed.");
        if (!gitError.empty()) ImGui::TextColored({ 1, .5f, .45f, 1 }, "%s", gitError.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const float w = 100 * ui;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - w * 2 - 8 * ui);
        if (ImGui::Button("Cancel", { w, 0 }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, 8 * ui);
        ImGui::BeginDisabled(gitUrl.empty());
        if ((ImGui::Button("Add", { w, 0 }) || enter) && !gitUrl.empty()) {
            gitError.clear();
            gitJobUrl = gitUrl;
            status = "Cloning " + gitUrl + "...";
            gitJob = std::async(std::launch::async, [url = gitUrl]() -> std::pair<bool, std::string> {
                std::string error, name;
                return PackageManager::InstallFromGit(url, error, &name) ? std::pair{ true, name } : std::pair{ false, error };
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // ---------------------------------------------------------------- list
    const float bottomBar = ImGui::GetFrameHeight() + 10 * ui;
    const float listWidth = std::max(240 * ui, ImGui::GetContentRegionAvail().x * 0.36f);
    std::string needle = search;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(29, 31, 35, 255));
    ImGui::BeginChild("##list", { listWidth, -bottomBar }, ImGuiChildFlags_None);
    std::string group;
    const ListedPackage* current = nullptr;
    for (auto& p : packages) {
        if (filter == 0 && !p.installed) continue;
        if (filter == 1 && !p.official) continue;
        std::string hay = p.info.displayName + " " + p.info.name;
        std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
        if (selectedName.empty()) selectedName = p.info.name;
        if (p.info.name == selectedName) current = &p;
        if (p.source != group) {
            group = p.source;
            const char* title = group == "Engine" ? "Stardust Engine" : group == "Stardust" ? "Stardust Packages" : group == "Git" ? "From Git" : "Local Packages";
            ImGui::Dummy({ 1, 4 * ui });
            ImGui::TextDisabled("%s", title);
        }
        ImGui::PushID(p.info.name.c_str());
        const ImVec2 a = ImGui::GetCursorScreenPos(), size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + 6 * ui);
        if (ImGui::InvisibleButton("##row", size)) selectedName = p.info.name;
        auto* d = ImGui::GetWindowDrawList();
        const bool on = p.info.name == selectedName, hovered = ImGui::IsItemHovered();
        if (on || hovered) d->AddRectFilled(a, { a.x + size.x, a.y + size.y }, on ? IM_COL32(44, 72, 115, 255) : IM_COL32(40, 43, 48, 255), 3);
        const float ty = a.y + (size.y - ImGui::GetFontSize()) * 0.5f;
        const ImVec4 clip(a.x, a.y, a.x + size.x - 90 * ui, a.y + size.y);
        d->AddText(nullptr, 0, { a.x + 10 * ui, ty }, p.installed || on ? IM_COL32(228, 230, 235, 255) : IM_COL32(160, 165, 172, 255), p.info.displayName.c_str(), nullptr, 0, &clip);
        // Version on the right, with a mark: installed (check), update available (arrow)
        const std::string version = p.info.version;
        const float vx = a.x + size.x - ImGui::CalcTextSize(version.c_str()).x - 30 * ui;
        d->AddText({ vx, ty }, IM_COL32(150, 154, 162, 255), version.c_str());
        const ImVec2 m(a.x + size.x - 14 * ui, a.y + size.y * 0.5f);
        if (p.updatable()) { d->AddTriangleFilled({ m.x - 5 * ui, m.y + 1 * ui }, { m.x + 5 * ui, m.y + 1 * ui }, { m.x, m.y - 5 * ui }, IM_COL32(80, 160, 255, 255)); d->AddRectFilled({ m.x - 2 * ui, m.y + 1 * ui }, { m.x + 2 * ui, m.y + 5 * ui }, IM_COL32(80, 160, 255, 255)); }
        else if (p.installed) { d->AddLine({ m.x - 5 * ui, m.y }, { m.x - 1.5f * ui, m.y + 3.5f * ui }, IM_COL32(110, 200, 110, 255), 2); d->AddLine({ m.x - 1.5f * ui, m.y + 3.5f * ui }, { m.x + 5 * ui, m.y - 4 * ui }, IM_COL32(110, 200, 110, 255), 2); }
        ImGui::PopID();
    }
    if (packages.empty() || (filter == 0 && std::none_of(packages.begin(), packages.end(), [](auto& p) { return p.installed; }))) ImGui::TextDisabled("No packages.");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 0);

    // ---------------------------------------------------------------- details
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * ui, 14 * ui));
    ImGui::BeginChild("##details", { 0, -bottomBar }, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    if (current) {
        const auto& p = *current;
        auto* d = ImGui::GetWindowDrawList();
        if (ImFont* bold = Application::BoldFont()) ImGui::PushFont(bold);
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextUnformatted(p.info.displayName.c_str());
        ImGui::SetWindowFontScale(1);
        if (Application::BoldFont()) ImGui::PopFont();
        ImGui::SameLine(0, 10 * ui);
        const char* badge = p.source == "Engine" ? "Engine" : p.source == "Stardust" ? "Official" : p.source == "Git" ? "Git" : "Local";
        Pill(d, { ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 5 * ui }, badge, SourceColor(p.source), IM_COL32(255, 255, 255, 255));
        ImGui::NewLine();
        ImGui::TextDisabled("%s", p.info.name.c_str());
        ImGui::Text("Version %s", p.info.version.c_str());
        if (p.updatable()) { ImGui::SameLine(); ImGui::TextColored({ .45f, .7f, 1, 1 }, "  Update available: %s", p.official->version.c_str()); }
        if (!p.info.author.empty()) ImGui::TextDisabled("By %s", p.info.author.c_str());
        if (!p.info.gitUrl.empty()) ImGui::TextDisabled("Git: %s", p.info.gitUrl.c_str());
        if (p.installed && ImGui::SmallButton("Show in Explorer")) ShellExecuteFile(p.info.directory);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(p.info.description.empty() ? "No description." : p.info.description.c_str());
        ImGui::PopTextWrapPos();
        if (!p.info.dependencies.empty()) {
            ImGui::Dummy({ 1, 6 * ui });
            ImGui::SeparatorText("Dependencies");
            for (auto& [name, version] : p.info.dependencies) ImGui::BulletText("%s  %s", name.c_str(), version == "*" ? "(any version)" : version.c_str());
        }
        if (!p.info.samples.empty()) {
            ImGui::Dummy({ 1, 6 * ui });
            ImGui::SeparatorText("Samples");
            for (auto& sample : p.info.samples) {
                ImGui::PushID(sample.c_str());
                const bool imported = fs::exists(Project::AssetsDir() / "Samples" / fs::u8path(p.info.displayName) / fs::u8path(sample));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(sample.c_str());
                ImGui::SameLine(ImGui::GetWindowWidth() - 110 * ui);
                ImGui::BeginDisabled(!p.installed);
                if (ImGui::Button(imported ? "Reimport" : "Import", { 90 * ui, 0 })) {
                    auto folder = PackageManager::ImportSample(p.info, sample);
                    status = folder.empty() ? "Sample import failed" : "Sample imported into " + AssetManager::ToRelative(folder);
                }
                ImGui::EndDisabled();
                if (!p.installed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Install the package first");
                ImGui::PopID();
            }
        }
    } else ImGui::TextDisabled("Select a package.");
    ImGui::EndChild();

    // ---------------------------------------------------------------- bottom bar: status, actions
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    if (gitJob.valid()) {
        const float t = (float)ImGui::GetTime() * 6;
        const ImVec2 c(ImGui::GetCursorScreenPos().x + 8 * ui, ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight() * 0.5f);
        ImGui::GetWindowDrawList()->PathArcTo(c, 5 * ui, t, t + 4.4f, 12);
        ImGui::GetWindowDrawList()->PathStroke(IM_COL32(150, 190, 245, 255), 0, 1.6f);
        ImGui::Dummy({ 18 * ui, 1 });
        ImGui::SameLine();
        ImGui::TextDisabled("Cloning %s...", gitJobUrl.c_str());
    } else ImGui::TextDisabled("%s", status.c_str());
    if (current) {
        const auto& p = *current;
        const float w = 100 * ui;
        int buttons = (!p.installed || p.updatable() ? 1 : 0) + (p.installed && !p.info.engine ? 1 : 0) + (!p.info.gitUrl.empty() ? 1 : 0);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - buttons * (w + 6 * ui));
        ImGui::BeginDisabled(gitJob.valid());
        if (!p.installed && p.official && ImGui::Button("Install", { w, 0 })) {
            if (PackageManager::InstallLocal(p.official->directory)) { status = "Installed: " + p.info.displayName; rebuild(); } else status = "Install failed (see the Console)";
        }
        if (p.updatable()) {
            if (ImGui::Button(("Update to " + p.official->version).c_str(), { w, 0 }) && PackageManager::Update(p.info.name, p.official->directory)) { status = "Updated: " + p.info.displayName; rebuild(); }
            ImGui::SameLine(0, 6 * ui);
        }
        if (!p.info.gitUrl.empty()) {
            if (ImGui::Button("Pull Latest", { w, 0 })) {
                gitJobUrl = p.info.gitUrl; status.clear();
                gitJob = std::async(std::launch::async, [url = p.info.gitUrl]() -> std::pair<bool, std::string> {
                    std::string error, name;
                    return PackageManager::InstallFromGit(url, error, &name) ? std::pair{ true, name } : std::pair{ false, error };
                });
            }
            ImGui::SameLine(0, 6 * ui);
        }
        if (p.installed && !p.info.engine) {
            if (ImGui::Button("Remove", { w, 0 }) && PackageManager::Remove(p.info.name)) { status = "Removed: " + p.info.displayName; rebuild(); }
        }
        if (p.installed && p.info.engine) { ImGui::SameLine(); ImGui::TextDisabled("Required by every project"); }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

} // namespace Stardust
