#include "EditorApp.h"
#include "Animation/ModelSkeleton.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptEngine.h"
#include "Renderer/Mesh.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <algorithm>

namespace Stardust {

static bool IsAudioFile(const std::string& p) {
    std::string ext = std::filesystem::path(p).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg";
}

bool EditorApp::HandleAssetDropOnEntity(EntityID target, const std::string& asset) {
    Registry& reg = ActiveScene().Reg();
    if (!reg.Valid(target)) return false;
    auto p=std::filesystem::u8path(asset);
    std::string ext = p.extension().string();
    if (ext == ".cs") {
        for (const auto& part : p) if (part=="Editor") return false;
        std::string className=FileSystem::Utf8(p.stem());
        for (const auto& info : ScriptEngine::Classes()) if (info.name==className || info.name.ends_with("."+className)) { className=info.name; break; }
        if (auto* scripts = reg.TryGet<ScriptComponent>(target)) scripts->additional.push_back({className, "{}"});
        else reg.Add<ScriptComponent>(target, className);
        if (ActiveScene().IsRunning()) ScriptEngine::Instantiate(target);
        m_Selected = target;
        return true;
    }
    if(ext==".sprite"||AssetManager::IsTextureFile(p))if(auto* image=reg.TryGet<UIImageComponent>(target)){if(ext==".sprite")image->sprite=asset;else{image->sprite.clear();image->texture=asset;}return true;}
    if(ext==".ttf"||ext==".otf")if(auto* text=reg.TryGet<UITextComponent>(target)){text->font=asset;return true;}
    if (ext == ".mat") {
        if(auto* image=reg.TryGet<UIImageComponent>(target)){image->material=asset;return true;}
        if(auto* text=reg.TryGet<UITextComponent>(target)){text->material=asset;return true;}
        auto* mr = reg.TryGet<MeshRendererComponent>(target);
        if (!mr) return false;
        mr->materialPath = asset;
        m_Selected = target;
        return true;
    }
    if (IsAudioFile(asset)) {
        auto& a = reg.Has<AudioSourceComponent>(target) ? reg.Get<AudioSourceComponent>(target) : reg.Add<AudioSourceComponent>(target);
        a.clip = asset;
        m_Selected = target;
        return true;
    }
    return false; // meshes / prefabs create new objects instead
}

EntityID EditorApp::CreateFromAsset(const std::string& asset, EntityID parent, const glm::vec3* position) {
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    auto p=std::filesystem::u8path(asset);
    EntityID e = kNullEntity;
    if (p.extension() == ".prefab") {
        e = SceneSerializer::InstantiatePrefab(scene, AssetManager::ToAbsolute(asset), parent);
    } else if (AssetManager::IsMeshFile(p)) {
        e = scene.CreateEntity(FileSystem::Utf8(p.stem()));
        auto& renderer = reg.Add<MeshRendererComponent>(e);
        renderer.mesh = asset;
        if (p.extension() == ".fbx") {
            for (const FbxContentItem& item : AssetManager::GetFbxContents(asset)) {
                if (!item.material) continue;
                if (renderer.materials.size() <= item.slot) renderer.materials.resize(item.slot + 1);
                renderer.materials[item.slot] = item.path;
            }
            ModelSkeleton::Import(scene, e, asset);
        }
    } else if (IsAudioFile(asset)) {
        e = scene.CreateEntity(FileSystem::Utf8(p.stem()));
        reg.Add<AudioSourceComponent>(e).clip = asset;
    }
    if (!e) return kNullEntity;
    if (parent && p.extension() != ".prefab") scene.SetParent(e, parent, false);
    if (position && !parent) reg.Get<TransformComponent>(e).position = *position;
    return e;
}

EntityID EditorApp::PickEntityCPU(const glm::vec3& ro, const glm::vec3& rd, float* distance) {
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    EntityID best = kNullEntity;
    float bestT = 1e30f;
    reg.Each<MeshRendererComponent>([&](EntityID e, MeshRendererComponent& mr) {
        if (!scene.IsActiveInHierarchy(e)) return;
        auto mesh = AssetManager::GetMesh(mr.mesh);
        if (!mesh) return;
        glm::mat4 inv = glm::inverse(scene.GetWorldMatrix(e));
        glm::vec3 o = glm::vec3(inv * glm::vec4(ro, 1)), d = glm::vec3(inv * glm::vec4(rd, 0));
        const AABB& b = mesh->Bounds();
        float t0 = 0, t1 = 1e30f;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(d[i]) < 1e-8f) { if (o[i] < b.min[i] || o[i] > b.max[i]) return; continue; }
            float a = (b.min[i] - o[i]) / d[i], c = (b.max[i] - o[i]) / d[i];
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a); t1 = std::min(t1, c);
            if (t0 > t1) return;
        }
        // t is in local units of d; d is not normalized so t is the world-space ray parameter
        if (t0 < bestT) { bestT = t0; best = e; }
    });
    if (distance) *distance = bestT;
    return best;
}

void EditorApp::DrawSceneIcons(bool allowPicking) {
    Scene& scene = m_AnimationPreviewScene?*m_AnimationPreviewScene:ActiveScene();
    Registry& reg = scene.Reg();
    RenderCamera cam = m_Camera.GetRenderCamera(m_SceneView.size.x / m_SceneView.size.y);
    glm::mat4 vp = cam.projection * cam.view;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto project = [&](const glm::vec3& p, ImVec2& out) {
        glm::vec4 c = vp * glm::vec4(p, 1.0f);
        if (c.w <= 0.01f) return false;
        out = ImVec2(m_SceneView.min.x + (c.x / c.w * 0.5f + 0.5f) * m_SceneView.size.x, m_SceneView.min.y + (0.5f - c.y / c.w * 0.5f) * m_SceneView.size.y);
        return true;
    };
    auto icon = [&](EntityID e, const char* label, ImU32 col) {
        glm::vec3 p = glm::vec3(scene.GetWorldMatrix(e)[3]);
        if (glm::length(p - cam.position) > 150.0f) return;
        ImVec2 s;
        if (!project(p, s)) return;
        dl->AddCircleFilled(s, 9, IM_COL32(20, 20, 20, 160));
        dl->AddCircle(s, 9, col, 0, 1.5f);
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(s.x - ts.x * 0.5f, s.y - ts.y * 0.5f), col, label);
        auto mouse=ImGui::GetMousePos();
        if (allowPicking && !m_AnimationPreviewScene && m_SceneView.hovered && std::hypot(mouse.x-s.x,mouse.y-s.y)<10 && ImGui::IsMouseClicked(0)) {
            m_Selected=e; m_InspectAsset=false; m_ScenePickPending=false;
        }
    };
    auto rangeCircle = [&](EntityID e, float r, ImU32 col) {
        glm::vec3 c = glm::vec3(scene.GetWorldMatrix(e)[3]);
        const glm::vec3 axes[3][2] = { {{1,0,0},{0,1,0}}, {{1,0,0},{0,0,1}}, {{0,1,0},{0,0,1}} };
        for (auto& ax : axes) {
            ImVec2 prev; bool hp = project(c + ax[0] * r, prev);
            for (int i = 1; i <= 48; ++i) {
                float a = i / 48.0f * 6.2831853f;
                ImVec2 cur; bool hc = project(c + (ax[0] * std::cos(a) + ax[1] * std::sin(a)) * r, cur);
                if (hp && hc) dl->AddLine(prev, cur, col, 1.0f);
                prev = cur; hp = hc;
            }
        }
    };
    reg.Each<BoneComponent>([&](EntityID e,BoneComponent& bone){ImVec2 position;if(!project(glm::vec3(scene.GetWorldMatrix(e)[3]),position))return;auto parent=scene.GetParent(e);ImVec2 other;if(parent&&reg.Has<BoneComponent>(parent)&&project(glm::vec3(scene.GetWorldMatrix(parent)[3]),other))dl->AddLine(position,other,IM_COL32(100,200,245,220),2);dl->AddCircle(position,3,IM_COL32(180,235,255,255),8,1);if(allowPicking&&!m_AnimationPreviewScene&&m_SceneView.hovered&&std::hypot(ImGui::GetMousePos().x-position.x,ImGui::GetMousePos().y-position.y)<5&&ImGui::IsMouseClicked(0)){m_Selected=e;m_InspectAsset=false;m_ScenePickPending=false;}});
    for (EntityID e : reg.AllEntities()) {
        if (reg.Has<CameraComponent>(e)) icon(e, "C", IM_COL32(200, 200, 255, 255));
        if (reg.Has<DirectionalLightComponent>(e)) icon(e, "D", IM_COL32(255, 230, 120, 255));
        if (auto* pl = reg.TryGet<PointLightComponent>(e)) {
            icon(e, "P", IM_COL32(255, 220, 120, 255));
            if (e == m_Selected) rangeCircle(e, pl->range, IM_COL32(255, 230, 120, 180));
        }
        if (auto* sl = reg.TryGet<SpotLightComponent>(e)) {
            icon(e, "S", IM_COL32(255, 220, 120, 255));
            if (e == m_Selected) {
                glm::mat4 w = scene.GetWorldMatrix(e);
                glm::vec3 o = glm::vec3(w[3]), f = -glm::normalize(glm::vec3(w[2]));
                glm::vec3 u = glm::normalize(glm::vec3(w[1])), r = glm::normalize(glm::vec3(w[0]));
                float rad = std::tan(glm::radians(sl->outerAngle)) * sl->range;
                ImVec2 a, b;
                for (int i = 0; i < 8; ++i) {
                    float ang = i / 8.0f * 6.2831853f;
                    glm::vec3 tip = o + f * sl->range + (r * std::cos(ang) + u * std::sin(ang)) * rad;
                    if (project(o, a) && project(tip, b)) dl->AddLine(a, b, IM_COL32(255, 230, 120, 180));
                }
            }
        }
        if (reg.Has<AudioSourceComponent>(e)) icon(e, "A", IM_COL32(140, 220, 255, 255));
    }
}

void EditorApp::DrawViewCube() {
    glm::mat4 view = m_Camera.View();
    glm::mat4 before = view;
    float sz = 96.0f;
    ImVec2 pos(m_SceneView.min.x + m_SceneView.size.x - sz - 8, m_SceneView.min.y + 30);
    ImGuizmo::ViewManipulate(glm::value_ptr(view), 8.0f, pos, ImVec2(sz, sz), 0x10101010);
    if (view != before) m_Camera.SetFromView(view);
}

} // namespace Stardust
