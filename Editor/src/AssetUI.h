#pragma once
#include "Renderer/Material.h"
#include "Scene/Components.h"
#include <filesystem>
#include <functional>
#include <vector>
#include <imgui.h>
#include <nlohmann/json.hpp>
namespace Stardust { class Scene; }
namespace Stardust::RHI { class Device; }

namespace Stardust::AssetUI {

// Combo listing project assets with the given extensions, plus drag & drop from the Project panel.
// Returns true when the path changed.
bool AssetField(const char* label, std::string& path, const std::vector<std::string>& extensions, const char* noneLabel = "None");
// A texture whose Texture Type is Sprite: it can be used wherever a .sprite is expected
bool IsSpriteTexture(const std::string& path);

// Shader dropdown + the properties that shader exposes. Returns true when anything changed.
bool MaterialEditor(Material& m);

// Mesh field, material slot (embedded or .mat asset) and the material editor
void MeshRendererInspector(MeshRendererComponent& mr);
void IconButton(const std::filesystem::path& path,bool directory,bool selected,glm::vec2 size);
std::string TakeAssetNavigation();
bool EntityField(Scene& scene,const std::string& type,nlohmann::json& value);
void DrawIcon(ImDrawList* draw,const std::filesystem::path& path,ImVec2 rectMin,ImVec2 rectMax,bool directory);

// Device and scene used for picker thumbnails and the picker's Scene tab; set once per frame.
void SetEditorContext(RHI::Device* device,Scene* scene);
std::string AssetTypeName(const std::string& extension);

struct PickerItem {
    std::string key;            // value handed back when picked; empty means None
    std::string name, detail;
    std::string icon;           // component name drawn for scene objects
    bool isEntity = false;
};
struct ObjectSlotDesc {
    std::string text, typeName, currentKey;
    PickerItem iconItem;
    int defaultTab = 0;         // 0 = Assets, 1 = Scene
    bool allowNone = true;
    std::string assetsHint;     // shown when the Assets tab is empty
    std::function<std::vector<PickerItem>()> assets, sceneItems;
};
// Object slot drawn at the current item width, with the grid picker behind its target button.
// Returns true when the user picked something (picked holds the key); the slot stays the last item,
// so callers can add a drag & drop target right after.
bool ObjectSlot(const ObjectSlotDesc& desc,std::string& picked,bool* doubleClicked=nullptr);
void FbxImportInspector(const std::string& asset,std::string& openAsset);
void AvatarInspector(const std::string& asset);
std::vector<PickerItem> AssetsUsedInScene(const std::vector<std::string>& extensions,bool includeBuiltins=false);

// Drag & drop payload id used by the Project panel (payload = UTF-8 asset-relative path)
inline constexpr const char* kAssetPayload = "STARDUST_ASSET";

} // namespace Stardust::AssetUI
