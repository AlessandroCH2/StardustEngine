#include "EditorApp.h"
#include "InspectorWidgets.h"
#include "AssetUI.h"
#include "Animation/AvatarSystem.h"
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
 auto renameParameterEverywhere=[&](const std::string& from,const std::string& to){auto fix=[&](json& list){for(auto& t:list){if(t.value("parameter","")==from)t["parameter"]=to;if(t.contains("conditions"))for(auto& c:t["conditions"])if(c.value("parameter","")==from)c["parameter"]=to;}};if(asset.contains("transitions"))fix(asset["transitions"]);for(auto& l:asset["layers"])if(l.contains("transitions"))fix(l["transitions"]);};
 for(auto& t:transitions)if(t.contains("parameter")){auto name=t.value("parameter","");bool exists=false;for(auto& p:parameters)if(p.value("name","")==name)exists=true;if(!name.empty()&&!exists){parameters.push_back({{"name",name},{"type","Float"},{"default",0.f}});dirty=true;}}

 AnimatorComponent* live=nullptr;
 if(m_AnimationPreviewScene)live=m_AnimationPreviewScene->Reg().TryGet<AnimatorComponent>(m_AnimationPreviewRoot);
 else if(m_State!=PlayState::Edit)live=ActiveScene().Reg().TryGet<AnimatorComponent>(m_AnimatorRoot);
 std::string* liveState=nullptr;float liveTime=0;if(live){if(layerIndex==0){liveState=&live->state;liveTime=live->time;}else if(layerIndex<=(int)live->layers.size()){liveState=&live->layers[layerIndex-1].state;liveTime=live->layers[layerIndex-1].time;}}
 float height=std::max(180.f,ImGui::GetContentRegionAvail().y);
 ImGui::BeginChild("ControllerSidebar",{210,height},ImGuiChildFlags_Borders);
 if(ImGui::BeginTabBar("ControllerTabs")){
  if(ImGui::BeginTabItem("Layers")){
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
  ImGui::EndTabItem();}
 if(ImGui::BeginTabItem("Parameters")){
  ImGui::SetNextItemWidth(-1);Insp::InputTextWithHint("##parameterName","Parameter name",&paramName);
  ImGui::SetNextItemWidth(115);Insp::Combo("##type",&paramType,"Float\0Int\0Bool\0Trigger\0");ImGui::SameLine();
  if(ImGui::Button("+")){bool unique=!paramName.empty();for(auto& p:parameters)if(p.value("name","")==paramName)unique=false;if(unique){parameters.push_back({{"name",paramName},{"type",paramType==0?"Float":paramType==1?"Int":paramType==2?"Bool":"Trigger"},{"default",0.f}});dirty=true;}}
  ImGui::Separator();int remove=-1;
  for(int i=0;i<(int)parameters.size();++i){auto& p=parameters[i];ImGui::PushID(i);auto name=p.value("name","Parameter"),type=p.value("type","Float");float value=live?live->parameters.try_emplace(name,p.value("default",0.f)).first->second:p.value("default",0.f);if(renameParameter==i){ImGui::SetNextItemWidth(100);if(focusRename){ImGui::SetKeyboardFocusHere();focusRename=false;}bool done=ImGui::InputText("##rename",&renameText,ImGuiInputTextFlags_EnterReturnsTrue);bool cancel=ImGui::IsKeyPressed(ImGuiKey_Escape);if(done||(ImGui::IsItemDeactivated()&&!cancel)){bool unique=!renameText.empty();for(auto& other:parameters)if(&other!=&p&&other.value("name","")==renameText)unique=false;if(unique&&renameText!=name){p["name"]=renameText;renameParameterEverywhere(name,renameText);if(live){auto value=live->parameters.find(name);if(value!=live->parameters.end()){float v=value->second;live->parameters.erase(value);live->parameters[renameText]=v;}}name=renameText;dirty=true;}}if(done||cancel||ImGui::IsItemDeactivated())renameParameter=-1;}
   else{ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(name.c_str());if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){renameParameter=i;renameText=name;focusRename=true;}ImGui::OpenPopupOnItemClick("ParameterMenu",ImGuiPopupFlags_MouseButtonRight);}ImGui::SameLine(110);ImGui::SetNextItemWidth(65);bool changed=false;
   if(type=="Bool"){bool b=value!=0;changed=Insp::Checkbox("##value",&b);value=b?1.f:0.f;}else if(type=="Trigger"){if(ImGui::SmallButton("Fire")){value=1;changed=true;}}else if(type=="Int"){int v=(int)value;changed=Insp::DragInt("##value",&v);value=(float)v;}else changed=Insp::DragFloat("##value",&value,.05f);
   if(changed){if(live)live->parameters[name]=value;else{p["default"]=value;dirty=true;}}
   ImGui::OpenPopupOnItemClick("ParameterMenu",ImGuiPopupFlags_MouseButtonRight);ImGui::SameLine(0,4);if(ImGui::SmallButton("X"))remove=i;if(ImGui::IsItemHovered())ImGui::SetTooltip("Delete parameter");
   if(ImGui::BeginPopup("ParameterMenu")){if(ImGui::MenuItem("Rename")){renameParameter=i;renameText=name;focusRename=true;}if(ImGui::MenuItem("Delete"))remove=i;ImGui::EndPopup();}ImGui::PopID();
  }if(remove>=0){auto removed=parameters[remove].value("name",std::string());parameters.erase(parameters.begin()+remove);
   // Conditions on a deleted parameter could never pass, and the old single-parameter form would bring it back
   auto purge=[&](json& list){for(auto& t:list){if(t.value("parameter","")==removed){t.erase("parameter");t.erase("threshold");}if(t.contains("conditions")&&t["conditions"].is_array()){auto& cs=t["conditions"];for(int c=(int)cs.size()-1;c>=0;--c)if(cs[c].value("parameter","")==removed)cs.erase(cs.begin()+c);}}};
   if(asset.contains("transitions"))purge(asset["transitions"]);for(auto& l:asset["layers"])if(l.contains("transitions"))purge(l["transitions"]);if(live)live->parameters.erase(removed);if(renameParameter>=remove)renameParameter=-1;dirty=true;}
  if(live)ImGui::TextDisabled("Live parameter values");ImGui::EndTabItem();
 }ImGui::EndTabBar();}
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
 auto node=[&](const std::string& name,bool special){auto p=positions.at(name);ImGui::SetCursorScreenPos(p);ImGui::PushID(name.c_str());ImGui::InvisibleButton("Node",{160*zoom,44*zoom});bool active=liveState&&*liveState==name;ImU32 color=name=="Entry"?IM_COL32(55,160,65,255):name=="Any State"?IM_COL32(60,140,155,255):name=="Exit"?IM_COL32(150,65,65,255):active?IM_COL32(55,135,70,255):entry==name?IM_COL32(190,130,40,255):IM_COL32(69, 71, 74, 255);draw->AddRectFilled(p,{p.x+160*zoom,p.y+44*zoom},color,8*zoom);draw->AddRect(p,{p.x+160*zoom,p.y+44*zoom},selected==name?IM_COL32(90,195,255,255):IM_COL32(19, 21, 24, 255),8*zoom,0,selected==name?2:1);auto ts=ImGui::CalcTextSize(name.c_str());draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()*zoom,{p.x+80*zoom-ts.x*zoom*.5f,p.y+22*zoom-ts.y*zoom*.5f},IM_COL32(245,245,245,255),name.c_str());
 if(active&&!special){float clipLength=1;try{auto clip=json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(states[name].value("clip",""))),nullptr,false);if(clip.is_object())clipLength=std::max(.001f,clip.value("duration",1.f));}catch(...){}float k=std::fmod(liveTime,clipLength)/clipLength;draw->AddRectFilled({p.x+6*zoom,p.y+36*zoom},{p.x+6*zoom+148*zoom*k,p.y+40*zoom},IM_COL32(90,190,255,255),2);}
 if(!special&&ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){auto clip=states[name].value("clip","");if(!clip.empty())m_AnimationAsset=clip;}
 if(ImGui::IsItemClicked(0)){if(!connectFrom.empty()&&name!="Entry"&&name!="Any State"){transitions.push_back({{"from",connectFrom},{"to",name},{"conditions",json::array()},{"hasExitTime",connectFrom!="Any State"},{"exitTime",1.f}});transition=(int)transitions.size()-1;connectFrom.clear();dirty=true;}else{selected=name;transition=-1;if(m_AnimationPreviewScene&&!special&&layerIndex==0){m_AnimationPreviewState=name;m_AnimationPreviewTime=0;}}}
 if(!special&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){states[name]["editorPosition"]={(p.x-origin.x-pan.x+io.MouseDelta.x)/zoom,(p.y-origin.y-pan.y+io.MouseDelta.y)/zoom};dirty=true;}
 if(ImGui::BeginPopupContextItem("NodeMenu")){if(name!="Entry"&&name!="Exit"&&ImGui::MenuItem("Make Transition"))connectFrom=name;if(!special){if(ImGui::MenuItem("Set as Default State"))makeDefault=name;if(ImGui::MenuItem("Delete State"))remove=name;}ImGui::EndPopup();}ImGui::PopID();};
 node("Entry",true);node("Any State",true);node("Exit",true);for(auto& [name,value]:states.items())node(name,false);
 draw->PopClipRect();ImGui::SetCursorScreenPos(origin);ImGui::Dummy(avail);
 if(ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)){std::string dropped((const char*)payload->Data);std::vector<std::string> clips;if(dropped.ends_with(".anim"))clips.push_back(dropped);else if(dropped.ends_with(".fbx")){std::string error;clips=AvatarSystem::ImportClips(dropped,&error);if(clips.empty())SD_WARN("{}",error);}float offsetY=0;for(auto& clip:clips){auto stem=FileSystem::Utf8(std::filesystem::u8path(clip).stem()),unique=stem;for(int n=1;states.contains(unique);++n)unique=std::format("{} {}",stem,n);bool loop=true;try{auto data=json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(clip)),nullptr,false);if(data.is_object())loop=data.value("loop",true);}catch(...){}states[unique]={{"clip",clip},{"loop",loop},{"editorPosition",{(mouse.x-origin.x-pan.x)/zoom,(mouse.y-origin.y-pan.y)/zoom+offsetY}}};offsetY+=60;if(states.size()==1)layer["defaultState"]=unique;selected=unique;dirty=true;}}ImGui::EndDragDropTarget();}
 if(ImGui::BeginPopupContextWindow("GraphMenu",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){ImGui::TextDisabled("New State");ImGui::SetNextItemWidth(220);if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();bool confirmName=ImGui::InputText("##stateName",&newName,ImGuiInputTextFlags_EnterReturnsTrue);if(confirmName)ImGui::CloseCurrentPopup();if((ImGui::MenuItem("Create State")||confirmName)&&!newName.empty()&&!states.contains(newName)&&newName!="Entry"&&newName!="Exit"&&newName!="Any State"){states[newName]={{"clip",""},{"loop",true},{"editorPosition",{(mouse.x-origin.x-pan.x)/zoom,(mouse.y-origin.y-pan.y)/zoom}}};if(states.size()==1)layer["defaultState"]=newName;selected=newName;dirty=true;}ImGui::EndPopup();}
 ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("ControllerProperties",{0,height},ImGuiChildFlags_Borders);
 if(!makeDefault.empty()){layer["defaultState"]=makeDefault;dirty=true;}
 if(!remove.empty()){states.erase(remove);for(int i=(int)transitions.size()-1;i>=0;--i)if(transitions[i].value("from","")==remove||transitions[i].value("to","")==remove)transitions.erase(transitions.begin()+i);if(entry==remove)layer["defaultState"]=states.empty()?std::string():states.begin().key();selected.clear();transition=-1;dirty=true;}
 if(states.contains(selected)){auto& n=states[selected];ImGui::SeparatorText("State");ImGui::TextWrapped("%s",selected.c_str());std::string clip=n.value("clip","");if(AssetUI::AssetField("Motion",clip,{".anim"})){n["clip"]=clip;dirty=true;}bool loop=n.value("loop",true);if(Insp::Checkbox("Loop",&loop)){n["loop"]=loop;dirty=true;}if(ImGui::Button("Make Transition"))connectFrom=selected;if(ImGui::Button("Set as Default")){layer["defaultState"]=selected;dirty=true;}}
 if(transition>=0&&transition<(int)transitions.size()){auto& t=transitions[transition];ImGui::SeparatorText("Transition");ImGui::TextWrapped("%s -> %s",t.value("from","").c_str(),t.value("to","").c_str());float blend=t.value("duration",0.f);if(Insp::DragFloat("Transition Duration",&blend,.01f,0,10)){t["duration"]=blend;dirty=true;}bool fixed=t.value("fixedDuration",true);if(Insp::Checkbox("Fixed Duration (seconds)",&fixed)){t["fixedDuration"]=fixed;dirty=true;}float offset=t.value("offset",0.f);if(Insp::DragFloat("Destination Offset (seconds)",&offset,.01f,0,100)){t["offset"]=offset;dirty=true;}bool exit=t.value("hasExitTime",false);if(Insp::Checkbox("Has Exit Time",&exit)){t["hasExitTime"]=exit;dirty=true;}if(exit){float value=t.value("exitTime",1.f);if(Insp::DragFloat("Exit Time",&value,.01f,0,100)){t["exitTime"]=value;dirty=true;}}
 if(!t.contains("conditions")){t["conditions"]=json::array();if(t.contains("parameter")){t["conditions"].push_back({{"parameter",t["parameter"]},{"threshold",t.value("threshold",.5f)},{"mode",">="}});t.erase("parameter");dirty=true;}}
 ImGui::SeparatorText("Conditions (all)");int erase=-1;auto& cs=t["conditions"];for(int i=0;i<(int)cs.size();++i){ImGui::PushID(i);auto& c=cs[i];auto parameter=c.value("parameter","");ImGui::SetNextItemWidth(-1);if(Insp::BeginCombo("##parameter",parameter.c_str())){for(auto& p:parameters){auto name=p.value("name","");if(ImGui::Selectable(name.c_str(),parameter==name)){c["parameter"]=name;c["mode"]=p.value("type","")=="Bool"||p.value("type","")=="Trigger"?"If":">";dirty=true;}}ImGui::EndCombo();}auto mode=c.value("mode",">");ImGui::SetNextItemWidth(80);if(Insp::BeginCombo("##mode",mode.c_str())){for(const char* op:{">","<","==","!=",">=","<=","If","IfNot"})if(ImGui::Selectable(op,mode==op)){c["mode"]=op;dirty=true;}ImGui::EndCombo();}if(mode!="If"&&mode!="IfNot"){ImGui::SameLine();float value=c.value("threshold",0.f);ImGui::SetNextItemWidth(75);if(Insp::DragFloat("##threshold",&value,.05f)){c["threshold"]=value;dirty=true;}}ImGui::SameLine();if(ImGui::SmallButton("X"))erase=i;ImGui::PopID();}if(erase>=0){cs.erase(cs.begin()+erase);dirty=true;}if(ImGui::Button("+ Condition")){cs.push_back({{"parameter",parameters.empty()?std::string():parameters[0].value("name","")},{"mode",">"},{"threshold",0.f}});dirty=true;}if(ImGui::Button("Delete Transition")){transitions.erase(transitions.begin()+transition);transition=-1;dirty=true;}}
 if(selected.empty()&&transition<0)ImGui::TextWrapped("Right-click the graph to create a state. Right-click a state to create a transition. Select an arrow to edit conditions.");
 ImGui::EndChild();
}
}
