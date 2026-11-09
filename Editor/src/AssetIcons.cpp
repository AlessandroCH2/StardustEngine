#include "AssetUI.h"
#include "Assets/AssetManager.h"
#include <imgui.h>
namespace Stardust::AssetUI {
void IconButton(const std::filesystem::path& path,bool directory,bool selected,glm::vec2 size) {
    auto p=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("##assetIcon",{size.x,size.y});auto* d=ImGui::GetWindowDrawList();
    if(selected||ImGui::IsItemHovered())d->AddRectFilled(p,{p.x+size.x,p.y+size.y},selected?IM_COL32(55,90,130,180):IM_COL32(65,65,65,160),5);
    DrawIcon(d,path,p,{p.x+size.x,p.y+size.y},directory);
}
void DrawIcon(ImDrawList* d,const std::filesystem::path& path,ImVec2 rectMin,ImVec2 rectMax,bool directory) {
    ImVec2 a(rectMin.x+(rectMax.x-rectMin.x)*.18f,rectMin.y+(rectMax.y-rectMin.y)*.12f),b(rectMin.x+(rectMax.x-rectMin.x)*.82f,rectMin.y+(rectMax.y-rectMin.y)*.9f);
    if(directory){d->AddRectFilled({a.x,a.y+5},{a.x+22,a.y+14},IM_COL32(225,177,69,255),3);d->AddRectFilled({a.x,a.y+12},b,IM_COL32(210,164,60,255),4);d->AddLine({a.x+4,a.y+17},{b.x-4,a.y+17},IM_COL32(255,218,126,255),2);return;}
    auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),::tolower);
    ImU32 color=IM_COL32(157,169,184,255);const char* symbol="?";
    if(ext==".cs"){symbol="{}";color=IM_COL32(111,189,123,255);}else if(ext==".scene"){symbol="S";color=IM_COL32(103,166,231,255);}else if(ext==".prefab"){symbol="P";color=IM_COL32(93,155,239,255);}else if(ext==".mat"){symbol="M";color=IM_COL32(218,133,187,255);}else if(ext==".sprite"){symbol="2D";color=IM_COL32(120,200,191,255);}else if(ext==".ttf"||ext==".otf"){symbol="Aa";}else if(ext==".hlsl"||ext==".spv"){symbol="<>";color=IM_COL32(168,129,231,255);}else if(ext==".anim"){symbol=">";color=IM_COL32(130,205,180,255);}else if(ext==".avatar"){symbol="AV";color=IM_COL32(130,205,180,255);}else if(ext==".controller"){symbol="o-o";color=IM_COL32(130,205,180,255);}else if(ext==".pak"){symbol="ZIP";color=IM_COL32(205,162,101,255);}else if((ext==".terrain"||ext==".terrainlayer")){symbol="/\\";color=IM_COL32(145,192,96,255);}else if(ext==".wav"||ext==".mp3"||ext==".flac"||ext==".ogg"){symbol="~";color=IM_COL32(224,161,94,255);}else if(AssetManager::IsMeshFile(path)){symbol="3D";color=IM_COL32(96,190,209,255);}else if(ext==".json"||ext==".sdproject"){symbol="{ }";}
    ImVec2 shape[]={{a.x,a.y},{b.x-10,a.y},{b.x,a.y+10},b,{a.x,b.y}};d->AddConvexPolyFilled(shape,5,IM_COL32(42,46,53,255));d->AddPolyline(shape,5,color,ImDrawFlags_Closed,2);
    d->AddLine({b.x-10,a.y},{b.x-10,a.y+10},color);d->AddLine({b.x-10,a.y+10},{b.x,a.y+10},color);
    auto text=ImGui::CalcTextSize(symbol);d->AddText({(a.x+b.x-text.x)*.5f,(a.y+b.y-text.y)*.5f},color,symbol);
}
}
