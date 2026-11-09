#include "InspectorWidgets.h"
#include "EditorApp.h"
#include "AssetUI.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureImport.h"
#include "Assets/Pak.h"
#include "Core/FileSystem.h"
#include "Core/Process.h"
#include "Core/Log.h"
#include "Project/Project.h"
#include "UI/UIAssets.h"
#include <imgui.h>
#include <imgui_stdlib.h>
namespace Stardust {
void EditorApp::DrawAssetInspector() {
    namespace fs = std::filesystem;
    auto path = AssetManager::ToAbsolute(m_SelectedAsset);
    ImGui::TextWrapped("%s",m_SelectedAsset.c_str()); ImGui::Separator();
    if (ImGui::Button("Open")) ShellExecuteFile(path);
    ImGui::SameLine(); if (ImGui::Button("Show Folder")) ShellExecuteFile(path.parent_path());
    ImGui::SameLine();if(ImGui::Button("Delete Asset"))RequestDeleteAsset(m_SelectedAsset);
    if (fs::is_directory(path)) {
        ImGui::TextDisabled("Folder");
        if (ImGui::Button("Build Content Pak")) {
            auto output = path.parent_path() / (path.filename().string() + ".pak");
            if (Pak::Build(path,output)) SD_INFO("Content pak built: {}",output.string());
        }
        return;
    }
    std::error_code ec; auto bytes = fs::file_size(path,ec);
    if (!ec) ImGui::TextDisabled("%llu bytes",(unsigned long long)bytes);
    auto ext = path.extension().string();
    if(AssetManager::IsTextureFile(path)){
        auto settings=TextureImportSettings::Load(m_SelectedAsset);bool changed=false;
        ImGui::SeparatorText("Texture Import Settings");changed|=Insp::Combo("Texture Type",&settings.type,"Default\0Normal Map\0Sprite (2D/UI)\0");
        changed|=Insp::Checkbox("sRGB (Color)",&settings.srgb);changed|=Insp::Checkbox("Generate Mip Maps",&settings.mipmaps);changed|=Insp::InputInt("Max Size",&settings.maxSize);
        changed|=Insp::Combo("Filter Mode",&settings.filter,"Point\0Bilinear\0Anisotropic\0");changed|=Insp::Combo("Wrap Mode",&settings.wrap,"Clamp\0Repeat\0");
        if(changed&&settings.Save(m_SelectedAsset)){GetDevice().WaitIdle();AssetManager::Invalidate(m_SelectedAsset);}
        if(settings.type==2&&ImGui::Button("Create Sprite Asset")){SpriteAsset sprite;sprite.texture=m_SelectedAsset;sprite.Save(m_SelectedAsset+".sprite");}
    }
    if(ext==".rendertexture"){
        auto j=nlohmann::json::parse(FileSystem::ReadText(path),nullptr,false);if(!j.is_object())j=nlohmann::json::object();int width=j.value("width",512),height=j.value("height",512);bool changed=Insp::InputInt("Width",&width);changed|=Insp::InputInt("Height",&height);if(changed)FileSystem::WriteText(path,nlohmann::json({{"width",std::clamp(width,16,8192)},{"height",std::clamp(height,16,8192)}}).dump(2));if(auto texture=AssetManager::GetTexture(m_SelectedAsset,false)){float w=std::min(360.f,ImGui::GetContentRegionAvail().x);ImGui::Image((ImTextureID)GetDevice().ImGuiTexture(texture.get()),{w,w*height/std::max(1,width)});}return;
    }
    if(ext==".prefab"&&ImGui::Button("Open Prefab Editor")){OpenPrefab(path);return;}
    if(ext==".sprite"){DrawSpriteEditor(m_SelectedAsset);return;}
    if(ext==".avatar"){AssetUI::AvatarInspector(m_SelectedAsset);return;}
    if(ext==".mat"){if(auto material=AssetManager::GetMaterial(m_SelectedAsset)){AssetUI::MaterialEditor(*material);if(material->dirty&&ImGui::Button("Save Material"))AssetManager::SaveMaterial(*material);if(m_MaterialPreview){float width=std::min(256.f,ImGui::GetContentRegionAvail().x);float gap=ImGui::GetContentRegionAvail().y-width-28;if(gap>0)ImGui::Dummy({1,gap});ImGui::SeparatorText("Material Preview");ImGui::Image((ImTextureID)GetDevice().ImGuiTexture(m_MaterialPreview->Output()),{width,width});if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(0))m_MaterialPreviewYaw+=ImGui::GetIO().MouseDelta.x*.01f;}}return;}
    if(ext==".anim"||ext==".controller")if(ImGui::Button(ext==".anim"?"Open Animation Window":"Open Animator Window")){m_AnimationAsset=m_SelectedAsset;m_ShowAnimationEditor=true;}
    if (AssetManager::IsTextureFile(path)) {
        if (auto tex=AssetManager::GetTexture(m_SelectedAsset,true)) {
            float width=std::max(1.0f,ImGui::GetContentRegionAvail().x);
            ImGui::Image((ImTextureID)GetDevice().ImGuiTexture(tex.get()),{width,width*tex->Height()/std::max(1u,tex->Width())});
            if(ImGui::Button("Create Sprite")){SpriteAsset sprite;sprite.texture=m_SelectedAsset;sprite.rect={0,0,tex->Width(),tex->Height()};auto destination=path;destination.replace_extension(".sprite");sprite.Save(AssetManager::ToRelative(destination));}
            static int cell[2]={64,64};static int gap=0;
            ImGui::InputInt2("Slice Cell",cell);Insp::InputInt("Slice Gap",&gap);
            if(ImGui::Button("Slice Grid Into Sprites")){
                int index=0;for(int y=0;y+(std::max(1,cell[1]))<=(int)tex->Height();y+=std::max(1,cell[1])+std::max(0,gap))for(int x=0;x+std::max(1,cell[0])<=(int)tex->Width()&&index<4096;x+=std::max(1,cell[0])+std::max(0,gap)){
                    SpriteAsset sprite;sprite.texture=m_SelectedAsset;sprite.rect={(float)x,(float)y,(float)std::max(1,cell[0]),(float)std::max(1,cell[1])};auto output=path.parent_path()/(path.stem().string()+"_"+std::to_string(index++)+".sprite");if(!std::filesystem::exists(output))sprite.Save(AssetManager::ToRelative(output));
                }
            }
        }
    } else if (AssetManager::IsMeshFile(path)) {
        if (auto mesh=AssetManager::GetMesh(m_SelectedAsset)) { ImGui::Text("Vertices: %u",mesh->VertexCount()); ImGui::Text("Triangles: %u",mesh->IndexCount()/3); }
        if (ImGui::Button("Add to Scene")) m_Selected=CreateFromAsset(m_SelectedAsset,kNullEntity,nullptr);
        if (ext==".fbx") { std::string open; AssetUI::FbxImportInspector(m_SelectedAsset,open); if(!open.empty()){m_SelectedAsset=open;m_InspectAsset=true;} }
    } else if (ext==".pak") {
        if (ImGui::Button("Mount Content Pak")) Pak::Mount(path,Project::AssetsDir());
    } else if (!ec && bytes < 1024*1024 && (ext==".cs" || ext==".hlsl" || ext==".anim" || ext==".controller" || ext==".terrainlayer" || ext==".json")) {
        static std::string loaded, text; static bool dirty=false;
        if (loaded!=FileSystem::Utf8(path)) { loaded=FileSystem::Utf8(path); text=FileSystem::ReadText(path); dirty=false; }
        dirty |= ImGui::InputTextMultiline("##source",&text,{-1,300},ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::Button(dirty ? "Save *" : "Save")) { if (FileSystem::WriteText(path,text)) dirty=false; }
        if (ext==".hlsl") {
            ImGui::SameLine();
            if (ImGui::Button("Compile Pixel Shader")) {
                if (FileSystem::WriteText(path,text)) {
                    dirty=false;
                    auto output=fs::path(FileSystem::Utf8(path)+".spv");
                    std::wstring command=L"\"" + (fs::exists(STARDUST_DXC_PATH)?fs::path(STARDUST_DXC_PATH):FileSystem::ExecutableDir()/"tools/dxc/bin/dxc").wstring() + L"\" -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -T ps_6_6 -E main -HV 2021 -O3 -Zpc -D STARDUST_VULKAN=1 -I \"" + (FileSystem::EngineRoot()/"Assets/Shaders").wstring() + L"\" -Fo \"" + output.wstring() + L"\" \"" + path.wstring() + L"\"";
                    int result=RunProcess(command,path.parent_path(),[](const std::string& line){SD_INFO("{}",line);});
                    if (result==0) SD_INFO("Shader compiled: {}",output.string()); else SD_ERROR("Shader compilation failed ({}); see Console",result);
                }
            }
            ImGui::SeparatorText("Shader Properties");static std::string propertiesPath,propertiesText;auto schemaPath=FileSystem::Utf8(path)+".spv.properties.json";if(propertiesPath!=schemaPath){propertiesPath=schemaPath;propertiesText=FileSystem::ReadText(fs::u8path(schemaPath));if(propertiesText.empty())propertiesText="{\"properties\":[]}";}
            ImGui::TextWrapped("Declare float, int, bool, color, vector2/3/4 and texture properties here. Array order is the HLSL material slot index.");ImGui::InputTextMultiline("##propertySchema",&propertiesText,{-1,180},ImGuiInputTextFlags_AllowTabInput);if(ImGui::Button("Save Shader Properties")){auto schema=nlohmann::json::parse(propertiesText,nullptr,false);if(schema.is_object()&&schema.contains("properties")&&schema["properties"].is_array())FileSystem::WriteText(fs::u8path(schemaPath),schema.dump(2));else SD_ERROR("Expected a JSON object containing a properties array");}

        }
    }
}
}
