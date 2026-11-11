// Scene view tools written in C#: a custom Editor's OnSceneGUI gets the mouse on the target's local XY plane
// and draws handles over the view (the Tilemap package paints tiles this way)
#include "EditorApp.h"
#include "Assets/AssetManager.h"
#include "Scripting/ScriptEngine.h"
#include "GizmoMath.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <format>

namespace Stardust {
using json = nlohmann::json;

bool EditorApp::UpdateManagedSceneTools() {
    if (m_State != PlayState::Edit || m_AnimationPreviewScene || m_Camera.IsFlying()) return false;
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    if (!reg.Valid(m_Selected)) return false;
    auto* scripts = reg.TryGet<ScriptComponent>(m_Selected);
    if (!scripts || !scripts->Count()) return false;

    // Mouse ray against the selected object's local XY plane
    const glm::mat4 world = scene.GetWorldMatrix(m_Selected), toLocal = glm::inverse(world);
    const ImVec2 mouse = ImGui::GetMousePos();
    const RenderCamera camera = m_Camera.GetRenderCamera(m_SceneView.size.x / std::max(1.0f, m_SceneView.size.y));
    const glm::mat4 inverseViewProjection = glm::inverse(camera.projection * camera.view);
    const glm::vec2 ndc((mouse.x - m_SceneView.min.x) / m_SceneView.size.x * 2.0f - 1.0f, 1.0f - (mouse.y - m_SceneView.min.y) / m_SceneView.size.y * 2.0f);
    const glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f), farPoint = inverseViewProjection * glm::vec4(ndc, 0.5f, 1.0f);
    const glm::vec3 direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - glm::vec3(nearPoint) / nearPoint.w);
    const glm::vec3 origin = m_Camera.Is2D() ? glm::vec3(nearPoint) / nearPoint.w : camera.position;
    const glm::vec3 normal = glm::normalize(glm::vec3(world[2])), planePoint = glm::vec3(world[3]);
    const float facing = glm::dot(direction, normal);
    bool hit = false;
    glm::vec3 hitWorld(0), hitLocal(0);
    if (std::abs(facing) > 1e-5f) {
        const float t = glm::dot(planePoint - origin, normal) / facing;
        if (t > 0) { hit = true; hitWorld = origin + direction * t; hitLocal = glm::vec3(toLocal * glm::vec4(hitWorld, 1)); }
    }
    const ImGuiIO& io = ImGui::GetIO();
    json input = {
        { "hovered", m_SceneView.hovered }, { "hit", hit },
        { "local", { hitLocal.x, hitLocal.y, hitLocal.z } }, { "world", { hitWorld.x, hitWorld.y, hitWorld.z } },
        { "down", { io.MouseDown[0], io.MouseDown[1], io.MouseDown[2] } },
        { "pressed", { ImGui::IsMouseClicked(0), ImGui::IsMouseClicked(1), ImGui::IsMouseClicked(2) } },
        { "released", { ImGui::IsMouseReleased(0), ImGui::IsMouseReleased(1), ImGui::IsMouseReleased(2) } },
        { "shift", io.KeyShift }, { "ctrl", io.KeyCtrl }, { "alt", io.KeyAlt }, { "scroll", m_SceneView.hovered ? io.MouseWheel : 0.0f },
    };
    // Mouse buttons pressed outside the view do not belong to the tool
    if (!m_SceneView.hovered) input["pressed"] = { false, false, false };

    // Handles are in the target's local space; lines crossing behind the camera are cut, shapes partly behind are skipped
    const glm::mat4 localToClip = camera.projection * camera.view * world;
    const ImVec2 viewMin(m_SceneView.min.x, m_SceneView.min.y), viewSize(m_SceneView.size.x, m_SceneView.size.y);
    auto toScreen = [&](glm::vec3 local, bool& visible) -> ImVec2 {
        ImVec2 out;
        visible &= Gizmo::ProjectPoint(localToClip, local, viewMin, viewSize, out);
        return out;
    };
    auto color = [](const json& c) { return c.is_array() && c.size() == 4 ? ImGui::ColorConvertFloat4ToU32({ c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>() }) : IM_COL32_WHITE; };
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({ m_SceneView.min.x, m_SceneView.min.y }, { m_SceneView.min.x + m_SceneView.size.x, m_SceneView.min.y + m_SceneView.size.y }, true);

    bool captured = false;
    const std::string entity = std::format("{:016x}", reg.Get<IDComponent>(m_Selected).uuid);
    for (size_t i = 0; i < scripts->Count(); ++i) {
        ScriptSlot& slot = scripts->Slot(i);
        auto fields = json::parse(slot.fieldsJson, nullptr, false);
        if (!fields.is_object()) fields = json::object();
        const json request = { { "kind", "scene" }, { "class", slot.className }, { "entity", entity }, { "fields", fields }, { "input", input } };
        const auto response = json::parse(ScriptEngine::DrawEditorUI(request.dump()), nullptr, false);
        if (!response.is_object() || !response.value("handled", false)) continue;
        if (response.contains("fields") && response["fields"].is_object()) {
            bool changed = false;
            for (auto& [name, value] : response["fields"].items()) if (!fields.contains(name) || fields[name] != value) { fields[name] = value; changed = true; }
            if (changed) { slot.fieldsJson = fields.dump(); m_SceneDirty = true; }
        }
        captured |= response.value("capture", false);
        for (const auto& h : response.value("handles", json::array())) {
            const std::string kind = h.value("kind", "");
            const ImU32 c = color(h.value("color", json()));
            const float width = h.value("width", 1.5f);
            if (kind == "line") {
                const auto a = h.value("a", std::array<float, 3>{}), b = h.value("b", std::array<float, 3>{});
                ImVec2 pa, pb;
                if (Gizmo::ProjectSegment(localToClip, { a[0], a[1], a[2] }, { b[0], b[1], b[2] }, viewMin, viewSize, pa, pb)) draw->AddLine(pa, pb, c, width);
            } else if (kind == "rect" || kind == "texture") {
                const auto lo = h.value("min", std::array<float, 2>{}), hi = h.value("max", std::array<float, 2>{});
                bool visible = true;
                const ImVec2 corners[4] = { toScreen({ lo[0], lo[1], 0 }, visible), toScreen({ hi[0], lo[1], 0 }, visible), toScreen({ hi[0], hi[1], 0 }, visible), toScreen({ lo[0], hi[1], 0 }, visible) };
                if (!visible) {
                    // Partly behind the camera: only the outline, cut at the camera
                    const glm::vec3 p[4] = { { lo[0], lo[1], 0 }, { hi[0], lo[1], 0 }, { hi[0], hi[1], 0 }, { lo[0], hi[1], 0 } };
                    for (int k = 0; k < 4 && kind == "rect"; ++k) { ImVec2 pa, pb; if (Gizmo::ProjectSegment(localToClip, p[k], p[(k + 1) % 4], viewMin, viewSize, pa, pb)) draw->AddLine(pa, pb, c, width); }
                } else if (kind == "texture") {
                    if (auto texture = AssetManager::GetTexture(h.value("texture", ""), true)) {
                        const float w = (float)texture->Desc().width, th = (float)texture->Desc().height;
                        auto r = h.value("rect", std::array<float, 4>{});
                        if (r[2] <= 0 || r[3] <= 0) r = { 0, 0, w, th };
                        const ImVec2 u0(r[0] / w, (r[1] + r[3]) / th), u1((r[0] + r[2]) / w, (r[1] + r[3]) / th), u2((r[0] + r[2]) / w, r[1] / th), u3(r[0] / w, r[1] / th);
                        draw->AddImageQuad((ImTextureID)GetDevice().ImGuiTexture(texture.get()), corners[0], corners[1], corners[2], corners[3], u0, u1, u2, u3, c);
                    }
                } else if (h.value("fill", false)) draw->AddQuadFilled(corners[0], corners[1], corners[2], corners[3], c);
                else draw->AddQuad(corners[0], corners[1], corners[2], corners[3], c, width);
            } else if (kind == "label") {
                const auto at = h.value("at", std::array<float, 3>{});
                bool visible = true;
                const ImVec2 p = toScreen({ at[0], at[1], at[2] }, visible);
                if (visible) draw->AddText(p, c, h.value("text", "").c_str());
            }
        }
    }
    draw->PopClipRect();
    return captured && m_SceneView.hovered;
}

} // namespace Stardust
