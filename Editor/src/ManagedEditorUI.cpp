#include <format>
#include "InspectorWidgets.h"
#include "ManagedEditorUI.h"
#include "Scripting/ScriptEngine.h"
#include "AssetUI.h"
#include "Assets/AssetManager.h"
#include "Core/Application.h"
#include "ManagedWidgets.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>
namespace Stardust {
using json=nlohmann::json;
static std::unordered_map<std::string,json> events;
static bool Commands(const json& response,json& next){
    bool defaultInspector=false;
    for(auto& command:response.value("commands",json::array())){
        auto kind=command.value("kind",""),id=command.value("id",""),label=command.value("label","");auto value=command.value("value",json());ImGui::PushID(id.c_str());
        bool changed=false;
        if(kind=="default")defaultInspector=true;
        if(kind=="label")ImGui::TextUnformatted(label.c_str());
        if(kind=="help")ImGui::TextWrapped("%s",label.c_str());
        if(kind=="space")ImGui::Spacing();
        if(kind=="float"){float v=value.get<float>();changed=Insp::DragFloat(label.c_str(),&v,.05f);value=v;}
        if(kind=="int"){int v=value.get<int>();changed=Insp::InputInt(label.c_str(),&v);value=v;}
        if(kind=="text"){std::string v=value.get<std::string>();changed=Insp::InputText(label.c_str(),&v);value=v;}
        if(kind=="bool"){bool v=value.get<bool>();changed=Insp::Checkbox(label.c_str(),&v);value=v;}
        if(kind=="button"){changed=ImGui::Button(label.c_str());value=true;}
        if(kind=="vector3"){auto v=value.get<std::array<float,3>>();changed=Insp::DragFloat3(label.c_str(),v.data(),.05f);value=v;}
        if(kind=="sameline"){const float spacing=value.is_number()?value.get<float>():-1.f;ImGui::SameLine(0,spacing>=0?spacing*Application::Get().UiScale():-1.f);}
        if(kind=="header"){ImGui::Dummy({1,2});ImGui::SeparatorText(label.c_str());}
        if(kind=="iconbutton"&&value.is_object()){if(ManagedWidgets::IconButton(label.c_str(),value.value("selected",false),value.value("tooltip","").c_str())){value["clicked"]=true;changed=true;}}
        if(kind=="toolbar"&&value.is_object()){int index=value.value("index",0);std::vector<std::string> options;for(auto& o:value.value("options",json::array()))if(o.is_string())options.push_back(o.get<std::string>());if(ManagedWidgets::Toolbar(index,options)){value["index"]=index;changed=true;}}
        if(kind=="slider"&&value.is_object()){float v=value.value("value",0.f);if(Insp::SliderFloat(label.c_str(),&v,value.value("min",0.f),value.value("max",1.f),"%.2f")){value["value"]=v;changed=true;}}
        if(kind=="tilegrid"&&value.is_object())changed=ManagedWidgets::TileGrid(label,value);
        if(kind=="togglebutton"){bool on=value.get<bool>();if(on)ImGui::PushStyleColor(ImGuiCol_Button,IM_COL32(47,128,237,255));if(ImGui::Button(label.c_str())){value=!on;changed=true;}if(on)ImGui::PopStyleColor();}
        if(kind=="vector2"){auto v=value.get<std::array<float,2>>();changed=Insp::DragFloat2(label.c_str(),v.data(),.05f);value=v;}
        if(kind=="asset"&&value.is_object()){std::string path=value.value("path","");std::vector<std::string> extensions;for(auto& e:value.value("extensions",json::array()))if(e.is_string())extensions.push_back(e.get<std::string>());changed=AssetUI::AssetField(label.c_str(),path,extensions);value["path"]=path;}
        if(kind=="popup"&&value.is_object()){int index=value.value("index",0);std::vector<std::string> options;for(auto& o:value.value("options",json::array()))if(o.is_string())options.push_back(o.get<std::string>());
            if(Insp::BeginCombo(label.c_str(),index>=0&&index<(int)options.size()?options[index].c_str():"")){for(int i=0;i<(int)options.size();++i)if(ImGui::Selectable(options[i].c_str(),i==index)&&i!=index){index=i;changed=true;}ImGui::EndCombo();}value["index"]=index;}
        if(kind=="texturebutton"&&value.is_object()){
            // A tile of a texture: pixel rectangle from the top-left, outlined in blue when selected
            const float size=std::max(8.f,value.value("size",48.f))*Application::Get().UiScale();auto rect=value.value("rect",std::array<float,4>{0,0,0,0});
            const ImVec2 p=ImGui::GetCursorScreenPos();bool clicked=ImGui::InvisibleButton("##tile",{size,size});
            auto* draw=ImGui::GetWindowDrawList();const bool selected=value.value("selected",false),hovered=ImGui::IsItemHovered();
            draw->AddRectFilled(p,{p.x+size,p.y+size},IM_COL32(28,30,34,255),3);
            if(auto texture=AssetManager::GetTexture(label,true)){const float w=(float)texture->Desc().width,h=(float)texture->Desc().height;if(rect[2]<=0||rect[3]<=0)rect={0,0,w,h};
                draw->AddImage((ImTextureID)Application::Get().GetDevice().ImGuiTexture(texture.get()),{p.x+2,p.y+2},{p.x+size-2,p.y+size-2},{rect[0]/w,rect[1]/h},{(rect[0]+rect[2])/w,(rect[1]+rect[3])/h});}
            if(selected||hovered)draw->AddRect(p,{p.x+size,p.y+size},selected?IM_COL32(80,160,255,255):IM_COL32(150,155,165,255),3,0,selected?2.5f:1.f);
            if(clicked){value["clicked"]=true;changed=true;}
        }
        if(kind=="color"){auto v=value.get<std::array<float,4>>();changed=Insp::ColorEdit4(label.c_str(),v.data());value=v;}
        if(changed)next[id]=value;ImGui::PopID();
    }
    if(response.contains("error"))ImGui::TextWrapped("%s",response["error"].get<std::string>().c_str());
    return defaultInspector;
}
bool DrawManagedInspector(Scene& scene,EntityID entity,ScriptSlot& script,size_t slot,bool live){
    auto key="inspector:"+std::to_string(scene.Reg().Get<IDComponent>(entity).uuid)+":"+std::to_string(slot);auto values=json::parse(live?ScriptEngine::GetInstanceFields(entity,slot):script.fieldsJson,nullptr,false);if(!values.is_object())values=json::object();
    json request={{"kind","inspector"},{"class",script.className},{"entity",std::format("{:016x}",scene.Reg().Get<IDComponent>(entity).uuid)},{"fields",values},{"events",events.contains(key)?events[key]:json::object()}};
    auto response=json::parse(ScriptEngine::DrawEditorUI(request.dump()),nullptr,false);events[key]=json::object();
    if(!response.is_object()||!response.value("handled",false))return false;
    if(response.contains("fields"))for(auto& [name,value]:response["fields"].items())if(!values.contains(name)||values[name]!=value){values[name]=value;if(live)ScriptEngine::SetInstanceField(entity,name,value.dump(),slot);}
    if(!live)script.fieldsJson=values.dump();
    return !Commands(response,events[key]);
}
void DrawManagedWindows(){
    auto response=json::parse(ScriptEngine::DrawEditorUI(R"({"kind":"windows"})"),nullptr,false);if(!response.is_object())return;
    for(auto& window:response.value("windows",json::array())){
        auto id=window.value("id",""),title=window.value("title",id);bool open=true;ImGui::SetNextWindowSize({460,360},ImGuiCond_FirstUseEver);
        bool visible=ImGui::Begin((title+"###Managed:"+id).c_str(),&open);
        json q={{"kind","window"},{"id",id},{"events",events.contains(id)?events[id]:json::object()},{"close",!open}};
        if(visible||!open){auto output=json::parse(ScriptEngine::DrawEditorUI(q.dump()),nullptr,false);events[id]=json::object();if(output.is_object())Commands(output,events[id]);}
        ImGui::End();
    }
}
}

