#include "InspectorWidgets.h"
#include "EditorApp.h"
#include "EditorIcons.h"
#include "AssetUI.h"
#include "UI/CanvasRenderer.h"
#include "UI/RectTransformUtils.h"
#include "Scripting/ScriptEngine.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
namespace Stardust {
namespace {
void Property(const char* label,const std::function<void()>& widget){
    ImGui::PushID(label);ImGui::BeginTable("##property",2,ImGuiTableFlags_SizingStretchProp);ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthFixed,Insp::LabelWidth());ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);widget();ImGui::EndTable();ImGui::PopID();
}
void Pair(const char* a,float& first,const char* b,float& second,float speed=1){
    float width=std::max(36.f,(ImGui::GetContentRegionAvail().x-42)*.5f);ImGui::PushID(a);ImGui::TextDisabled("%s",a);ImGui::SameLine(0,4);ImGui::SetNextItemWidth(width);Insp::DragFloat("##value",&first,speed,0,0,"%.2f");ImGui::PopID();ImGui::SameLine(0,6);ImGui::PushID(b);ImGui::TextDisabled("%s",b);ImGui::SameLine(0,4);ImGui::SetNextItemWidth(width);Insp::DragFloat("##value",&second,speed,0,0,"%.2f");ImGui::PopID();
}
void PresetGraphic(ImVec2 p,float size,glm::vec2 minimum,glm::vec2 maximum){
    auto* draw=ImGui::GetWindowDrawList();float pad=6;ImVec2 a(p.x+pad,p.y+pad),b(p.x+size-pad,p.y+size-pad);draw->AddRect(a,b,IM_COL32(140,145,150,255));
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){glm::vec2 n(x?maximum.x:minimum.x,y?maximum.y:minimum.y);ImVec2 c(a.x+(b.x-a.x)*n.x,b.y-(b.y-a.y)*n.y);draw->AddLine({c.x-3,c.y},{c.x+3,c.y},IM_COL32(245,145,90,255),1.5f);draw->AddLine({c.x,c.y-3},{c.x,c.y+3},IM_COL32(245,145,90,255),1.5f);}
    if(minimum.x!=maximum.x)draw->AddLine({a.x+3,(a.y+b.y)*.5f},{b.x-3,(a.y+b.y)*.5f},IM_COL32(100,195,245,255),2);
    if(minimum.y!=maximum.y)draw->AddLine({(a.x+b.x)*.5f,a.y+3},{(a.x+b.x)*.5f,b.y-3},IM_COL32(100,195,245,255),2);
}
struct EditState{int start=0,end=0;};
int Selection(ImGuiInputTextCallbackData* data){auto* state=(EditState*)data->UserData;state->start=data->SelectionStart;state->end=data->SelectionEnd;return 0;}
}
void EditorApp::DrawRectInspector(EntityID e,RectTransformComponent& r){
    glm::vec2 parentSize(1280,720);auto parent=ActiveScene().GetParent(e);for(auto& rect:CanvasRenderer::Layout(ActiveScene(),m_SceneView.size,true))if(rect.entity==parent){parentSize=rect.size;break;}
    auto p=ImGui::GetCursorScreenPos();if(ImGui::Button("##AnchorPresets",{50,50}))ImGui::OpenPopup("Anchor Presets");PresetGraphic(p,50,r.anchorMin,r.anchorMax);if(ImGui::IsItemHovered())ImGui::SetTooltip("Anchor Presets\nShift: set pivot too\nAlt: align position / stretch offsets");
    ImGui::SameLine();ImGui::BeginGroup();ImGui::TextUnformatted("Rect Transform");ImGui::TextDisabled("Anchors relative to parent");ImGui::EndGroup();
    ImGui::SetNextWindowSizeConstraints({252,0},{252,360});
    if(ImGui::BeginPopup("Anchor Presets")){
        ImGui::TextDisabled("Shift: pivot  |  Alt: position");
        const char* horizontal[]={"left","center","right","stretch"};const char* vertical[]={"top","middle","bottom","stretch"};
        ImGui::BeginTable("##presets",5,ImGuiTableFlags_SizingFixedFit);ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed,40);for(int i=0;i<4;++i)ImGui::TableSetupColumn(horizontal[i],ImGuiTableColumnFlags_WidthFixed,42);
        ImGui::TableNextColumn();for(auto label:horizontal){ImGui::TableNextColumn();ImGui::TextDisabled("%s",label);}
        for(int y=0;y<4;++y){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextDisabled("%s",vertical[y]);for(int x=0;x<4;++x){ImGui::TableNextColumn();ImGui::PushID(y*4+x);glm::vec2 minimum(x==3?0:x*.5f,y==3?0:1-y*.5f),maximum(x==3?1:x*.5f,y==3?1:1-y*.5f);auto pos=ImGui::GetCursorScreenPos();bool selected=glm::length(r.anchorMin-minimum)<.001f&&glm::length(r.anchorMax-maximum)<.001f;if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.18f,.4f,.6f,1));if(ImGui::Button("##preset",{38,38})){
            if(ImGui::GetIO().KeyShift)RectTransformUtils::SetPivot(r,parentSize,glm::vec2(x==3?.5f:x*.5f,y==3?.5f:1-y*.5f));RectTransformUtils::SetAnchors(r,parentSize,minimum,maximum);
            if(ImGui::GetIO().KeyAlt){r.anchoredPosition={0,0};if(x==3)r.sizeDelta.x=0;if(y==3)r.sizeDelta.y=0;}ImGui::CloseCurrentPopup();
        }if(selected)ImGui::PopStyleColor();PresetGraphic(pos,38,minimum,maximum);ImGui::PopID();}}
        ImGui::EndTable();ImGui::EndPopup();
    }
    bool sx=std::abs(r.anchorMax.x-r.anchorMin.x)>.0001f,sy=std::abs(r.anchorMax.y-r.anchorMin.y)>.0001f;
    auto lower=RectTransformUtils::OffsetMin(r),upper=RectTransformUtils::OffsetMax(r);float x=sx?lower.x:r.anchoredPosition.x,y=sy?lower.y:r.anchoredPosition.y,w=sx?-upper.x:r.sizeDelta.x,h=sy?-upper.y:r.sizeDelta.y;
    Property(sx?"Horizontal Stretch":"Horizontal",[&]{Pair(sx?"L":"X",x,sx?"R":"W",w);});Property(sy?"Vertical Stretch":"Vertical",[&]{Pair(sy?"B":"Y",y,sy?"T":"H",h);});
    if(sx){lower.x=x;upper.x=-w;}else{r.anchoredPosition.x=x;r.sizeDelta.x=w;}if(sy){lower.y=y;upper.y=-h;}else{r.anchoredPosition.y=y;r.sizeDelta.y=h;}
    if(sx||sy){auto delta=r.sizeDelta,position=r.anchoredPosition;RectTransformUtils::SetOffsets(r,lower,upper);if(!sx){r.sizeDelta.x=delta.x;r.anchoredPosition.x=position.x;}if(!sy){r.sizeDelta.y=delta.y;r.anchoredPosition.y=position.y;}}
    if(ImGui::TreeNode("Anchors")){auto minimum=r.anchorMin,maximum=r.anchorMax;Property("Min",[&]{Insp::DragFloat2("##min",glm::value_ptr(minimum),.005f,0,1);});Property("Max",[&]{Insp::DragFloat2("##max",glm::value_ptr(maximum),.005f,0,1);});minimum=glm::min(minimum,maximum);RectTransformUtils::SetAnchors(r,parentSize,minimum,maximum);ImGui::TreePop();}
    Property("Pivot",[&]{auto pivot=r.pivot;if(Insp::DragFloat2("##pivot",glm::value_ptr(pivot),.005f,0,1))RectTransformUtils::SetPivot(r,parentSize,pivot);});Property("Rotation",[&]{Insp::DragFloat("##rotation",&r.rotation,.5f,0,0,"%.1f deg");});Property("Scale",[&]{Insp::DragFloat2("##scale",glm::value_ptr(r.scale),.01f,.001f,100);});
}
void EditorApp::DrawTextInspector(EntityID e,UITextComponent& text){
    static std::unordered_map<u64,EditState> selections;auto& selection=selections[ActiveScene().Reg().Get<IDComponent>(e).uuid];
    auto wrap=[&](const std::string& open,const std::string& close){int a=std::clamp(std::min(selection.start,selection.end),0,(int)text.text.size()),b=std::clamp(std::max(selection.start,selection.end),a,(int)text.text.size());auto body=b>a?text.text.substr(a,b-a):std::string("text");text.text.replace(a,b-a,open+body+close);selection.start=a+(int)open.size();selection.end=selection.start+(int)body.size();text.richText=true;};
    auto tool=[&](const char* label,const char* tip,const char* tag){if(ImGui::Button(label,{26,24}))wrap(std::string("<")+tag+">",std::string("</")+tag+">");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",tip);ImGui::SameLine(0,3);};
    tool("B","Bold selection","b");tool("I","Italic selection","i");tool("U","Underline selection","u");tool("S","Strikethrough selection","s");
    if(ImGui::Button("A##colour",{30,24}))ImGui::OpenPopup("Text Colour Tag");if(ImGui::IsItemHovered())ImGui::SetTooltip("Insert colour tag");ImGui::SameLine(0,3);if(ImGui::Button("A+",{32,24}))ImGui::OpenPopup("Text Size Tag");
    if(ImGui::BeginPopup("Text Colour Tag")){static glm::vec4 colour(1,.8f,.3f,1);ImGui::ColorPicker4("##colour",glm::value_ptr(colour));if(ImGui::Button("Insert Colour")){wrap(std::format("<color=#{:02X}{:02X}{:02X}{:02X}>",(int)(colour.r*255),(int)(colour.g*255),(int)(colour.b*255),(int)(colour.a*255)),"</color>");ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
    if(ImGui::BeginPopup("Text Size Tag")){static float size=32;ImGui::SetNextItemWidth(140);Insp::DragFloat("Size",&size,1,1,512);if(ImGui::Button("Insert Size")){wrap(std::format("<size={:.0f}>",size),"</size>");ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
    ImGui::SetNextItemWidth(-1);ImGui::InputTextMultiline("##content",&text.text,{-1,112},ImGuiInputTextFlags_CallbackAlways,Selection,&selection);
    ImGui::SeparatorText("Aa  Font & Colour");AssetUI::AssetField("Font",text.font,{".ttf",".otf"},"System Default");AssetUI::AssetField("Material",text.material,{".mat"});
    Property("Size",[&]{float width=std::max(50.f,ImGui::GetContentRegionAvail().x-88);ImGui::SetNextItemWidth(width);Insp::DragFloat("##size",&text.fontSize,1,1,512,"%.0f px");ImGui::SameLine();Insp::Checkbox("Auto",&text.autoSize);});
    if(text.autoSize)Property("Auto Size Range",[&]{Pair("Min",text.minFontSize,"Max",text.maxFontSize);});
    Property("Colour",[&]{EditorIcons::ColorField("##colour",glm::value_ptr(text.color));});
    ImGui::SeparatorText("Layout");
    Property("Alignment",[&]{auto start=ImGui::GetCursorScreenPos();for(int y=0;y<3;++y){if(y)ImGui::SetCursorScreenPos({start.x,start.y+y*25});for(int x=0;x<3;++x){if(x)ImGui::SameLine(0,3);ImGui::PushID(y*3+x);auto p=ImGui::GetCursorScreenPos();bool active=text.alignment==x&&text.verticalAlignment==y;if(active)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.2f,.45f,.65f,1));if(ImGui::Button("##align",{29,22})){text.alignment=x;text.verticalAlignment=y;}if(active)ImGui::PopStyleColor();auto* d=ImGui::GetWindowDrawList();for(int line=0;line<3;++line){float length=line==1?10:17;float left=x==0?4:x==1?(29-length)*.5f:25-length;float top=y==0?4:y==1?6:9;d->AddLine({p.x+left,p.y+top+line*3},{p.x+left+length,p.y+top+line*3},IM_COL32(225,230,235,255));}ImGui::PopID();}}});
    Property("Wrapping",[&]{Insp::Checkbox("Word Wrap",&text.wordWrap);});Property("Overflow",[&]{Insp::Combo("##overflow",&text.overflow,"Clip\0Overflow\0Ellipsis\0");});
    if(ImGui::TreeNode("Spacing & Options")){Property("Character",[&]{Insp::DragFloat("##character",&text.characterSpacing,.1f);});Property("Line Height",[&]{Insp::DragFloat("##line",&text.lineSpacing,.01f,.1f,5);});Insp::Checkbox("Rich Text Tags",&text.richText);Insp::Checkbox("Raycast Target",&text.raycastTarget);ImGui::TreePop();}
    if(ImGui::TreeNode("Outline & Shadow")){Property("Outline",[&]{EditorIcons::ColorField("##outline",glm::value_ptr(text.outlineColor));});Property("Thickness",[&]{Insp::SliderFloat("##width",&text.outlineWidth,0,.25f);});Property("Shadow",[&]{EditorIcons::ColorField("##shadow",glm::value_ptr(text.shadowColor));});Property("Offset",[&]{Insp::DragFloat2("##offset",glm::value_ptr(text.shadowOffset),.1f);});ImGui::TreePop();}
}
void EditorApp::DrawButtonInspector(EntityID e,UIButtonComponent& c){
    Insp::Combo("Transition",&c.transition,"None\0Color Tint\0Animation\0");Insp::DragFloat("Fade Duration",&c.fadeDuration,.01f,0,10);
    if(c.transition==2){Insp::InputText("Normal State",&c.normalState);Insp::InputText("Highlighted State",&c.highlightedState);Insp::InputText("Pressed State",&c.pressedState);Insp::InputText("Disabled State",&c.disabledState);}

    Property("Interactable",[&]{Insp::Checkbox("##value",&c.interactable);});
    if(ImGui::TreeNode("Transition Colours")){Property("Highlighted",[&]{EditorIcons::ColorField("##value",glm::value_ptr(c.highlightedColor));});Property("Pressed",[&]{EditorIcons::ColorField("##value",glm::value_ptr(c.pressedColor));});Property("Disabled",[&]{EditorIcons::ColorField("##value",glm::value_ptr(c.disabledColor));});ImGui::TreePop();}
    ImGui::SeparatorText("On Click ()");auto& scene=ActiveScene();auto& reg=scene.Reg();int remove=-1;
    for(int i=0;i<(int)c.actions.size();++i){auto& a=c.actions[i];ImGui::PushID(i);ImGui::BeginGroup();Insp::Checkbox("##enabled",&a.enabled);ImGui::SameLine();ImGui::Text("Action %d",i+1);ImGui::SameLine();if(ImGui::SmallButton("X"))remove=i;
        Property("Target",[&]{nlohmann::json value=a.target?std::format("{:016x}",a.target):"0";if(AssetUI::EntityField(scene,"Entity",value)){a.target=std::strtoull(value.get<std::string>().c_str(),nullptr,16);a.method.clear();}});
        if(!a.target)ImGui::TextDisabled("Target: this GameObject");
        auto target=a.target?scene.FindByUUID(a.target):e;
        std::vector<std::pair<std::string,int>> choices{{"Entity",-1},{"Transform",-1}};
        if(target){
            if(reg.Has<UIInputFieldComponent>(target))choices.push_back({"InputField",-1});if(reg.Has<CanvasComponent>(target))choices.push_back({"Canvas",-1});if(reg.Has<RectTransformComponent>(target))choices.push_back({"RectTransform",-1});if(reg.Has<CameraComponent>(target))choices.push_back({"Camera",-1});if(reg.Has<AnimatorComponent>(target))choices.push_back({"Animator",-1});if(reg.Has<BoxColliderComponent>(target))choices.push_back({"BoxCollider",-1});if(reg.Has<SphereColliderComponent>(target))choices.push_back({"SphereCollider",-1});if(reg.Has<CapsuleColliderComponent>(target))choices.push_back({"CapsuleCollider",-1});
            if(reg.Has<UITextComponent>(target))choices.push_back({"Text",-1});if(reg.Has<UIImageComponent>(target))choices.push_back({"Image",-1});if(reg.Has<UIButtonComponent>(target))choices.push_back({"Button",-1});if(reg.Has<CanvasGroupComponent>(target))choices.push_back({"CanvasGroup",-1});if(reg.Has<RigidbodyComponent>(target))choices.push_back({"Rigidbody",-1});if(reg.Has<AudioSourceComponent>(target))choices.push_back({"AudioSource",-1});if(reg.Has<MeshRendererComponent>(target))choices.push_back({"MeshRenderer",-1});
            if(auto* scripts=reg.TryGet<ScriptComponent>(target))for(int slot=0;slot<1+(int)scripts->additional.size();++slot)choices.push_back({scripts->Slot(slot).className,slot});
        }
        std::string scriptClass;if(a.component=="Script"&&target)if(auto* scripts=reg.TryGet<ScriptComponent>(target);scripts&&a.scriptSlot>=0&&a.scriptSlot<1+(int)scripts->additional.size())scriptClass=scripts->Slot(a.scriptSlot).className;
        Property("Component",[&]{auto preview=a.component=="Script"?scriptClass:a.component;float width=std::max(180.f,ImGui::GetContentRegionAvail().x);ImGui::SetNextWindowSizeConstraints({width,0},{width,280});if(Insp::BeginCombo("##component",preview.c_str())){for(auto& [name,slot]:choices){ImGui::PushID(slot);if(ImGui::Selectable(name.c_str(),slot<0?a.component==name:a.component=="Script"&&a.scriptSlot==slot)){a.component=slot<0?name:"Script";a.scriptSlot=std::max(0,slot);scriptClass=slot<0?"":name;a.method.clear();a.argumentsJson="[]";}ImGui::PopID();}ImGui::EndCombo();}});
        auto methods=ScriptEngine::ActionMethods(a.component,scriptClass);
        Property("Method",[&]{float width=std::max(180.f,ImGui::GetContentRegionAvail().x);ImGui::SetNextWindowSizeConstraints({width,0},{width,320});if(Insp::BeginCombo("##method",a.method.empty()?"Select a method...":a.method.c_str())){for(auto& method:methods)if(ImGui::Selectable(method.signature.c_str(),method.signature==a.method)){a.method=method.signature;nlohmann::json args=nlohmann::json::array();for(auto& p:method.parameters)args.push_back(nlohmann::json::parse(p.defaultJson,nullptr,false));a.argumentsJson=args.dump();}ImGui::EndCombo();}});
        auto method=std::find_if(methods.begin(),methods.end(),[&](auto& m){return m.signature==a.method;});
        if(method!=methods.end()){a.argumentTypes.clear();for(auto& p:method->parameters)a.argumentTypes.push_back(p.type);auto args=nlohmann::json::parse(a.argumentsJson,nullptr,false);if(!args.is_array()||args.size()!=method->parameters.size()){args=nlohmann::json::array();for(auto& p:method->parameters)args.push_back(nlohmann::json::parse(p.defaultJson,nullptr,false));}
            for(int arg=0;arg<(int)method->parameters.size();++arg){auto& p=method->parameters[arg];ImGui::PushID(arg);Property(p.name.c_str(),[&]{auto& v=args[arg];try{if(p.type=="int"){int n=v.is_number()?v.get<int>():0;if(Insp::DragInt("##value",&n))v=n;}else if(p.type=="float"){float n=v.is_number()?v.get<float>():0;if(Insp::DragFloat("##value",&n,.1f))v=n;}else if(p.type=="bool"){bool n=v.is_boolean()?v.get<bool>():false;if(Insp::Checkbox("##value",&n))v=n;}else if(p.type=="string"){std::string n=v.is_string()?v.get<std::string>():"";if(Insp::InputText("##value",&n))v=n;}else if(p.type=="Vector2"||p.type=="Vector3"||p.type=="Color"){float n[4]={0,0,0,1};int count=p.type=="Vector2"?2:p.type=="Vector3"?3:4;for(int k=0;k<count;++k)if(v.is_array()&&k<v.size())n[k]=v[k].get<float>();bool changed=count==2?Insp::DragFloat2("##value",n,.1f):count==3?Insp::DragFloat3("##value",n,.1f):Insp::ColorEdit4("##value",n);if(changed){v=nlohmann::json::array();for(int k=0;k<count;++k)v.push_back(n[k]);}}else{if(!v.is_string())v="0";AssetUI::EntityField(scene,p.type,v);}}catch(...){ImGui::TextDisabled("Invalid argument");}});ImGui::PopID();}a.argumentsJson=args.dump();
        }else if(!a.method.empty())ImGui::TextDisabled("Method unavailable: rebuild scripts / choose a method");
        ImGui::EndGroup();ImGui::Separator();ImGui::PopID();
    }
    if(remove>=0)c.actions.erase(c.actions.begin()+remove);
    if(ImGui::Button("+ Add Action",{-1,0}))c.actions.emplace_back();
    if(c.actions.empty()){ImGui::TextDisabled("Default: OnClick() on this object's scripts");Property("Legacy method",[&]{Insp::InputText("##value",&c.method);});}
}
}
