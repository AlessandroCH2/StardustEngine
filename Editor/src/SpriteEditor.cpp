#include "EditorApp.h"
#include "AssetUI.h"
#include "UI/UIAssets.h"
#include "UI/CanvasRenderer.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Project/Project.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>
namespace Stardust {
void EditorApp::DrawSpriteEditor(const std::string& path) {
    auto sprite=SpriteAsset::Load(path);ImGui::SeparatorText("Sprite");ImGui::TextWrapped("Texture: %s",sprite.texture.c_str());ImGui::Text("Region: %.0f x %.0f px",sprite.rect.z,sprite.rect.w);
    if(ImGui::Button("Open Sprite Editor",{-1,30})){m_SpriteEditorPath=path;m_SpriteEditorOpen=true;}
}
void EditorApp::DrawSpriteEditorWindow(){
    static std::string path;static SpriteAsset sprite;static bool dirty=false;static float zoom=1;static ImVec2 pan{};static bool fit=true;static int mode=0,handle=-1;static glm::vec2 startMouse;static glm::vec4 startRect,startBorder;static glm::vec2 startPivot;
    if(!m_SpriteEditorOpen)return;
    if(path!=m_SpriteEditorPath){if(dirty){ImGui::OpenPopup("Unsaved Sprite");if(ImGui::BeginPopupModal("Unsaved Sprite",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Save the current sprite before opening another?");if(ImGui::Button("Save")){sprite.Save(path);dirty=false;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Discard")){dirty=false;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel")){m_SpriteEditorPath=path;ImGui::CloseCurrentPopup();}ImGui::EndPopup();}}else{path=m_SpriteEditorPath;sprite=SpriteAsset::Load(path);fit=true;pan={};handle=-1;}}
    ImGui::SetNextWindowSize({950,650},ImGuiCond_FirstUseEver);bool open=true;
    if(ImGui::Begin("Sprite Editor",&open)){
        ImGui::TextUnformatted(path.c_str());if(dirty){ImGui::SameLine();ImGui::TextColored({1,.75f,.3f,1},"* Unsaved");}
        if(ImGui::Button("Apply")){sprite.Save(path);AssetManager::Invalidate(path);CanvasRenderer::Clear();dirty=false;}ImGui::SameLine();if(ImGui::Button("Revert")){sprite=SpriteAsset::Load(path);dirty=false;}ImGui::SameLine();if(ImGui::Button("Fit")){fit=true;pan={};}ImGui::SameLine();if(ImGui::Button("1:1")){fit=false;zoom=1;pan={};}ImGui::SameLine();ImGui::SetNextItemWidth(110);ImGui::Combo("##tool",&mode,"Rect\0Border\0Pivot\0");ImGui::SameLine();ImGui::TextDisabled("Wheel: zoom | Middle drag: pan");
        ImGui::Separator();float sidebar=245;ImGui::BeginChild("SpritePreview",{std::max(100.f,ImGui::GetContentRegionAvail().x-sidebar-10),0},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        auto origin=ImGui::GetCursorScreenPos(),available=ImGui::GetContentRegionAvail();auto* draw=ImGui::GetWindowDrawList();ImGui::InvisibleButton("##surface",available,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);bool hovered=ImGui::IsItemHovered();auto mouse=ImGui::GetMousePos();
        draw->PushClipRect(origin,{origin.x+available.x,origin.y+available.y},true);
        for(int y=0;y<(int)available.y;y+=16)for(int x=0;x<(int)available.x;x+=16)draw->AddRectFilled({origin.x+x,origin.y+y},{origin.x+std::min(x+16,(int)available.x),origin.y+std::min(y+16,(int)available.y)},((x/16+y/16)%2)?IM_COL32(65,65,65,255):IM_COL32(48,48,48,255));
        if(auto texture=AssetManager::GetTexture(sprite.texture,true)){
            glm::vec2 textureSize(texture->Width(),texture->Height());if(fit)zoom=std::max(.02f,std::min((available.x-40)/textureSize.x,(available.y-40)/textureSize.y));
            ImVec2 center(origin.x+available.x*.5f+pan.x,origin.y+available.y*.5f+pan.y);
            if(hovered&&ImGui::GetIO().MouseWheel!=0){float old=zoom;zoom=std::clamp(zoom*std::pow(1.15f,ImGui::GetIO().MouseWheel),.02f,32.f);pan.x+=(mouse.x-center.x)*(1-zoom/old);pan.y+=(mouse.y-center.y)*(1-zoom/old);fit=false;center={origin.x+available.x*.5f+pan.x,origin.y+available.y*.5f+pan.y};}
            if(hovered&&ImGui::IsMouseDragging(2)){pan.x+=ImGui::GetIO().MouseDelta.x;pan.y+=ImGui::GetIO().MouseDelta.y;center={origin.x+available.x*.5f+pan.x,origin.y+available.y*.5f+pan.y};}
            ImVec2 image(center.x-textureSize.x*zoom*.5f,center.y-textureSize.y*zoom*.5f);draw->AddImage((ImTextureID)GetDevice().ImGuiTexture(texture.get()),image,{image.x+textureSize.x*zoom,image.y+textureSize.y*zoom});
            auto point=[&](glm::vec2 v){return ImVec2(image.x+v.x*zoom,image.y+v.y*zoom);};glm::vec2 local((mouse.x-image.x)/zoom,(mouse.y-image.y)/zoom);
            ImVec2 a=point({sprite.rect.x,sprite.rect.y}),b=point({sprite.rect.x+sprite.rect.z,sprite.rect.y+sprite.rect.w});draw->AddRect(a,b,IM_COL32(100,210,255,255),0,0,2);
            int hot=-1;auto hit=[&](ImVec2 v,int id){draw->AddRectFilled({v.x-4,v.y-4},{v.x+4,v.y+4},IM_COL32(120,220,255,255));if(std::hypot(mouse.x-v.x,mouse.y-v.y)<9)hot=id;};
            if(mode==0){hit(a,0);hit({b.x,a.y},1);hit(b,2);hit({a.x,b.y},3);}
            float left=a.x+sprite.border.x*zoom,right=b.x-sprite.border.z*zoom,top=a.y+sprite.border.w*zoom,bottom=b.y-sprite.border.y*zoom;
            draw->AddLine({left,a.y},{left,b.y},IM_COL32(100,245,135,255));draw->AddLine({right,a.y},{right,b.y},IM_COL32(100,245,135,255));draw->AddLine({a.x,top},{b.x,top},IM_COL32(100,245,135,255));draw->AddLine({a.x,bottom},{b.x,bottom},IM_COL32(100,245,135,255));
            if(mode==1&&mouse.x>=a.x-8&&mouse.x<=b.x+8&&mouse.y>=a.y-8&&mouse.y<=b.y+8){if(std::abs(mouse.x-left)<6)hot=10;else if(std::abs(mouse.x-right)<6)hot=12;else if(std::abs(mouse.y-top)<6)hot=13;else if(std::abs(mouse.y-bottom)<6)hot=11;}
            auto pivot=point({sprite.rect.x+sprite.pivot.x*sprite.rect.z,sprite.rect.y+(1-sprite.pivot.y)*sprite.rect.w});draw->AddCircle(pivot,6,IM_COL32(255,210,100,255),16,2);if(mode==2&&std::hypot(mouse.x-pivot.x,mouse.y-pivot.y)<12)hot=20;
            if(hovered&&ImGui::IsMouseClicked(0)){handle=hot;if(mode==0&&hot<0&&ImGui::GetIO().KeyCtrl)handle=30;startMouse=local;startRect=sprite.rect;startBorder=sprite.border;startPivot=sprite.pivot;}
            if(!ImGui::IsMouseDown(0))handle=-1;
            if(handle>=0&&ImGui::IsMouseDragging(0)){
                auto d=local-startMouse;
                if(handle<4){bool rightSide=handle==1||handle==2,bottomSide=handle>=2;float x0=startRect.x,y0=startRect.y,x1=x0+startRect.z,y1=y0+startRect.w;if(rightSide)x1=std::clamp(x1+d.x,x0+1,textureSize.x);else x0=std::clamp(x0+d.x,0.f,x1-1);if(bottomSide)y1=std::clamp(y1+d.y,y0+1,textureSize.y);else y0=std::clamp(y0+d.y,0.f,y1-1);sprite.rect={x0,y0,x1-x0,y1-y0};}
                else if(handle<14){int index=handle-10;sprite.border[index]=std::max(0.f,startBorder[index]+(index==0?d.x:index==2?-d.x:index==1?-d.y:d.y));}
                else if(handle==20)sprite.pivot=glm::clamp(startPivot+glm::vec2(d.x/std::max(1.f,sprite.rect.z),-d.y/std::max(1.f,sprite.rect.w)),glm::vec2(0),glm::vec2(1));
                else if(handle==30){auto lo=glm::clamp(glm::min(startMouse,local),glm::vec2(0),textureSize-glm::vec2(1));auto hi=glm::clamp(glm::max(startMouse,local),lo+glm::vec2(1),textureSize);sprite.rect={lo.x,lo.y,hi.x-lo.x,hi.y-lo.y};}dirty=true;
            }
            sprite.rect.x=std::clamp(sprite.rect.x,0.f,textureSize.x-1);sprite.rect.y=std::clamp(sprite.rect.y,0.f,textureSize.y-1);sprite.rect.z=std::clamp(sprite.rect.z,1.f,textureSize.x-sprite.rect.x);sprite.rect.w=std::clamp(sprite.rect.w,1.f,textureSize.y-sprite.rect.y);sprite.border.x=std::clamp(sprite.border.x,0.f,sprite.rect.z);sprite.border.z=std::clamp(sprite.border.z,0.f,sprite.rect.z-sprite.border.x);sprite.border.y=std::clamp(sprite.border.y,0.f,sprite.rect.w);sprite.border.w=std::clamp(sprite.border.w,0.f,sprite.rect.w-sprite.border.y);
        }else{draw->AddText({origin.x+20,origin.y+20},IM_COL32(220,220,220,255),"Assign a texture to begin");}
        draw->PopClipRect();ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("SpriteProperties",{0,0});
        ImGui::SeparatorText("Source");dirty|=AssetUI::AssetField("Texture",sprite.texture,{".png",".jpg",".tga",".bmp"});ImGui::SeparatorText("Pixel Region");ImGui::SetNextItemWidth(-1);dirty|=ImGui::DragFloat2("##origin",glm::value_ptr(sprite.rect),1,0,65536,"%.0f");ImGui::TextDisabled("X / Y (top left)");ImGui::SetNextItemWidth(-1);dirty|=ImGui::DragFloat2("##extent",glm::value_ptr(sprite.rect)+2,1,1,65536,"%.0f");ImGui::TextDisabled("Width / Height");ImGui::SeparatorText("9-Slice Border");ImGui::SetNextItemWidth(-1);dirty|=ImGui::DragFloat4("##border",glm::value_ptr(sprite.border),1,0,8192,"%.0f");ImGui::TextDisabled("Left / Bottom / Right / Top");ImGui::SeparatorText("Pivot");ImGui::SetNextItemWidth(-1);dirty|=ImGui::DragFloat2("##pivot",glm::value_ptr(sprite.pivot),.01f,0,1);if(ImGui::Button("Center")){sprite.pivot={.5f,.5f};dirty=true;}ImGui::TextUnformatted("Pixels Per Unit");ImGui::SetNextItemWidth(-1);dirty|=ImGui::DragFloat("##pixelsPerUnit",&sprite.pixelsPerUnit,1,1,10000);ImGui::Spacing();ImGui::TextWrapped("Drag blue corners to resize the region. Ctrl + drag creates a new region. Border and Pivot tools edit the green guides and yellow pivot.");ImGui::EndChild();
    }ImGui::End();if(!open){if(dirty){ImGui::OpenPopup("Close Sprite Editor?");}else m_SpriteEditorOpen=false;}
    if(ImGui::BeginPopupModal("Close Sprite Editor?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("This sprite has unsaved changes.");if(ImGui::Button("Save & Close")){sprite.Save(path);dirty=false;m_SpriteEditorOpen=false;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Discard & Close")){dirty=false;path.clear();m_SpriteEditorOpen=false;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
}
void EditorApp::RequestDeleteAsset(const std::string& path){m_DeleteAsset=path;}
void EditorApp::DrawDeleteAssetDialog(){
    if(m_DeleteAsset.empty())return;ImGui::OpenPopup("Delete Asset?");
    if(ImGui::BeginPopupModal("Delete Asset?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("Move '%s' to the project Trash?",m_DeleteAsset.c_str());ImGui::TextDisabled("References to this asset will become missing.");
        if(ImGui::Button("Move to Trash")){
            namespace fs=std::filesystem;auto source=fs::weakly_canonical(AssetManager::ToAbsolute(m_DeleteAsset));auto assets=fs::weakly_canonical(m_ProjectRoot);auto relative=source.lexically_relative(assets);
            if(!relative.empty()&&relative!="."&&*relative.begin()!="..")try{auto trash=Project::Dir()/"Trash"/std::to_string(std::chrono::system_clock::now().time_since_epoch().count());fs::create_directories(trash);fs::rename(source,trash/source.filename());AssetManager::Invalidate(m_DeleteAsset);CanvasRenderer::Clear();m_SelectedAsset.clear();m_InspectAsset=false;if(!fs::exists(m_ProjectCurrentDir))m_ProjectCurrentDir=m_ProjectRoot;SD_INFO("Asset moved to {}",trash.string());}catch(const std::exception& e){SD_ERROR("Cannot delete asset: {}",e.what());}
            else SD_ERROR("Only assets inside this project's Assets folder can be deleted");
            m_DeleteAsset.clear();ImGui::CloseCurrentPopup();
        }ImGui::SameLine();if(ImGui::Button("Cancel")){m_DeleteAsset.clear();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
    }
}
}
