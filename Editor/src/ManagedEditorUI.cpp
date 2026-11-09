#include "InspectorWidgets.h"
#include "ManagedEditorUI.h"
#include "Scripting/ScriptEngine.h"
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

