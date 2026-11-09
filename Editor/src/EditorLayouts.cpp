// Workspaces: named editor layouts. Each one remembers its own panels, so switching never loses an arrangement.
#include <format>
#include "EditorApp.h"
#include "Core/FileSystem.h"
#include "TitleBar.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <shlobj.h>
#endif

namespace Stardust {

namespace fs = std::filesystem;

static fs::path LayoutDirectory() {
    fs::path base = fs::temp_directory_path();
#ifdef _WIN32
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) base = appData;
    CoTaskMemFree(appData);
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config) base = config;
    else if (const char* home = std::getenv("HOME"); home && *home) base = fs::path(home) / ".config";
#endif
    return base / "Stardust" / "Layouts";
}
static fs::path LayoutFile(const std::string& name) {
    std::string safe = name;
    for (char& c : safe) if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
    return LayoutDirectory() / fs::u8path(safe + ".layout");
}

std::vector<bool*> EditorApp::LayoutWindowFlags() {
    return { &m_ShowHierarchy, &m_ShowInspector, &m_ShowProject, &m_ShowConsole, &m_ShowStats, &m_ShowLighting, &m_ShowAnimationEditor, &m_ShowAnimatorEditor };
}

static u32 PresetWindows(int preset) {
    const u32 common = 0b1111; // Hierarchy, Inspector, Project, Console
    return preset == 1 ? common | (1u << 6) | (1u << 7) : preset == 2 ? common | (1u << 5) : preset == 3 ? common | (1u << 4) : common;
}

void EditorApp::InitWorkspaces() {
    m_Workspaces = { { "World Building", 0, true }, { "Animation", 1, true }, { "Lighting", 2, true }, { "Profiling", 3, true } };
    std::error_code ec;
    fs::create_directories(LayoutDirectory(), ec);
    for (const auto& entry : fs::directory_iterator(LayoutDirectory(), ec)) {
        if (entry.path().extension() != ".layout") continue;
        std::string name = FileSystem::Utf8(entry.path().stem());
        auto existing = std::find_if(m_Workspaces.begin(), m_Workspaces.end(), [&](const WorkspaceLayout& w) { return w.name == name; });
        if (existing == m_Workspaces.end()) { m_Workspaces.push_back({ name, 0, false }); existing = m_Workspaces.end() - 1; }
        std::string text = FileSystem::ReadText(entry.path());
        if (text.starts_with("; windows=")) {
            existing->windows = (u32)std::strtoul(text.c_str() + 10, nullptr, 10);
            existing->hasWindows = true;
            text.erase(0, text.find('\n') + 1);
        }
        existing->ini = std::move(text);
    }
    auto state = nlohmann::json::parse(FileSystem::ReadText(LayoutDirectory() / "state.json"), nullptr, false);
    if (state.is_object()) {
        std::string current = state.value("current", std::string());
        for (int i = 0; i < (int)m_Workspaces.size(); ++i) if (m_Workspaces[i].name == current) m_Workspace = i;
        if (state.contains("windows")) { u32 bits = state["windows"].get<u32>(); auto flags = LayoutWindowFlags(); for (size_t i = 0; i < flags.size(); ++i) *flags[i] = bits & (1u << i); }
    }
}

void EditorApp::CaptureWorkspace() {
    if (m_Workspaces.empty()) return;
    WorkspaceLayout& layout = m_Workspaces[m_Workspace];
    layout.ini = ImGui::SaveIniSettingsToMemory();
    layout.windows = 0;
    auto flags = LayoutWindowFlags();
    for (size_t i = 0; i < flags.size(); ++i) if (*flags[i]) layout.windows |= 1u << i;
    layout.hasWindows = true;
    FileSystem::WriteText(LayoutFile(layout.name), std::format("; windows={}\n{}", layout.windows, layout.ini));
    FileSystem::WriteText(LayoutDirectory() / "state.json", nlohmann::json({ { "current", layout.name }, { "windows", layout.windows } }).dump());
}

void EditorApp::SwitchWorkspace(int workspace) {
    if (workspace == m_Workspace || workspace < 0 || workspace >= (int)m_Workspaces.size()) return;
    CaptureWorkspace();
    m_PendingWorkspace = workspace;
}

void EditorApp::ResetWorkspace(int workspace) {
    WorkspaceLayout& layout = m_Workspaces[workspace];
    layout.ini.clear();
    layout.hasWindows = false;
    std::error_code ec;
    fs::remove(LayoutFile(layout.name), ec);
    if (workspace == m_Workspace) m_PendingWorkspace = workspace;
}

void EditorApp::SaveWorkspaceAs(const std::string& name) {
    if (name.empty()) return;
    auto existing = std::find_if(m_Workspaces.begin(), m_Workspaces.end(), [&](const WorkspaceLayout& w) { return w.name == name; });
    if (existing == m_Workspaces.end()) { m_Workspaces.push_back({ name, m_Workspaces[m_Workspace].preset, false }); existing = m_Workspaces.end() - 1; }
    m_Workspace = (int)(existing - m_Workspaces.begin());
    CaptureWorkspace();
}

void EditorApp::DeleteWorkspace(int workspace) {
    if (m_Workspaces[workspace].builtin) return;
    std::error_code ec;
    fs::remove(LayoutFile(m_Workspaces[workspace].name), ec);
    bool current = workspace == m_Workspace;
    m_Workspaces.erase(m_Workspaces.begin() + workspace);
    if (current) { m_Workspace = 0; m_PendingWorkspace = 0; }
    else if (workspace < m_Workspace) --m_Workspace;
}

// ImGui can only swap docking state between frames
void EditorApp::OnBeforeNewFrame() {
    if (m_PendingWorkspace < 0) return;
    m_Workspace = std::exchange(m_PendingWorkspace, -1);
    const WorkspaceLayout& layout = m_Workspaces[m_Workspace];
    u32 windows = layout.hasWindows ? layout.windows : PresetWindows(layout.preset);
    auto flags = LayoutWindowFlags();
    for (size_t i = 0; i < flags.size(); ++i) *flags[i] = windows & (1u << i);
    if (layout.ini.empty()) {
        m_ResetLayout = true;
        if (layout.preset == 1) m_AnimationFocusRequest = true;
        if (layout.preset == 2) m_LightingFocusRequest = true;
        if (layout.preset == 3) m_StatsFocusRequest = true;
    } else {
        ImGui::ClearIniSettings();
        ImGui::LoadIniSettingsFromMemory(layout.ini.c_str(), layout.ini.size());
    }
    m_LayoutFade = 0;
}

void EditorApp::DrawWorkspaceTabs() {
    for (int i = 0; i < (int)m_Workspaces.size(); ++i) {
        if (i) ImGui::SameLine(0, 2);
        const bool active = m_Workspace == i;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(54 / 255.0f, 57 / 255.0f, 63 / 255.0f, 1) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushID(i);
        if (ImGui::Button(m_Workspaces[i].name.c_str())) SwitchWorkspace(i);
        ImGui::PopStyleColor(2);
        if (ImGui::BeginPopupContextItem("##workspaceMenu")) {
            if (ImGui::MenuItem(m_Workspaces[i].builtin ? "Reset to Default" : "Reset to World Building")) ResetWorkspace(i);
            if (!m_Workspaces[i].builtin && ImGui::MenuItem("Delete Layout")) { DeleteWorkspace(i); ImGui::EndPopup(); ImGui::PopID(); return; }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::SameLine(0, 6);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button("+")) m_OpenSaveLayout = true;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the current panels as a new layout");
    if (std::exchange(m_OpenSaveLayout, false)) { m_NewLayoutName = "My Layout"; ImGui::OpenPopup("Save Layout"); }
    if (ImGui::BeginPopupModal("Save Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool confirm = ImGui::InputText("##layoutName", &m_NewLayoutName, ImGuiInputTextFlags_EnterReturnsTrue);
        confirm |= ImGui::Button("Save", { 100, 0 });
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100, 0 })) ImGui::CloseCurrentPopup();
        if (confirm && !m_NewLayoutName.empty()) { SaveWorkspaceAs(m_NewLayoutName); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void EditorApp::DrawLayoutMenu() {
    if (!ImGui::BeginMenu("Layouts")) return;
    for (int i = 0; i < (int)m_Workspaces.size(); ++i)
        if (ImGui::MenuItem(m_Workspaces[i].name.c_str(), nullptr, i == m_Workspace)) SwitchWorkspace(i);
    ImGui::Separator();
    if (ImGui::MenuItem("Save Layout...")) m_OpenSaveLayout = true;
    if (ImGui::MenuItem("Reset Current Layout")) ResetWorkspace(m_Workspace);
    ImGui::EndMenu();
}

// Minimize / maximize / close, and the drag area between the menus and `rightEdge`
void EditorApp::DrawWindowButtons(float menusEnd, float dragEnd) {
#ifndef _WIN32
    (void)menusEnd; (void)dragEnd; // the window manager draws these on Linux
    return;
#endif
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float height = window->MenuBarHeight, width = 46.0f, windowWidth = ImGui::GetWindowWidth();
    GLFWwindow* native = GetWindow().Native();
    const bool maximized = glfwGetWindowAttrib(native, GLFW_MAXIMIZED);
    TitleBar::SetDragArea(height, menusEnd, std::min(dragEnd, windowWidth - width * 3));
    for (int i = 0; i < 3; ++i) {
        ImVec2 a(windowWidth - width * (3 - i), 0);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        bool pressed = ImGui::InvisibleButton("##windowButton", { width, height });
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        auto* draw = ImGui::GetWindowDrawList();
        if (hovered) draw->AddRectFilled(a, { a.x + width, a.y + height }, i == 2 ? IM_COL32(205, 50, 50, 255) : IM_COL32(52, 55, 61, 255));
        ImVec2 c(a.x + width * 0.5f, height * 0.5f);
        const float r = 5.0f;
        const ImU32 color = IM_COL32(205, 208, 214, 255);
        if (i == 0) draw->AddLine({ c.x - r, c.y }, { c.x + r, c.y }, color);
        if (i == 1 && !maximized) draw->AddRect({ c.x - r, c.y - r }, { c.x + r, c.y + r }, color);
        if (i == 1 && maximized) {
            draw->AddRect({ c.x - r + 2, c.y - r - 1 }, { c.x + r + 1, c.y + r - 2 }, color);
            draw->AddRectFilled({ c.x - r - 1, c.y - r + 2 }, { c.x + r - 2, c.y + r + 1 }, hovered ? IM_COL32(52, 55, 61, 255) : IM_COL32(19, 20, 23, 255));
            draw->AddRect({ c.x - r - 1, c.y - r + 2 }, { c.x + r - 2, c.y + r + 1 }, color);
        }
        if (i == 2) { draw->AddLine({ c.x - r, c.y - r }, { c.x + r, c.y + r }, color); draw->AddLine({ c.x + r, c.y - r }, { c.x - r, c.y + r }, color); }
        if (pressed) {
            if (i == 0) glfwIconifyWindow(native);
            if (i == 1) { if (maximized) glfwRestoreWindow(native); else glfwMaximizeWindow(native); }
            if (i == 2) glfwSetWindowShouldClose(native, GLFW_TRUE);
        }
    }
}

} // namespace Stardust
