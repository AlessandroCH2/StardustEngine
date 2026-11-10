// LOD Group inspector: Unity's coloured LOD bar with draggable transitions, the Scene camera marker,
// and the renderers of the selected level
#include "InspectorWidgets.h"
#include "Scene/Scene.h"
#include "Renderer/Quality.h"
#include <cmath>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <format>
#include <functional>

namespace Stardust {

namespace {
const ImU32 kLevelColors[] = { IM_COL32(68, 120, 44, 255), IM_COL32(46, 96, 128, 255), IM_COL32(40, 112, 116, 255), IM_COL32(98, 72, 130, 255),
                               IM_COL32(130, 98, 40, 255), IM_COL32(120, 60, 70, 255), IM_COL32(70, 90, 140, 255), IM_COL32(100, 110, 60, 255) };

// Heights like Unity's defaults: 60%, 30%, 10%, then halving
float DefaultHeight(int index) { static const float first[] = { 0.6f, 0.3f, 0.1f }; return index < 3 ? first[index] : 0.1f / float(1 << (index - 2)); }

// "_LOD2" at the end of a name, any case: 2
int LevelFromName(const std::string& name) {
    auto at = name.size();
    while (at > 0 && std::isdigit((unsigned char)name[at - 1])) --at;
    if (at == name.size() || at < 4) return -1;
    std::string tag = name.substr(at - 4, 4);
    std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return tag == "_lod" ? std::stoi(name.substr(at)) : -1;
}
}

void DrawLODGroupInspector(Scene& scene, EntityID e, LODGroupComponent& g, const RenderCamera* sceneCamera) {
    static EntityID shownFor = kNullEntity; static int selected = 0, dragging = -1;
    if (shownFor != e) { shownFor = e; selected = 0; dragging = -1; }
    if (g.levels.empty()) g.levels.push_back({ DefaultHeight(0), {} });
    selected = std::clamp(selected, 0, (int)g.levels.size() - 1);
    auto& reg = scene.Reg();

    // ---------------------------------------------------------------- the bar
    const float width = std::max(120.f, ImGui::GetContentRegionAvail().x), height = 44;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##lodBar", { width, height });
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    auto* draw = ImGui::GetWindowDrawList();
    // Square-root scale, like Unity's bar: small percentages (the far levels) get room to be dragged
    auto x = [&](float h) { return origin.x + (1 - std::sqrt(std::clamp(h, 0.f, 1.f))) * width; };
    const ImVec2 mouse = ImGui::GetMousePos();
    const float along = 1 - std::clamp((mouse.x - origin.x) / width, 0.f, 1.f), mouseHeight = along * along;

    int separatorHot = -1;
    for (int i = 0; i < (int)g.levels.size(); ++i) if (std::abs(mouse.x - x(g.levels[i].screenHeight)) < 5 && hovered) separatorHot = i;
    if (ImGui::IsItemClicked(0)) {
        dragging = separatorHot;
        if (separatorHot < 0) { selected = (int)g.levels.size() - 1; for (int i = 0; i < (int)g.levels.size(); ++i) if (mouseHeight >= g.levels[i].screenHeight) { selected = i; break; } }
        else selected = separatorHot;
    }
    if (!active) dragging = -1;
    if (dragging >= 0) {
        const float upper = dragging > 0 ? g.levels[dragging - 1].screenHeight - 0.005f : 1.0f;
        const float lower = dragging + 1 < (int)g.levels.size() ? g.levels[dragging + 1].screenHeight + 0.005f : 0.0005f;
        g.levels[dragging].screenHeight = std::clamp(mouseHeight, lower, upper);
    }
    if (separatorHot >= 0 || dragging >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    float start = 1;
    for (int i = 0; i < (int)g.levels.size(); ++i) {
        const float end = g.levels[i].screenHeight;
        ImU32 color = kLevelColors[i % 8];
        draw->AddRectFilled({ x(start), origin.y }, { x(end), origin.y + height }, color);
        if (i == selected) draw->AddRect({ x(start) + 1, origin.y + 1 }, { x(end) - 1, origin.y + height - 1 }, IM_COL32(255, 255, 255, 200), 0, 0, 2);
        auto label = std::format("LOD {}\n{:.0f}%", i, start * 100);
        draw->PushClipRect({ x(start), origin.y }, { x(end), origin.y + height }, true);
        draw->AddText({ x(start) + 5, origin.y + 5 }, IM_COL32(240, 240, 240, 255), label.c_str());
        draw->PopClipRect();
        start = end;
    }
    draw->AddRectFilled({ x(start), origin.y }, { x(0), origin.y + height }, IM_COL32(90, 30, 30, 255));
    {
        auto label = std::format("Culled\n{:.0f}%", start * 100);
        draw->PushClipRect({ x(start), origin.y }, { x(0), origin.y + height }, true);
        draw->AddText({ x(start) + 5, origin.y + 5 }, IM_COL32(230, 200, 200, 255), label.c_str());
        draw->PopClipRect();
    }
    for (int i = 0; i < (int)g.levels.size(); ++i) {
        const float sx = x(g.levels[i].screenHeight);
        draw->AddLine({ sx, origin.y }, { sx, origin.y + height }, separatorHot == i || dragging == i ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255), 2);
    }
    // Where the Scene view camera stands: the level it sees
    int sceneLevel = -2;
    if (sceneCamera) {
        const float h = scene.LODScreenHeight(e, *sceneCamera);
        sceneLevel = scene.SelectLOD(e, *sceneCamera);
        const float cx = x(std::min(h, 1.f));
        draw->AddLine({ cx, origin.y - 3 }, { cx, origin.y + height + 3 }, IM_COL32(255, 220, 90, 255), 2);
        draw->AddTriangleFilled({ cx - 5, origin.y + height + 9 }, { cx + 5, origin.y + height + 9 }, { cx, origin.y + height + 3 }, IM_COL32(255, 220, 90, 255));
        ImGui::Dummy({ 1, 8 });
        ImGui::TextDisabled("Scene camera: %.1f%% (LOD Bias %.1f), %s", h * 100, GraphicsQuality::Current().lodBias, sceneLevel < 0 ? "culled" : std::format("LOD {}", sceneLevel).c_str());
    }
    if (hovered && separatorHot < 0 && dragging < 0) ImGui::SetTooltip("Click a level to select it, drag a line to change where it switches");

    // ---------------------------------------------------------------- levels
    if (ImGui::Button("Add LOD")) {
        const int at = selected + 1;
        const float above = g.levels[selected].screenHeight, below = at < (int)g.levels.size() ? g.levels[at].screenHeight : 0.f;
        g.levels.insert(g.levels.begin() + at, { (above + below) * 0.5f, {} });
        selected = at;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g.levels.size() <= 1);
    if (ImGui::Button("Remove LOD")) { g.levels.erase(g.levels.begin() + selected); selected = std::max(0, selected - 1); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Fill From Children")) {
        // Children named Name_LOD0, Name_LOD1...; without such names, one child per level in order
        std::vector<std::pair<int, EntityID>> found;
        std::function<void(EntityID)> visit = [&](EntityID parent) { for (EntityID child : scene.GetChildren(parent)) { int level = LevelFromName(reg.Get<NameComponent>(child).name); if (level >= 0 && level < 16) found.push_back({ level, child }); else visit(child); } };
        visit(e);
        if (found.empty()) { int i = 0; for (EntityID child : scene.GetChildren(e)) if (i < 16) found.push_back({ i++, child }); }
        if (!found.empty()) {
            int count = 0; for (auto& [level, child] : found) count = std::max(count, level + 1);
            g.levels.clear();
            for (int i = 0; i < count; ++i) g.levels.push_back({ DefaultHeight(i), {} });
            for (auto& [level, child] : found) g.levels[level].renderers.push_back(reg.Get<IDComponent>(child).uuid);
            g.size = 0; scene.RecalculateLODBounds(e); selected = 0;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Uses children named ..._LOD0, ..._LOD1 (as FBX exporters name them)");

    ImGui::SeparatorText(std::format("LOD {}", selected).c_str());
    LODLevel& level = g.levels[selected];
    float percent = level.screenHeight * 100;
    const float upper = selected > 0 ? g.levels[selected - 1].screenHeight * 100 - 0.5f : 100.f;
    const float lower = selected + 1 < (int)g.levels.size() ? g.levels[selected + 1].screenHeight * 100 + 0.5f : 0.05f;
    if (Insp::DragFloat("Transition (% of screen)", &percent, 0.1f, lower, upper, "%.1f%%")) level.screenHeight = std::clamp(percent, lower, upper) / 100;
    int remove = -1;
    for (int i = 0; i < (int)level.renderers.size(); ++i) {
        ImGui::PushID(i);
        EntityID r = scene.FindByUUID(level.renderers[i]);
        if (ImGui::SmallButton("X")) remove = i;
        ImGui::SameLine();
        if (r) ImGui::TextUnformatted(reg.Get<NameComponent>(r).name.c_str()); else ImGui::TextDisabled("(missing object)");
        ImGui::PopID();
    }
    if (remove >= 0) { level.renderers.erase(level.renderers.begin() + remove); g.size = 0; }
    ImGui::Button("Drop objects here from the Hierarchy", { -1, 34 });
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("STARDUST_ENTITY")) {
            EntityID dropped = *(const EntityID*)payload->Data;
            if (reg.Valid(dropped)) {
                const u64 id = reg.Get<IDComponent>(dropped).uuid;
                if (std::find(level.renderers.begin(), level.renderers.end(), id) == level.renderers.end()) { level.renderers.push_back(id); g.size = 0; }
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::TextDisabled("Each object is shown with its children.");

    ImGui::SeparatorText("Bounds");
    ImGui::TextDisabled("Size %.2f m", g.size);
    ImGui::SameLine();
    if (ImGui::SmallButton("Recalculate Bounds")) { g.size = 0; scene.RecalculateLODBounds(e); }
}

} // namespace Stardust
