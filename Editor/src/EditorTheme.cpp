#include "EditorApp.h"
#include <imgui.h>

namespace Stardust {

// Near-black chrome with slightly lighter panels, flat fields and a blue accent
void ApplyEditorTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = { 8, 7 };
    s.FramePadding = { 7, 4 };
    s.ItemSpacing = { 6, 4 };
    s.ItemInnerSpacing = { 5, 4 };
    s.IndentSpacing = 14;
    s.ScrollbarSize = 10;
    s.GrabMinSize = 9;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.TabBarOverlineSize = 2;
    s.WindowRounding = 6;
    s.ChildRounding = 0;
    s.PopupRounding = 6;
    s.FrameRounding = 4;
    s.GrabRounding = 8;
    s.TabRounding = 2;
    s.ScrollbarRounding = 8;
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.DockingSeparatorSize = 5;

    auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
    const ImVec4 chrome = rgb(14, 15, 17), bar = rgb(19, 20, 23), panel = rgb(35, 37, 41), field = rgb(25, 27, 31);
    const ImVec4 accent = rgb(47, 128, 237), accentHover = rgb(82, 150, 245);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = rgb(208, 211, 217);
    c[ImGuiCol_TextDisabled]         = rgb(122, 126, 135);
    c[ImGuiCol_WindowBg]             = panel;
    c[ImGuiCol_ChildBg]              = panel;
    c[ImGuiCol_PopupBg]              = rgb(42, 44, 49);
    c[ImGuiCol_Border]               = rgb(16, 17, 19);
    c[ImGuiCol_BorderShadow]         = rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = field;
    c[ImGuiCol_FrameBgHovered]       = rgb(32, 34, 39);
    c[ImGuiCol_FrameBgActive]        = rgb(21, 23, 26);
    c[ImGuiCol_TitleBg]              = chrome;
    c[ImGuiCol_TitleBgActive]        = chrome;
    c[ImGuiCol_TitleBgCollapsed]     = chrome;
    c[ImGuiCol_MenuBarBg]            = bar;
    c[ImGuiCol_ScrollbarBg]          = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = rgb(66, 69, 75);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(84, 88, 95);
    c[ImGuiCol_ScrollbarGrabActive]  = rgb(104, 108, 116);
    c[ImGuiCol_CheckMark]            = accentHover;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentHover;
    c[ImGuiCol_Button]               = rgb(54, 57, 63);
    c[ImGuiCol_ButtonHovered]        = rgb(68, 72, 79);
    c[ImGuiCol_ButtonActive]         = accent;
    c[ImGuiCol_Header]               = rgb(43, 62, 92);
    c[ImGuiCol_HeaderHovered]        = rgb(47, 50, 56);
    c[ImGuiCol_HeaderActive]         = rgb(40, 84, 148);
    c[ImGuiCol_Separator]            = chrome;
    c[ImGuiCol_SeparatorHovered]     = accent;
    c[ImGuiCol_SeparatorActive]      = accent;
    c[ImGuiCol_ResizeGrip]           = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]    = accent;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_Tab]                  = chrome;
    c[ImGuiCol_TabHovered]           = rgb(44, 47, 52);
    c[ImGuiCol_TabSelected]          = panel;
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = chrome;
    c[ImGuiCol_TabDimmedSelected]    = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview]       = rgb(47, 128, 237, 0.6f);
    c[ImGuiCol_DockingEmptyBg]       = chrome;
    c[ImGuiCol_TextSelectedBg]       = rgb(47, 128, 237, 0.45f);
    c[ImGuiCol_NavCursor]            = accent;
    c[ImGuiCol_TableHeaderBg]        = rgb(29, 31, 35);
    c[ImGuiCol_TableBorderStrong]    = chrome;
    c[ImGuiCol_TableBorderLight]     = rgb(29, 31, 35);
    c[ImGuiCol_TableRowBgAlt]        = rgb(255, 255, 255, 0.02f);
}

} // namespace Stardust
