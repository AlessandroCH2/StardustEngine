#pragma once
#include <imgui.h>
#include <cmath>
#include <string_view>
namespace Stardust::EditorIcons {
enum class Tool { Move, Rotate, Scale };
inline void DrawTool(ImDrawList* d,ImVec2 p,float r,Tool tool,ImU32 color){
 auto line=[&](float x,float y,float a,float b){d->AddLine({p.x+x*r,p.y+y*r},{p.x+a*r,p.y+b*r},color,1.5f);};
 if(tool==Tool::Move){line(-1,0,1,0);line(0,-1,0,1);for(int sign:{-1,1}){line(sign,0,sign*.65f,-.3f);line(sign,0,sign*.65f,.3f);line(0,sign,-.3f,sign*.65f);line(0,sign,.3f,sign*.65f);}}
 else if(tool==Tool::Rotate){d->PathArcTo(p,r,.3f,5.6f,20);d->PathStroke(color,0,1.6f);d->AddTriangleFilled({p.x+r*.85f,p.y-r*.62f},{p.x+r*.18f,p.y-r*.65f},{p.x+r*.64f,p.y-r*.05f},color);}
 else {line(-.8f,.8f,.8f,-.8f);d->AddRect({p.x-r*.9f,p.y+r*.3f},{p.x-r*.3f,p.y+r*.9f},color);d->AddRectFilled({p.x+r*.3f,p.y-r*.9f},{p.x+r*.9f,p.y-r*.3f},color);line(-.7f,-.8f,-.7f,.1f);line(-.7f,-.8f,.2f,-.8f);}
}
inline bool ToolButton(const char* id,Tool tool,bool active,const char* tooltip){
 float h=ImGui::GetFrameHeight();ImGui::PushStyleColor(ImGuiCol_Button,active?IM_COL32(55,105,155,255):IM_COL32(44,47,53,255));bool clicked=ImGui::Button(id,{h+5,h});ImGui::PopStyleColor();auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();DrawTool(ImGui::GetWindowDrawList(),{(a.x+b.x)*.5f,(a.y+b.y)*.5f},h*.28f,tool,IM_COL32(220,224,230,255));if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",tooltip);return clicked;
}
inline void Component(ImDrawList* d,ImVec2 p,float s,std::string_view name,ImU32 tint=IM_COL32(185,195,207,255)){
 float x=p.x,y=p.y,r=s*.45f;auto line=[&](float a,float b,float c,float e,ImU32 color=0){d->AddLine({x+a*r,y+b*r},{x+c*r,y+e*r},color?color:tint,1.25f);};
 if(name.find("Transform")!=name.npos){line(-.7f,.7f,.9f,.7f,IM_COL32(225,100,95,255));line(-.7f,.7f,-.7f,-.9f,IM_COL32(110,205,120,255));line(-.7f,.7f,.5f,-.5f,IM_COL32(100,155,235,255));}
 else if(name.find("Camera")!=name.npos){d->AddRect({x-r,y-r*.6f},{x+r*.4f,y+r*.6f},tint,1);d->AddTriangle({x+r*.4f,y},{x+r,y-r*.7f},{x+r,y+r*.7f},tint);}
 else if(name.find("Light")!=name.npos){d->AddCircle(p,r*.4f,IM_COL32(235,205,120,255),12,1.3f);for(int i=0;i<8;++i){float a=i*.785398f;line(std::cos(a)*.65f,std::sin(a)*.65f,std::cos(a),std::sin(a));}}
 else if(name.find("Terrain")!=name.npos){d->AddTriangle({x-r,y+r*.7f},{x-r*.2f,y-r},{x+r*.6f,y+r*.7f},IM_COL32(130,190,125,255),1.3f);line(.15f,-.25f,.6f,-.7f);line(.6f,-.7f,1,.7f);}
 else if(name.find("Script")!=name.npos){line(-.4f,-.8f,-.95f,0);line(-.95f,0,-.4f,.8f);line(.4f,-.8f,.95f,0);line(.95f,0,.4f,.8f);line(.15f,-1,-.15f,1);}
 else if(name.find("Audio")!=name.npos){d->AddQuad({x-r,y-r*.3f},{x-r*.4f,y-r*.3f},{x+r*.1f,y-r*.9f},{x+r*.1f,y+r*.9f},tint);line(-1,.3f,-.4f,.3f);d->PathArcTo({x+r*.05f,y},r*.95f,-.8f,.8f,10);d->PathStroke(tint,0,1.3f);}
 else if(name.find("Animator")!=name.npos){d->AddRect({x-r,y-r},{x-r*.2f,y-r*.3f},tint);d->AddRect({x+r*.15f,y+r*.3f},{x+r,y+r},tint);line(-.5f,-.3f,.5f,.3f);}
 else if(name.find("Canvas")!=name.npos||name.find("UI")!=name.npos||name.find("Rect")!=name.npos){d->AddRect({x-r,y-r*.8f},{x+r,y+r*.8f},IM_COL32(125,195,220,255));line(-.6f,-.35f,.6f,-.35f);line(-.6f,.2f,.3f,.2f);}
 else if(name.find("Bone")!=name.npos||name.find("Skeleton")!=name.npos){line(-.6f,.6f,.6f,-.6f);d->AddCircle({x-r*.6f,y+r*.6f},r*.25f,tint);d->AddCircle({x+r*.6f,y-r*.6f},r*.25f,tint);}
 else {ImVec2 points[]={{x,y-r},{x+r*.85f,y-r*.45f},{x+r*.85f,y+r*.45f},{x,y+r},{x-r*.85f,y+r*.45f},{x-r*.85f,y-r*.45f}};d->AddPolyline(points,6,tint,ImDrawFlags_Closed,1.2f);line(-.85f,-.45f,0,0);line(.85f,-.45f,0,0);line(0,0,0,1);}
}
inline bool ColorField(const char* id,float* value,int components=4){
 ImGui::PushID(id);
 float color[4]={value[0],value[1],value[2],components==4?value[3]:1};bool changed=false;
 float width=ImGui::CalcItemWidth(),height=ImGui::GetFrameHeight(),rounding=ImGui::GetStyle().FrameRounding;
 ImVec2 a=ImGui::GetCursorScreenPos(),b(a.x+width,a.y+height);
 if(ImGui::InvisibleButton("##swatch",{width,height}))ImGui::OpenPopup("Color Picker");
 auto* draw=ImGui::GetWindowDrawList();
 draw->AddRectFilled(a,b,ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],1}),rounding);
 if(components==4&&color[3]<1){draw->AddRectFilled({a.x+1,b.y-3},{b.x-1,b.y-1},IM_COL32(0,0,0,255));draw->AddRectFilled({a.x+1,b.y-3},{a.x+1+(width-2)*color[3],b.y-1},IM_COL32(255,255,255,255));}
 draw->AddRect(a,b,ImGui::IsItemHovered()?IM_COL32(120,120,120,255):IM_COL32(14, 16, 19, 255),rounding);
 if(ImGui::BeginPopup("Color Picker")){ImGui::SetNextItemWidth(260);changed=ImGui::ColorPicker4("##picker",color,components==3?ImGuiColorEditFlags_NoAlpha:ImGuiColorEditFlags_AlphaBar);ImGui::EndPopup();}
 if(changed)for(int i=0;i<components;++i)value[i]=color[i];
 ImGui::PopID();return changed;
}
// Compact inspector row: small foldout, vector icon, optional enable box, label and menu.
inline bool ComponentHeader(const char* label,bool* enabled,bool removable,bool& remove){
 auto id=ImGui::GetID("##componentRow");auto* storage=ImGui::GetStateStorage();bool open=storage->GetBool(id,true);
 // Space and a dark rule above every component, and a lighter band for its header, so components read as separate blocks
 ImGui::Dummy({0,3});auto p=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x,h=ImGui::GetFontSize()+9;ImGui::InvisibleButton("##componentRow",{width,h});bool hover=ImGui::IsItemHovered();auto* d=ImGui::GetWindowDrawList();
 float fullMin=ImGui::GetWindowPos().x,fullMax=fullMin+ImGui::GetWindowWidth();
 d->AddRectFilled({fullMin,p.y},{fullMax,p.y+h},hover?IM_COL32(56, 59, 65, 255):IM_COL32(47, 50, 55, 255));d->AddLine({fullMin,p.y},{fullMax,p.y},IM_COL32(16, 17, 19, 255),1.5f);d->AddLine({fullMin,p.y+1.5f},{fullMax,p.y+1.5f},IM_COL32(64, 68, 75, 255));d->AddLine({fullMin,p.y+h},{fullMax,p.y+h},IM_COL32(24, 26, 29, 255));float cy=p.y+h*.5f;
 float toggleX=p.x+width-(removable?50:30),labelX=p.x+35;if(ImGui::IsItemClicked(0)){float x=ImGui::GetMousePos().x;if(removable&&x>p.x+width-23)ImGui::OpenPopup("ComponentSettings");else if(enabled&&x>=toggleX-2&&x<=toggleX+26)*enabled=!*enabled;else{open=!open;storage->SetBool(id,open);}}
 if(ImGui::IsItemClicked(1))ImGui::OpenPopup("ComponentSettings");ImU32 arrow=IM_COL32(150,150,150,255);if(open)d->AddTriangleFilled({p.x+4,cy-2},{p.x+12,cy-2},{p.x+8,cy+3},arrow);else d->AddTriangleFilled({p.x+5,cy-4},{p.x+5,cy+4},{p.x+10,cy},arrow);
 Component(d,{p.x+23,cy},13,label);
 if(enabled){bool on=*enabled;d->AddRectFilled({toggleX,cy-6.5f},{toggleX+24,cy+6.5f},on?IM_COL32(47,128,237,255):IM_COL32(70,73,80,255),6.5f);d->AddCircleFilled({on?toggleX+17.5f:toggleX+6.5f,cy},5,IM_COL32(240,242,245,255));}
 ImVec4 clip(labelX,p.y,(enabled?toggleX:p.x+width-(removable?26:3))-4,p.y+h);d->AddText(nullptr,0,{labelX,cy-ImGui::GetFontSize()*.5f},IM_COL32(226,228,232,255),label,nullptr,0,&clip);
 if(removable)for(int i=-1;i<=1;++i)d->AddCircleFilled({p.x+width-11,cy+i*4},1,IM_COL32(180,180,180,255));
 if(ImGui::BeginPopup("ComponentSettings")){if(removable&&ImGui::MenuItem("Remove Component"))remove=true;ImGui::EndPopup();}
 return open;
}
inline void SettingsTabs(const char* id,int& selected,const char* const* names,int count){
 ImGui::PushID(id);ImGui::BeginChild("Navigation",{0,ImGui::GetFrameHeight()+12},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);float width=(ImGui::GetContentRegionAvail().x-(count-1)*4)/count;
 for(int i=0;i<count;++i){if(i)ImGui::SameLine(0,4);ImGui::PushStyleColor(ImGuiCol_Button,selected==i?IM_COL32(41, 43, 46, 255):IM_COL32(26, 28, 31, 255));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(46, 48, 51, 255));ImGui::PushStyleColor(ImGuiCol_Text,selected==i?IM_COL32(255,255,255,255):IM_COL32(194,194,194,255));if(ImGui::Button(names[i],{width,ImGui::GetFrameHeight()+3}))selected=i;ImGui::PopStyleColor(3);}ImGui::EndChild();ImGui::PopID();ImGui::Separator();
}
}
