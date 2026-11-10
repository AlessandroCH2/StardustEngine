#include "AssetUI.h"
#include "EditorIcons.h"
#include "InspectorWidgets.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureImport.h"
#include "Core/Log.h"
#include "Core/FileSystem.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Stardust::AssetUI {

namespace fs = std::filesystem;
static std::string navigation;
std::string TakeAssetNavigation(){return std::exchange(navigation,{});}

static void Label(const char* label) {
    if(label[0]=='#'&&label[1]=='#'){ImGui::SetNextItemWidth(-1);return;}
    Insp::LabelCell(label);
}

bool IsSpriteTexture(const std::string& path) { return AssetManager::IsTextureFile(AssetManager::ToAbsolute(path)) && TextureImportSettings::Load(path).type == 2; }

static bool AcceptAssetDrop(std::string& path, const std::vector<std::string>& extensions) {
    bool changed = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetPayload)) {
            std::string dropped((const char*)p->Data, p->DataSize - 1);
            std::string ext = fs::path(dropped).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) { path = dropped; changed = true; }
            else if (std::find(extensions.begin(), extensions.end(), ".sprite") != extensions.end() && IsSpriteTexture(dropped)) { path = dropped; changed = true; }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

static bool AssetSlot(std::string& path, const std::vector<std::string>& extensions, const char* noneLabel, bool builtins) {
    ObjectSlotDesc desc;
    desc.typeName = AssetTypeName(extensions.empty() ? std::string() : extensions[0]);
    bool isBuiltin = path.starts_with("builtin:");
    std::string name = isBuiltin ? path.substr(8) : FileSystem::Utf8(fs::u8path(path).stem());
    desc.text = !path.empty() ? name : std::string_view(noneLabel) == "None" ? "None (" + desc.typeName + ")" : std::string(noneLabel);
    desc.currentKey = path;
    desc.allowNone = !builtins;
    desc.iconItem.key = path;
    desc.assets = [&extensions, builtins] {
        std::vector<PickerItem> items;
        if (builtins)
            for (u32 t = 0; t < (u32)PrimitiveType::Count; ++t) {
                PickerItem item;
                item.key = AssetManager::BuiltinRef((PrimitiveType)t);
                item.name = PrimitiveName((PrimitiveType)t);
                item.detail = "Built-in";
                items.push_back(std::move(item));
            }
        std::vector<std::string> found = AssetManager::Find(extensions);
        // Textures imported as Sprite are sprites too
        if (std::find(extensions.begin(), extensions.end(), ".sprite") != extensions.end())
            for (const std::string& asset : AssetManager::Find({ ".png", ".jpg", ".jpeg", ".tga", ".bmp" }))
                if (IsSpriteTexture(asset) && std::find(found.begin(), found.end(), asset) == found.end()) found.push_back(asset);
        for (const std::string& asset : found) {
            PickerItem item;
            item.key = asset;
            item.name = FileSystem::Utf8(fs::u8path(asset).stem());
            item.detail = asset;
            items.push_back(std::move(item));
        }
        return items;
    };
    desc.sceneItems = [&extensions, builtins] { return AssetsUsedInScene(extensions, builtins); };
    std::string picked;
    bool doubleClicked = false;
    bool changed = ObjectSlot(desc, picked, &doubleClicked) && picked != path;
    if (changed) path = picked;
    if (doubleClicked && !path.empty() && !isBuiltin) navigation = path;
    if (!path.empty() && !isBuiltin && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
    changed |= AcceptAssetDrop(path, extensions);
    return changed;
}

bool AssetField(const char* label, std::string& path, const std::vector<std::string>& extensions, const char* noneLabel) {
    ImGui::PushID(label);
    Label(label);
    bool changed = AssetSlot(path, extensions, noneLabel, false);
    ImGui::PopID();
    return changed;
}

static bool TextureSlot(const char* label, std::string& path) {
    static const std::vector<std::string> exts = { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
    return AssetField(label, path, exts);
}

static bool Slider(const char* label, float& v, float a, float b) {
    ImGui::PushID(label);
    Label(label);
    bool c = Insp::SliderFloat("##v", &v, a, b, "%.2f");
    ImGui::PopID();
    return c;
}

bool MaterialEditor(Material& m) {
    bool changed = false;
    ImGui::PushID(&m);
    changed |= AssetField("Custom Shader",m.customPixelShader,{".spv"},"(built-in shader)");
    Label(m.customPixelShader.empty()?"Shader":"Surface Pipeline");
    if (Insp::BeginCombo("##shader", ShaderName(m.shader))) {
        for (int s = 0; s < (int)ShaderType::Count; ++s)
            if (ImGui::Selectable(ShaderName((ShaderType)s), (int)m.shader == s)) { m.shader = (ShaderType)s; changed = true; }
        ImGui::EndCombo();
    }

    if(!m.customPixelShader.empty()||m.shader==ShaderType::Water){
        using json=nlohmann::json;auto layout=m.PropertyLayout(),values=json::parse(m.propertiesJson,nullptr,false);if(!values.is_object())values=json::object();
        ImGui::SeparatorText(m.shader==ShaderType::Water&&m.customPixelShader.empty()?"Water Surface":"Shader Properties");
        if(layout.empty())ImGui::TextWrapped("This shader has no declared properties. Define them in the shader's .spv.properties.json asset.");
        for(size_t index=0;index<layout.size();++index){auto& field=layout[index];if(!field.is_object())continue;auto name=field.value("name","");auto label=field.value("label",name);auto type=field.value("type","float");auto value=values.contains(name)?values[name]:field.value("default",json(0));bool edit=false;ImGui::PushID((int)index);
            if(type=="texture"){std::string path=value.is_string()?value.get<std::string>():"";edit=AssetField(label.c_str(),path,{".png",".jpg",".jpeg",".bmp",".tga",".rendertexture"});if(edit)value=path;}
            else {Label(label.c_str());if(type=="bool"){bool v=value.is_boolean()?value.get<bool>():value.is_number()&&value.get<float>()!=0;edit=Insp::Checkbox("##value",&v);if(edit)value=v;}
                else if(type=="int"){int v=value.is_number()?value.get<int>():0;edit=Insp::DragInt("##value",&v,1,field.value("min",0),field.value("max",0));if(edit)value=v;}
                else if(type=="float"){float v=value.is_number()?value.get<float>():0;if(field.contains("min")&&field.contains("max"))edit=Insp::SliderFloat("##value",&v,field["min"].get<float>(),field["max"].get<float>());else edit=Insp::DragFloat("##value",&v,.01f);if(edit)value=v;}
                else{float v[4]={0,0,0,1};int count=type=="vector2"?2:type=="vector3"?3:4;if(value.is_array())for(int i=0;i<std::min<int>(count,(int)value.size());++i)if(value[i].is_number())v[i]=value[i].get<float>();edit=type=="color"?EditorIcons::ColorField("##value",v):count==2?Insp::DragFloat2("##value",v,.01f):count==3?Insp::DragFloat3("##value",v,.01f):Insp::DragFloat4("##value",v,.01f);if(edit){value=json::array();for(int i=0;i<count;++i)value.push_back(v[i]);}}
            }
            if(edit){values[name]=value;changed=true;}ImGui::PopID();
        }
        if(ImGui::Button("Reset Shader Properties")){values=json::object();changed=true;}if(changed)m.propertiesJson=values.dump();
        Label("Double Sided");changed|=Insp::Checkbox("##ds",&m.doubleSided);ImGui::PopID();if(changed)m.dirty=true;return changed;
    }
    Label("Color");
    changed |= m.shader == ShaderType::Transparent ? Insp::ColorEdit4("##col", glm::value_ptr(m.color))
                                                   : Insp::ColorEdit3("##col", glm::value_ptr(m.color));
    changed |= TextureSlot("Albedo Map", m.albedoMap);
    if (m.shader != ShaderType::Unlit) {
        changed |= TextureSlot("Normal Map", m.normalMap);
        if (!m.normalMap.empty()) changed |= Slider("Normal Strength", m.normalStrength, 0.0f, 2.0f);
        changed |= Slider("Roughness", m.roughness, 0.02f, 1.0f);
        if (m.shader != ShaderType::Toon) changed |= Slider("Metallic", m.metallic, 0.0f, 1.0f);
    }
    if (m.shader == ShaderType::Toon) {
        changed |= Slider("Bands", m.toonSteps, 1.0f, 8.0f);
        changed |= Slider("Rim Light", m.rimStrength, 0.0f, 1.0f);
    }
    Label("Emission");
    changed |= Insp::ColorEdit3("##em", glm::value_ptr(m.emissive));
    if (glm::length(m.emissive) > 0.0f) changed |= Slider("Emission Power", m.emissiveIntensity, 0.0f, 50.0f);
    if (m.shader != ShaderType::Transparent) changed |= Slider("Alpha Cutoff", m.alphaCutoff, 0.0f, 1.0f);
    Label("Tiling");
    changed |= Insp::DragFloat2("##til", glm::value_ptr(m.tiling), 0.01f);
    Label("Offset");
    changed |= Insp::DragFloat2("##off", glm::value_ptr(m.offset), 0.01f);
    Label("Double Sided");
    changed |= Insp::Checkbox("##ds", &m.doubleSided);
    ImGui::PopID();
    if (changed) m.dirty = true;
    return changed;
}

void MeshRendererInspector(MeshRendererComponent& mr) {
    ImGui::SeparatorText("Materials");int size=(int)mr.materials.size();Label("Size");if(Insp::DragInt("##materialCount",&size,1,1,256))mr.materials.resize(std::clamp(size,1,256));
    auto mesh=AssetManager::GetMesh(mr.mesh);size_t count=1;if(mesh)for(auto& part:mesh->SubMeshes())count=std::max(count,(size_t)part.materialSlot+1);
    if(mr.materials.empty()){mr.materials.resize(count,mr.materialPath);mr.materialPath.clear();}if(mr.materials.size()<count)mr.materials.resize(count);
    for(size_t i=0;i<mr.materials.size();++i){ImGui::PushID((int)i);std::string label=std::format("Element {}",i);AssetField(label.c_str(),mr.materials[i],{".mat"},"Default Material");ImGui::PopID();}
    ImGui::TextDisabled("Double-click a material to reveal its asset.");ImGui::SeparatorText("Lighting");Label("Cast Shadows");Insp::Checkbox("##cs",&mr.castShadows);Label("Receive Shadows");Insp::Checkbox("##rs",&mr.receiveShadows);
    if(ImGui::TreeNode("Geometry")){
    static const std::vector<std::string> meshExts = { ".obj", ".gltf", ".glb", ".fbx" };
    // Mesh: built-in primitives + imported files
    ImGui::PushID("mesh");
    Label("Mesh");
    AssetSlot(mr.mesh, meshExts, "None", true);
    ImGui::PopID();
    if (!mr.mesh.starts_with("builtin:") && !AssetManager::GetMesh(mr.mesh))
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Mesh could not be loaded");



    ImGui::TreePop();}

}

} // namespace Stardust::AssetUI
