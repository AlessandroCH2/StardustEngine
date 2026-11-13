// Editor chrome: dock tabs with icons and centered labels, the engine icon, the status bar
#include "EditorApp.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Scripting/ScriptEngine.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image.h>
#include <cmath>
#include <cstring>

namespace Stardust {

namespace {
enum class TabIcon { None, Scene, Game, Hierarchy, Project, Console, Inspector, Animation, Animator, Lighting };

TabIcon IconFor(const char* name, const char* end) {
    const std::string_view label(name, end - name);
    if (label == "Scene") return TabIcon::Scene;
    if (label == "Game") return TabIcon::Game;
    if (label == "Hierarchy") return TabIcon::Hierarchy;
    if (label == "Project") return TabIcon::Project;
    if (label == "Console") return TabIcon::Console;
    if (label == "Inspector") return TabIcon::Inspector;
    if (label == "Animation") return TabIcon::Animation;
    if (label == "Animator") return TabIcon::Animator;
    if (label == "Lighting") return TabIcon::Lighting;
    return TabIcon::None;
}

// Small line icons drawn in a box of side `s` centered on `c`
void DrawTabIcon(ImDrawList* d, ImVec2 c, float s, TabIcon icon, ImU32 col) {
    const float r = s * 0.5f, t = std::max(1.0f, s / 12.0f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    auto line = [&](float x0, float y0, float x1, float y1) { d->AddLine(P(x0, y0), P(x1, y1), col, t); };
    switch (icon) {
    case TabIcon::Scene: // grid
        d->AddRect(P(-.85f, -.85f), P(.85f, .85f), col, 1.5f, 0, t);
        line(-.28f, -.85f, -.28f, .85f); line(.28f, -.85f, .28f, .85f);
        line(-.85f, -.28f, .85f, -.28f); line(-.85f, .28f, .85f, .28f);
        break;
    case TabIcon::Game: // gamepad
        d->AddRect(P(-.95f, -.5f), P(.95f, .6f), col, r * .45f, 0, t);
        line(-.6f, .05f, -.2f, .05f); line(-.4f, -.15f, -.4f, .25f);
        d->AddCircleFilled(P(.35f, -.02f), t * 1.2f, col); d->AddCircleFilled(P(.6f, .2f), t * 1.2f, col);
        break;
    case TabIcon::Hierarchy: // indented list
        d->AddRectFilled(P(-.95f, -.85f), P(-.55f, -.45f), col);
        line(-.75f, -.45f, -.75f, .6f); line(-.75f, 0, -.3f, 0); line(-.75f, .6f, -.3f, .6f);
        d->AddRectFilled(P(-.3f, -.18f), P(.1f, .18f), col); d->AddRectFilled(P(-.3f, .42f), P(.1f, .78f), col);
        line(-.35f, -.65f, .95f, -.65f); line(.3f, 0, .95f, 0); line(.3f, .6f, .95f, .6f);
        break;
    case TabIcon::Project: { // folder
        ImVec2 pts[] = { P(-.95f, -.7f), P(-.3f, -.7f), P(-.1f, -.45f), P(.95f, -.45f), P(.95f, .75f), P(-.95f, .75f) };
        d->AddPolyline(pts, 6, col, ImDrawFlags_Closed, t);
        line(-.95f, -.2f, .95f, -.2f);
        break;
    }
    case TabIcon::Console: // terminal prompt
        d->AddRect(P(-.95f, -.8f), P(.95f, .8f), col, 1.5f, 0, t);
        line(-.6f, -.35f, -.25f, 0); line(-.25f, 0, -.6f, .35f); line(0, .4f, .55f, .4f);
        break;
    case TabIcon::Inspector: // info
        d->AddCircle(c, r * .92f, col, 16, t);
        d->AddCircleFilled(P(0, -.45f), t * 1.1f, col); line(0, -.12f, 0, .5f);
        break;
    case TabIcon::Animation: { // keyframe on a timeline
        line(-.95f, .55f, .95f, .55f);
        ImVec2 k[] = { P(0, -.85f), P(.45f, -.3f), P(0, .25f), P(-.45f, -.3f) };
        d->AddConvexPolyFilled(k, 4, col);
        line(-.7f, .35f, -.7f, .75f); line(.7f, .35f, .7f, .75f);
        break;
    }
    case TabIcon::Animator: // two states and a transition
        d->AddRect(P(-.95f, -.85f), P(-.15f, -.25f), col, 1.5f, 0, t);
        d->AddRect(P(.15f, .25f), P(.95f, .85f), col, 1.5f, 0, t);
        line(-.55f, -.25f, -.55f, .55f); line(-.55f, .55f, .15f, .55f);
        break;
    case TabIcon::Lighting: // sun
        d->AddCircle(c, r * .38f, col, 14, t);
        for (int i = 0; i < 8; ++i) { float a = i * 0.785398f; line(std::cos(a) * .62f, std::sin(a) * .62f, std::cos(a) * .95f, std::sin(a) * .95f); }
        break;
    default: break;
    }
}
} // namespace

// ImGui draws dock tabs with the label on the left and room for a close button. After every window is
// submitted this lays the tabs out again (wider, label centered, an icon for the main panels) and paints
// over ImGui's label. The new widths apply from the next frame.
void EditorApp::DecorateDockTabs() {
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const float scale = UiScale(), pad = 13 * scale, iconSize = std::round(g.FontSize * 0.78f), gap = 6 * scale;

    // Tabs close from the node's X button or the Window menu; no per-tab X
    for (ImGuiWindow* window : g.Windows) if (window->DockNode) window->WindowClass.TabItemFlagsOverrideSet |= ImGuiTabItemFlags_NoCloseButton;

    for (int n = 0; n < g.DockContext.Nodes.Data.Size; ++n) {
        auto* node = (ImGuiDockNode*)g.DockContext.Nodes.Data[n].val_p;
        if (!node || !node->IsLeafNode() || !node->TabBar || !node->HostWindow || !node->IsVisible || node->IsHiddenTabBar() || node->IsNoTabBar()) continue;
        ImGuiTabBar* bar = node->TabBar;
        if (bar->CurrFrameVisible != g.FrameCount || !node->HostWindow->Active || node->HostWindow->Hidden) continue;
        ImDrawList* draw = node->HostWindow->DrawList;
        draw->PushTextureID(g.Font->ContainerAtlas->TexID);
        draw->PushClipRect({ bar->ScrollingRectMinX, bar->BarRect.Min.y }, { bar->ScrollingRectMaxX, bar->BarRect.Max.y }, false);
        const bool focused = (bar->Flags & ImGuiTabBarFlags_IsFocused) != 0;
        for (ImGuiTabItem& tab : bar->Tabs) {
            if (!tab.Window || tab.LastFrameVisible != g.FrameCount) continue;
            const char* name = tab.Window->Name;
            const char* end = ImGui::FindRenderedTextEnd(name);
            const TabIcon icon = IconFor(name, end);
            const float textWidth = ImGui::CalcTextSize(name, end).x;
            const float content = textWidth + (icon != TabIcon::None ? iconSize + gap : 0);
            tab.RequestedWidth = std::round(content + pad * 2);

            const ImVec2 min(bar->BarRect.Min.x + IM_TRUNC(tab.Offset - bar->ScrollingAnim), bar->BarRect.Min.y);
            const ImVec2 max(min.x + tab.Width, bar->BarRect.Max.y);
            if (max.x - min.x < 4) continue;
            const bool selected = bar->VisibleTabId == tab.ID;
            const bool hovered = g.HoveredId == tab.ID || g.ActiveId == tab.ID;
            const ImU32 background = ImGui::GetColorU32(hovered ? ImGuiCol_TabHovered
                : selected ? (focused ? ImGuiCol_TabSelected : ImGuiCol_TabDimmedSelected) : (focused ? ImGuiCol_Tab : ImGuiCol_TabDimmed));
            const float textY = min.y + style.FramePadding.y;
            draw->AddRectFilled({ min.x + 1, textY - 1 }, { max.x - 1, std::min(max.y - 1, textY + g.FontSize + 1) }, background);

            const ImU32 color = selected || hovered ? IM_COL32(226, 229, 234, 255) : IM_COL32(150, 154, 162, 255);
            float x = min.x + std::max(std::min(pad, (max.x - min.x) * 0.25f), std::round((max.x - min.x - content) * 0.5f));
            if (icon != TabIcon::None) {
                DrawTabIcon(draw, { x + iconSize * 0.5f, textY + g.FontSize * 0.5f }, iconSize, icon, selected ? IM_COL32(120, 175, 245, 255) : color);
                x += iconSize + gap;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::RenderTextEllipsis(draw, { x, textY }, { max.x - 4, textY + g.FontSize }, max.x - 4, max.x - 4, name, end, nullptr);
            ImGui::PopStyleColor();
        }
        draw->PopClipRect();
        draw->PopTextureID();
    }
}

// Soft shadows under floating windows, popups and menus, so they stand out from the panels behind them.
// ImGui has no window shadows: the shadow is built in a separate draw list and put in front of the window's own
// commands, so it is drawn just before (under) the window and on top of whatever lies behind it.
void EditorApp::DrawWindowShadows() {
    ImGuiContext& g = *GImGui;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    for (ImGuiWindow* window : g.Windows) {
        if (!window->Active || window->Hidden || window->DockIsActive || (window->Flags & ImGuiWindowFlags_ChildWindow) || (window->Flags & ImGuiWindowFlags_NoBackground)) continue;
        if (window->Size.x >= viewport->WorkSize.x - 1 && window->Size.y >= viewport->WorkSize.y - 1) continue; // the editor's own full-screen window
        if (window->Viewport != viewport) continue;
        ImDrawList* list = window->DrawList;
        if (list->CmdBuffer.Size == 0 || list->VtxBuffer.Size == 0) continue;
        const bool small = (window->Flags & (ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_Popup)) != 0;

        ImDrawList shadow(ImGui::GetDrawListSharedData());
        shadow._ResetForNewFrame();
        shadow.PushClipRect(viewport->Pos, { viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y }, false);
        shadow.PushTextureID(g.Font->ContainerAtlas->TexID);
        const ImVec2 a = window->Pos, b = { window->Pos.x + window->Size.x, window->Pos.y + window->Size.y };
        const float offset = small ? 3.0f : 6.0f, rounding = window->WindowRounding;
        const int layers = small ? 6 : 10;
        for (int i = layers; i >= 1; --i) {
            // Wider and fainter layers on the outside, slightly below the window like light from above
            const float grow = i * (small ? 1.6f : 2.4f), t = 1.0f - (float)i / (layers + 1);
            const int alpha = (int)((small ? 20.0f : 26.0f) * t * t + 3);
            shadow.AddRectFilled({ a.x - grow, a.y - grow + offset }, { b.x + grow, b.y + grow + offset }, IM_COL32(0, 0, 0, alpha), rounding + grow);
        }
        shadow.PopTextureID();
        shadow.PopClipRect();
        shadow._PopUnusedDrawCmd();
        if (shadow.CmdBuffer.Size == 0 || shadow.VtxBuffer.Size == 0) continue;

        // Shadow first: the window's commands move after it
        const int vtx = shadow.VtxBuffer.Size, idx = shadow.IdxBuffer.Size;
        for (ImDrawCmd& cmd : list->CmdBuffer) { cmd.VtxOffset += vtx; cmd.IdxOffset += idx; }
        auto prepend = [](auto& target, const auto& front) {
            const int old = target.Size;
            target.resize(old + front.Size);
            std::memmove(target.Data + front.Size, target.Data, old * sizeof(*target.Data));
            std::memcpy(target.Data, front.Data, front.Size * sizeof(*target.Data));
        };
        prepend(list->VtxBuffer, shadow.VtxBuffer);
        prepend(list->IdxBuffer, shadow.IdxBuffer);
        prepend(list->CmdBuffer, shadow.CmdBuffer);
        list->_VtxWritePtr = list->VtxBuffer.Data + list->VtxBuffer.Size;
        list->_IdxWritePtr = list->IdxBuffer.Data + list->IdxBuffer.Size;
    }
}

void* EditorApp::EngineIconTexture() {
    if (!m_EngineIconLoaded) {
        m_EngineIconLoaded = true;
        auto bytes = FileSystem::ReadBinary(FileSystem::ExecutableDir() / "engineIcon.png");
        if (bytes.empty()) bytes = FileSystem::ReadBinary(FileSystem::EngineRoot() / "engineIcon.png");
        int w = 0, h = 0, channels = 0;
        if (stbi_uc* pixels = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &channels, 4)) {
            RHI::TextureDesc desc; desc.width = (u32)w; desc.height = (u32)h; desc.format = RHI::Format::RGBA8_UNORM;
            desc.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::CopyDst; desc.name = "Engine Icon";
            m_EngineIcon = GetDevice().CreateTexture(desc);
            GetDevice().UploadTexture(m_EngineIcon.get(), pixels, (u64)w * h * 4);
            stbi_image_free(pixels);
        }
    }
    return m_EngineIcon ? GetDevice().ImGuiTexture(m_EngineIcon.get()) : nullptr;
}

namespace {
ImU32 LevelColor(LogLevel level) {
    return level == LogLevel::Error ? IM_COL32(255, 107, 102, 255) : level == LogLevel::Warn ? IM_COL32(255, 209, 89, 255)
         : level == LogLevel::Trace ? IM_COL32(150, 150, 150, 255) : IM_COL32(205, 208, 214, 255);
}
} // namespace

// Small badge for a log level: (i) info, triangle warning, (x) error
void DrawLogLevelIcon(ImDrawList* d, ImVec2 c, float r, LogLevel level) {
    const ImU32 col = level == LogLevel::Error ? IM_COL32(230, 75, 70, 255) : level == LogLevel::Warn ? IM_COL32(240, 190, 60, 255)
                    : level == LogLevel::Trace ? IM_COL32(110, 114, 122, 255) : IM_COL32(150, 160, 175, 255);
    const ImU32 ink = IM_COL32(20, 21, 24, 255);
    if (level == LogLevel::Warn) {
        d->AddTriangleFilled({ c.x, c.y - r }, { c.x + r * 1.05f, c.y + r * .85f }, { c.x - r * 1.05f, c.y + r * .85f }, col);
        d->AddLine({ c.x, c.y - r * .35f }, { c.x, c.y + r * .3f }, ink, 1.6f);
        d->AddCircleFilled({ c.x, c.y + r * .58f }, 1.1f, ink);
    } else if (level == LogLevel::Error) {
        d->AddCircleFilled(c, r, col, 16);
        const float k = r * .42f;
        d->AddLine({ c.x - k, c.y - k }, { c.x + k, c.y + k }, ink, 1.6f); d->AddLine({ c.x + k, c.y - k }, { c.x - k, c.y + k }, ink, 1.6f);
    } else {
        d->AddCircleFilled(c, r, col, 16);
        d->AddCircleFilled({ c.x, c.y - r * .45f }, 1.1f, ink);
        d->AddLine({ c.x, c.y - r * .1f }, { c.x, c.y + r * .55f }, ink, 1.6f);
    }
}

// Status bar: the last console message, scripts state on the right. Clicking opens the Console.
void EditorApp::DrawStatusBar(float height) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + ImGui::GetContentRegionAvail().x, min.y + height);
    draw->AddRectFilled(min, max, IM_COL32(19, 20, 23, 255));
    draw->AddLine(min, { max.x, min.y }, IM_COL32(10, 11, 13, 255));
    const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;

    // Newest entry that is not a trace message
    LogEntry last{ LogLevel::Info, "Ready", 0 };
    { auto entries = Log::Snapshot(); for (auto it = entries.rbegin(); it != entries.rend(); ++it) if (it->level != LogLevel::Trace) { last = *it; break; } }
    std::string text = last.message.substr(0, last.message.find('\n'));

    float right = max.x - 10;
    if (ScriptEngine::IsBuilding()) {
        const char* building = "Compiling scripts...";
        const float w = ImGui::CalcTextSize(building).x;
        draw->AddText({ right - w, textY }, IM_COL32(150, 190, 245, 255), building);
        const float t = (float)ImGui::GetTime() * 6, cx = right - w - 12, cy = textY + ImGui::GetFontSize() * 0.5f;
        draw->PathArcTo({ cx, cy }, 5, t, t + 4.4f, 12); draw->PathStroke(IM_COL32(150, 190, 245, 255), 0, 1.6f);
        right -= w + 26;
    }

    ImGui::InvisibleButton("##statusBar", { right - min.x, height });
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) draw->AddRectFilled({ min.x, min.y + 1 }, { right, max.y }, IM_COL32(32, 34, 39, 255));
    float x = min.x + 10;
    if (!last.message.empty() && last.time > 0) { DrawLogLevelIcon(draw, { x + 6, textY + ImGui::GetFontSize() * 0.5f }, 6, last.level); x += 18; }
    ImVec4 clip(x, min.y, right - 8, max.y);
    draw->AddText(nullptr, 0, { x, textY }, last.time > 0 ? LevelColor(last.level) : IM_COL32(150, 154, 162, 255), text.c_str(), nullptr, 0, &clip);
    if (hovered && last.time > 0) ImGui::SetTooltip("Open the Console");
    if (ImGui::IsItemClicked()) {
        m_ShowConsole = true;
        m_ConsoleFocusRequest = true;
        m_ConsoleSelectTime = last.time;
    }
}

} // namespace Stardust
