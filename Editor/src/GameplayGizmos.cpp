// Scene view overlays of the components in Scene/GameplayComponents.h: spline curves with draggable
// knots, the baked nav mesh, reflection probe and decal boxes, character capsules and joint anchors.
#include "EditorApp.h"
#include "GizmoMath.h"
#include "Scene/Navigation.h"
#include "Scene/Splines.h"
#include <imgui.h>
#include <glm/gtx/matrix_decompose.hpp>

namespace Stardust {

bool EditorApp::DrawGameplayGizmos() {
    if (!m_ShowGizmos || m_AnimationPreviewScene) return false;
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    const float aspect = m_SceneView.size.x / std::max(1.0f, m_SceneView.size.y);
    RenderCamera cam = m_Camera.GetRenderCamera(aspect);
    const glm::mat4 vp = cam.projection * cam.view;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 vmin{ m_SceneView.min.x, m_SceneView.min.y }, vsize{ m_SceneView.size.x, m_SceneView.size.y };
    auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float width = 1.5f) {
        ImVec2 pa, pb; if (Gizmo::ProjectSegment(vp, a, b, vmin, vsize, pa, pb)) dl->AddLine(pa, pb, color, width);
    };
    auto box = [&](const glm::mat4& m, ImU32 color) {
        glm::vec3 c[8];
        for (int i = 0; i < 8; ++i) c[i] = glm::vec3(m * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f));
        const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
        for (auto& e : edges) line(c[e[0]], c[e[1]], color);
    };
    auto circle = [&](const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float r, ImU32 color) {
        glm::vec3 prev = center + u * r;
        for (int i = 1; i <= 24; ++i) { float a = i / 24.0f * glm::two_pi<float>(); glm::vec3 p = center + (u * std::cos(a) + v * std::sin(a)) * r; line(prev, p, color); prev = p; }
    };

    // Baked nav mesh (whole scene, translucent)
    if (Navigation::DebugDraw()) {
        bool hasSurface = false;
        reg.Each<NavMeshSurfaceComponent>([&](EntityID, NavMeshSurfaceComponent&) { hasSurface = true; });
        if (hasSurface) {
            const auto& tris = Navigation::DebugTriangles(scene);
            const float maxDistance = 120.0f;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                if (glm::length(tris[i] - cam.position) > maxDistance) continue;
                ImVec2 a, b, c;
                if (!Gizmo::ProjectPoint(vp, tris[i], vmin, vsize, a) || !Gizmo::ProjectPoint(vp, tris[i + 1], vmin, vsize, b) || !Gizmo::ProjectPoint(vp, tris[i + 2], vmin, vsize, c)) continue;
                dl->AddTriangleFilled(a, b, c, IM_COL32(40, 170, 255, 55));
                dl->AddTriangle(a, b, c, IM_COL32(60, 190, 255, 110), 1.0f);
            }
        }
    }

    bool active = false;
    if (!reg.Valid(m_Selected)) return false;
    const EntityID e = m_Selected;
    const glm::mat4 world = scene.GetWorldMatrix(e);

    if (auto* c = reg.TryGet<ReflectionProbeComponent>(e)) {
        glm::vec3 scale, t, skew; glm::vec4 persp; glm::quat r; glm::decompose(world, scale, r, t, skew, persp);
        box(glm::translate(glm::mat4(1), t + c->center) * glm::scale(glm::mat4(1), c->size * glm::abs(scale)), IM_COL32(255, 210, 80, 220));
    }
    if (auto* c = reg.TryGet<DecalComponent>(e)) {
        const glm::mat4 m = world * glm::scale(glm::mat4(1), c->size);
        box(m, IM_COL32(120, 255, 160, 220));
        const glm::vec3 center = glm::vec3(world[3]);
        line(center, center - glm::normalize(glm::vec3(world[2])) * c->size.z * 0.6f, IM_COL32(120, 255, 160, 255), 2.0f);
    }
    if (auto* c = reg.TryGet<CharacterControllerComponent>(e)) {
        glm::vec3 scale, t, skew; glm::vec4 persp; glm::quat r; glm::decompose(world, scale, r, t, skew, persp);
        scale = glm::abs(scale);
        const float radius = c->radius * std::max(scale.x, scale.z), half = std::max(0.0f, c->height * 0.5f * scale.y - radius);
        const glm::vec3 center = t + c->center * scale, up(0, 1, 0);
        const ImU32 col = IM_COL32(140, 255, 140, 220);
        for (float s : { -1.0f, 1.0f }) circle(center + up * half * s, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), radius, col);
        for (int i = 0; i < 4; ++i) { float a = i * glm::half_pi<float>(); glm::vec3 d(std::cos(a) * radius, 0, std::sin(a) * radius); line(center + d + up * half, center + d - up * half, col); }
        circle(center + up * half, glm::vec3(1, 0, 0), up, radius, col); circle(center - up * half, glm::vec3(1, 0, 0), up, radius, col);
        circle(center + up * half, glm::vec3(0, 0, 1), up, radius, col); circle(center - up * half, glm::vec3(0, 0, 1), up, radius, col);
    }
    if (auto* c = reg.TryGet<JointComponent>(e)) {
        const glm::vec3 anchor = glm::vec3(world * glm::vec4(c->anchor, 1.0f));
        const ImU32 col = IM_COL32(255, 140, 60, 240);
        ImVec2 p; if (Gizmo::ProjectPoint(vp, anchor, vmin, vsize, p)) dl->AddCircleFilled(p, 5, col);
        if (c->type == 1 || c->type == 3 || c->type == 4) {
            glm::vec3 axis = glm::mat3(world) * c->axis; if (glm::length(axis) > 1e-5f) axis = glm::normalize(axis);
            line(anchor - axis * 0.6f, anchor + axis * 0.6f, col, 2.0f);
        }
        if (c->connectedBody) if (EntityID other = scene.FindByUUID(c->connectedBody)) line(anchor, glm::vec3(scene.GetWorldMatrix(other)[3]), IM_COL32(255, 140, 60, 120));
    }
    if (auto* c = reg.TryGet<NavMeshSurfaceComponent>(e); c && c->useBounds) box(glm::translate(glm::mat4(1), c->boundsCenter) * glm::scale(glm::mat4(1), c->boundsSize), IM_COL32(60, 190, 255, 200));
    if (auto* c = reg.TryGet<LineRendererComponent>(e)) {
        for (size_t i = 0; i < c->positions.size(); ++i) {
            const glm::vec3 p = c->useWorldSpace ? c->positions[i] : glm::vec3(world * glm::vec4(c->positions[i], 1.0f));
            ImVec2 s; if (Gizmo::ProjectPoint(vp, p, vmin, vsize, s)) dl->AddCircle(s, 4, IM_COL32(255, 255, 255, 200));
        }
    }

    // Splines: the curve, and knots dragged on the camera plane
    if (auto* c = reg.TryGet<SplineComponent>(e)) {
        std::vector<glm::vec3> pts;
        Splines::Sample(*c, pts, 24);
        for (size_t i = 1; i < pts.size(); ++i) line(glm::vec3(world * glm::vec4(pts[i - 1], 1)), glm::vec3(world * glm::vec4(pts[i], 1)), IM_COL32(255, 230, 90, 255), 2.5f);
        static int dragging = -1, draggingTangent = 0; // tangent: 0 knot, -1 in, 1 out
        const ImVec2 mouse = ImGui::GetMousePos();
        const glm::mat4 inverseWorld = glm::inverse(world);
        auto handle = [&](const glm::vec3& local, int index, int tangent, ImU32 color, float size) {
            const glm::vec3 p = glm::vec3(world * glm::vec4(local, 1));
            ImVec2 s; if (!Gizmo::ProjectPoint(vp, p, vmin, vsize, s)) return;
            const bool hover = m_SceneView.hovered && std::hypot(mouse.x - s.x, mouse.y - s.y) < size + 3;
            dl->AddCircleFilled(s, size, hover || (dragging == index && draggingTangent == tangent) ? IM_COL32(255, 255, 255, 255) : color);
            if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { dragging = index; draggingTangent = tangent; }
            if (hover) active = true;
        };
        for (size_t i = 0; i < c->knots.size(); ++i) {
            auto& k = c->knots[i];
            if (c->mode == 1) {
                line(glm::vec3(world * glm::vec4(k.position + k.tangentIn, 1)), glm::vec3(world * glm::vec4(k.position + k.tangentOut, 1)), IM_COL32(200, 200, 200, 160));
                handle(k.position + k.tangentIn, (int)i, -1, IM_COL32(120, 200, 255, 230), 4);
                handle(k.position + k.tangentOut, (int)i, 1, IM_COL32(255, 140, 120, 230), 4);
            }
            handle(k.position, (int)i, 0, IM_COL32(255, 200, 40, 255), 6);
        }
        if (dragging >= 0 && dragging < (int)c->knots.size()) {
            active = true;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = -1;
            else {
                auto& k = c->knots[dragging];
                glm::vec3 local = draggingTangent == 0 ? k.position : k.position + (draggingTangent < 0 ? k.tangentIn : k.tangentOut);
                const glm::vec3 p = glm::vec3(world * glm::vec4(local, 1));
                // Ray through the mouse, on the plane facing the camera through the handle
                const glm::mat4 inv = glm::inverse(vp);
                const float nx = (mouse.x - vmin.x) / vsize.x * 2 - 1, ny = 1 - (mouse.y - vmin.y) / vsize.y * 2;
                glm::vec4 a = inv * glm::vec4(nx, ny, 1, 1), b = inv * glm::vec4(nx, ny, 0.5f, 1);
                glm::vec3 ro = glm::vec3(a) / a.w, rd = glm::normalize(glm::vec3(b) / b.w - ro);
                const glm::vec3 normal = glm::normalize(cam.position - p);
                const float denom = glm::dot(rd, normal);
                if (std::abs(denom) > 1e-5f) {
                    const float t = glm::dot(p - ro, normal) / denom;
                    if (t > 0) {
                        glm::vec3 moved = glm::vec3(inverseWorld * glm::vec4(ro + rd * t, 1));
                        if (draggingTangent == 0) k.position = moved;
                        else if (draggingTangent < 0) { k.tangentIn = moved - k.position; if (!ImGui::GetIO().KeyAlt) k.tangentOut = -k.tangentIn; }
                        else { k.tangentOut = moved - k.position; if (!ImGui::GetIO().KeyAlt) k.tangentIn = -k.tangentOut; }
                    }
                }
            }
        }
    }
    return active;
}

} // namespace Stardust
