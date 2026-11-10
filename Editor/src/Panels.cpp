#include "Assets/AssetMeta.h"
#include "EditorApp.h"
#include <set>
#include "EditorIcons.h"
#include "InspectorWidgets.h"
#include "Scene/ComponentState.h"
#include "ManagedEditorUI.h"
#include "Animation/ModelSkeleton.h"
#include "Core/Log.h"
#include "Assets/AssetManager.h"
#include "AssetUI.h"
#include "Scripting/ScriptEngine.h"
#include "Scene/SceneSerializer.h"
#include "Project/Project.h"
#include "Audio/AudioEngine.h"
#include "Core/FileSystem.h"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

namespace Stardust {
void DrawTerrainVegetationInspector(Scene& scene, EntityID e, TerrainVegetationComponent& c);

// ------------------------------------------------------------------------------------------------ widgets

// Unity-like labelled row: label column on the left, widget filling the rest
static void BeginProperty(const char* label) {
    ImGui::PushID(label);
    float labelWidth = Insp::LabelWidth();
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, labelWidth);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::NextColumn();
    ImGui::SetNextItemWidth(-1);
}
static void EndProperty() {
    ImGui::Columns(1);
    ImGui::PopID();
}

static bool Vec3Control(const char* label, glm::vec3& v, float reset = 0.0f, float speed = 0.05f) {
    bool changed = false;
    BeginProperty(label);
    float w = (ImGui::GetContentRegionAvail().x - 3 * 20.0f - 2 * 4.0f) / 3.0f;
    const ImVec4 colors[3] = { { 0.92f, 0.42f, 0.42f, 1 }, { 0.48f, 0.82f, 0.48f, 1 }, { 0.42f, 0.64f, 0.96f, 1 } };
    const char* axes[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(axes[i], ImVec2(20, 0))) { v[i] = reset; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset %s", axes[i]);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 0);
        ImGui::SetNextItemWidth(w);
        changed |= ImGui::DragFloat("##v", &v[i], speed, 0, 0, "%.3f");
        if (i < 2) ImGui::SameLine(0, 4);
        ImGui::PopID();
    }
    EndProperty();
    return changed;
}

static bool FloatProp(const char* label, float& v, float speed = 0.01f, float mn = 0, float mx = 0) {
    BeginProperty(label);
    bool c = ImGui::DragFloat("##f", &v, speed, mn, mx, "%.3f");
    EndProperty();
    return c;
}
static bool SliderProp(const char* label, float& v, float mn, float mx) {
    BeginProperty(label);
    bool c = Insp::SliderFloat("##s", &v, mn, mx, "%.2f");
    EndProperty();
    return c;
}
static bool ColorProp(const char* label, glm::vec3& c) {
    BeginProperty(label);
    bool r = EditorIcons::ColorField("##c",glm::value_ptr(c),3);
    EndProperty();
    return r;
}
static bool BoolProp(const char* label, bool& b) {
    BeginProperty(label);
    bool r = ImGui::Checkbox("##b", &b);
    EndProperty();
    return r;
}

// Collapsible component section with a context menu to remove it
template<typename T, typename Fn>
static void ComponentSection(Scene& scene, EntityID e, const char* name, bool removable, Fn&& body) {
    auto& reg=scene.Reg();T* comp=reg.TryGet<T>(e);if(!comp)return;ImGui::PushID(name);bool remove=false;bool enabled=reg.IsEnabled<T>(e),wasEnabled=enabled;
    bool open=EditorIcons::ComponentHeader(name,ToggleableComponent<T>?&enabled:nullptr,removable,remove);
    if(enabled!=wasEnabled){reg.SetEnabled<T>(e,enabled);VisitToggleComponents([&]<typename C>(int type,const char*){if constexpr(std::is_same_v<T,C>)scene.RefreshComponentState(e,type);});}
    if(open){ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{6,3});body(*comp);ImGui::PopStyleVar();ImGui::Spacing();}
    ImGui::PopID();if(remove){reg.Remove<T>(e);VisitToggleComponents([&]<typename C>(int type,const char*){if constexpr(std::is_same_v<T,C>)scene.RefreshComponentState(e,type);});}
}

// ------------------------------------------------------------------------------------------------ hierarchy

void EditorApp::DrawEntityContextItems() {
    Scene& scene = ActiveScene();
    if (ImGui::MenuItem("Create Empty")) m_Selected = scene.CreateEntity("GameObject");
    if (ImGui::BeginMenu("3D Object")) {
        for (u32 t = 0; t < (u32)PrimitiveType::Count; ++t)
            if (ImGui::MenuItem(PrimitiveName((PrimitiveType)t))) {
                m_Selected = scene.CreatePrimitive((PrimitiveType)t);
                scene.Reg().Get<TransformComponent>(m_Selected).position = m_Camera.Position() + m_Camera.Forward() * 6.0f;
            }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Light")) {
        if (ImGui::MenuItem("Directional Light")) {
            m_Selected = scene.CreateEntity("Directional Light");
            scene.Reg().Add<DirectionalLightComponent>(m_Selected);
            scene.Reg().Get<TransformComponent>(m_Selected).SetEuler({ -45, 30, 0 });
        }
        if (ImGui::MenuItem("Point Light")) {
            m_Selected = scene.CreateEntity("Point Light");
            scene.Reg().Add<PointLightComponent>(m_Selected);
            scene.Reg().Get<TransformComponent>(m_Selected).position = m_Camera.Position() + m_Camera.Forward() * 6.0f;
        }
        if (ImGui::MenuItem("Spot Light")) {
            m_Selected = scene.CreateEntity("Spot Light");
            scene.Reg().Add<SpotLightComponent>(m_Selected);
            auto& t = scene.Reg().Get<TransformComponent>(m_Selected);
            t.position = m_Camera.Position() + m_Camera.Forward() * 6.0f;
            t.SetEuler({ -90, 0, 0 });
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Audio")) {
        if (ImGui::MenuItem("Audio Source")) { m_Selected = scene.CreateEntity("Audio Source"); scene.Reg().Add<AudioSourceComponent>(m_Selected); }
        if (ImGui::MenuItem("Audio Listener")) { m_Selected = scene.CreateEntity("Audio Listener"); scene.Reg().Add<AudioListenerComponent>(m_Selected); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI")) {
        if (ImGui::MenuItem("Canvas")) { m_Selected = scene.CreateEntity("Canvas"); scene.Reg().Add<CanvasComponent>(m_Selected); }
        for (const char* kind : {"Image", "Text", "Button", "InputField"}) if (ImGui::MenuItem(kind)) {
            EntityID parent = m_Selected;
            EntityID canvas = parent;
            while (canvas && !scene.Reg().Has<CanvasComponent>(canvas)) canvas = scene.GetParent(canvas);
            if (!canvas) { canvas = scene.CreateEntity("Canvas"); scene.Reg().Add<CanvasComponent>(canvas); parent = canvas; }
            m_Selected = scene.CreateEntity(kind); scene.SetParent(m_Selected, parent, false);
            scene.Reg().Add<RectTransformComponent>(m_Selected);
            if (std::string(kind) != "Text") scene.Reg().Add<UIImageComponent>(m_Selected);
            if (std::string(kind) != "Image" && std::string(kind)!="InputField") scene.Reg().Add<UITextComponent>(m_Selected).text = kind;
            if(std::string(kind)=="InputField")scene.Reg().Add<UIInputFieldComponent>(m_Selected);
            if (std::string(kind) == "Button") scene.Reg().Add<UIButtonComponent>(m_Selected);
        }
        ImGui::EndMenu();
    }
    if(ImGui::MenuItem("Water")){auto root=scene.InstantiatePrefab("Packages/com.stardust.core/Prefabs/Water.prefab");if(root)m_Selected=root;}
    if (ImGui::MenuItem("Camera")) {
        m_Selected = scene.CreateEntity("Camera");
        scene.Reg().Add<CameraComponent>(m_Selected).primary = false;
    }
    if (ImGui::MenuItem("Environment")) {
        m_Selected = scene.CreateEntity("Environment");
        scene.Reg().Add<EnvironmentComponent>(m_Selected);
    }
    if (ImGui::MenuItem("Terrain")) {
        m_Selected = scene.CreateEntity("Terrain");
        scene.Reg().Add<TerrainComponent>(m_Selected);
    }
}

// Deferred hierarchy edits: the tree must not change while ImGui is iterating it
struct HierarchyOps {
    EntityID toDelete = kNullEntity;
    EntityID dragged = kNullEntity, newParent = kNullEntity;
    int insertIndex = -1;
    bool reparent = false;
};
static HierarchyOps s_Ops;

static void HandleDropTarget(Scene& scene, EntityID target) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("STARDUST_ENTITY",ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();float y=ImGui::GetMousePos().y,h=max.y-min.y;if(y<min.y+h*.25f||y>max.y-h*.25f){float line=y<min.y+h*.25f?min.y:max.y;ImGui::GetWindowDrawList()->AddLine({min.x,line},{max.x,line},IM_COL32(90,180,255,255),2);}
        if(!payload->IsDelivery()){ImGui::EndDragDropTarget();return;}
        EntityID dragged = *(const EntityID*)payload->Data;
        if (dragged != target && !scene.IsDescendantOf(target, dragged)) {
            // Upper/lower quarter of the row: insert as sibling before/after. Middle: make it a child.
            float y = ImGui::GetMousePos().y;
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            float h = mx.y - mn.y;
            EntityID parent = scene.GetParent(target);
            auto siblings = parent ? scene.GetChildren(parent) : scene.GetRoots();
            int idx = (int)(std::find(siblings.begin(), siblings.end(), target) - siblings.begin());
            if(scene.GetParent(dragged)==parent){auto before=std::find(siblings.begin(),siblings.end(),dragged);if(before!=siblings.end()&&before<siblings.begin()+idx)--idx;}
            s_Ops.dragged = dragged;
            s_Ops.reparent = true;
            if (y < mn.y + h * 0.25f) { s_Ops.newParent = parent; s_Ops.insertIndex = idx; }
            else if (y > mx.y - h * 0.25f) { s_Ops.newParent = parent; s_Ops.insertIndex = idx + 1; }
            else { s_Ops.newParent = target; s_Ops.insertIndex = -1; }
        }
    }
    ImGui::EndDragDropTarget();
}

void EditorApp::DrawEntityNode(EntityID e) {
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    auto& n = reg.Get<NameComponent>(e);
    const auto children = scene.GetChildren(e);

    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) f |= ImGuiTreeNodeFlags_Leaf;
    m_HierarchyRows.push_back(e);
    if (std::find(m_EntitySelection.begin(),m_EntitySelection.end(),e)!=m_EntitySelection.end()) f |= ImGuiTreeNodeFlags_Selected;
    // keep the path to the selection expanded
    if (m_Selected && scene.Reg().Valid(m_Selected) && scene.IsDescendantOf(m_Selected, e)) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    bool inactive = !scene.IsActiveInHierarchy(e);
    bool prefab=reg.Has<PrefabInstanceComponent>(e)||!n.prefab.empty();
    for(EntityID p=scene.GetParent(e);p&&!prefab;p=scene.GetParent(p))prefab=reg.Has<PrefabInstanceComponent>(p);
    if(prefab)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.35f,.65f,1,1));
    if (inactive) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = ImGui::TreeNodeEx((void*)(uintptr_t)e, f, "     %s", n.name.c_str());
    auto iconMin=ImGui::GetItemRectMin();float x=iconMin.x+ImGui::GetTreeNodeToLabelSpacing()+7,y=iconMin.y+ImGui::GetFrameHeight()*.5f-3;auto* dl=ImGui::GetWindowDrawList();ImU32 col=prefab?IM_COL32(90,165,245,255):inactive?IM_COL32(110,110,110,255):IM_COL32(185,190,195,255);
    if(reg.Has<BoneComponent>(e)){dl->AddLine({x-4,y+4},{x+4,y-4},col,2);dl->AddCircle({x-4,y+4},2,col);dl->AddCircle({x+4,y-4},2,col);}
    else if(reg.Has<CameraComponent>(e)){dl->AddRect({x-6,y-4},{x+3,y+4},col);dl->AddTriangle({x+3,y},{x+7,y-4},{x+7,y+4},col);}
    else if(reg.Has<DirectionalLightComponent>(e)||reg.Has<PointLightComponent>(e)||reg.Has<SpotLightComponent>(e)){dl->AddCircle({x,y},3,IM_COL32(230,200,100,255));for(int i=0;i<8;++i){float a=i*glm::pi<float>()/4;dl->AddLine({x+std::cos(a)*5,y+std::sin(a)*5},{x+std::cos(a)*7,y+std::sin(a)*7},col);}}
    else if(reg.Has<TerrainComponent>(e)){dl->AddTriangle({x-7,y+5},{x-1,y-5},{x+5,y+5},IM_COL32(130,190,125,255));dl->AddLine({x+1,y-1},{x+4,y-4},col);dl->AddLine({x+4,y-4},{x+8,y+5},col);}
    else if(reg.Has<CanvasComponent>(e)||reg.Has<RectTransformComponent>(e)){dl->AddRect({x-6,y-5},{x+6,y+5},IM_COL32(125,195,220,255));dl->AddLine({x-3,y-2},{x+3,y-2},col);}
    else{ImVec2 points[]={{x,y-7},{x+6,y-3},{x+6,y+4},{x,y+7},{x-6,y+4},{x-6,y-3}};dl->AddPolyline(points,6,col,ImDrawFlags_Closed,1);dl->AddLine({x-6,y-3},{x,y},col);dl->AddLine({x+6,y-3},{x,y},col);dl->AddLine({x,y},{x,y+7},col);}

    if (inactive) ImGui::PopStyleColor();
    if(prefab)ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragPastThreshold(0) && !ImGui::IsItemToggledOpen()) { SelectHierarchy(e); }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { m_Selected = e; FocusSelected(); }

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("STARDUST_ENTITY", &e, sizeof(EntityID));
        ImGui::Text("%s", n.name.c_str());
        ImGui::EndDragDropSource();
    }
    HandleDropTarget(scene, e);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)) {
            std::string asset((const char*)p->Data);
            if (!HandleAssetDropOnEntity(e, asset)) { EntityID c = CreateFromAsset(asset, e, nullptr); if (c) m_Selected = c; }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if(std::find(m_EntitySelection.begin(),m_EntitySelection.end(),e)==m_EntitySelection.end()){m_EntitySelection={e};m_Selected=e;}
        if(ImGui::MenuItem("Copy","Ctrl+C"))CopyEntities();
        if(ImGui::MenuItem("Paste","Ctrl+V",false,!m_EntityClipboard.empty()))PasteEntities();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateEntities();
        if (ImGui::MenuItem("Delete", "Del")) s_Ops.toDelete = e;
        if (scene.GetParent(e) && ImGui::MenuItem("Unparent")) { s_Ops.dragged = e; s_Ops.newParent = kNullEntity; s_Ops.insertIndex = -1; s_Ops.reparent = true; }
        if (ImGui::MenuItem("Create Prefab")) {
            namespace fs = std::filesystem;
            fs::path dir = Project::AssetsDir() / "Prefabs";
            fs::create_directories(dir);
            fs::path p = dir / (n.name + ".prefab");
            if (SceneSerializer::SavePrefab(scene, e, p)) SD_INFO("Saved prefab {}", p.string());
        }
        if (ImGui::MenuItem("Create Empty Child")) {
            EntityID c = scene.CreateEntity("GameObject");
            scene.SetParent(c, e, false);
            m_Selected = c;
        }
        if (ImGui::BeginMenu("Create")) {
            auto before=m_Selected;auto oldRoots=scene.GetRoots();
            DrawEntityContextItems();
            if(m_Selected!=before) {for(auto root:scene.GetRoots())if(std::find(oldRoots.begin(),oldRoots.end(),root)==oldRoots.end()&&root!=e)scene.SetParent(root,e,false);}
            ImGui::EndMenu();
        }
        auto parent=scene.GetParent(e);auto siblings=parent?scene.GetChildren(parent):scene.GetRoots();int index=(int)(std::find(siblings.begin(),siblings.end(),e)-siblings.begin());
        auto move=[&](int at){s_Ops.dragged=e;s_Ops.newParent=parent;s_Ops.insertIndex=at;s_Ops.reparent=true;};
        ImGui::Separator();if(ImGui::MenuItem("Move Up",nullptr,false,index>0))move(index-1);if(ImGui::MenuItem("Move Down",nullptr,false,index+1<(int)siblings.size()))move(index+1);if(ImGui::MenuItem("Set as First Sibling"))move(0);if(ImGui::MenuItem("Set as Last Sibling"))move(-1);
        ImGui::EndPopup();
    }

    if (open) {
        for (EntityID c : std::vector<EntityID>(children)) DrawEntityNode(c);
        ImGui::TreePop();
    }
}

void EditorApp::DrawHierarchy() {
    if (!ImGui::Begin("Hierarchy", &m_ShowHierarchy)) { ImGui::End(); return; }
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    s_Ops = {};
    m_PreviousHierarchyRows=m_HierarchyRows;m_HierarchyRows.clear();
    std::erase_if(m_EntitySelection,[&](auto e){return !reg.Valid(e);});
    if(m_Selected&&std::find(m_EntitySelection.begin(),m_EntitySelection.end(),m_Selected)==m_EntitySelection.end())m_EntitySelection={m_Selected};
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput){auto& io=ImGui::GetIO();if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_A)){m_EntitySelection=reg.AllEntities();if(!m_EntitySelection.empty())m_Selected=m_EntitySelection.back();}if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_C))CopyEntities();if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_V))PasteEntities();}


    if (!m_PrefabAsset.empty()) {
        if (ImGui::Button("<##prefabBack")) m_PrefabClosePending = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save and return to the scene");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(FileSystem::Utf8(m_PrefabAsset.stem()).c_str());
        ImGui::Separator();
    }
    if (ImGui::Button("+")) ImGui::OpenPopup("CreateMenu");
    if (ImGui::BeginPopup("CreateMenu")) { DrawEntityContextItems(); ImGui::EndPopup(); }
    ImGui::SameLine();
    static std::string filter;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search...", &filter);
    ImGui::Separator();

    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.24f,.24f,.24f,1));
    bool rootOpen = ImGui::TreeNodeEx("##sceneRoot", rootFlags|ImGuiTreeNodeFlags_Framed, "Scene  %s%s", scene.Name().c_str(),m_SceneDirty?" *":"");ImGui::PopStyleColor();
    // Dropping on the scene row moves the object back to the root
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("STARDUST_ENTITY")) {
            s_Ops.dragged = *(const EntityID*)payload->Data;
            s_Ops.newParent = kNullEntity;
            s_Ops.insertIndex = -1;
            s_Ops.reparent = true;
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)) {
            glm::vec3 pos = m_Camera.Position() + m_Camera.Forward() * 6.0f;
            EntityID c = CreateFromAsset((const char*)p->Data, kNullEntity, &pos);
            if (c) m_Selected = c;
        }
        ImGui::EndDragDropTarget();
    }
    if (rootOpen) {
        if (filter.empty()) {
            for (EntityID e : scene.GetRoots()) DrawEntityNode(e);
        } else {
            // flat list of matches while searching
            for (EntityID e : reg.AllEntities()) {
                auto& n = reg.Get<NameComponent>(e);
                if (n.name.find(filter) == std::string::npos) continue;
                m_HierarchyRows.push_back(e);if (ImGui::Selectable(n.name.c_str(), std::find(m_EntitySelection.begin(),m_EntitySelection.end(),e)!=m_EntitySelection.end())) SelectHierarchy(e);
            }
        }
        ImGui::TreePop();
    }

    if (s_Ops.reparent && reg.Valid(s_Ops.dragged)) {
        scene.SetParent(s_Ops.dragged, s_Ops.newParent, true, s_Ops.insertIndex);
        m_Selected = s_Ops.dragged;
    }
    if (s_Ops.toDelete) DeleteEntities();
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()){m_Selected=0;m_EntitySelection.clear();}

    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if(ImGui::MenuItem("Paste","Ctrl+V",false,!m_EntityClipboard.empty()))PasteEntities();
        DrawEntityContextItems();
        ImGui::EndPopup();
    }
    ImGui::End();
}

static bool ReferenceField(Scene& scene,const std::string& type,nlohmann::json& value,bool allowAssets=true);

// ------------------------------------------------------------------------------------------------ inspector

static void DrawEnvironmentSettings(EnvironmentComponent& env) {
        ImGui::SeparatorText("Skybox");Insp::Combo("Mode",&env.skyMode,"Procedural (Default)\0Solid Colour\0Material / Panorama\0Six Sided\0");
        if(env.skyMode==1)Insp::ColorEdit4("Background",glm::value_ptr(env.skyColor));
        if(env.skyMode==2)AssetUI::AssetField("Sky Material",env.skyMaterial,{".mat"});
        if(env.skyMode==3){const char* faces[]={"Right (+X)","Left (-X)","Top (+Y)","Bottom (-Y)","Front (+Z)","Back (-Z)"};for(int i=0;i<6;++i)AssetUI::AssetField(faces[i],env.skyFaces[i],{".png",".jpg",".jpeg",".tga",".bmp"});}
        if(env.skyMode>=2){FloatProp("Rotation",env.skyRotation,.5f);FloatProp("Sky Exposure (EV)",env.skyExposure,.05f,-10,10);}
        ImGui::SeparatorText("Lighting");
        SliderProp("Shadow Strength",env.shadowStrength,0,1);FloatProp("Minimum Ambient",env.minimumAmbient,.01f,0,1);
          FloatProp("Exposure", env.exposure, 0.01f, 0.01f, 10.0f);
        FloatProp("Ambient", env.ambientIntensity, 0.01f, 0.0f, 5.0f);
        ColorProp("Ambient Sky",env.ambientSky);ColorProp("Ambient Horizon",env.ambientHorizon);ColorProp("Ambient Ground",env.ambientGround);
        ImGui::SeparatorText("Sky & Weather");
        SliderProp("Cloud Coverage", env.cloudCoverage, 0.0f, 1.0f);
        SliderProp("Cloud Density", env.cloudDensity, 0.05f, 3.0f);
        BeginProperty("Fog Density");
        Insp::DragFloat("##fd", &env.fogDensity, 0.00001f, 0.0f, 0.01f, "%.5f");
        EndProperty();
        BeginProperty("Fog Falloff");
        Insp::DragFloat("##ff", &env.fogHeightFalloff, 0.0001f, 0.0f, 0.05f, "%.4f");
        EndProperty();
        SliderProp("Wind Direction", env.windDirectionDeg, 0.0f, 360.0f);
        SliderProp("Wind Strength", env.windStrength, 0.0f, 2.0f);
        ImGui::SeparatorText("Post Processing");
        SliderProp("Bloom", env.bloomIntensity, 0.0f, 0.3f);
        SliderProp("Contrast", env.contrast, 0.8f, 1.4f);
        SliderProp("Saturation", env.saturation, 0.0f, 2.0f);
        SliderProp("Vignette", env.vignette, 0.0f, 1.5f);
        BoolProp("FXAA", env.fxaa);
}

void EditorApp::DrawInspector() {
    if (!ImGui::Begin("Inspector", &m_ShowInspector)) { ImGui::End(); return; }
    Scene& inspectScene=m_AnimationRecording&&m_AnimationPreviewScene?*m_AnimationPreviewScene:ActiveScene();
    Registry& reg = inspectScene.Reg();
    EntityID e = m_Selected;
    if (m_InspectAsset && !m_SelectedAsset.empty()) {
        DrawAssetInspector();
        ImGui::End();
        return;
    }
    if (!reg.Valid(e)) {
        ImGui::TextDisabled("Select a GameObject to inspect it.");
        ImGui::End();
        return;
    }

    if(m_EntitySelection.size()>1&&ImGui::CollapsingHeader("Multiple Objects",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Text("%zu objects selected",m_EntitySelection.size());
        auto apply=[&](auto operation){for(auto target:m_EntitySelection)if(reg.Valid(target))operation(target);};
        bool active=reg.Get<NameComponent>(e).active;if(Insp::Checkbox("Active (all)",&active))apply([&](auto t){reg.Get<NameComponent>(t).active=active;});
        int layer=reg.Get<NameComponent>(e).layer;if(Insp::SliderInt("Layer (all)",&layer,0,31))apply([&](auto t){reg.Get<NameComponent>(t).layer=layer;});
        auto transform=reg.Get<TransformComponent>(e);if(Vec3Control("Position (all)",transform.position))apply([&](auto t){reg.Get<TransformComponent>(t).position=transform.position;});if(Vec3Control("Rotation (all)",transform.eulerHint))apply([&](auto t){reg.Get<TransformComponent>(t).SetEuler(transform.eulerHint);});if(Vec3Control("Scale (all)",transform.scale,1))apply([&](auto t){reg.Get<TransformComponent>(t).scale=transform.scale;});
        static std::string material;if(AssetUI::AssetField("Material (all meshes)",material,{".mat"}))apply([&](auto t){if(auto* mesh=reg.TryGet<MeshRendererComponent>(t)){mesh->materialPath=material;mesh->materials.assign(mesh->materials.size(),material);}});
        bool scriptsEnabled=true;apply([&](auto t){if(auto* scripts=reg.TryGet<ScriptComponent>(t))for(size_t i=0;i<scripts->Count();++i)scriptsEnabled&=scripts->Slot(i).enabled;});if(Insp::Checkbox("Scripts Enabled (all)",&scriptsEnabled))apply([&](auto t){if(auto* scripts=reg.TryGet<ScriptComponent>(t))for(size_t i=0;i<scripts->Count();++i)scripts->Slot(i).enabled=scriptsEnabled;});
    }
    auto& name = reg.Get<NameComponent>(e);
    Insp::Checkbox("##active", &name.active);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    Insp::InputText("##name", &name.name);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Tag");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    if (Insp::BeginCombo("##tag", name.tag.c_str())) {
        if (Project::IsLoaded())
            for (auto& t : Project::Config().tags)
                if (ImGui::Selectable(t.c_str(), t == name.tag)) name.tag = t;
        ImGui::EndCombo();
    }
    if (!name.prefab.empty()) { ImGui::SameLine(); ImGui::TextDisabled("Prefab: %s", name.prefab.c_str()); }
    ImGui::TextDisabled("UUID %016llx", (unsigned long long)reg.Get<IDComponent>(e).uuid);
    ImGui::Separator();

    ComponentSection<PrefabInstanceComponent>(inspectScene,e,"Prefab Instance",false,[&](auto& c){ImGui::TextWrapped("%s",c.asset.c_str());
        auto action=[&](const char* name,int value){if(ImGui::Button(name)){m_PrefabTarget=reg.Get<IDComponent>(e).uuid;m_PrefabAction=value;}};
        action("Open Prefab",1);ImGui::SameLine();action("Apply to Prefab",3);action("Revert Overrides",2);ImGui::SameLine();action("Unpack",4);
    });
    if(!reg.Has<RectTransformComponent>(e)) ComponentSection<TransformComponent>(inspectScene, e, "Transform", false, [&](TransformComponent& t) {
        auto previous=t;
        Vec3Control("Position", t.position);
        glm::vec3 euler = t.eulerHint;
        if (Vec3Control("Rotation", euler, 0.0f, 0.5f)) t.SetEuler(euler);
        Vec3Control("Scale", t.scale, 1.0f);
        if(m_AnimationRecording&&(previous.position!=t.position||previous.eulerHint!=t.eulerHint||previous.scale!=t.scale))m_AnimationTransformChanged=true;
    });

    ComponentSection<MeshRendererComponent>(inspectScene, e, "Mesh Renderer", true, [&](MeshRendererComponent& mr) {
        AssetUI::MeshRendererInspector(mr);if(mr.mesh.ends_with(".fbx")&&!reg.Has<FbxRigComponent>(e)&&ImGui::Button("Import Bone Hierarchy"))ModelSkeleton::Import(ActiveScene(),e,mr.mesh);
        auto asset=AssetUI::TakeAssetNavigation();if(!asset.empty()){m_SelectedAsset=asset;m_ProjectCurrentDir=AssetManager::ToAbsolute(asset).parent_path();m_InspectAsset=true;}
    });

    auto& layer=reg.Get<NameComponent>(e).layer;auto& layerNames=Project::Config().layers;if(Insp::BeginCombo("Layer",(layer<(int)layerNames.size()&&!layerNames[layer].empty()?layerNames[layer]:std::to_string(layer)).c_str())){for(int i=0;i<32;++i)if(ImGui::Selectable((std::to_string(i)+" "+(i<(int)layerNames.size()?layerNames[i]:"")).c_str(),layer==i))layer=i;ImGui::EndCombo();}
    ComponentSection<VideoPlayerComponent>(inspectScene,e,"Video Player",true,[](auto& c){AssetUI::AssetField("Video Clip (MPEG-1)",c.clip,{".mpg",".mpeg"});Insp::InputText("Target Texture",&c.targetTexture);Insp::Checkbox("Play On Start",&c.playOnStart);Insp::Checkbox("Loop",&c.loop);Insp::Checkbox("Playing",&c.playing);Insp::DragFloat("Speed",&c.speed,.05f,0,8);Insp::SliderFloat("Time",&c.time,0,c.duration);ImGui::TextDisabled("Attach Rect Transform to show video in a Canvas. Audio uses a separate Audio Source.");});
    ComponentSection<SpriteRendererComponent>(inspectScene,e,"Sprite Renderer",true,[](auto& c){AssetUI::AssetField("Sprite",c.sprite,{".sprite",".png",".jpg"});AssetUI::AssetField("Material",c.material,{".mat"});Insp::ColorEdit4("Color",&c.color.x);Insp::DragFloat("Pixels Per Unit",&c.pixelsPerUnit,1,.01f,100000);Insp::Checkbox("Flip X",&c.flipX);Insp::Checkbox("Flip Y",&c.flipY);Insp::InputInt("Sorting Order",&c.sortingOrder);});
    ComponentSection<CameraComponent>(inspectScene, e, "Camera", true, [](CameraComponent& c) {
        ImGui::InputScalar("Culling Mask (hex)",ImGuiDataType_U32,&c.cullingMask,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal);
        AssetUI::AssetField("Target Texture",c.targetTexture,{".rendertexture"});Insp::InputInt("Target Width",&c.targetWidth);Insp::InputInt("Target Height",&c.targetHeight);
        BoolProp("Orthographic (2D)",c.orthographic);if(c.orthographic)FloatProp("Size",c.orthographicSize,.1f,.01f,100000);else SliderProp("Field of View", c.fovDegrees, 10.0f, 120.0f);FloatProp("Far Clip",c.farPlane,1,c.nearPlane+.1f,1000000);
        FloatProp("Near Clip", c.nearPlane, 0.01f, 0.01f, 10.0f);
        BoolProp("Primary", c.primary);
    });

    ComponentSection<DirectionalLightComponent>(inspectScene, e, "Directional Light", true, [](DirectionalLightComponent& l) {
        ColorProp("Color", l.color);
        FloatProp("Intensity", l.intensity, 0.05f, 0.0f, 100.0f);
        BoolProp("Cast Shadows", l.castShadows);
        FloatProp("Shadow Distance", l.shadowDistance, 0.5f, 5.0f, 2000.0f);
    });

    ComponentSection<PointLightComponent>(inspectScene, e, "Point Light", true, [](PointLightComponent& l) {
        ColorProp("Color", l.color);
        FloatProp("Intensity", l.intensity, 0.1f, 0.0f, 10000.0f);
        FloatProp("Range", l.range, 0.05f, 0.1f, 1000.0f);
    });
    ComponentSection<SpotLightComponent>(inspectScene, e, "Spot Light", true, [](SpotLightComponent& l) {
        ColorProp("Color", l.color);
        FloatProp("Intensity", l.intensity, 0.1f, 0.0f, 10000.0f);
        FloatProp("Range", l.range, 0.05f, 0.1f, 1000.0f);
        SliderProp("Inner Angle", l.innerAngle, 1.0f, 89.0f);
        SliderProp("Outer Angle", l.outerAngle, 1.0f, 89.0f);
        if (l.innerAngle > l.outerAngle) l.innerAngle = l.outerAngle;
    });
    ComponentSection<AudioSourceComponent>(inspectScene, e, "Audio Source", true, [](AudioSourceComponent& a) {
        AssetUI::AssetField("Clip", a.clip, { ".wav", ".mp3", ".flac", ".ogg" });
        if (!a.clip.empty()) {
            if (ImGui::SmallButton("Preview")) AudioEngine::PreviewClip(a.clip);
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) AudioEngine::StopPreview();
        }
        SliderProp("Volume", a.volume, 0.0f, 1.0f);
        SliderProp("Pitch", a.pitch, 0.1f, 3.0f);
        BoolProp("Loop", a.loop);
        BoolProp("Play On Start", a.playOnStart);
        BoolProp("Spatial (3D)", a.spatial);
        if (a.spatial) {
            FloatProp("Min Distance", a.minDistance, 0.05f, 0.01f, 1000.0f);
            FloatProp("Max Distance", a.maxDistance, 0.1f, 0.1f, 10000.0f);
        }
    });
    ComponentSection<AudioListenerComponent>(inspectScene, e, "Audio Listener", true, [](AudioListenerComponent&) {
        ImGui::TextDisabled("Receives 3D audio (usually on the main camera).");
    });

    ComponentSection<EnvironmentComponent>(inspectScene, e, "Environment", true, [](EnvironmentComponent& env) { DrawEnvironmentSettings(env); });

    ComponentSection<TerrainComponent>(inspectScene, e, "Terrain", true, [&](TerrainComponent& tc) { DrawTerrainInspector(e, tc); });
    ComponentSection<ProceduralTerrainComponent>(inspectScene,e,"Procedural Terrain Generator",true,[&](auto& c){DrawProceduralTerrainInspector(e,c);});
    ComponentSection<TerrainVegetationComponent>(inspectScene,e,"Terrain Vegetation",true,[&](auto& c){DrawTerrainVegetationInspector(inspectScene,e,c);});

    auto physMat = [](PhysicsMaterialProps& m) {
        SliderProp("Friction", m.friction, 0.0f, 1.5f);
        SliderProp("Bounciness", m.restitution, 0.0f, 1.0f);
        BoolProp("Is Trigger", m.isTrigger);
    };
    ComponentSection<RigidbodyComponent>(inspectScene, e, "Rigidbody", true, [](RigidbodyComponent& rb) {
        BeginProperty("Body Type");
        const char* types[] = { "Static", "Dynamic", "Kinematic" };
        int t = (int)rb.type;
        if (Insp::Combo("##bt", &t, types, 3)) rb.type = (BodyType)t;
        EndProperty();
        FloatProp("Mass", rb.mass, 0.05f, 0.001f, 100000.0f);
        SliderProp("Linear Damping", rb.linearDamping, 0.0f, 5.0f);
        SliderProp("Angular Damping", rb.angularDamping, 0.0f, 5.0f);
        BoolProp("Use Gravity", rb.useGravity);
        BoolProp("Freeze Rotation", rb.freezeRotation);
    });
    ComponentSection<BoxColliderComponent>(inspectScene, e, "Box Collider", true, [&](BoxColliderComponent& c) {
        Vec3Control("Size", c.size, 1.0f);
        Vec3Control("Center", c.center);
        physMat(c.material);
    });
    ComponentSection<SphereColliderComponent>(inspectScene, e, "Sphere Collider", true, [&](SphereColliderComponent& c) {
        FloatProp("Radius", c.radius, 0.01f, 0.001f, 1000.0f);
        Vec3Control("Center", c.center);
        physMat(c.material);
    });
    ComponentSection<CapsuleColliderComponent>(inspectScene, e, "Capsule Collider", true, [&](CapsuleColliderComponent& c) {
        FloatProp("Radius", c.radius, 0.01f, 0.001f, 1000.0f);
        FloatProp("Height", c.height, 0.01f, 0.001f, 1000.0f);
        Vec3Control("Center", c.center);
        physMat(c.material);
    });
    ComponentSection<MeshColliderComponent>(inspectScene, e, "Mesh Collider", true, [&](MeshColliderComponent& c) {
        BoolProp("Convex", c.convex);
        if (!c.convex) ImGui::TextDisabled("Non-convex mesh colliders are static only.");
        physMat(c.material);
    });
    ComponentSection<CanvasComponent>(inspectScene,e,"Canvas",true,[&](auto& c) {
        Insp::Combo("Render Mode",&c.renderMode,"Screen Space Overlay\0World Space\0Screen Space Camera\0");
        Insp::DragFloat2("Reference Resolution",glm::value_ptr(c.referenceResolution),1,1,8192); BoolProp("Scale with screen",c.scaleWithScreen); Insp::InputInt("Sorting Order",&c.sortingOrder); FloatProp("Pixels Per Unit",c.pixelsPerUnit,1,1,1000);if(c.renderMode==2)FloatProp("Plane Distance",c.planeDistance,.1f,.1f,1000);
    });
    ComponentSection<RectTransformComponent>(inspectScene,e,"Rect Transform",true,[&](auto& c){DrawRectInspector(e,c);});
    ComponentSection<UIImageComponent>(inspectScene,e,"UI Image",true,[&](auto& c) {
        FloatProp("Pixels Per Unit Multiplier",c.pixelsPerUnitMultiplier,.01f,.01f,100);AssetUI::AssetField("Sprite",c.sprite,{".sprite"});AssetUI::AssetField("Texture",c.texture,{".png",".jpg",".tga",".rendertexture"});AssetUI::AssetField("Material",c.material,{".mat"}); Insp::ColorEdit4("Tint",glm::value_ptr(c.color));
        Insp::Combo("Image Type",&c.type,"Simple\0Sliced\0Tiled\0Filled\0");Insp::Checkbox("Preserve Aspect",&c.preserveAspect);Insp::Checkbox("Raycast Target",&c.raycastTarget);
        if(c.type==3){Insp::SliderFloat("Fill Amount",&c.fillAmount,0,1);Insp::Combo("Fill Method",&c.fillMethod,"Horizontal\0Vertical\0");}
    });
    ComponentSection<UITextComponent>(inspectScene,e,"Text  [SDF]",true,[&](auto& c){DrawTextInspector(e,c);});
    ComponentSection<UIInputFieldComponent>(inspectScene,e,"Input Field",true,[&](auto& c){Insp::InputText("Text",&c.text);Insp::InputText("Placeholder",&c.placeholder);AssetUI::AssetField("Font",c.font,{".ttf",".otf"},"System Default");FloatProp("Font Size",c.fontSize,1,1,256);Insp::ColorEdit4("Text Colour",glm::value_ptr(c.color));Insp::ColorEdit4("Placeholder Colour",glm::value_ptr(c.placeholderColor));BoolProp("Interactable",c.interactable);BoolProp("Read Only",c.readOnly);BoolProp("Password",c.password);BoolProp("Multiline",c.multiline);Insp::DragInt("Character Limit",&c.characterLimit,1,0,65536);});
    ComponentSection<UIButtonComponent>(inspectScene,e,"Button  [On Click]",true,[&](auto& c){DrawButtonInspector(e,c);});
    ComponentSection<CanvasGroupComponent>(inspectScene,e,"Canvas Group",true,[&](auto& c){Insp::SliderFloat("Alpha",&c.alpha,0,1);Insp::Checkbox("Interactable",&c.interactable);Insp::Checkbox("Blocks Raycasts",&c.blocksRaycasts);});
    ComponentSection<UILayoutGroupComponent>(inspectScene,e,"Layout Group",true,[&](auto& c){Insp::Combo("Layout",&c.mode,"Horizontal\0Vertical\0Grid\0");Insp::DragFloat2("Spacing",glm::value_ptr(c.spacing),1,0,100);Insp::DragFloat4("Padding L/B/R/T",glm::value_ptr(c.padding),1,0,1000);if(c.mode==2){Insp::DragInt("Columns",&c.columns,1,1,100);Insp::DragFloat2("Cell Size",glm::value_ptr(c.cellSize),1,1,1000);}Insp::Combo("Start Corner",&c.startCorner,"Upper Left\0Upper Right\0Lower Left\0Lower Right\0");if(c.mode==2)Insp::Combo("Start Axis",&c.startAxis,"Horizontal\0Vertical\0");Insp::Combo("Child Alignment",&c.childAlignment,"Upper Left\0Upper Center\0Upper Right\0Middle Left\0Middle Center\0Middle Right\0Lower Left\0Lower Center\0Lower Right\0");Insp::Checkbox("Fit Content",&c.fitContent);});
    ComponentSection<RectMaskComponent>(inspectScene,e,"Rect Mask 2D",true,[&](auto&){ImGui::TextDisabled("Clips child graphics to this rectangle");});
    ComponentSection<UIMaskComponent>(inspectScene,e,"Mask",true,[&](auto& c){Insp::Checkbox("Show Mask Graphic",&c.showGraphic);if(!reg.Has<UIImageComponent>(e))ImGui::TextColored({1,.65f,.3f,1},"Needs an Image on this object to follow its shape.");else ImGui::TextDisabled("Children follow the alpha of this Image");});
    ComponentSection<FbxRigComponent>(inspectScene,e,"FBX Skeleton",false,[&](auto& c){ImGui::TextWrapped("%s",c.source.c_str());int count=0;reg.Each<BoneComponent>([&](EntityID,BoneComponent& bone){if(bone.root==reg.Get<IDComponent>(e).uuid)++count;});ImGui::Text("Bones: %d",count);});
    ComponentSection<BoneComponent>(inspectScene,e,"Bone",false,[&](auto& c){ImGui::Text("Imported node: %u",c.nodeId);ImGui::TextDisabled("Use Transform to pose this bone or attach children.");});
    ComponentSection<AnimatorComponent>(inspectScene,e,"Animator",true,[&](auto& c) { AssetUI::AssetField("Controller",c.controller,{".controller"});AssetUI::AssetField("Avatar",c.avatar,{".avatar"}); Insp::InputText("State",&c.state); FloatProp("Speed",c.speed); BoolProp("Playing",c.playing);if(ImGui::Button("Open Animator Window")){m_ShowAnimatorEditor=true;m_AnimatorFocusRequest=true;}ImGui::SameLine();if(ImGui::Button("Open Animation Window")){m_ShowAnimationEditor=true;m_AnimationFocusRequest=true;}if(m_State==PlayState::Edit){if(ImGui::Button(m_AnimationPreviewScene?"Stop Preview":"Preview in Editor")){if(m_AnimationPreviewScene)StopAnimationPreview();else StartAnimationPreview(e);}if(m_AnimationPreviewScene){Insp::Checkbox("Preview Playing",&m_AnimationPreviewPlaying);Insp::DragFloat("Preview Time",&m_AnimationPreviewTime,.01f,0,36000);}} });
    if (auto* scripts = reg.TryGet<ScriptComponent>(e)) {
        int remove = -1;
        for (size_t i = 0; i < scripts->Count(); ++i) {
            ImGui::PushID((int)i);
            auto& slot = scripts->Slot(i);
            bool removeSlot=false;bool expanded=EditorIcons::ComponentHeader((slot.className+" (Script)").c_str(),&slot.enabled,true,removeSlot);if(removeSlot)remove=(int)i;
            auto beforeFields=nlohmann::json::parse(slot.fieldsJson,nullptr,false);
            if (expanded) DrawScriptInspector(e, slot, i);
            if(m_EntitySelection.size()>1&&beforeFields.is_object()){
                auto fields=nlohmann::json::parse(slot.fieldsJson,nullptr,false);if(fields.is_object()&&fields!=beforeFields)for(auto target:m_EntitySelection)if(target!=e)if(auto* others=reg.TryGet<ScriptComponent>(target))for(size_t index=0;index<others->Count();++index)if(others->Slot(index).className==slot.className){auto values=nlohmann::json::parse(others->Slot(index).fieldsJson,nullptr,false);if(!values.is_object())values=nlohmann::json::object();for(auto& [key,value]:fields.items())if(!beforeFields.contains(key)||beforeFields[key]!=value){values[key]=value;if(ActiveScene().IsRunning())ScriptEngine::SetInstanceField(target,key,value.dump(),index);}others->Slot(index).fieldsJson=values.dump();}
            }
            ImGui::PopID();
        }
        if (remove >= 0 && m_State == PlayState::Edit) {
            if (remove > 0) scripts->additional.erase(scripts->additional.begin() + remove - 1);
            else if (!scripts->additional.empty()) { static_cast<ScriptSlot&>(*scripts) = scripts->additional.front(); scripts->additional.erase(scripts->additional.begin()); }
            else reg.Remove<ScriptComponent>(e);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    float bw = 180.0f;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - bw) * 0.5f);
    if (ImGui::Button("Add Component", ImVec2(bw, 0))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        if (!reg.Has<MeshRendererComponent>(e) && ImGui::MenuItem("Mesh Renderer")) reg.Add<MeshRendererComponent>(e);
        if(!reg.Has<VideoPlayerComponent>(e)&&ImGui::MenuItem("Video Player"))reg.Add<VideoPlayerComponent>(e);
        if (!reg.Has<SpriteRendererComponent>(e) && ImGui::MenuItem("Sprite Renderer")) reg.Add<SpriteRendererComponent>(e);
        if (!reg.Has<CameraComponent>(e) && ImGui::MenuItem("Camera")) reg.Add<CameraComponent>(e);
        if (!reg.Has<DirectionalLightComponent>(e) && ImGui::MenuItem("Directional Light")) reg.Add<DirectionalLightComponent>(e);
        if (!reg.Has<EnvironmentComponent>(e) && ImGui::MenuItem("Environment")) reg.Add<EnvironmentComponent>(e);
        if (!reg.Has<TerrainComponent>(e) && ImGui::MenuItem("Terrain")) reg.Add<TerrainComponent>(e);
        if(!reg.Has<ProceduralTerrainComponent>(e)&&ImGui::MenuItem("Procedural Terrain Generator"))reg.Add<ProceduralTerrainComponent>(e);
        if(!reg.Has<TerrainVegetationComponent>(e)&&ImGui::MenuItem("Terrain Vegetation"))reg.Add<TerrainVegetationComponent>(e);
        if (!reg.Has<PointLightComponent>(e) && ImGui::MenuItem("Point Light")) reg.Add<PointLightComponent>(e);
        if (!reg.Has<SpotLightComponent>(e) && ImGui::MenuItem("Spot Light")) reg.Add<SpotLightComponent>(e);
        ImGui::SeparatorText("Audio");
        if (!reg.Has<AudioSourceComponent>(e) && ImGui::MenuItem("Audio Source")) reg.Add<AudioSourceComponent>(e);
        if (!reg.Has<AudioListenerComponent>(e) && ImGui::MenuItem("Audio Listener")) reg.Add<AudioListenerComponent>(e);
        ImGui::SeparatorText("Physics");
        if (!reg.Has<RigidbodyComponent>(e) && ImGui::MenuItem("Rigidbody")) reg.Add<RigidbodyComponent>(e);
        if (!reg.Has<BoxColliderComponent>(e) && ImGui::MenuItem("Box Collider")) reg.Add<BoxColliderComponent>(e);
        if (!reg.Has<SphereColliderComponent>(e) && ImGui::MenuItem("Sphere Collider")) reg.Add<SphereColliderComponent>(e);
        if (!reg.Has<CapsuleColliderComponent>(e) && ImGui::MenuItem("Capsule Collider")) reg.Add<CapsuleColliderComponent>(e);
        if (!reg.Has<MeshColliderComponent>(e) && ImGui::MenuItem("Mesh Collider")) reg.Add<MeshColliderComponent>(e);
        ImGui::SeparatorText("UI / Animation");
        if(!reg.Has<UILayoutGroupComponent>(e)&&ImGui::MenuItem("Layout Group"))reg.Add<UILayoutGroupComponent>(e);
        if(!reg.Has<CanvasGroupComponent>(e)&&ImGui::MenuItem("Canvas Group"))reg.Add<CanvasGroupComponent>(e);
        if(!reg.Has<RectMaskComponent>(e)&&ImGui::MenuItem("Rect Mask 2D"))reg.Add<RectMaskComponent>(e);
        if(!reg.Has<UIMaskComponent>(e)&&ImGui::MenuItem("Mask"))reg.Add<UIMaskComponent>(e);
        if (!reg.Has<CanvasComponent>(e) && ImGui::MenuItem("Canvas")) reg.Add<CanvasComponent>(e);
        if (!reg.Has<RectTransformComponent>(e) && ImGui::MenuItem("Rect Transform")) reg.Add<RectTransformComponent>(e);
        if (!reg.Has<UIImageComponent>(e) && ImGui::MenuItem("UI Image")) reg.Add<UIImageComponent>(e);
        if (!reg.Has<UITextComponent>(e) && ImGui::MenuItem("UI Text")) reg.Add<UITextComponent>(e);
        if(!reg.Has<UIInputFieldComponent>(e)&&ImGui::MenuItem("UI Input Field"))reg.Add<UIInputFieldComponent>(e);
        if (!reg.Has<UIButtonComponent>(e) && ImGui::MenuItem("UI Button")) reg.Add<UIButtonComponent>(e);
        if (!reg.Has<AnimatorComponent>(e) && ImGui::MenuItem("Animator")) reg.Add<AnimatorComponent>(e);
        ImGui::SeparatorText("Scripting");
        if (ImGui::BeginMenu("Script")) {
            if (ScriptEngine::Classes().empty()) ImGui::TextDisabled("No scripts (Scripts > Build)");
            for (auto& c : ScriptEngine::Classes())
                if (ImGui::MenuItem(c.name.c_str())) {
                    if (auto* scripts = reg.TryGet<ScriptComponent>(e)) scripts->additional.push_back({c.name, "{}"});
                    else reg.Add<ScriptComponent>(e, c.name);
                    if (ActiveScene().IsRunning()) ScriptEngine::Instantiate(e);
                }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    // Drop scripts / audio / materials anywhere in the Inspector to add them to the selected object
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(40.0f, ImGui::GetContentRegionAvail().y)));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload))
            HandleAssetDropOnEntity(e, (const char*)p->Data);
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------ script inspector

// Does entity e satisfy a reference field type ("Entity", "Component:Camera", "Script:FlyCamera")?
static bool MatchesReference(Registry& reg, EntityID e, const std::string& type) {
    if (type == "Entity") return true;
    if (type.starts_with("Script:")) {
        auto* sc = reg.TryGet<ScriptComponent>(e);
        if (!sc) return false;
        for (size_t i = 0; i < sc->Count(); ++i) if (sc->Slot(i).className == type.substr(7)) return true;
        return false;
    }
    std::string c = type.substr(10); // after "Component:"
    if (c == "Transform") return true;
    if (c == "Camera") return reg.Has<CameraComponent>(e);
    if (c == "MeshRenderer") return reg.Has<MeshRendererComponent>(e);
    if (c == "Rigidbody") return reg.Has<RigidbodyComponent>(e);
    if (c == "BoxCollider") return reg.Has<BoxColliderComponent>(e);
    if (c == "SphereCollider") return reg.Has<SphereColliderComponent>(e);
    if (c == "CapsuleCollider") return reg.Has<CapsuleColliderComponent>(e);
    if (c == "MeshCollider") return reg.Has<MeshColliderComponent>(e);
    if (c == "DirectionalLight") return reg.Has<DirectionalLightComponent>(e);
    if (c == "Light") return reg.Has<DirectionalLightComponent>(e) || reg.Has<PointLightComponent>(e) || reg.Has<SpotLightComponent>(e);
    if (c == "AudioSource") return reg.Has<AudioSourceComponent>(e);
    if (c == "Animator") return reg.Has<AnimatorComponent>(e);
    if (c == "Canvas") return reg.Has<CanvasComponent>(e);
    if (c == "RectTransform") return reg.Has<RectTransformComponent>(e);
    if (c == "Image") return reg.Has<UIImageComponent>(e);
    if (c == "Text") return reg.Has<UITextComponent>(e);
    if(c=="InputField")return reg.Has<UIInputFieldComponent>(e);
    if (c == "Button") return reg.Has<UIButtonComponent>(e);
    if(c=="CanvasGroup")return reg.Has<CanvasGroupComponent>(e);
    if(c=="RectMask2D")return reg.Has<RectMaskComponent>(e);
    if(c=="LayoutGroup")return reg.Has<UILayoutGroupComponent>(e);
    return false;
}

static bool MatchesPrefab(const std::string& path,const std::string& type){
 if(!path.ends_with(".prefab"))return false;
 struct Result{std::filesystem::file_time_type stamp{};bool matches=false;};static std::unordered_map<std::string,Result> results;
 auto absolute=AssetManager::ToAbsolute(path);std::error_code ec;auto stamp=std::filesystem::last_write_time(absolute,ec);if(ec)return false;auto key=FileSystem::Utf8(absolute)+"|"+type;auto found=results.find(key);if(found!=results.end()&&found->second.stamp==stamp)return found->second.matches;
 bool matches=false;try{auto scene=SceneSerializer::FromJson(FileSystem::ReadText(absolute));if(scene&&!scene->GetRoots().empty())matches=MatchesReference(scene->Reg(),scene->GetRoots()[0],type);}catch(...){}
 results[key]={stamp,matches};return matches;
}
static bool AssetReferenceField(const char* label,const std::string& kind,nlohmann::json& value){
 std::string path=value.is_object()?value.value("asset",std::string()):value.is_string()?value.get<std::string>():"";
 bool changed=false;
 static const std::unordered_map<std::string,std::vector<std::string>> kinds={
  {"Asset:RenderTexture",{".rendertexture"}},{"Asset:Sprite",{".sprite",".png",".jpg",".jpeg",".tga",".bmp"}},{"Asset:Prefab",{".prefab"}},
  {"Asset:Material",{".mat"}},{"Asset:Texture",{".png",".jpg",".jpeg",".tga",".bmp"}},{"Asset:Texture2D",{".png",".jpg",".jpeg",".tga",".bmp"}},
  {"Asset:AudioClip",{".wav",".mp3",".flac",".ogg"}},{"Asset:Font",{".ttf",".otf"}},{"Asset:Mesh",{".obj",".gltf",".glb",".fbx"}},
  {"Asset:AnimationClip",{".anim"}},{"Asset:TextAsset",{".txt",".json",".xml",".csv",".md",".yaml",".yml",".ini",".bytes"}},{"Asset:AnimatorController",{".controller"}}};
 if(auto found=kinds.find(kind);found!=kinds.end())changed=AssetUI::AssetField(label,path,found->second);
 else {ImGui::SetNextItemWidth(-1);changed=Insp::InputText(label,&path);if(ImGui::BeginDragDropTarget()){if(auto* p=ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)){path=(const char*)p->Data;changed=true;}ImGui::EndDragDropTarget();}}
 if(changed)value={{"asset",path}};return changed;
}

static std::string EntityIconName(Registry& reg, EntityID e) {
    if (reg.Has<CameraComponent>(e)) return "Camera";
    if (reg.Has<DirectionalLightComponent>(e) || reg.Has<PointLightComponent>(e) || reg.Has<SpotLightComponent>(e)) return "Light";
    if (reg.Has<AnimatorComponent>(e)) return "Animator";
    if (reg.Has<ScriptComponent>(e)) return "Script";
    if (reg.Has<AudioSourceComponent>(e)) return "Audio";
    if (reg.Has<TerrainComponent>(e)) return "Terrain";
    return "";
}

// Unity-style object slot: shows the referenced entity, accepts drag & drop from the Hierarchy, or pick from a list
static bool ReferenceField(Scene& scene, const std::string& type, nlohmann::json& v,bool allowAssets) {
    Registry& reg = scene.Reg();
    u64 uuid = 0;
    if (v.is_string()) uuid = std::strtoull(v.get<std::string>().c_str(), nullptr, 16);
    EntityID target = uuid ? scene.FindByUUID(uuid) : kNullEntity;
    std::string shortType = type.substr(type.find(':') + 1);
    std::string asset=v.is_object()?v.value("asset",std::string()):"";
    std::string label = !asset.empty()?asset+"  (Prefab: "+shortType+")":target ? reg.Get<NameComponent>(target).name + "  (" + shortType + ")"
                               : (uuid ? "Missing (" + shortType + ")" : "None (" + shortType + ")");
    bool changed = false;
    auto set = [&](EntityID e) { v = e ? std::format("{:016x}", reg.Get<IDComponent>(e).uuid) : std::string("0"); changed = true; };

    AssetUI::ObjectSlotDesc desc;
    desc.text = label;
    desc.typeName = shortType;
    desc.defaultTab = 1;
    desc.currentKey = !asset.empty() ? "asset:" + asset : target ? std::format("{:016x}", uuid) : std::string();
    if (target) { desc.iconItem.isEntity = true; desc.iconItem.icon = EntityIconName(reg, target); }
    else if (!asset.empty()) desc.iconItem.key = "asset:" + asset;
    if (!allowAssets) desc.assetsHint = "This field only accepts objects from the scene.";
    desc.assets = [&, allowAssets] {
        std::vector<AssetUI::PickerItem> items;
        if (allowAssets)
            for (auto& path : AssetManager::Find({ ".prefab" })) {
                if (!MatchesPrefab(path, type)) continue;
                AssetUI::PickerItem item;
                item.key = "asset:" + path;
                item.name = FileSystem::Utf8(std::filesystem::u8path(path).stem());
                item.detail = path;
                items.push_back(std::move(item));
            }
        return items;
    };
    desc.sceneItems = [&] {
        std::vector<AssetUI::PickerItem> items;
        for (EntityID c : reg.AllEntities()) {
            if (!MatchesReference(reg, c, type)) continue;
            AssetUI::PickerItem item;
            item.key = std::format("{:016x}", reg.Get<IDComponent>(c).uuid);
            item.name = reg.Get<NameComponent>(c).name;
            item.detail = item.name;
            for (EntityID p = scene.GetParent(c); p; p = scene.GetParent(p)) item.detail = reg.Get<NameComponent>(p).name + "/" + item.detail;
            item.isEntity = true;
            item.icon = EntityIconName(reg, c);
            items.push_back(std::move(item));
        }
        return items;
    };
    std::string picked;
    if (AssetUI::ObjectSlot(desc, picked)) {
        if (picked.empty()) set(kNullEntity);
        else if (picked.starts_with("asset:")) { v = { { "asset", picked.substr(6) } }; changed = true; }
        else { v = picked; changed = true; }
    }
    if (ImGui::BeginDragDropTarget()) {
        if(allowAssets)if(auto* payload=ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)){std::string path((const char*)payload->Data);if(MatchesPrefab(path,type)){v={{"asset",path}};changed=true;}else SD_WARN("Prefab root does not match {}",shortType);}
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STARDUST_ENTITY")) {
            EntityID dropped = *(const EntityID*)p->Data;
            if (reg.Valid(dropped) && MatchesReference(reg, dropped, type)) set(dropped);
            else SD_WARN("'{}' has no {}", reg.Valid(dropped) ? reg.Get<NameComponent>(dropped).name : "?", shortType);
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

// Edits the serialized field values (edit mode) or the live instance (play mode)
bool AssetUI::EntityField(Scene& scene,const std::string& type,nlohmann::json& value){return ReferenceField(scene,type,value,false);}
static nlohmann::json SchemaDefault(const nlohmann::json& schema){return schema.value("default",nlohmann::json());}
static bool ScriptValue(Scene& scene,const char* label,const nlohmann::json& schema,nlohmann::json& value,int depth=0){
 using json=nlohmann::json;bool changed=false;auto kind=schema.value("kind","");if(depth>8){ImGui::TextDisabled("Nesting limit");return false;}ImGui::PushID(label);
 if(kind=="List"||kind=="Dictionary"){
  if(!value.is_array()){value=json::array();changed=true;}if(ImGui::TreeNodeEx("##collection",ImGuiTreeNodeFlags_DefaultOpen,"%s  [%zu]",label,value.size())){
   if(kind=="List"){int size=(int)value.size();ImGui::SetNextItemWidth(100);if(Insp::DragInt("Size",&size,1,0,10000)){size=std::clamp(size,0,10000);while(value.size()<(size_t)size)value.push_back(SchemaDefault(schema["element"]));while(value.size()>(size_t)size)value.erase(value.end()-1);changed=true;}}
   int remove=-1,swap=-1;for(int i=0;i<(int)value.size();++i){ImGui::PushID(i);if(ImGui::SmallButton("X"))remove=i;ImGui::SameLine();
    if(kind=="List"){if(ImGui::SmallButton("^"))swap=i;ImGui::SameLine();changed|=ScriptValue(scene,std::format("Element {}",i).c_str(),schema["element"],value[i],depth+1);}
    else {if(!value[i].is_object())value[i]={{"key",SchemaDefault(schema["key"])},{"value",SchemaDefault(schema["value"])}};ImGui::BeginGroup();changed|=ScriptValue(scene,"Key",schema["key"],value[i]["key"],depth+1);changed|=ScriptValue(scene,"Value",schema["value"],value[i]["value"],depth+1);for(int previous=0;previous<i;++previous)if(value[previous]["key"]==value[i]["key"])ImGui::TextColored({1,.4f,.3f,1},"Duplicate key");ImGui::EndGroup();ImGui::Separator();}
    ImGui::PopID();}
   if(remove>=0){value.erase(value.begin()+remove);changed=true;}if(swap>0){std::swap(value[swap],value[swap-1]);changed=true;}
   if(ImGui::Button(kind=="List"?"+ Element":"+ Entry")){if(kind=="List")value.push_back(SchemaDefault(schema["element"]));else{auto key=SchemaDefault(schema["key"]);auto keyKind=schema["key"].value("kind","");if(keyKind=="string")key="Key"+std::to_string(value.size());else if(keyKind=="int")key=(int)value.size();value.push_back({{"key",key},{"value",SchemaDefault(schema["value"])}});}changed=true;}
   ImGui::TreePop();}
 }else if(kind.starts_with("Asset:")){changed=AssetReferenceField(label,kind,value);}
 else if(kind=="float"){float v=value.is_number()?value.get<float>():0;if(Insp::DragFloat(label,&v,.05f)){value=v;changed=true;}}
 else if(kind=="int"){int v=value.is_number()?value.get<int>():0;if(Insp::DragInt(label,&v)){value=v;changed=true;}}
 else if(kind=="bool"){bool v=value.is_boolean()&&value.get<bool>();if(Insp::Checkbox(label,&v)){value=v;changed=true;}}
 else if(kind=="string"){std::string v=value.is_string()?value.get<std::string>():"";if(Insp::InputText(label,&v)){value=v;changed=true;}}
 else if(kind=="enum"){auto names=schema.value("names",json::array()),values=schema.value("values",json::array());int v=value.is_number()?value.get<int>():0;std::string preview=std::to_string(v);for(size_t i=0;i<values.size();++i)if(values[i]==v)preview=names[i];if(Insp::BeginCombo(label,preview.c_str())){for(size_t i=0;i<values.size();++i)if(ImGui::Selectable(names[i].get<std::string>().c_str(),values[i]==v)){value=values[i];changed=true;}ImGui::EndCombo();}}
 else if(kind=="Vector2"||kind=="Vector3"||kind=="Color"){int n=kind=="Vector2"?2:kind=="Vector3"?3:4;float v[4]={0,0,0,1};if(value.is_array())for(int i=0;i<n&&i<(int)value.size();++i)v[i]=value[i].get<float>();changed=n==2?Insp::DragFloat2(label,v,.05f):n==3?Insp::DragFloat3(label,v,.05f):Insp::ColorEdit4(label,v);if(changed){value=json::array();for(int i=0;i<n;++i)value.push_back(v[i]);}}
 else if(kind=="Entity"||kind.starts_with("Component:")||kind.starts_with("Script:")){if(!value.is_string()&&!value.is_object())value="0";if(Insp::KeepsLayout(label))ImGui::SetNextItemWidth(-FLT_MIN);else Insp::LabelCell(label);changed=ReferenceField(scene,kind,value);}
 ImGui::PopID();return changed;
}

void EditorApp::DrawScriptInspector(EntityID e, ScriptSlot& sc, size_t slot) {
    BeginProperty("Script");
    ImGui::BeginDisabled(m_State != PlayState::Edit);
    if (Insp::BeginCombo("##class", sc.className.empty() ? "(none)" : sc.className.c_str())) {
        for (auto& c : ScriptEngine::Classes())
            if (ImGui::Selectable(c.name.c_str(), c.name == sc.className)) sc.className = c.name;
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    EndProperty();
    const ScriptClassInfo* info = ScriptEngine::FindClass(sc.className);
    if (!info) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), ScriptEngine::IsReady() ? "Class not found: build the scripts (Ctrl+B)" : "C# scripting unavailable");
        return;
    }
    bool live = m_State != PlayState::Edit && ScriptEngine::HasInstance(e, slot);
    if(DrawManagedInspector(ActiveScene(),e,sc,slot,live))return;
    nlohmann::json values = nlohmann::json::parse(live ? ScriptEngine::GetInstanceFields(e, slot) : sc.fieldsJson, nullptr, false);
    if (!values.is_object()) values = nlohmann::json::object();
    bool changed = false;
    std::string changedField;
    for (auto& f : info->fields) {
        nlohmann::json v = values.contains(f.name) ? values[f.name] : nlohmann::json::parse(f.defaultJson, nullptr, false);
        bool c = false;
        ImGui::PushID(f.name.c_str());
        if(f.type=="List"||f.type=="Dictionary"||f.type=="enum"){auto schema=nlohmann::json::parse(f.schemaJson,nullptr,false);if(schema.is_object())c=ScriptValue(ActiveScene(),f.name.c_str(),schema,v);ImGui::PopID();if(c){values[f.name]=v;changed=true;if(live)ScriptEngine::SetInstanceField(e,f.name,v.dump(),slot);}continue;}
        BeginProperty(f.name.c_str());
        if (f.type == "float") { float x = v.is_number() ? v.get<float>() : 0.0f; if ((c = Insp::DragFloat("##v", &x, 0.05f))) v = x; }
        else if (f.type == "int") { int x = v.is_number() ? v.get<int>() : 0; if ((c = Insp::DragInt("##v", &x))) v = x; }
        else if (f.type == "bool") { bool x = v.is_boolean() && v.get<bool>(); if ((c = Insp::Checkbox("##v", &x))) v = x; }
        else if (f.type == "string") { std::string x = v.is_string() ? v.get<std::string>() : ""; if ((c = Insp::InputText("##v", &x))) v = x; }
        else if (f.type == "Vector2" || f.type == "Vector3" || f.type == "Color") {
            int n = f.type == "Vector2" ? 2 : f.type == "Vector3" ? 3 : 4;
            float x[4] = { 0, 0, 0, 1 };
            if (v.is_array()) for (int k = 0; k < n && k < (int)v.size(); ++k) x[k] = v[k].get<float>();
            c = n == 2 ? Insp::DragFloat2("##v", x, 0.05f) : n == 3 ? Insp::DragFloat3("##v", x, 0.05f) : EditorIcons::ColorField("##v",x);
            if (c) { v = nlohmann::json::array(); for (int k = 0; k < n; ++k) v.push_back(x[k]); }
        }
        else if(f.type.starts_with("Asset:")){c=AssetReferenceField("##asset",f.type,v);}
        else if (f.type == "Entity" || f.type.starts_with("Component:") || f.type.starts_with("Script:")) {
            c = ReferenceField(ActiveScene(), f.type, v);
        }
        EndProperty();
        ImGui::PopID();
        if (c) {
            values[f.name] = v;
            changed = true;
            if (live) ScriptEngine::SetInstanceField(e, f.name, v.dump(), slot);
        }
    }
    if (changed && !live) sc.fieldsJson = values.dump();
    if (live) ImGui::TextDisabled("Play mode: live values (not saved)");
}

// ------------------------------------------------------------------------------------------------ console

void EditorApp::DrawLightingWindow() {
    static bool wasOpen = false;
    if (!m_ShowLighting) { wasOpen = false; return; }
    auto* self = ImGui::FindWindowByName("Lighting");
    if (auto* inspector = ImGui::FindWindowByName("Inspector"); !wasOpen && inspector && inspector->DockId && (!self || !self->DockId) && !(ImGui::FindWindowSettingsByID(ImHashStr("Lighting")) && ImGui::FindWindowSettingsByID(ImHashStr("Lighting"))->DockId))
        ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_Always);
    if (!wasOpen || std::exchange(m_LightingFocusRequest, false)) ImGui::SetNextWindowFocus();
    wasOpen = true;
    ImGui::SetNextWindowSize({ 420, 640 }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lighting", &m_ShowLighting)) { ImGui::End(); return; }
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    if (ImGui::BeginTabBar("##lightingTabs")) {
        if (ImGui::BeginTabItem("Environment")) {
            EnvironmentComponent* environment = nullptr;
            EntityID owner = 0;
            reg.Each<EnvironmentComponent>([&](EntityID e, EnvironmentComponent& env) { if (!environment) { environment = &env; owner = e; } });
            if (environment) {
                ImGui::TextDisabled("Settings from \"%s\"", reg.Get<NameComponent>(owner).name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Select")) { m_Selected = owner; m_EntitySelection = { owner }; m_InspectAsset = false; }
                ImGui::BeginChild("##environmentScroll");
                DrawEnvironmentSettings(*environment);
                ImGui::EndChild();
            } else {
                ImGui::TextDisabled("This scene has no Environment object.");
                if (ImGui::Button("Create Environment")) { EntityID e = scene.CreateEntity("Environment"); reg.Add<EnvironmentComponent>(e); m_Selected = e; m_EntitySelection = { e }; }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lights")) {
            int total = 0;
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##lights", 6, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 28);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 74);
                ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Intensity", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableHeadersRow();
                auto row = [&](EntityID e, const char* type, float* color, float& intensity, float* range) {
                    ++total;
                    ImGui::PushID((int)e);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Checkbox("##on", &reg.Get<NameComponent>(e).active);
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    if (ImGui::Selectable(reg.Get<NameComponent>(e).name.c_str(), e == m_Selected, ImGuiSelectableFlags_SpanAvailWidth)) { m_Selected = e; m_EntitySelection = { e }; m_InspectAsset = false; }
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", type);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    EditorIcons::ColorField("##color", color, 3);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##intensity", &intensity, 0.05f, 0.0f, 100000.0f, "%.2f");
                    ImGui::TableNextColumn();
                    if (range) { ImGui::SetNextItemWidth(-FLT_MIN); ImGui::DragFloat("##range", range, 0.05f, 0.1f, 100000.0f, "%.1f"); }
                    else ImGui::TextDisabled("-");
                    ImGui::PopID();
                };
                for (EntityID e : reg.AllEntities()) {
                    if (auto* l = reg.TryGet<DirectionalLightComponent>(e)) row(e, "Directional", glm::value_ptr(l->color), l->intensity, nullptr);
                    if (auto* l = reg.TryGet<PointLightComponent>(e)) row(e, "Point", glm::value_ptr(l->color), l->intensity, &l->range);
                    if (auto* l = reg.TryGet<SpotLightComponent>(e)) row(e, "Spot", glm::value_ptr(l->color), l->intensity, &l->range);
                }
                ImGui::EndTable();
            }
            if (!total) ImGui::TextDisabled("No lights in this scene.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorApp::DrawConsole() {
    if (!ImGui::Begin("Console", &m_ShowConsole)) { ImGui::End(); return; }
    static bool showInfo = true, showWarn = true, showError = true, showTrace = false, autoScroll = true;
    if (ImGui::Button("Clear")) Log::Clear();
    ImGui::SameLine(); ImGui::Checkbox("Trace", &showTrace);
    ImGui::SameLine(); ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine(); ImGui::Checkbox("Warnings", &showWarn);
    ImGui::SameLine(); ImGui::Checkbox("Errors", &showError);
    ImGui::SameLine(); ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::Separator();

    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    static u64 seenVersion = 0;
    for (const LogEntry& entry : Log::Snapshot()) {
        bool show = (entry.level == LogLevel::Trace && showTrace) || (entry.level == LogLevel::Info && showInfo) ||
                    (entry.level == LogLevel::Warn && showWarn) || (entry.level == LogLevel::Error && showError);
        if (!show) continue;
        ImVec4 col = entry.level == LogLevel::Error ? ImVec4(1.0f, 0.42f, 0.4f, 1)
                   : entry.level == LogLevel::Warn ? ImVec4(1.0f, 0.82f, 0.35f, 1)
                   : entry.level == LogLevel::Trace ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(0.85f, 0.85f, 0.85f, 1);
        ImGui::TextDisabled("[%7.2f]", entry.time);
        ImGui::SameLine();
        ImGui::TextColored(col, "%s", entry.message.c_str());
    }
    u64 v = Log::Version();
    if (autoScroll && v != seenVersion) ImGui::SetScrollHereY(1.0f);
    seenVersion = v;
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------ project

void EditorApp::DrawProject() {
    if (!ImGui::Begin("Project", &m_ShowProject)) { ImGui::End(); return; }
    m_AnimationDockId=ImGui::GetWindowDockID();
    namespace fs = std::filesystem;
    if (!fs::exists(m_ProjectCurrentDir)) m_ProjectCurrentDir = m_ProjectRoot;
    if (ImGui::SmallButton("Assets##rootAssets")) m_ProjectCurrentDir=m_ProjectRoot;
    ImGui::SameLine();
    if (ImGui::SmallButton("Packages##rootPackages")) m_ProjectCurrentDir=Project::Dir()/"Packages";
    ImGui::SameLine();

    // Breadcrumb
    fs::path rel = fs::relative(m_ProjectCurrentDir, m_ProjectRoot.parent_path());
    fs::path accum = m_ProjectRoot.parent_path();
    bool first = true;
    for (auto& part : rel) {
        accum /= part;
        if (!first) { ImGui::SameLine(0, 2); ImGui::TextDisabled(">"); ImGui::SameLine(0, 2); }
        first = false;
        if (ImGui::SmallButton((FileSystem::Utf8(part)+"##crumb"+FileSystem::Utf8(accum)).c_str())) m_ProjectCurrentDir = accum;
    }
    ImGui::SameLine();
    static std::string projectSearch;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 8, ImGui::GetWindowWidth() - 268));
    ImGui::SetNextItemWidth(236);
    ImGui::InputTextWithHint("##projectSearch", "Search assets...", &projectSearch);
    if (!projectSearch.empty()) { ImGui::SameLine(0, 2); if (ImGui::SmallButton("x##clearSearch")) projectSearch.clear(); }
    ImGui::Separator();

    float cell = 92.0f;
    int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cell));
    ImGui::BeginChild("##files");
    if (ImGui::BeginTable("##grid", cols)) {
        // Listing folders and resolving asset paths is slow, so the listing is kept and refreshed about once a second
        static std::vector<fs::directory_entry> entries;
        static std::vector<std::string> order;
        static std::set<std::string> expandedModels;
        static std::set<fs::path> modelChildren;
        static fs::path listedDir;
        static std::string listedSearch;
        static double listedAt = -10;
        static bool listingStale = true;
        if (listingStale || listedDir != m_ProjectCurrentDir || listedSearch != projectSearch || ImGui::GetTime() - listedAt > 1.0) {
        listingStale = false; listedDir = m_ProjectCurrentDir; listedSearch = projectSearch; listedAt = ImGui::GetTime();
        entries.clear(); modelChildren.clear();
        std::error_code ec;
        auto hidden = [](const fs::directory_entry& de) { // files that belong to a model are shown under its arrow, or not at all
            return (de.is_directory() && de.path().extension() == ".fbm") || (!de.is_directory() && AssetMeta::IsSideFile(de.path()));
        };
        if (projectSearch.empty()) {
            for (auto& de : fs::directory_iterator(m_ProjectCurrentDir, ec)) if (!hidden(de)) entries.push_back(de);
        } else {
            // Searching looks through every folder of the project
            auto lower = [](std::string text) { std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return text; };
            const std::string needle = lower(projectSearch);
            for (auto it = fs::recursive_directory_iterator(m_ProjectRoot, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (hidden(*it)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
                if (lower(FileSystem::Utf8(it->path().filename())).find(needle) != std::string::npos) entries.push_back(*it);
                if (entries.size() >= 500) break;
            }
        }
        std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
            if (a.is_directory() != b.is_directory()) return a.is_directory();
            return a.path().filename() < b.path().filename();
        });
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].is_directory() || !expandedModels.count(AssetManager::ToRelative(entries[i].path()))) continue;
            size_t insertAt = i + 1;
            for (const FbxContentItem& item : AssetManager::GetFbxContents(AssetManager::ToRelative(entries[i].path()))) {
                fs::path child = AssetManager::ToAbsolute(item.path);
                modelChildren.insert(child);
                entries.insert(entries.begin() + insertAt++, fs::directory_entry(child));
            }
            i = insertAt - 1;
        }
        order.clear();for(auto& de:entries)order.push_back(AssetManager::ToRelative(de.path()));
        }
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_A))m_AssetSelection=order;
        for (size_t entryIndex = 0; entryIndex < entries.size() && entryIndex < order.size(); ++entryIndex) {
            const fs::directory_entry de = entries[entryIndex]; // copies: clicks below may refresh the listing
            ImGui::TableNextColumn();
            std::string fname = FileSystem::Utf8(de.path().filename());
            std::string relPath = order[entryIndex];
            ImGui::PushID(relPath.c_str());
            if (modelChildren.count(de.path())) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(38, 54, 78, 255));
            bool dir = de.is_directory();
            std::string ext = de.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            bool selected = std::find(m_AssetSelection.begin(),m_AssetSelection.end(),relPath)!=m_AssetSelection.end();
            if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.16f,.35f,.55f,1));
            ImVec2 size(cell - 16, cell - 36);
            if (!dir && AssetManager::IsTextureFile(de.path())) {
                auto tex = AssetManager::GetTexture(relPath, true);
                void* id = tex ? GetDevice().ImGuiTexture(tex.get()) : nullptr;
                if (id) ImGui::ImageButton("##thumb", (ImTextureID)id, ImVec2(size.x - 8, size.y - 8));
                else ImGui::Button("IMG", size);
                if (AssetUI::IsSpriteTexture(relPath)) {
                    auto* badgeDraw = ImGui::GetWindowDrawList();
                    ImVec2 corner = ImGui::GetItemRectMin(); corner.x += 3; corner.y += 3;
                    badgeDraw->AddRectFilled(corner, { corner.x + 22, corner.y + 14 }, IM_COL32(60, 120, 200, 230), 3);
                    badgeDraw->AddText({ corner.x + 4, corner.y }, IM_COL32(255, 255, 255, 255), "2D");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sprite (double-click to edit)");
                }
            } else {
                AssetUI::IconButton(de.path(),dir,selected,{size.x,size.y});
            }
            if(selected)ImGui::PopStyleColor();
            ImVec2 tileMin = ImGui::GetItemRectMin(), tileMax = ImGui::GetItemRectMax();
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragPastThreshold(0)) {SelectProject(relPath,order);m_LastSelected=m_Selected;}
            if(ImGui::BeginPopupContextItem("AssetContext")) {
                if(std::find(m_AssetSelection.begin(),m_AssetSelection.end(),relPath)==m_AssetSelection.end())m_AssetSelection={relPath};m_SelectedAsset=relPath;m_InspectAsset=true;
                if(ImGui::MenuItem("Copy","Ctrl+C"))CopyAssets();if(ImGui::MenuItem("Paste","Ctrl+V",false,!m_AssetClipboard.empty()))PasteAssets();
                if(ImGui::MenuItem("Open")) {if(dir)m_ProjectCurrentDir=de.path();else ShellExecuteFile(de.path());}
                if(ImGui::MenuItem("Delete Asset"))RequestDeleteAsset(relPath);
                ImGui::EndPopup();
            }
            if (!dir && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(AssetUI::kAssetPayload, relPath.c_str(), relPath.size() + 1);
                ImGui::Text("%s", fname.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                if (dir) { m_ProjectCurrentDir = de.path(); projectSearch.clear(); }
                else if (ext == ".scene") OpenScene(de.path());
                else if (ext == ".prefab") OpenPrefab(de.path());
                else if (ext == ".sprite" || AssetUI::IsSpriteTexture(relPath)) {m_SpriteEditorPath=relPath;m_SpriteEditorOpen=true;}
                else if (ext == ".anim" || ext == ".controller") {m_AnimationAsset=relPath;m_ShowAnimationEditor=true;}
                else if (ext == ".cs") ShellExecuteFile(de.path());
                else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") AudioEngine::PreviewClip(relPath);
            }
            if (ext == ".fbx" && !modelChildren.count(de.path())) {
                // Arrow on the model's right edge: opens the materials and textures inside it
                const bool isOpen = expandedModels.count(relPath) > 0;
                const float midY = (tileMin.y + tileMax.y) * 0.5f, right = tileMax.x + 2;
                const ImVec2 hitMin(right - 14, midY - 14), hitMax(right + 2, midY + 14);
                // The arrow overlaps the icon, which grabs the press first: ignore that so the whole arrow works
                const bool overArrow = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(hitMin, hitMax, false);
                auto* arrowDraw = ImGui::GetWindowDrawList();
                arrowDraw->AddRectFilled(hitMin, hitMax, overArrow ? IM_COL32(70, 110, 170, 255) : IM_COL32(44, 47, 53, 230), 3);
                const ImU32 arrowColor = IM_COL32(225, 228, 235, 255);
                const float cx = (hitMin.x + hitMax.x) * 0.5f;
                if (isOpen) arrowDraw->AddTriangleFilled({ cx + 3, midY - 5 }, { cx + 3, midY + 5 }, { cx - 3, midY }, arrowColor);
                else arrowDraw->AddTriangleFilled({ cx - 3, midY - 5 }, { cx - 3, midY + 5 }, { cx + 3, midY }, arrowColor);
                if (overArrow) ImGui::SetTooltip("%s", isOpen ? "Hide materials and textures" : "Show materials and textures");
                if (overArrow && ImGui::IsMouseClicked(0)) { if (!expandedModels.erase(relPath)) expandedModels.insert(relPath); listingStale = true; }
            }
            ImGui::TextWrapped("%s", fname.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(40.0f, ImGui::GetContentRegionAvail().y)));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STARDUST_ENTITY")) {
            EntityID de = *(const EntityID*)p->Data;
            Scene& sc = ActiveScene();
            if (sc.Reg().Valid(de)) {
                fs::path pp = m_ProjectCurrentDir / (sc.Reg().Get<NameComponent>(de).name + ".prefab");
                if (SceneSerializer::SavePrefab(sc, de, pp)) SD_INFO("Saved prefab {}", pp.string());
            }
        }
        ImGui::EndDragDropTarget();
    }
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput){auto& io=ImGui::GetIO();if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_C))CopyAssets();if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_V))PasteAssets();if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_D)){CopyAssets();PasteAssets();}}
    if (ImGui::BeginPopupContextWindow("ProjectContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if(ImGui::MenuItem("Paste","Ctrl+V",false,!m_AssetClipboard.empty()))PasteAssets();
        if (ImGui::MenuItem("Create Folder")) {
            fs::path p = m_ProjectCurrentDir / "New Folder";
            for (int n = 1; fs::exists(p); ++n) p = m_ProjectCurrentDir / std::format("New Folder {}", n);
            fs::create_directories(p);
        }
        if (ImGui::MenuItem("Create Material")) {
            fs::path p = m_ProjectCurrentDir / "New Material.mat";
            for (int n = 1; fs::exists(p); ++n) p = m_ProjectCurrentDir / std::format("New Material {}.mat", n);
            AssetManager::CreateMaterial(AssetManager::ToRelative(p));
            m_SelectedAsset = AssetManager::ToRelative(p);
            m_InspectAsset = true;
        }
        if (ImGui::MenuItem("Create C# Script")) CreateScriptAsset(m_ProjectCurrentDir);
        if (ImGui::MenuItem("Create Editor Script")) {
            auto path=m_ProjectCurrentDir/"Editor"/"EditorCommands.cs";
            if (!fs::exists(path)) FileSystem::WriteText(path,"using Stardust;\nusing Stardust.Editor;\npublic static class EditorCommands {\n    [MenuItem(\"Tools/Hello\")]\n    static void Hello() { Debug.Log(\"Hello from Editor Only scripts\"); }\n}\n");
            Project::WriteEditorScriptProject();
        }
        if (ImGui::MenuItem("Create Custom Shader")) {
            auto path=m_ProjectCurrentDir/"Custom.ps.hlsl";
            for (int n=1;fs::exists(path);++n) path=m_ProjectCurrentDir/std::format("Custom{}.ps.hlsl",n);
            FileSystem::WriteText(path,R"(#include "MeshCommon.hlsli"
MeshPSOut main(MeshVSOut i) {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    MeshPSOut o;
    o.color = MaterialProperty(0,float4(1,1,1,1)) * MaterialFloat(1,1); // Tint and brightness declared in the property schema.
    o.entityId = d.ids.z;
    return o;
}
)");
            FileSystem::WriteText(fs::path(FileSystem::Utf8(path)+".spv.properties.json"),R"({"properties":[{"name":"Tint","label":"Tint","type":"color","default":[1,1,1,1]},{"name":"Brightness","label":"Brightness","type":"float","default":1,"min":0,"max":5}]})");
            m_SelectedAsset=AssetManager::ToRelative(path); m_InspectAsset=true;
        }
        if(ImGui::MenuItem("Create Custom Sky Shader")){auto path=m_ProjectCurrentDir/"CustomSky.ps.hlsl";for(int n=1;fs::exists(path);++n)path=m_ProjectCurrentDir/std::format("CustomSky{}.ps.hlsl",n);FileSystem::WriteText(path,R"(#include "SkyCommon.hlsli"
PSOut main(float4 position:SV_Position,float2 uv:TEXCOORD0){float3 direction=SkyDirection(uv);PSOut result;result.color=float4(lerp(float3(.05,.08,.15),g_Push.color.rgb,saturate(direction.y*.5+.5))*exp2(g_Push.parameters.y),1);result.entityId=0;return result;}
)");m_SelectedAsset=AssetManager::ToRelative(path);m_InspectAsset=true;}
        if(ImGui::MenuItem("Create Render Texture")){auto path=m_ProjectCurrentDir/"New Render Texture.rendertexture";for(int i=1;fs::exists(path);++i)path=m_ProjectCurrentDir/std::format("New Render Texture {}.rendertexture",i);FileSystem::WriteText(path,R"({"width":512,"height":512})");m_SelectedAsset=AssetManager::ToRelative(path);m_InspectAsset=true;}
        if (ImGui::MenuItem("Create Animation")) {
            auto path=m_ProjectCurrentDir/"New Animation.anim";
            if (!fs::exists(path)) FileSystem::WriteText(path,R"({"duration":1,"tracks":[{"path":"","rotation":[[0,0,0,0],[1,0,360,0]]}]})");
            m_SelectedAsset=AssetManager::ToRelative(path); m_InspectAsset=true;
        }
        if (ImGui::MenuItem("Create Animator Controller")) {
            auto path=m_ProjectCurrentDir/"New Animator.controller";
            if (!fs::exists(path)) FileSystem::WriteText(path,nlohmann::json({{"defaultState","Default"},{"states",{{"Default",{{"clip",AssetManager::ToRelative(m_ProjectCurrentDir/"New Animation.anim")},{"loop",true}}}}},{"transitions",nlohmann::json::array()}}).dump(2));
            m_SelectedAsset=AssetManager::ToRelative(path); m_InspectAsset=true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer")) ShellExecuteFile(m_ProjectCurrentDir);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------ stats

void EditorApp::DrawStats() {
    if (std::exchange(m_StatsFocusRequest, false)) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Stats", &m_ShowStats)) { ImGui::End(); return; }
    ImGui::Text("GPU: %s", GetDevice().GetAdapterName());
    ImGui::Text("Frame: %.2f ms (%.0f fps)", 1000.0f / std::max(FPS(), 1.0f), FPS());
    ImGui::Text("CPU (record): %.2f ms", CpuFrameMs());
    ImGui::Separator();
    auto& s = m_SceneRenderer->Stats();
    ImGui::Text("Scene view: %u draws, %llu tris, %ux%u", s.drawCalls, (unsigned long long)s.triangles, m_SceneRenderer->Width(), m_SceneRenderer->Height());
    ImGui::SeparatorText("GPU passes");
    if (ImGui::BeginTable("##gpu", 2, ImGuiTableFlags_RowBg)) {
        for (auto& t : GetDevice().GpuTimings()) {
            ImGui::TableNextColumn(); ImGui::TextUnformatted(t.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.3f ms", t.ms);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace Stardust

