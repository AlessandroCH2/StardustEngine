#include "InspectorWidgets.h"
#include "EditorApp.h"
#include "Renderer/Nature/TerrainNeighbors.h"
#include "AssetUI.h"
#include "Assets/AssetManager.h"
#include "Core/Input.h"
#include "Core/Log.h"
#include "Core/FileSystem.h"
#include "Project/Project.h"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <limits>
#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

namespace Stardust {

static void Row(const char* label) {
    Insp::LabelCell(label);
}

void EditorApp::DrawTerrainInspector(EntityID e, TerrainComponent& tc) {
    Registry& reg = ActiveScene().Reg();
    u64 uuid = reg.Get<IDComponent>(e).uuid;
    auto data = TerrainData::Find(uuid);
    TerrainSettings& s = tc.settings;

    int category=m_TerrainTool==TerrainBrush::Paint?1:m_TerrainTool==TerrainBrush::Trees?2:m_TerrainTool==TerrainBrush::None?3:0;
    const char* tips[]={"Sculpt terrain","Paint texture","Paint trees","Terrain settings"};for(int tab=0;tab<4;++tab){if(tab)ImGui::SameLine(0,3);ImGui::PushID(tab);auto p=ImGui::GetCursorScreenPos();if(tab==category)ImGui::PushStyleColor(ImGuiCol_Button,{.25f,.4f,.55f,1});if(ImGui::Button("##terrainTab",{46,34})){m_TerrainTool=tab==0?TerrainBrush::Raise:tab==1?TerrainBrush::Paint:tab==2?TerrainBrush::Trees:TerrainBrush::None;}if(tab==category)ImGui::PopStyleColor();auto* d=ImGui::GetWindowDrawList();auto color=IM_COL32(225,230,235,255);if(tab==0){d->AddTriangle({p.x+8,p.y+25},{p.x+23,p.y+8},{p.x+37,p.y+25},color,2);}if(tab==1){d->AddLine({p.x+15,p.y+25},{p.x+32,p.y+8},color,4);d->AddCircleFilled({p.x+13,p.y+27},5,color);}if(tab==2){d->AddCircle({p.x+23,p.y+13},8,color,12,2);d->AddLine({p.x+23,p.y+20},{p.x+23,p.y+29},color,3);}if(tab==3){d->AddCircle({p.x+23,p.y+17},9,color,8,3);d->AddCircle({p.x+23,p.y+17},3,color,12,2);}if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",tips[tab]);ImGui::PopID();}
    if(category==0){int operation=m_TerrainTool==TerrainBrush::Smooth?1:m_TerrainTool==TerrainBrush::Flatten?2:m_TerrainTool==TerrainBrush::SetHeight?3:0;ImGui::SetNextItemWidth(-1);if(Insp::Combo("##SculptOperation",&operation,"Raise / Lower Terrain\0Smooth Height\0Flatten Height\0Set Height\0"))m_TerrainTool=operation==0?TerrainBrush::Raise:operation==1?TerrainBrush::Smooth:operation==2?TerrainBrush::Flatten:TerrainBrush::SetHeight;}
    ImGui::TextWrapped(category==1?"Paint the selected terrain layer. Shift erases.":category==2?"Place trees. Shift removes trees.":category==3?"Terrain geometry, neighbors and rendering settings.":"Sculpt the heightmap. Hold Shift to lower terrain.");
    if(m_TerrainTool!=TerrainBrush::None){ImGui::SeparatorText("Brushes");const char* labels[]={"Soft Round","Hard Round","Soft Falloff","Noise","Star","Custom Mask"};for(int brush=0;brush<6;++brush){if(brush&&brush%4)ImGui::SameLine(0,4);ImGui::PushID(brush);auto p=ImGui::GetCursorScreenPos();if(ImGui::Button("##brush",{44,44}))m_BrushStamp.shape=brush;auto* d=ImGui::GetWindowDrawList();TerrainBrushStamp stamp;stamp.shape=brush;for(int y=0;y<12;++y)for(int x=0;x<12;++x){float weight=TerrainData::BrushWeight({(x-5.5f)/5.5f,(y-5.5f)/5.5f},stamp);if(weight>0)d->AddRectFilled({p.x+4+x*3.f,p.y+4+y*3.f},{p.x+7+x*3.f,p.y+7+y*3.f},IM_COL32(220,225,235,(int)(weight*255)));}if(m_BrushStamp.shape==brush)d->AddRect(p,{p.x+44,p.y+44},IM_COL32(80,170,255,255),0,0,2);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",labels[brush]);ImGui::PopID();}if(m_BrushStamp.shape==5)AssetUI::AssetField("Brush Mask",m_BrushStamp.texture,{".png",".jpg",".tga",".bmp"});Row("Rotation");Insp::SliderFloat("##rotation",&m_BrushStamp.rotation,0,360,"%.0f deg");}
    if (m_TerrainTool != TerrainBrush::None) {
        ImGui::TextDisabled("Left mouse in the Scene view to apply.  Esc / click the tool again to stop.");
        Row("Brush Size"); Insp::SliderFloat("##bs", &m_BrushSize, 0.5f, 200.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
        Row("Opacity"); Insp::SliderFloat("##bst", &m_BrushStrength, 0.01f, 1.0f);
        if (m_TerrainTool != TerrainBrush::Trees) { Row("Hardness"); Insp::SliderFloat("##bh", &m_BrushHardness, 0.0f, 0.95f); }
        if (m_TerrainTool == TerrainBrush::SetHeight) { Row("Height"); Insp::DragFloat("##bht", &m_BrushHeight, 0.1f); }
        if (m_TerrainTool == TerrainBrush::Paint) {
            Row("Layer");
            if (Insp::BeginCombo("##pl", s.layers[std::clamp(m_PaintLayer,0,(int)s.layers.size()-1)].name.c_str())) {
                for (int l = 0; l < (int)s.layers.size(); ++l)
                    if (ImGui::Selectable(s.layers[l].name.c_str(), l == m_PaintLayer)) m_PaintLayer = l;
                ImGui::EndCombo();
            }
        }
        if (Input::IsKeyPressed(Key::Escape)) m_TerrainTool = TerrainBrush::None;
    }

    // ---------------------------------------------------------------- layers
    if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Surface: Stardust / Terrain");float thumbnail=std::clamp((ImGui::GetContentRegionAvail().x-18)/4,48.f,90.f);for(size_t i=0;i<s.layers.size();++i){if(i%4)ImGui::SameLine(0,4);ImGui::PushID((int)i);auto& layer=s.layers[i];auto tex=AssetManager::GetTexture(layer.albedoMap,true);auto p=ImGui::GetCursorScreenPos();bool clicked=false;if(tex)clicked=ImGui::ImageButton("##layerPreview",(ImTextureID)GetDevice().ImGuiTexture(tex.get()),{thumbnail,thumbnail});else{ImGui::PushStyleColor(ImGuiCol_Button,{layer.tint.x,layer.tint.y,layer.tint.z,1});clicked=ImGui::Button("##layerPreview",{thumbnail+8,thumbnail+8});ImGui::PopStyleColor();}if(clicked){m_PaintLayer=(int)i;m_TerrainTool=TerrainBrush::Paint;}if(m_PaintLayer==(int)i)ImGui::GetWindowDrawList()->AddRect(p,{p.x+thumbnail+8,p.y+thumbnail+8},IM_COL32(80,170,255,255),0,0,3);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",layer.name.c_str());ImGui::PopID();}
        for (u32 l = 0; l < s.layers.size(); ++l) {if(l!=(u32)std::clamp(m_PaintLayer,0,(int)s.layers.size()-1))continue;
            TerrainLayer& layer = s.layers[l];
            ImGui::PushID((int)l);
            if (ImGui::TreeNodeEx("##layer",ImGuiTreeNodeFlags_DefaultOpen, "%u  %s%s", l, layer.name.c_str(), layer.albedoMap.empty() ? "  (tint)" : "")) {
                if(AssetUI::AssetField("Layer Asset",layer.assetPath,{".terrainlayer"}))TerrainData::ResolveLayers(s);
                Row("Name"); Insp::InputText("##n", &layer.name);
                if(ImGui::SmallButton("Save Layer Asset")){if(layer.assetPath.empty())layer.assetPath=std::format("TerrainLayers/Layer_{:016x}_{}.terrainlayer",uuid,l);nlohmann::json j={{"name",layer.name},{"albedoMap",layer.albedoMap},{"tint",{layer.tint.x,layer.tint.y,layer.tint.z}},{"tiling",layer.tiling},{"roughness",layer.roughness}};FileSystem::WriteText(AssetManager::ToAbsolute(layer.assetPath),j.dump(2));}

                AssetUI::AssetField("Albedo", layer.albedoMap, { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }, "Solid Colour");
                Row("Tint"); Insp::ColorEdit3("##t", glm::value_ptr(layer.tint));
                Row("Tiling (m)"); Insp::DragFloat("##ti", &layer.tiling, 0.05f, 0.1f, 200.0f);
                Row("Roughness"); Insp::SliderFloat("##r", &layer.roughness, 0.05f, 1.0f);
                ImGui::TreePop();
            }
            if(s.layers.size()>1&&ImGui::SmallButton("Remove Layer")){if(auto data=TerrainData::Find(uuid))data->RemoveLayer(l);s.layers.erase(s.layers.begin()+l);m_PaintLayer=std::clamp(m_PaintLayer,0,(int)s.layers.size()-1);ImGui::PopID();break;}
            ImGui::PopID();
        }
        if(s.layers.size()<kTerrainLayers&&ImGui::Button("+ Add Terrain Layer")){TerrainLayer layer;layer.name="Layer "+std::to_string(s.layers.size());s.layers.push_back(layer);}
        ImGui::TextDisabled("Up to 16 editable layers. Remove remaps painted weights.");
    }

    if(ImGui::CollapsingHeader("Neighbors")){const char* sides[]={"Left","Right","Bottom","Top"};for(int side=0;side<4;++side){ImGui::PushID(side);ImGui::TextUnformatted(sides[side]);nlohmann::json ref=tc.neighbors[side]?std::format("{:016x}",tc.neighbors[side]):"0";if(AssetUI::EntityField(ActiveScene(),"Entity",ref)){auto id=std::strtoull(ref.get<std::string>().c_str(),nullptr,16);auto target=ActiveScene().FindByUUID(id);if(!id||(target&&reg.Has<TerrainComponent>(target)))tc.neighbors[side]=id;}ImGui::PopID();}
      if(ImGui::Button("Auto Connect"))TerrainNeighbors::AutoConnect(ActiveScene());ImGui::SameLine();if(ImGui::Button("Stitch Edges")){auto count=TerrainNeighbors::Stitch(ActiveScene(),e);SD_INFO("Stitched {} terrain edges (tiles need equal size and resolution)",count);}
      ImGui::TextUnformatted("Create Neighbor");for(int side=0;side<4;++side){if(side)ImGui::SameLine();ImGui::PushID(side);if(ImGui::Button(sides[side])){auto settings=tc;settings.neighbors={};settings.dataPath.clear();auto world=ActiveScene().GetWorldMatrix(e);world[3]+=glm::vec4(side==0?-s.size:side==1?s.size:0,0,side==2?-s.size:side==3?s.size:0,0);auto created=ActiveScene().CreateEntity("Terrain "+std::string(sides[side]));reg.Add<TerrainComponent>(created)=settings;ActiveScene().SetWorldMatrix(created,world);TerrainNeighbors::AutoConnect(ActiveScene());ActiveScene().PrepareTerrains(GetDevice());TerrainNeighbors::Stitch(ActiveScene(),created);m_Selected=created;ImGui::PopID();return;}ImGui::PopID();}}
    // ---------------------------------------------------------------- grass / trees
    if (ImGui::CollapsingHeader("Grass & Flowers")) {
        Row("Enabled"); Insp::Checkbox("##ge", &s.grass);
        Row("Density"); Insp::SliderFloat("##gd", &s.grassDensity, 0.1f, 3.0f);
        Row("Height"); Insp::SliderFloat("##gh", &s.grassHeight, 0.1f, 2.0f);
        Row("Distance"); Insp::SliderFloat("##gdi", &s.grassDistance, 10.0f, 150.0f);
        Row("Flowers"); Insp::SliderFloat("##gf", &s.flowerAmount, 0.0f, 0.5f);
        Row("Color"); Insp::ColorEdit3("##gc", glm::value_ptr(s.grassColor));
        Row("Dry Color"); Insp::ColorEdit3("##gdc", glm::value_ptr(s.dryGrassColor));
        ImGui::TextDisabled("Grass grows where the first layer is painted.");
    }
    if (ImGui::CollapsingHeader("Trees")) {
        Row("Visible"); Insp::Checkbox("##te", &s.trees);
        Row("Scatter Density"); Insp::SliderFloat("##td", &s.treeDensity, 0.0f, 3.0f);
        Row("Max Trees"); { int v = (int)s.maxTrees; if (Insp::DragInt("##tm", &v, 10.0f, 0, 40000)) s.maxTrees = (u32)v; }
        Row("Seed"); { int v = (int)s.treeSeed; if (Insp::DragInt("##ts", &v, 1.0f, 0, 100000)) s.treeSeed = (u32)v; }
        if (data) {
            ImGui::Text("%zu trees", data->Trees().size());
            if (ImGui::Button("Scatter Trees")) { data->SetSettings(s); data->ScatterTrees(); }
            ImGui::SameLine();
            if (ImGui::Button("Clear Trees")) data->ClearTrees();
        }
    }

    // ---------------------------------------------------------------- create / generate
    if (ImGui::CollapsingHeader("Create & Generate")) {
        Row("Size (m)"); Insp::DragFloat("##sz", &s.size, 4.0f, 64.0f, 16384.0f, "%.0f");
        Row("Resolution");
        const u32 resolutions[] = { 256, 512, 1024, 2048 };
        if (Insp::BeginCombo("##res", std::to_string(s.resolution).c_str())) {
            for (u32 r : resolutions)
                if (ImGui::Selectable(std::to_string(r).c_str(), s.resolution == r)) s.resolution = r;
            ImGui::EndCombo();
        }
        ImGui::SeparatorText("Flat");
        Row("Height"); Insp::DragFloat("##fh", &m_FlatHeight, 0.1f);
        if (data && ImGui::Button("Create Flat Terrain", ImVec2(-1, 0))) { data->SetSettings(s); data->MakeFlat(m_FlatHeight, s.resolution); }
        ImGui::TextDisabled("Procedural generation is available as a separate component.");
    }

    if (ImGui::CollapsingHeader("Data & Physics")) {
        Row("Collider"); Insp::Checkbox("##col", &tc.collider);
        Row("Data File"); ImGui::TextDisabled("%s", tc.dataPath.empty() ? "(created when the scene is saved)" : tc.dataPath.c_str());
        if (data) ImGui::TextDisabled("%ux%u samples, heights %.1f .. %.1f m", data->Resolution(), data->Resolution(), data->MinHeight(), data->MaxHeight());
    }
}

void EditorApp::DrawProceduralTerrainInspector(EntityID e,ProceduralTerrainComponent& c){
    auto* terrain=ActiveScene().Reg().TryGet<TerrainComponent>(e);if(!terrain){ImGui::TextDisabled("Add a Terrain component first");return;}
    Insp::DragFloat("Mountain Height",&c.mountainHeight,1,0,3000);Insp::DragFloat("Hill Height",&c.hillHeight,.1f,0,300);Insp::DragFloat("Meadow Radius",&c.meadowRadius,1,0,8000);int seed=(int)c.seed;if(Insp::InputInt("Seed",&seed))c.seed=(u32)std::max(0,seed);
    Insp::Checkbox("Generate if data missing",&c.generateOnMissing);Insp::Checkbox("Procedural surface fallback",&c.proceduralSurface);
    const char* roles[]={"Low slope","Medium slope","High slope","Sand"};for(int i=0;i<4;++i){ImGui::PushID(i);auto& settings=terrain->settings;c.layers[i]=std::min((u32)settings.layers.size()-1,c.layers[i]);if(Insp::BeginCombo(roles[i],settings.layers[c.layers[i]].name.c_str())){for(u32 j=0;j<settings.layers.size();++j)if(ImGui::Selectable(settings.layers[j].name.c_str(),c.layers[i]==j))c.layers[i]=j;ImGui::EndCombo();}ImGui::PopID();}
    if(ImGui::Button("Generate Into Terrain")){auto data=TerrainData::Find(ActiveScene().Reg().Get<IDComponent>(e).uuid);if(data){auto settings=terrain->settings;settings.mountainHeight=c.mountainHeight;settings.hillHeight=c.hillHeight;settings.meadowRadius=c.meadowRadius;settings.seed=c.seed;settings.proceduralAppearance=c.proceduralSurface;data->Generate(settings,c.layers);}}
    ImGui::TextWrapped("Generation writes editable height and layer data. Painting and sculpting remain available after removing this component.");
}

bool EditorApp::RaycastTerrain(const TerrainData& t, const glm::vec3& origin, const glm::vec3& ro, const glm::vec3& rd, glm::vec3& hit) {
    float half = t.Settings().size * 0.5f;
    auto below = [&](float d) {
        glm::vec3 p = ro + rd * d;
        glm::vec2 l(p.x - origin.x, p.z - origin.z);
        if (std::abs(l.x) > half || std::abs(l.y) > half) return false;
        return p.y < origin.y + t.HeightAt(l.x, l.y);
    };
    float prev = 0.0f, step = 0.25f;
    for (float d = step; d < 6000.0f; d += step) {
        if (below(d)) {
            float a = prev, b = d;
            for (int i = 0; i < 20; ++i) { float m = (a + b) * 0.5f; if (below(m)) b = m; else a = m; }
            hit = ro + rd * b;
            return true;
        }
        prev = d;
        step = std::min(step * 1.02f, 4.0f);
    }
    return false;
}

bool EditorApp::UpdateTerrainBrush() {
    Registry& reg = ActiveScene().Reg();
    if (m_TerrainTool == TerrainBrush::None || !reg.Valid(m_Selected) || !reg.Has<TerrainComponent>(m_Selected)) {
        m_Stroking = false;
        return false;
    }
    auto data = TerrainData::Find(reg.Get<IDComponent>(m_Selected).uuid);
    if (!data) return false;
    glm::vec3 origin = glm::vec3(ActiveScene().GetWorldMatrix(m_Selected)[3]);

    // Ray from the mouse through the editor camera
    ImVec2 mouse = ImGui::GetMousePos();
    float aspect = m_SceneView.size.x / m_SceneView.size.y;
    RenderCamera cam = m_Camera.GetRenderCamera(aspect);
    glm::mat4 invVP = glm::inverse(cam.projection * cam.view);
    glm::vec2 ndc((mouse.x - m_SceneView.min.x) / m_SceneView.size.x * 2.0f - 1.0f, 1.0f - (mouse.y - m_SceneView.min.y) / m_SceneView.size.y * 2.0f);
    glm::vec4 p0 = invVP * glm::vec4(ndc, 1.0f, 1.0f), p1 = invVP * glm::vec4(ndc, 0.5f, 1.0f);
    glm::vec3 a = glm::vec3(p0) / p0.w, b = glm::vec3(p1) / p1.w;
    glm::vec3 rd = glm::normalize(b - a);

    glm::vec3 hit;
    EntityID targetTerrain=m_Selected;auto rayOrigin=m_Camera.Is2D()?glm::vec3(p0)/p0.w:cam.position;bool hasHit=false;float best=std::numeric_limits<float>::max();std::vector<EntityID> connected{m_Selected};
    for(size_t i=0;i<connected.size();++i)if(auto* tile=reg.TryGet<TerrainComponent>(connected[i]))for(auto uuid:tile->neighbors)if(auto neighbor=ActiveScene().FindByUUID(uuid);neighbor&&std::find(connected.begin(),connected.end(),neighbor)==connected.end())connected.push_back(neighbor);
    if(m_SceneView.hovered&&!m_Camera.IsFlying())for(auto tile:connected){auto candidate=TerrainData::Find(reg.Get<IDComponent>(tile).uuid);if(!candidate)continue;auto position=glm::vec3(ActiveScene().GetWorldMatrix(tile)[3]);glm::vec3 point;if(RaycastTerrain(*candidate,position,rayOrigin,rd,point)){float distance=glm::length(point-rayOrigin);if(distance<best){best=distance;hasHit=true;hit=point;targetTerrain=tile;}}}
    if(hasHit){data=TerrainData::Find(reg.Get<IDComponent>(targetTerrain).uuid);origin=glm::vec3(ActiveScene().GetWorldMatrix(targetTerrain)[3]);}
    bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool shift = ImGui::GetIO().KeyShift, ctrl = ImGui::GetIO().KeyCtrl;

    if (hasHit) {
        glm::vec2 local(hit.x - origin.x, hit.z - origin.z);
        // Brush outline projected onto the terrain
        ImDrawList* dl = ImGui::GetWindowDrawList();
        glm::mat4 vp = cam.projection * cam.view;
        ImVec2 pts[64];
        int n = 0;
        for (int i = 0; i < 64; ++i) {
            float ang = i / 64.0f * 6.2831853f;
            glm::vec2 q = local + glm::vec2(std::cos(ang), std::sin(ang)) * m_BrushSize;
            glm::vec4 c = vp * glm::vec4(origin.x + q.x, origin.y + data->HeightAt(q.x, q.y) + 0.05f, origin.z + q.y, 1.0f);
            if (c.w <= 0.01f) continue;
            pts[n++] = ImVec2(m_SceneView.min.x + (c.x / c.w * 0.5f + 0.5f) * m_SceneView.size.x,
                              m_SceneView.min.y + (0.5f - c.y / c.w * 0.5f) * m_SceneView.size.y);
        }
        ImU32 col = shift ? IM_COL32(255, 120, 90, 220) : IM_COL32(120, 200, 255, 220);
        if (n > 2) dl->AddPolyline(pts, n, col, ImDrawFlags_Closed, 2.0f);

        if (ctrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_TerrainTool == TerrainBrush::SetHeight) {
            m_BrushHeight = data->HeightAt(local.x, local.y);
        } else if (lmb && (m_Stroking || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
            if (!m_Stroking) { m_Stroking = true; m_StrokeHeight = data->HeightAt(local.x, local.y); m_StrokeTerrain = targetTerrain; }
            float target = m_TerrainTool == TerrainBrush::Flatten ? m_StrokeHeight : m_BrushHeight;
            auto selectedLayer=std::clamp(m_PaintLayer,0,(int)reg.Get<TerrainComponent>(m_Selected).settings.layers.size()-1);auto& referenceLayer=reg.Get<TerrainComponent>(m_Selected).settings.layers[selectedLayer];
            for(auto tile:connected){auto destination=TerrainData::Find(reg.Get<IDComponent>(tile).uuid);if(!destination)continue;auto destinationOrigin=glm::vec3(ActiveScene().GetWorldMatrix(tile)[3]);auto brushLocal=glm::vec2(hit.x-destinationOrigin.x,hit.z-destinationOrigin.z);if(std::abs(brushLocal.x)>destination->Settings().size*.5f+m_BrushSize||std::abs(brushLocal.y)>destination->Settings().size*.5f+m_BrushSize)continue;u32 layer=std::min((u32)selectedLayer,(u32)destination->Settings().layers.size()-1);for(size_t i=0;i<destination->Settings().layers.size();++i){auto& candidate=destination->Settings().layers[i];if((!referenceLayer.assetPath.empty()&&candidate.assetPath==referenceLayer.assetPath)||candidate.name==referenceLayer.name){layer=(u32)i;break;}}destination->ApplyBrush(m_TerrainTool,brushLocal,m_BrushSize,m_BrushStrength,m_BrushHardness,target+origin.y-destinationOrigin.y,layer,shift,std::min(DeltaTime(),.05f),m_BrushStamp);}

        }
    }
    if (m_Stroking && !lmb) {
        for(auto tile:connected)if(auto edited=TerrainData::Find(reg.Get<IDComponent>(tile).uuid))edited->EndStroke();
        m_Stroking = false;
        data->EndStroke();
    }
    return true;
}

} // namespace Stardust
