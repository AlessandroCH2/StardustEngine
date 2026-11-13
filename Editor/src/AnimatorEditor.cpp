#include "EditorApp.h"
#include "InspectorWidgets.h"
#include "AssetUI.h"
#include "Animation/AvatarSystem.h"
#include "Animation/AnimatorSystem.h"
#include "Core/FileSystem.h"
#include "Assets/AssetManager.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include "Core/Log.h"
namespace Stardust {
using json=nlohmann::json;
namespace {
const char* kParameterTypes[] = { "Float", "Int", "Bool", "Trigger", "Vector2" };

// Parameters a condition or a blend tree can read: a Vector2 shows up as its .x and .y floats
std::vector<std::pair<std::string, std::string>> ScalarParameters(const json& parameters) {
    std::vector<std::pair<std::string, std::string>> list;
    for (const json& p : parameters) {
        auto name = p.value("name", std::string()), type = p.value("type", std::string("Float"));
        if (type == "Vector2") { list.push_back({ name + ".x", "Float" }); list.push_back({ name + ".y", "Float" }); }
        else list.push_back({ name, type });
    }
    return list;
}
std::string ParameterType(const json& parameters, const std::string& name) {
    for (auto& [n, t] : ScalarParameters(parameters)) if (n == name) return t;
    return {};
}
float ParameterDefault(const json& parameters, const std::string& name) {
    for (const json& p : parameters) {
        auto pname = p.value("name", std::string());
        auto d = p.find("default");
        if (pname == name) return d != p.end() && d->is_number() ? d->get<float>() : 0.f;
        if (p.value("type", std::string()) == "Vector2" && (name == pname + ".x" || name == pname + ".y"))
            return d != p.end() && d->is_array() && d->size() == 2 ? (*d)[name.back() == 'y' ? 1 : 0].get<float>() : 0.f;
    }
    return 0.f;
}
void SetParameterDefault(json& parameters, const std::string& name, float value) {
    for (json& p : parameters) {
        auto pname = p.value("name", std::string());
        if (pname == name) { p["default"] = value; return; }
        if (p.value("type", std::string()) == "Vector2" && (name == pname + ".x" || name == pname + ".y")) {
            if (!p.contains("default") || !p["default"].is_array() || p["default"].size() != 2) p["default"] = json::array({ 0.f, 0.f });
            p["default"][name.back() == 'y' ? 1 : 0] = value;
            return;
        }
    }
}
// Live values while the preview or Play mode runs, the asset's defaults otherwise
float ReadParameter(const json& parameters, AnimatorComponent* live, const std::string& name) {
    if (live) { auto f = live->parameters.find(name); if (f != live->parameters.end()) return f->second; }
    return ParameterDefault(parameters, name);
}
void WriteParameter(json& parameters, AnimatorComponent* live, const std::string& name, float value, bool& dirty) {
    if (live) live->parameters[name] = value;
    else { SetParameterDefault(parameters, name, value); dirty = true; }
}
std::unordered_map<std::string, float> ParameterValues(const json& parameters, AnimatorComponent* live) {
    std::unordered_map<std::string, float> values;
    for (auto& [name, type] : ScalarParameters(parameters)) values[name] = ReadParameter(parameters, live, name);
    return values;
}

void DrawBlendTreeEditor(json& tree, json& parameters, AnimatorComponent* live, bool& dirty) {
    static const char* types[] = { "1D", "2D Simple Directional", "2D Freeform Directional", "2D Freeform Cartesian" };
    std::string type = tree.value("type", std::string("1D"));
    int typeIndex = 0;
    for (int i = 0; i < 4; ++i) if (type == types[i]) typeIndex = i;
    ImGui::SeparatorText("Blend Tree");
    if (Insp::Combo("Blend Type", &typeIndex, "1D\0" "2D Simple Directional\0" "2D Freeform Directional\0" "2D Freeform Cartesian\0")) { tree["type"] = types[typeIndex]; type = types[typeIndex]; dirty = true; }
    const bool twoD = type != "1D";
    auto scalars = ScalarParameters(parameters);
    auto parameterCombo = [&](const char* label, const char* key) {
        auto current = tree.value(key, std::string());
        if (!Insp::BeginCombo(label, current.empty() ? "(none)" : current.c_str())) return;
        // A Vector2 fills both axes at once
        if (twoD && std::string(key) == "parameter")
            for (const json& p : parameters) if (p.value("type", std::string()) == "Vector2") {
                auto name = p.value("name", std::string());
                if (ImGui::Selectable((name + " (Vector2)").c_str(), current == name + ".x")) { tree["parameter"] = name + ".x"; tree["parameterY"] = name + ".y"; dirty = true; }
            }
        for (auto& [name, ptype] : scalars) if (ptype == "Float" || ptype == "Int")
            if (ImGui::Selectable(name.c_str(), current == name)) { tree[key] = name; dirty = true; }
        ImGui::EndCombo();
    };
    parameterCombo(twoD ? "Parameter X" : "Parameter", "parameter");
    if (twoD) parameterCombo("Parameter Y", "parameterY");
    if (!tree.contains("children") || !tree["children"].is_array()) tree["children"] = json::array();
    json& children = tree["children"];

    auto values = ParameterValues(parameters, live);
    glm::vec2 point = AnimatorSystem::BlendPoint(tree, values);
    std::vector<float> weights = AnimatorSystem::BlendWeights(tree, point);
    auto childPosition = [&](const json& child) { auto p = child.value("position", json::array({ 0.f, 0.f })); return p.is_array() && p.size() == 2 ? glm::vec2(p[0].get<float>(), p[1].get<float>()) : glm::vec2(0); };

    // The blend space: drag the red marker to try values
    auto* draw = ImGui::GetWindowDrawList();
    const float width = std::max(120.f, ImGui::GetContentRegionAvail().x);
    const ImU32 background = IM_COL32(30, 32, 35, 255), axis = IM_COL32(60, 63, 68, 255), motion = IM_COL32(90, 170, 255, 255), marker = IM_COL32(235, 80, 70, 255);
    if (!children.empty()) {
        if (twoD) {
            const float size = std::min(width, 250.f);
            float extent = std::max({ std::abs(point.x), std::abs(point.y), .001f });
            for (const json& c : children) { auto p = childPosition(c); extent = std::max({ extent, std::abs(p.x), std::abs(p.y) }); }
            extent *= 1.15f;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##blendSpace", { size, size });
            ImVec2 center(origin.x + size * .5f, origin.y + size * .5f);
            auto toScreen = [&](glm::vec2 p) { return ImVec2(center.x + p.x / extent * size * .5f, center.y - p.y / extent * size * .5f); };
            if (ImGui::IsItemActive()) {
                auto m = ImGui::GetMousePos();
                glm::vec2 value((m.x - center.x) / (size * .5f) * extent, -(m.y - center.y) / (size * .5f) * extent);
                WriteParameter(parameters, live, tree.value("parameter", std::string()), value.x, dirty);
                WriteParameter(parameters, live, tree.value("parameterY", std::string()), value.y, dirty);
                point = value; weights = AnimatorSystem::BlendWeights(tree, point);
            }
            draw->AddRectFilled(origin, { origin.x + size, origin.y + size }, background, 4);
            draw->AddLine({ center.x, origin.y }, { center.x, origin.y + size }, axis);
            draw->AddLine({ origin.x, center.y }, { origin.x + size, center.y }, axis);
            for (size_t i = 0; i < children.size(); ++i) {
                auto p = toScreen(childPosition(children[i]));
                float w = i < weights.size() ? weights[i] : 0.f;
                if (w > .001f) draw->AddCircleFilled(p, 4 + w * size * .12f, IM_COL32(90, 170, 255, 70));
                draw->AddCircleFilled(p, 4, motion);
            }
            auto m = toScreen(point);
            draw->AddQuadFilled({ m.x, m.y - 6 }, { m.x + 6, m.y }, { m.x, m.y + 6 }, { m.x - 6, m.y }, marker);
        } else {
            float lo = point.x, hi = point.x;
            for (const json& c : children) { float t = c.value("threshold", 0.f); lo = std::min(lo, t); hi = std::max(hi, t); }
            if (hi - lo < .001f) hi = lo + 1;
            const float height = 46;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##blendLine", { width, height });
            auto toX = [&](float v) { return origin.x + 8 + (v - lo) / (hi - lo) * (width - 16); };
            if (ImGui::IsItemActive()) {
                float value = lo + (ImGui::GetMousePos().x - origin.x - 8) / (width - 16) * (hi - lo);
                WriteParameter(parameters, live, tree.value("parameter", std::string()), value, dirty);
                point.x = value; weights = AnimatorSystem::BlendWeights(tree, point);
            }
            draw->AddRectFilled(origin, { origin.x + width, origin.y + height }, background, 4);
            // Each motion's influence is a tent between its neighbours' thresholds
            std::vector<float> sorted;
            for (const json& c : children) sorted.push_back(c.value("threshold", 0.f));
            std::sort(sorted.begin(), sorted.end());
            for (size_t i = 0; i < children.size(); ++i) {
                float t = children[i].value("threshold", 0.f);
                auto at = std::lower_bound(sorted.begin(), sorted.end(), t);
                float before = at == sorted.begin() ? t : *(at - 1), after = at + 1 == sorted.end() ? t : *(at + 1);
                float top = origin.y + 6, bottom = origin.y + height - 6;
                ImU32 color = i < weights.size() && weights[i] > .001f ? motion : IM_COL32(90, 120, 150, 255);
                draw->AddLine({ toX(before), bottom }, { toX(t), top }, color, 1.5f);
                draw->AddLine({ toX(t), top }, { toX(after), bottom }, color, 1.5f);
            }
            draw->AddLine({ toX(point.x), origin.y }, { toX(point.x), origin.y + height }, marker, 2);
        }
        ImGui::TextDisabled(twoD ? "Value %.2f, %.2f (drag to preview)" : "Value %.2f (drag to preview)", point.x, point.y);
    }

    ImGui::SeparatorText("Motions");
    int remove = -1;
    for (int i = 0; i < (int)children.size(); ++i) {
        json& child = children[i];
        ImGui::PushID(i);
        std::string clip = child.value("clip", std::string());
        if (AssetUI::AssetField("Motion", clip, { ".anim" })) { child["clip"] = clip; dirty = true; }
        if (twoD) {
            glm::vec2 p = childPosition(child);
            if (Insp::DragFloat2("Position", &p.x, .01f)) { child["position"] = { p.x, p.y }; dirty = true; }
        } else {
            float threshold = child.value("threshold", 0.f);
            if (Insp::DragFloat("Threshold", &threshold, .01f)) { child["threshold"] = threshold; dirty = true; }
        }
        float speed = child.value("speed", 1.f);
        if (Insp::DragFloat("Speed", &speed, .01f, -10, 10)) { child["speed"] = speed; dirty = true; }
        ImGui::TextDisabled("Weight %.2f", i < (int)weights.size() ? weights[i] : 0.f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) remove = i;
        ImGui::Separator();
        ImGui::PopID();
    }
    if (remove >= 0) { children.erase(children.begin() + remove); dirty = true; }
    if (ImGui::Button("+ Add Motion")) {
        json child = { { "clip", "" }, { "speed", 1.f } };
        // Spread new motions out so they do not all sit on the same spot
        const float index = (float)children.size();
        child["threshold"] = index;
        static const float ring[][2] = { { 0, 0 }, { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } };
        const auto& r = ring[children.size() % 9];
        child["position"] = { r[0], r[1] };
        children.push_back(child);
        dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("You can also drop .anim files on the Motion slots");
}

// Transition graph: the source state, the destination and where they overlap
void DrawTransitionTimeline(const json& t, const json& states, const std::unordered_map<std::string, float>& values) {
    auto from = t.value("from", std::string()), to = t.value("to", std::string());
    auto length = [&](const std::string& name) { return states.contains(name) ? std::max(.01f, AnimatorSystem::StateDuration(states[name], values)) : 1.f; };
    const float destination = length(to), source = from == "Any State" ? destination : length(from);
    const float exitAt = t.value("hasExitTime", false) ? t.value("exitTime", 1.f) * source : 0.f;
    const float blend = std::max(0.f, t.value("duration", 0.f)) * (t.value("fixedDuration", true) ? 1.f : source);
    const float offset = std::max(0.f, t.value("offset", 0.f));
    const float total = std::max({ source, exitAt + blend, exitAt + std::max(.01f, destination - offset) }) * 1.05f;
    const float width = std::max(120.f, ImGui::GetContentRegionAvail().x), row = 18;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy({ width, row * 2 + 10 });
    auto* draw = ImGui::GetWindowDrawList();
    auto x = [&](float seconds) { return origin.x + seconds / total * width; };
    draw->AddRectFilled(origin, { origin.x + width, origin.y + row * 2 + 10 }, IM_COL32(30, 32, 35, 255), 3);
    const float sourceEnd = from == "Any State" ? exitAt : std::max(exitAt + blend, std::min(source, exitAt + blend));
    draw->AddRectFilled({ origin.x, origin.y + 3 }, { x(sourceEnd), origin.y + 3 + row }, IM_COL32(70, 90, 115, 255), 3);
    draw->AddText({ origin.x + 4, origin.y + 5 }, IM_COL32(230, 230, 230, 255), from.c_str());
    const float start = exitAt, end = exitAt + std::max(.01f, destination - offset);
    draw->AddRectFilled({ x(start), origin.y + 7 + row }, { x(end), origin.y + 7 + row * 2 }, IM_COL32(70, 115, 85, 255), 3);
    draw->AddText({ x(start) + 4, origin.y + 9 + row }, IM_COL32(230, 230, 230, 255), to.c_str());
    if (blend > 0) draw->AddRectFilledMultiColor({ x(exitAt), origin.y + 3 }, { x(exitAt + blend), origin.y + 7 + row * 2 }, IM_COL32(90, 170, 255, 90), IM_COL32(90, 170, 255, 10), IM_COL32(90, 170, 255, 10), IM_COL32(90, 170, 255, 90));
    draw->AddLine({ x(exitAt), origin.y }, { x(exitAt), origin.y + row * 2 + 10 }, IM_COL32(240, 200, 80, 255), 1.5f);
    ImGui::TextDisabled("Starts at %.2fs, blends for %.2fs", exitAt, blend);
}
}

void EditorApp::DrawControllerEditor(json& asset,bool& dirty){
 static std::string loaded,selected,connectFrom,paramName="New Parameter",newName="New State";
 static int transition=-1,paramType=0;static ImVec2 pan{0,0};static float zoom=1;static bool frameAll=true;
 if(loaded!=m_AnimatorAsset){loaded=m_AnimatorAsset;selected.clear();transition=-1;connectFrom.clear();pan={0,0};zoom=1;frameAll=true;}
 static int layerIndex=0,pendingDelete=-1,renameLayer=-2,renameParameter=-1;static bool pendingAdd=false,focusRename=false;static std::string renameText;
 if(loaded.empty())layerIndex=0;
 if(!asset.contains("layers")||!asset["layers"].is_array())asset["layers"]=json::array();
 if(pendingAdd){pendingAdd=false;std::string name="New Layer";int n=1;for(bool taken=true;taken;){taken=false;for(auto& l:asset["layers"])if(l.value("name","")==name){taken=true;name=std::format("New Layer {}",n++);break;}}asset["layers"].push_back({{"name",name},{"weight",1.f},{"blending","Override"},{"mask",json::array()},{"defaultState",""},{"states",json::object()},{"transitions",json::array()}});layerIndex=(int)asset["layers"].size();selected.clear();transition=-1;dirty=true;}
 if(pendingDelete>0){if(pendingDelete<=(int)asset["layers"].size())asset["layers"].erase(asset["layers"].begin()+pendingDelete-1);pendingDelete=-1;layerIndex=0;selected.clear();transition=-1;dirty=true;}
 if(layerIndex>(int)asset["layers"].size())layerIndex=0;
 json& layer=layerIndex==0?asset:asset["layers"][layerIndex-1];
 if(!layer.contains("states")||!layer["states"].is_object())layer["states"]=json::object();
 if(!layer.contains("transitions")||!layer["transitions"].is_array())layer["transitions"]=json::array();
 if(!asset.contains("parameters")||!asset["parameters"].is_array())asset["parameters"]=json::array();
 auto& states=layer["states"];auto& transitions=layer["transitions"];auto& parameters=asset["parameters"];
 auto layerName=[&](int index){return index==0?asset.value("baseLayerName",std::string("Base Layer")):asset["layers"][index-1].value("name",std::string("Layer"));};
 auto renameParameterEverywhere=[&](const std::string& from,const std::string& to){
  auto map=[&](json& field){if(!field.is_string())return;auto v=field.get<std::string>();if(v==from)field=to;else if(v.starts_with(from+"."))field=to+v.substr(from.size());};
  auto fix=[&](json& owner){if(owner.contains("transitions"))for(auto& t:owner["transitions"]){if(t.contains("parameter"))map(t["parameter"]);if(t.contains("conditions"))for(auto& c:t["conditions"])if(c.contains("parameter"))map(c["parameter"]);}
   if(owner.contains("states")&&owner["states"].is_object())for(auto& [n,st]:owner["states"].items())if(st.contains("blendTree")){auto& tree=st["blendTree"];if(tree.contains("parameter"))map(tree["parameter"]);if(tree.contains("parameterY"))map(tree["parameterY"]);}};
  fix(asset);for(auto& l:asset["layers"])fix(l);};
 for(auto& t:transitions)if(t.contains("parameter")){auto name=t.value("parameter","");bool exists=false;for(auto& p:parameters)if(p.value("name","")==name)exists=true;if(!name.empty()&&!exists){parameters.push_back({{"name",name},{"type","Float"},{"default",0.f}});dirty=true;}}

 AnimatorComponent* live=nullptr;
 if(m_AnimationPreviewScene)live=m_AnimationPreviewScene->Reg().TryGet<AnimatorComponent>(m_AnimationPreviewRoot);
 else if(m_State!=PlayState::Edit)live=ActiveScene().Reg().TryGet<AnimatorComponent>(m_AnimatorRoot);
 std::string* liveState=nullptr;float liveTime=0;if(live){if(layerIndex==0){liveState=&live->state;liveTime=live->time;}else if(layerIndex<=(int)live->layers.size()){liveState=&live->layers[layerIndex-1].state;liveTime=live->layers[layerIndex-1].time;}}
 float height=std::max(180.f,ImGui::GetContentRegionAvail().y);
 ImGui::BeginChild("ControllerSidebar",{210,height},ImGuiChildFlags_Borders);
 {static int controllerTab=0;const char* controllerTabs[]={"Layers","Parameters"};EditorIcons::SettingsTabs("ControllerTabs",controllerTab,controllerTabs,2);
  if(controllerTab==0){
  if(ImGui::SmallButton("+ Add Layer"))pendingAdd=true;
  for(int i=0;i<=(int)asset["layers"].size();++i){ImGui::PushID(i);std::string name=layerName(i);
   if(renameLayer==i){ImGui::SetNextItemWidth(-1);if(focusRename){ImGui::SetKeyboardFocusHere();focusRename=false;}bool done=ImGui::InputText("##layerName",&renameText,ImGuiInputTextFlags_EnterReturnsTrue);bool cancel=ImGui::IsKeyPressed(ImGuiKey_Escape);if((done||(ImGui::IsItemDeactivated()&&!cancel))&&!renameText.empty()){if(i==0)asset["baseLayerName"]=renameText;else asset["layers"][i-1]["name"]=renameText;dirty=true;}if(done||cancel||ImGui::IsItemDeactivated())renameLayer=-2;}
   else{if(ImGui::Selectable(name.c_str(),layerIndex==i)&&layerIndex!=i){layerIndex=i;selected.clear();transition=-1;connectFrom.clear();frameAll=true;}if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){renameLayer=i;renameText=name;focusRename=true;}
    if(ImGui::BeginPopupContextItem("LayerMenu")){if(ImGui::MenuItem("Rename")){renameLayer=i;renameText=name;focusRename=true;}if(i>0&&ImGui::MenuItem("Delete Layer"))pendingDelete=i;ImGui::EndPopup();}}
   ImGui::PopID();}
  ImGui::Separator();
  if(layerIndex==0)ImGui::TextDisabled("Weight: 1.0 (base layer)");
  else{float weight=layer.value("weight",1.f);if(Insp::SliderFloat("Weight",&weight,0,1,"%.2f")){layer["weight"]=weight;dirty=true;}
   int blending=layer.value("blending",std::string("Override"))=="Additive"?1:0;if(Insp::Combo("Blending",&blending,"Override\0Additive\0")){layer["blending"]=blending?"Additive":"Override";dirty=true;}
   if(!layer.contains("mask")||!layer["mask"].is_array())layer["mask"]=json::array();
   ImGui::SeparatorText("Mask");if(layer["mask"].empty())ImGui::TextDisabled("Whole body");int removeMask=-1;
   for(int i=0;i<(int)layer["mask"].size();++i){ImGui::PushID(i);if(ImGui::SmallButton("X"))removeMask=i;ImGui::SameLine();auto path=layer["mask"][i].get<std::string>();ImGui::TextUnformatted(path.empty()?"(root)":path.substr(path.rfind('/')==std::string::npos?0:path.rfind('/')+1).c_str());if(ImGui::IsItemHovered()&&!path.empty())ImGui::SetTooltip("%s",path.c_str());ImGui::PopID();}
   if(removeMask>=0){layer["mask"].erase(layer["mask"].begin()+removeMask);dirty=true;}
   Scene& maskScene=ActiveScene();bool canAdd=m_AnimatorRoot&&maskScene.Reg().Valid(m_Selected)&&(m_Selected==m_AnimatorRoot||maskScene.IsDescendantOf(m_Selected,m_AnimatorRoot));
   ImGui::BeginDisabled(!canAdd);if(ImGui::SmallButton("Add Selected Bone")){std::string path;for(EntityID node=m_Selected;node&&node!=m_AnimatorRoot;node=maskScene.GetParent(node)){const auto& part=maskScene.Reg().Get<NameComponent>(node).name;path=path.empty()?part:part+"/"+path;}layer["mask"].push_back(path);dirty=true;}ImGui::EndDisabled();
   if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("The layer affects this object and its children only");}
  }
 if(controllerTab==1){
  ImGui::SetNextItemWidth(-1);Insp::InputTextWithHint("##parameterName","Parameter name",&paramName);
  ImGui::SetNextItemWidth(115);Insp::Combo("##type",&paramType,"Float\0Int\0Bool\0Trigger\0Vector2\0");ImGui::SameLine();
  if(ImGui::Button("+")){bool unique=!paramName.empty();for(auto& p:parameters)if(p.value("name","")==paramName)unique=false;if(unique){parameters.push_back({{"name",paramName},{"type",kParameterTypes[std::clamp(paramType,0,4)]},{"default",paramType==4?json::array({0.f,0.f}):json(0.f)}});dirty=true;}}
  ImGui::Separator();int remove=-1;
  for(int i=0;i<(int)parameters.size();++i){auto& p=parameters[i];ImGui::PushID(i);auto name=p.value("name","Parameter"),type=p.value("type","Float");float value=type=="Vector2"?0.f:ReadParameter(parameters,live,name);if(renameParameter==i){ImGui::SetNextItemWidth(100);if(focusRename){ImGui::SetKeyboardFocusHere();focusRename=false;}bool done=ImGui::InputText("##rename",&renameText,ImGuiInputTextFlags_EnterReturnsTrue);bool cancel=ImGui::IsKeyPressed(ImGuiKey_Escape);if(done||(ImGui::IsItemDeactivated()&&!cancel)){bool unique=!renameText.empty();for(auto& other:parameters)if(&other!=&p&&other.value("name","")==renameText)unique=false;if(unique&&renameText!=name){p["name"]=renameText;renameParameterEverywhere(name,renameText);if(live){auto value=live->parameters.find(name);if(value!=live->parameters.end()){float v=value->second;live->parameters.erase(value);live->parameters[renameText]=v;}}name=renameText;dirty=true;}}if(done||cancel||ImGui::IsItemDeactivated())renameParameter=-1;}
   else{ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(name.c_str());if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){renameParameter=i;renameText=name;focusRename=true;}ImGui::OpenPopupOnItemClick("ParameterMenu",ImGuiPopupFlags_MouseButtonRight);}ImGui::SameLine(110);ImGui::SetNextItemWidth(65);bool changed=false;
   if(type=="Vector2"){float v[2]={ReadParameter(parameters,live,name+".x"),ReadParameter(parameters,live,name+".y")};if(ImGui::DragFloat2("##value",v,.02f,0,0,"%.1f")){WriteParameter(parameters,live,name+".x",v[0],dirty);WriteParameter(parameters,live,name+".y",v[1],dirty);}}
   else if(type=="Bool"){bool b=value!=0;changed=Insp::Checkbox("##value",&b);value=b?1.f:0.f;}else if(type=="Trigger"){if(ImGui::SmallButton("Fire")){value=1;changed=true;}}else if(type=="Int"){int v=(int)value;changed=Insp::DragInt("##value",&v);value=(float)v;}else changed=Insp::DragFloat("##value",&value,.05f);
   if(changed){if(live)live->parameters[name]=value;else{p["default"]=value;dirty=true;}}
   ImGui::OpenPopupOnItemClick("ParameterMenu",ImGuiPopupFlags_MouseButtonRight);ImGui::SameLine(0,4);if(ImGui::SmallButton("X"))remove=i;if(ImGui::IsItemHovered())ImGui::SetTooltip("Delete parameter");
   if(ImGui::BeginPopup("ParameterMenu")){if(ImGui::MenuItem("Rename")){renameParameter=i;renameText=name;focusRename=true;}if(ImGui::MenuItem("Delete"))remove=i;ImGui::EndPopup();}ImGui::PopID();
  }if(remove>=0){auto removed=parameters[remove].value("name",std::string());parameters.erase(parameters.begin()+remove);
   // Conditions on a deleted parameter could never pass, and the old single-parameter form would bring it back
   auto gone=[&](const std::string& v){return v==removed||v.starts_with(removed+".");};
   auto purge=[&](json& list){for(auto& t:list){if(gone(t.value("parameter",""))){t.erase("parameter");t.erase("threshold");}if(t.contains("conditions")&&t["conditions"].is_array()){auto& cs=t["conditions"];for(int c=(int)cs.size()-1;c>=0;--c)if(gone(cs[c].value("parameter","")))cs.erase(cs.begin()+c);}}};
   if(asset.contains("transitions"))purge(asset["transitions"]);for(auto& l:asset["layers"])if(l.contains("transitions"))purge(l["transitions"]);if(live){live->parameters.erase(removed);live->parameters.erase(removed+".x");live->parameters.erase(removed+".y");}if(renameParameter>=remove)renameParameter=-1;dirty=true;}
  if(live)ImGui::TextDisabled("Live parameter values");
 }}
 ImGui::EndChild();ImGui::SameLine();
 float graphWidth=std::max(180.f,ImGui::GetContentRegionAvail().x-275);
 ImGui::BeginChild("ControllerGraph",{graphWidth,height},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
 ImGui::TextUnformatted(layerName(layerIndex).c_str());ImGui::SameLine();if(ImGui::SmallButton("Frame All"))frameAll=true;ImGui::SameLine();ImGui::TextDisabled("%.0f%%",zoom*100);
 if(!connectFrom.empty())ImGui::TextColored({1,.8f,.3f,1},"Select destination for %s (Esc cancels)",connectFrom.c_str());
 auto origin=ImGui::GetCursorScreenPos(),avail=ImGui::GetContentRegionAvail();
 if(frameAll){float minX=0,minY=0,maxX=600,maxY=180;int index=0;for(auto& [name,node]:states.items()){auto p=node.value("editorPosition",json::array({220+(index%2)*210,130+(index/2)*100}));float x=p[0],y=p[1];minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x+160);maxY=std::max(maxY,y+44);++index;}zoom=std::clamp(std::min((avail.x-24)/std::max(1.f,maxX-minX),(avail.y-24)/std::max(1.f,maxY-minY)),.2f,1.f);pan={12-minX*zoom,12-minY*zoom};frameAll=false;}
auto* draw=ImGui::GetWindowDrawList();auto mouse=ImGui::GetMousePos();auto& io=ImGui::GetIO();
 bool hovered=ImGui::IsWindowHovered();if(hovered&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){pan.x+=io.MouseDelta.x;pan.y+=io.MouseDelta.y;}
 if(hovered&&io.MouseWheel!=0){float old=zoom;zoom=std::clamp(zoom*std::pow(1.12f,io.MouseWheel),.35f,2.f);pan.x=(pan.x-(mouse.x-origin.x))*zoom/old+(mouse.x-origin.x);pan.y=(pan.y-(mouse.y-origin.y))*zoom/old+(mouse.y-origin.y);}
 if(ImGui::IsKeyPressed(ImGuiKey_Escape))connectFrom.clear();
 draw->PushClipRect(origin,{origin.x+avail.x,origin.y+avail.y},true);draw->AddRectFilled(origin,{origin.x+avail.x,origin.y+avail.y},IM_COL32(26, 28, 31, 255));
 float step=20*zoom;for(float x=std::fmod(pan.x,step);x<avail.x;x+=step)draw->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+avail.y},IM_COL32(35, 37, 40, 255));for(float y=std::fmod(pan.y,step);y<avail.y;y+=step)draw->AddLine({origin.x,origin.y+y},{origin.x+avail.x,origin.y+y},IM_COL32(35, 37, 40, 255));
 std::unordered_map<std::string,ImVec2> positions;auto screen=[&](float x,float y){return ImVec2(origin.x+pan.x+x*zoom,origin.y+pan.y+y*zoom);};
 positions["Entry"]=screen(30,75);positions["Any State"]=screen(30,5);positions["Exit"]=screen(440,5);
 int index=0;for(auto& [name,node]:states.items()){auto pos=node.value("editorPosition",json::array({220+(index%2)*210,130+(index/2)*100}));positions[name]=screen(pos[0].get<float>(),pos[1].get<float>());++index;}
 auto arrow=[&](ImVec2 a,ImVec2 b,ImU32 color){glm::vec2 delta(b.x-a.x,b.y-a.y);float len=glm::length(delta);if(len<1)return;auto n=delta/len;draw->AddLine(a,b,color,2);ImVec2 m((a.x+b.x)*.5f,(a.y+b.y)*.5f);draw->AddTriangleFilled({m.x+n.x*8,m.y+n.y*8},{m.x-n.x*6-n.y*5,m.y-n.y*6+n.x*5},{m.x-n.x*6+n.y*5,m.y-n.y*6-n.x*5},color);};
 auto center=[&](const std::string& n){auto p=positions.at(n);return ImVec2(p.x+80*zoom,p.y+22*zoom);};
 auto entry=layer.value("defaultState","");if(positions.contains(entry))arrow(center("Entry"),center(entry),IM_COL32(225,170,55,255));
 for(int i=0;i<(int)transitions.size();++i){auto& t=transitions[i];auto from=t.value("from",""),to=t.value("to","");if(!positions.contains(from)||!positions.contains(to))continue;auto a=center(from),b=center(to);{glm::vec2 dir(b.x-a.x,b.y-a.y);float len=std::max(1.f,glm::length(dir));glm::vec2 side(-dir.y/len*6*zoom,dir.x/len*6*zoom);a.x+=side.x;a.y+=side.y;b.x+=side.x;b.y+=side.y;}arrow(a,b,transition==i?IM_COL32(80,180,255,255):IM_COL32(205,205,205,255));glm::vec2 ab(b.x-a.x,b.y-a.y),am(mouse.x-a.x,mouse.y-a.y);float k=std::clamp(glm::dot(am,ab)/std::max(1.f,glm::dot(ab,ab)),0.f,1.f);if(hovered&&glm::length(am-ab*k)<6&&ImGui::IsMouseClicked(0)){transition=i;selected.clear();}}
 if(!connectFrom.empty()&&positions.contains(connectFrom))arrow(center(connectFrom),mouse,IM_COL32(240,240,240,255));
 std::string remove,makeDefault;
 auto node=[&](const std::string& name,bool special){auto p=positions.at(name);ImGui::SetCursorScreenPos(p);ImGui::PushID(name.c_str());ImGui::InvisibleButton("Node",{160*zoom,44*zoom});bool active=liveState&&*liveState==name;ImU32 color=name=="Entry"?IM_COL32(55,160,65,255):name=="Any State"?IM_COL32(60,140,155,255):name=="Exit"?IM_COL32(150,65,65,255):active?IM_COL32(55,135,70,255):entry==name?IM_COL32(190,130,40,255):IM_COL32(69, 71, 74, 255);draw->AddRectFilled(p,{p.x+160*zoom,p.y+44*zoom},color,8*zoom);draw->AddRect(p,{p.x+160*zoom,p.y+44*zoom},selected==name?IM_COL32(90,195,255,255):IM_COL32(19, 21, 24, 255),8*zoom,0,selected==name?2:1);const bool isTree=!special&&states[name].contains("blendTree");auto ts=ImGui::CalcTextSize(name.c_str());draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()*zoom,{p.x+80*zoom-ts.x*zoom*.5f,p.y+(isTree?16:22)*zoom-ts.y*zoom*.5f},IM_COL32(245,245,245,255),name.c_str());
 if(isTree){const char* sub="Blend Tree";auto ss=ImGui::CalcTextSize(sub);float scale=zoom*.8f;draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()*scale,{p.x+80*zoom-ss.x*scale*.5f,p.y+29*zoom-ss.y*scale*.5f},IM_COL32(175,180,190,255),sub);}
 if(active&&!special){float clipLength=std::max(.001f,AnimatorSystem::StateDuration(states[name],live->parameters));float k=std::fmod(liveTime,clipLength)/clipLength;draw->AddRectFilled({p.x+6*zoom,p.y+36*zoom},{p.x+6*zoom+148*zoom*k,p.y+40*zoom},IM_COL32(90,190,255,255),2);}
 if(!special&&ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){auto clip=states[name].value("clip","");if(!clip.empty())m_AnimationAsset=clip;}
 if(ImGui::IsItemClicked(0)){if(!connectFrom.empty()&&name!="Entry"&&name!="Any State"){transitions.push_back({{"from",connectFrom},{"to",name},{"conditions",json::array()},{"hasExitTime",connectFrom!="Any State"},{"exitTime",1.f}});transition=(int)transitions.size()-1;connectFrom.clear();dirty=true;}else{selected=name;transition=-1;if(m_AnimationPreviewScene&&!special&&layerIndex==0){m_AnimationPreviewState=name;m_AnimationPreviewTime=0;}}}
 if(!special&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){states[name]["editorPosition"]={(p.x-origin.x-pan.x+io.MouseDelta.x)/zoom,(p.y-origin.y-pan.y+io.MouseDelta.y)/zoom};dirty=true;}
 if(ImGui::BeginPopupContextItem("NodeMenu")){if(name!="Entry"&&name!="Exit"&&ImGui::MenuItem("Make Transition"))connectFrom=name;if(!special){if(ImGui::MenuItem("Set as Default State"))makeDefault=name;if(ImGui::MenuItem("Delete State"))remove=name;}ImGui::EndPopup();}ImGui::PopID();};
 node("Entry",true);node("Any State",true);node("Exit",true);for(auto& [name,value]:states.items())node(name,false);
 draw->PopClipRect();ImGui::SetCursorScreenPos(origin);ImGui::Dummy(avail);
 if(ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)){std::string dropped((const char*)payload->Data);std::vector<std::string> clips;if(dropped.ends_with(".anim"))clips.push_back(dropped);else if(dropped.ends_with(".fbx")){std::string error;clips=AvatarSystem::ImportClips(dropped,&error);if(clips.empty())SD_WARN("{}",error);}float offsetY=0;for(auto& clip:clips){auto stem=FileSystem::Utf8(std::filesystem::u8path(clip).stem()),unique=stem;for(int n=1;states.contains(unique);++n)unique=std::format("{} {}",stem,n);bool loop=true;try{auto data=json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(clip)),nullptr,false);if(data.is_object())loop=data.value("loop",true);}catch(...){}states[unique]={{"clip",clip},{"loop",loop},{"editorPosition",{(mouse.x-origin.x-pan.x)/zoom,(mouse.y-origin.y-pan.y)/zoom+offsetY}}};offsetY+=60;if(states.size()==1)layer["defaultState"]=unique;selected=unique;dirty=true;}}ImGui::EndDragDropTarget();}
 if(ImGui::BeginPopupContextWindow("GraphMenu",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){ImGui::TextDisabled("New State");ImGui::SetNextItemWidth(220);if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();bool confirmName=ImGui::InputText("##stateName",&newName,ImGuiInputTextFlags_EnterReturnsTrue);if(confirmName)ImGui::CloseCurrentPopup();if((ImGui::MenuItem("Create State")||confirmName)&&!newName.empty()&&!states.contains(newName)&&newName!="Entry"&&newName!="Exit"&&newName!="Any State"){states[newName]={{"clip",""},{"loop",true},{"editorPosition",{(mouse.x-origin.x-pan.x)/zoom,(mouse.y-origin.y-pan.y)/zoom}}};if(states.size()==1)layer["defaultState"]=newName;selected=newName;dirty=true;}
 if(ImGui::MenuItem("Create Blend Tree")){std::string name=newName.empty()||newName=="New State"?std::string("Blend Tree"):newName;for(int k=1;states.contains(name)||name=="Entry"||name=="Exit"||name=="Any State";++k)name=std::format("Blend Tree {}",k);std::string axis;for(auto& [pn,pt]:ScalarParameters(parameters))if(pt=="Float"){axis=pn;break;}
  states[name]={{"loop",true},{"blendTree",{{"type","1D"},{"parameter",axis},{"children",json::array()}}},{"editorPosition",{(mouse.x-origin.x-pan.x)/zoom,(mouse.y-origin.y-pan.y)/zoom}}};if(states.size()==1)layer["defaultState"]=name;selected=name;transition=-1;dirty=true;}
 ImGui::EndPopup();}
 ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("ControllerProperties",{0,height},ImGuiChildFlags_Borders);
 if(!makeDefault.empty()){layer["defaultState"]=makeDefault;dirty=true;}
 if(!remove.empty()){states.erase(remove);for(int i=(int)transitions.size()-1;i>=0;--i)if(transitions[i].value("from","")==remove||transitions[i].value("to","")==remove)transitions.erase(transitions.begin()+i);if(entry==remove)layer["defaultState"]=states.empty()?std::string():states.begin().key();selected.clear();transition=-1;dirty=true;}
 int pickTransition=-1;
 if(states.contains(selected)){auto& n=states[selected];ImGui::SeparatorText("State");ImGui::TextWrapped("%s",selected.c_str());
  if(n.contains("blendTree")&&n["blendTree"].is_object()){DrawBlendTreeEditor(n["blendTree"],parameters,live,dirty);
   if(ImGui::Button("Convert to Single Motion")){auto children=n["blendTree"].value("children",json::array());n["clip"]=children.empty()?std::string():children[0].value("clip",std::string());n.erase("blendTree");dirty=true;}}
  else{std::string clip=n.value("clip","");if(AssetUI::AssetField("Motion",clip,{".anim"})){n["clip"]=clip;dirty=true;}
   if(ImGui::Button("Convert to Blend Tree")){std::string axis;for(auto& [pn,pt]:ScalarParameters(parameters))if(pt=="Float"){axis=pn;break;}json tree={{"type","1D"},{"parameter",axis},{"children",json::array()}};if(!clip.empty())tree["children"].push_back({{"clip",clip},{"threshold",0.f},{"position",{0.f,0.f}},{"speed",1.f}});n["blendTree"]=tree;n.erase("clip");dirty=true;}
   if(ImGui::IsItemHovered())ImGui::SetTooltip("Blend several clips by one or two parameters, e.g. walk and run by Speed");}
  bool loop=n.value("loop",true);if(Insp::Checkbox("Loop",&loop)){n["loop"]=loop;dirty=true;}if(ImGui::Button("Make Transition"))connectFrom=selected;ImGui::SameLine();if(ImGui::Button("Set as Default")){layer["defaultState"]=selected;dirty=true;}
  ImGui::SeparatorText("Transitions");bool any=false;
  for(int i=0;i<(int)transitions.size();++i){auto& tr=transitions[i];if(tr.value("from","")!=selected)continue;any=true;ImGui::PushID(i);std::string label=selected+"  ->  "+tr.value("to","");if(tr.value("mute",false))label+="  (muted)";if(ImGui::Selectable(label.c_str()))pickTransition=i;ImGui::PopID();}
  if(!any)ImGui::TextDisabled("None. Use Make Transition, then click the destination.");}
 if(pickTransition>=0){transition=pickTransition;selected.clear();}
 if(transition>=0&&transition<(int)transitions.size()){auto& t=transitions[transition];ImGui::SeparatorText("Transition");ImGui::TextWrapped("%s -> %s",t.value("from","").c_str(),t.value("to","").c_str());bool mute=t.value("mute",false);if(Insp::Checkbox("Mute",&mute)){t["mute"]=mute;dirty=true;}bool self=t.value("canTransitionToSelf",false);if(t.value("from","")=="Any State"&&Insp::Checkbox("Can Transition To Self",&self)){t["canTransitionToSelf"]=self;dirty=true;}float blend=t.value("duration",0.f);if(Insp::DragFloat("Transition Duration",&blend,.01f,0,10)){t["duration"]=blend;dirty=true;}bool fixed=t.value("fixedDuration",true);if(Insp::Checkbox("Fixed Duration (seconds)",&fixed)){t["fixedDuration"]=fixed;dirty=true;}float offset=t.value("offset",0.f);if(Insp::DragFloat("Destination Offset (seconds)",&offset,.01f,0,100)){t["offset"]=offset;dirty=true;}bool exit=t.value("hasExitTime",false);if(Insp::Checkbox("Has Exit Time",&exit)){t["hasExitTime"]=exit;dirty=true;}if(exit){float value=t.value("exitTime",1.f);if(Insp::DragFloat("Exit Time",&value,.01f,0,100)){t["exitTime"]=value;dirty=true;}}
 DrawTransitionTimeline(t,states,ParameterValues(parameters,live));
 if(!t.contains("conditions")){t["conditions"]=json::array();if(t.contains("parameter")){t["conditions"].push_back({{"parameter",t["parameter"]},{"threshold",t.value("threshold",.5f)},{"mode",">="}});t.erase("parameter");dirty=true;}}
 ImGui::SeparatorText("Conditions (all)");int erase=-1;auto& cs=t["conditions"];auto scalars=ScalarParameters(parameters);
 auto defaultMode=[](const std::string& type){return type=="Bool"||type=="Trigger"?std::string("If"):type=="Int"?std::string("=="):std::string(">");};
 for(int i=0;i<(int)cs.size();++i){ImGui::PushID(i);auto& c=cs[i];auto parameter=c.value("parameter","");auto type=ParameterType(parameters,parameter);auto mode=c.value("mode",">");
  // Bools and triggers have no threshold: old comparisons become true/false by what they used to accept
  if((type=="Bool"||type=="Trigger")&&mode!="If"&&mode!="IfNot"){float th=c.value("threshold",0.f);auto test=[&](float v){return mode==">"?v>th:mode=="<"?v<th:mode=="=="?v==th:mode=="!="?v!=th:mode=="<="?v<=th:v>=th;};mode=!test(1)&&test(0)&&type=="Bool"?"IfNot":"If";c["mode"]=mode;c.erase("threshold");dirty=true;}
  if(type!="Bool"&&type!="Trigger"&&!type.empty()&&(mode=="If"||mode=="IfNot")){mode=defaultMode(type);c["mode"]=mode;dirty=true;}
  ImGui::SetNextItemWidth(-1);if(Insp::BeginCombo("##parameter",parameter.empty()?"(none)":parameter.c_str())){for(auto& [pn,pt]:scalars)if(ImGui::Selectable(pn.c_str(),parameter==pn)){c["parameter"]=pn;if(ParameterType(parameters,parameter)!=pt){c["mode"]=defaultMode(pt);c["threshold"]=0.f;}dirty=true;}ImGui::EndCombo();}
  if(type=="Bool"){int v=mode=="IfNot"?1:0;ImGui::SetNextItemWidth(90);if(ImGui::Combo("##bool",&v,"true\0false\0")){c["mode"]=v?"IfNot":"If";dirty=true;}}
  else if(type=="Trigger"){ImGui::AlignTextToFramePadding();ImGui::TextDisabled("when fired");}
  else if(!type.empty()){
   struct Op{const char* label;const char* mode;};static const Op floatOps[]={{"Greater",">"},{"Less","<"}};static const Op intOps[]={{"Greater",">"},{"Less","<"},{"Equals","=="},{"NotEqual","!="}};
   const Op* ops=type=="Int"?intOps:floatOps;int count=type=="Int"?4:2;const char* shown=mode.c_str();for(int k=0;k<count;++k)if(mode==ops[k].mode)shown=ops[k].label;
   ImGui::SetNextItemWidth(90);if(ImGui::BeginCombo("##mode",shown)){for(int k=0;k<count;++k)if(ImGui::Selectable(ops[k].label,mode==ops[k].mode)){c["mode"]=ops[k].mode;dirty=true;}ImGui::EndCombo();}
   ImGui::SameLine();ImGui::SetNextItemWidth(75);
   if(type=="Int"){int value=(int)c.value("threshold",0.f);if(ImGui::DragInt("##threshold",&value)){c["threshold"]=(float)value;dirty=true;}}
   else{float value=c.value("threshold",0.f);if(ImGui::DragFloat("##threshold",&value,.05f)){c["threshold"]=value;dirty=true;}}}
  else{ImGui::TextDisabled("Pick a parameter");}
  ImGui::SameLine();if(ImGui::SmallButton("X"))erase=i;ImGui::PopID();}
 if(erase>=0){cs.erase(cs.begin()+erase);dirty=true;}
 if(ImGui::Button("+ Condition")){std::string pn=scalars.empty()?std::string():scalars[0].first,pt=scalars.empty()?std::string():scalars[0].second;json c={{"parameter",pn},{"mode",defaultMode(pt)}};if(pt!="Bool"&&pt!="Trigger")c["threshold"]=0.f;cs.push_back(c);dirty=true;}
 if(cs.empty()&&!t.value("hasExitTime",false))ImGui::TextColored({1,.75f,.3f,1},"No conditions and no exit time: this transition fires at once.");
 if(ImGui::Button("Delete Transition")){transitions.erase(transitions.begin()+transition);transition=-1;dirty=true;}}
 if(selected.empty()&&transition<0)ImGui::TextWrapped("Right-click the graph to create a state or a blend tree. Right-click a state and choose Make Transition, then click the destination. Select an arrow to edit its conditions.");
 ImGui::EndChild();
}
}
