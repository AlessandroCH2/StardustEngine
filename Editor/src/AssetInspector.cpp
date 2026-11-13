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
#include <imgui_internal.h>
#include "UI/CanvasRenderer.h"
#include <format>
#include <imgui_stdlib.h>
#include "ShaderAssets.h"
#include "ShaderGraphEditor.h"
#include "ScriptEditor.h"
#include "Renderer/Material.h"
#include <nlohmann/json.hpp>
namespace Stardust {
void EditorApp::DrawAssetInspector() {
    namespace fs = std::filesystem;
    auto path = AssetManager::ToAbsolute(m_SelectedAsset);
    ImGui::TextWrapped("%s",m_SelectedAsset.c_str());
    if(m_AssetSelection.size()>1&&std::find(m_AssetSelection.begin(),m_AssetSelection.end(),m_SelectedAsset)!=m_AssetSelection.end())ImGui::TextDisabled("%d assets selected",(int)m_AssetSelection.size());
    ImGui::Separator();
    if (ImGui::Button("Open")) ShellExecuteFile(path);
    ImGui::SameLine(); if (ImGui::Button("Show Folder")) ShellExecuteFile(path.parent_path());
    ImGui::SameLine();if(ImGui::Button("Delete Asset"))RequestDeleteAsset(m_SelectedAsset);
    if (ShaderAssets::IsShaderAsset(path)) {
        const bool graph = path.extension() == ".shadergraph";
        ImGui::SeparatorText(graph ? "Shader Graph" : "Shader");
        auto schema = nlohmann::json::parse(FileSystem::ReadText(fs::path(FileSystem::Utf8(path) + ".spv.properties.json")), nullptr, false);
        const std::string error = ShaderAssets::LastError(path);
        if (ShaderAssets::IsCompiling()) ImGui::TextColored({ 1, 0.85f, 0.3f, 1 }, "Compiling...");
        else if (!error.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1)); ImGui::TextWrapped("%s", error.c_str()); ImGui::PopStyleColor(); }
        else if (!fs::exists(fs::path(FileSystem::Utf8(path) + ".spv"))) ImGui::TextDisabled("Not compiled yet");
        if (schema.is_object()) {
            ImGui::Text("%s", schema.value("name", std::string()).c_str());
            const auto state = schema.value("renderState", nlohmann::json::object());
            ImGui::TextDisabled("Queue %s, blend %s%s", state.value("queue", std::string("geometry")).c_str(), state.value("blend", std::string("opaque")).c_str(), state.contains("cull") ? (", cull " + state.value("cull", std::string())).c_str() : "");
            for (auto& p : schema.value("properties", nlohmann::json::array())) ImGui::BulletText("%s  (%s)  %s", p.value("label", std::string()).c_str(), p.value("type", std::string()).c_str(), p.value("name", std::string()).c_str());
        }
        if (ImGui::Button(graph ? "Open Shader Graph" : "Edit Shader")) { if (graph) ShaderGraphEditor::Open(m_SelectedAsset); else ScriptEditor::Open(path); }
        ImGui::SameLine(); if (ImGui::Button("Compile")) ShaderAssets::CompileAsync(path);
        ImGui::SameLine();
        if (ImGui::Button("Create Material")) {
            Material material; material.name = path.stem().string(); material.customPixelShader = m_SelectedAsset;
            auto output = path.parent_path() / (path.stem().string() + ".mat");
            for (int n = 1; fs::exists(output); ++n) output = path.parent_path() / std::format("{} {}.mat", path.stem().string(), n);
            nlohmann::json j; material.ToJson(j);
            FileSystem::WriteText(output, j.dump(2));
            m_SelectedAsset = AssetManager::ToRelative(output); m_InspectAsset = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A material using this shader, next to it");
        return;
    }
    if (fs::is_directory(path)) {
        ImGui::TextDisabled("Folder");
        if (ImGui::Button("Build Content Pak")) {
            auto output = path.parent_path() / (path.filename().string() + ".pak");
            if (Pak::Build(path,output,Pak::HasKey())) SD_INFO("Content pak built: {}",output.string());
        }
        return;
    }
    std::error_code ec; auto bytes = fs::file_size(path,ec);
    if (!ec) ImGui::TextDisabled("%llu bytes",(unsigned long long)bytes);
    auto ext = path.extension().string();
    if(AssetManager::IsTextureFile(path)){
        // With several textures selected, a change applies to all of them, field by field
        std::vector<std::string> targets;
        for(auto& asset:m_AssetSelection)if(AssetManager::IsTextureFile(AssetManager::ToAbsolute(asset)))targets.push_back(asset);
        if(std::find(targets.begin(),targets.end(),m_SelectedAsset)==targets.end())targets={m_SelectedAsset};
        std::vector<TextureImportSettings> all;for(auto& t:targets)all.push_back(TextureImportSettings::Load(t));
        auto settings=TextureImportSettings::Load(m_SelectedAsset);const auto before=settings;bool changed=false;
        auto differs=[&](auto get){for(auto& s:all)if(get(s)!=get(all[0]))return true;return false;};
        // Values that differ between the selected textures are drawn in amber (checkboxes show a dash)
        auto field=[&](bool mixed,auto draw){if(mixed){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.75f,.35f,1));ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);}bool c=draw();if(mixed){ImGui::PopItemFlag();ImGui::PopStyleColor();if(ImGui::IsItemHovered())ImGui::SetTooltip("Different in the selected textures");}changed|=c;};
        ImGui::SeparatorText(targets.size()>1?std::format("Texture Import Settings ({} textures)",targets.size()).c_str():"Texture Import Settings");
        field(differs([](auto& s){return s.type;}),[&]{return Insp::Combo("Texture Type",&settings.type,"Default\0Normal Map\0Sprite (2D and UI)\0");});
        field(differs([](auto& s){return s.srgb;}),[&]{return Insp::Checkbox("sRGB (Color)",&settings.srgb);});
        field(differs([](auto& s){return s.mipmaps;}),[&]{return Insp::Checkbox("Generate Mip Maps",&settings.mipmaps);});
        field(differs([](auto& s){return s.maxSize;}),[&]{return Insp::InputInt("Max Size",&settings.maxSize);});
        field(differs([](auto& s){return s.filter;}),[&]{return Insp::Combo("Filter Mode",&settings.filter,"Point\0Bilinear\0Anisotropic\0");});
        field(differs([](auto& s){return s.wrap;}),[&]{return Insp::Combo("Wrap Mode",&settings.wrap,"Clamp\0Repeat\0");});
        if(changed){
            GetDevice().WaitIdle();
            for(size_t i=0;i<targets.size();++i){auto s=all[i];
                if(settings.type!=before.type){s.type=settings.type;if(s.type==2&&before.type!=2){s.mipmaps=false;s.wrap=0;}}
                if(settings.srgb!=before.srgb)s.srgb=settings.srgb;if(settings.mipmaps!=before.mipmaps)s.mipmaps=settings.mipmaps;
                if(settings.maxSize!=before.maxSize)s.maxSize=std::clamp(settings.maxSize,16,16384);if(settings.filter!=before.filter)s.filter=settings.filter;if(settings.wrap!=before.wrap)s.wrap=settings.wrap;
                if(s.Save(targets[i]))AssetManager::Invalidate(targets[i]);}
            CanvasRenderer::Clear();
            settings=TextureImportSettings::Load(m_SelectedAsset);
        }
        if(settings.type==2){
            // The sprite lives in the texture's .meta: no separate asset, use the texture itself in Image fields
            std::vector<std::string> sprites;for(auto& t:targets)if(TextureImportSettings::Load(t).type==2)sprites.push_back(t);
            std::vector<SpriteAsset> current;for(auto& t:sprites)current.push_back(SpriteAsset::Load(t));
            auto sprite=SpriteAsset::Load(m_SelectedAsset);const auto old=sprite;bool edited=false;
            auto spriteDiffers=[&](auto get){for(auto& s:current)if(get(s)!=get(current[0]))return true;return false;};
            auto spriteField=[&](bool mixed,auto draw){if(mixed)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.75f,.35f,1));bool c=draw();if(mixed){ImGui::PopStyleColor();if(ImGui::IsItemHovered())ImGui::SetTooltip("Different in the selected textures");}edited|=c;};
            ImGui::SeparatorText("Sprite");
            spriteField(spriteDiffers([](auto& s){return s.pixelsPerUnit;}),[&]{return Insp::DragFloat("Pixels Per Unit",&sprite.pixelsPerUnit,1,1,10000,"%.0f");});
            static const char* pivots[]={"Center","Top Left","Top","Top Right","Left","Right","Bottom Left","Bottom","Bottom Right","Custom"};
            static const glm::vec2 pivotValues[]={{.5f,.5f},{0,1},{.5f,1},{1,1},{0,.5f},{1,.5f},{0,0},{.5f,0},{1,0}};
            int pivotIndex=9;for(int i=0;i<9;++i)if(glm::all(glm::lessThan(glm::abs(sprite.pivot-pivotValues[i]),glm::vec2(1e-4f))))pivotIndex=i;
            const bool pivotMixed=spriteDiffers([](auto& s){return glm::vec2(s.pivot);});
            spriteField(pivotMixed,[&]{if(!Insp::BeginCombo("Pivot",pivots[pivotIndex]))return false;bool c=false;for(int i=0;i<9;++i)if(ImGui::Selectable(pivots[i],pivotIndex==i)){sprite.pivot=pivotValues[i];c=true;}ImGui::EndCombo();return c;});
            if(pivotIndex==9)spriteField(pivotMixed,[&]{return Insp::DragFloat2("Custom Pivot",&sprite.pivot.x,.01f,0,1);});
            spriteField(spriteDiffers([](auto& s){return glm::vec4(s.border);}),[&]{return Insp::DragFloat4("Border (L B R T)",&sprite.border.x,1,0,8192,"%.0f");});
            if(edited){
                for(size_t i=0;i<sprites.size();++i){auto s=current[i];
                    if(sprite.pixelsPerUnit!=old.pixelsPerUnit)s.pixelsPerUnit=std::max(1.f,sprite.pixelsPerUnit);
                    if(sprite.pivot!=old.pivot)s.pivot=glm::clamp(sprite.pivot,glm::vec2(0),glm::vec2(1));
                    if(sprite.border!=old.border)s.border=glm::max(sprite.border,glm::vec4(0));
                    s.Save(sprites[i]);}
                CanvasRenderer::Clear();
            }
            if(sprites.size()==1&&ImGui::Button("Sprite Editor")){m_SpriteEditorPath=m_SelectedAsset;m_SpriteEditorOpen=true;}
            ImGui::TextDisabled("Drop the texture on an Image or Sprite Renderer to use it.");
        }
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
            if(TextureImportSettings::Load(m_SelectedAsset).type!=2&&ImGui::Button("Create Sprite")){SpriteAsset sprite;sprite.texture=m_SelectedAsset;sprite.rect={0,0,tex->Width(),tex->Height()};auto destination=path;destination.replace_extension(".sprite");sprite.Save(AssetManager::ToRelative(destination));}
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
                    std::wstring command=L"\"" + (fs::exists(FileSystem::ExecutableDir()/"tools/dxc/bin")?FileSystem::ExecutableDir()/"tools/dxc/bin/dxc":fs::path("dxc")).wstring() + L"\" -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -T ps_6_6 -E main -HV 2021 -O3 -Zpc -D STARDUST_VULKAN=1 -I \"" + (FileSystem::EngineRoot()/"Assets/Shaders").wstring() + L"\" -Fo \"" + output.wstring() + L"\" \"" + path.wstring() + L"\"";
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
