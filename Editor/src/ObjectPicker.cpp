// Unity-style object slot ("None (Material)" + target button) and the grid picker with Assets / Scene tabs.
#include "AssetUI.h"
#include "EditorIcons.h"
#include "InspectorWidgets.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "RHI/RHI.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <set>

namespace Stardust::AssetUI {

namespace fs = std::filesystem;

static RHI::Device* s_Device = nullptr;
static Scene* s_Scene = nullptr;

void SetEditorContext(RHI::Device* device, Scene* scene) { s_Device = device; s_Scene = scene; }

static std::string LowerExtension(const std::string& path) {
    std::string ext = FileSystem::Utf8(fs::u8path(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string AssetTypeName(const std::string& ext) {
    if (ext == ".mat") return "Material";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return "Texture";
    if (ext == ".rendertexture") return "Render Texture";
    if (ext == ".sprite") return "Sprite";
    if (ext == ".ttf" || ext == ".otf") return "Font";
    if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg") return "Audio Clip";
    if (ext == ".anim") return "Animation Clip";
    if (ext == ".controller") return "Animator Controller";
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") return "Mesh";
    if (ext == ".spv") return "Shader";
    if (ext == ".prefab") return "Prefab";
    if (ext == ".avatar") return "Avatar";
    if (ext == ".cs") return "Script";
    if (ext == ".terrainlayer") return "Terrain Layer";
    if (ext == ".terrain") return "Terrain Data";
    return "Asset";
}

// A lit ball, standing in for a rendered material preview
static void DrawMaterialBall(ImDrawList* draw, ImVec2 center, float radius, const std::string& path) {
    glm::vec3 base(0.75f);
    void* texture = nullptr;
    if (auto material = AssetManager::GetMaterial(path)) {
        base = glm::vec3(material->color);
        if (!material->albedoMap.empty() && s_Device)
            if (auto albedo = AssetManager::GetTexture(material->albedoMap, true)) texture = s_Device->ImGuiTexture(albedo.get());
    }
    ImVec2 lo(center.x - radius, center.y - radius), hi(center.x + radius, center.y + radius);
    if (texture) draw->AddImageRounded((ImTextureID)texture, lo, hi, { 0, 0 }, { 1, 1 }, IM_COL32_WHITE, radius);
    const int steps = std::max(8, (int)radius);
    for (int i = 0; i < steps; ++i) {
        float k = i / (float)(steps - 1);
        float r = radius * (1.0f - k * 0.96f);
        ImVec2 c(center.x - radius * 0.32f * k, center.y - radius * 0.38f * k);
        float light = 0.16f + 0.84f * std::pow(k, 1.4f);
        if (texture) {
            // darken the rim only; the image underneath carries the colour
            int alpha = (int)((1.0f - k) * 28.0f);
            draw->AddCircleFilled(c, r, IM_COL32(0, 0, 0, alpha), 40);
        } else {
            glm::vec3 color = glm::clamp(base * light + glm::vec3(std::pow(k, 18.0f) * 0.7f), glm::vec3(0), glm::vec3(1));
            draw->AddCircleFilled(c, r, IM_COL32((int)(color.r * 255), (int)(color.g * 255), (int)(color.b * 255), 255), 40);
        }
    }
}

// Fits a textured preview inside [a, b], keeping it square-ish; returns false when this asset has no image
static bool DrawImagePreview(ImDrawList* draw, ImVec2 a, ImVec2 b, const std::string& path) {
    if (!s_Device || !AssetManager::IsTextureFile(fs::u8path(path))) return false;
    auto texture = AssetManager::GetTexture(path, true);
    void* id = texture ? s_Device->ImGuiTexture(texture.get()) : nullptr;
    if (!id) return false;
    draw->AddImage((ImTextureID)id, a, b);
    return true;
}

static void DrawThumbnail(ImDrawList* draw, ImVec2 a, ImVec2 b, const PickerItem& item, bool small) {
    ImVec2 center((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    float size = std::min(b.x - a.x, b.y - a.y);
    if (item.isEntity) { EditorIcons::Component(draw, center, size * 0.9f, item.icon); return; }
    if (item.key.starts_with("builtin:")) { EditorIcons::Component(draw, center, size * 0.9f, "Mesh"); return; }
    std::string path = item.key.starts_with("asset:") ? item.key.substr(6) : item.key;
    if (LowerExtension(path) == ".mat") { DrawMaterialBall(draw, center, size * 0.46f, path); return; }
    if (DrawImagePreview(draw, a, b, path)) return;
    if (small) { EditorIcons::Component(draw, center, size * 0.9f, ""); return; }
    DrawIcon(draw, fs::u8path(path), a, b, false);
}

struct PickerState {
    std::string filter, hovered;
    int tab = 0;
    float tile = 76.0f;
    std::vector<PickerItem> items[2];
};
static PickerState s_Picker;

static bool DrawPickerPopup(const ObjectSlotDesc& desc, std::string& picked) {
    PickerState& state = s_Picker;
    if (!ImGui::BeginPopup("##objectPicker")) return false;
    if (ImGui::IsWindowAppearing()) {
        state.filter.clear();
        state.tab = desc.defaultTab;
        state.items[0] = desc.assets ? desc.assets() : std::vector<PickerItem>{};
        state.items[1] = desc.sceneItems ? desc.sceneItems() : std::vector<PickerItem>{};
        ImGui::SetKeyboardFocusHere();
    }
    const float width = 560.0f;
    bool result = false;

    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("##pickerSearch", "Search...", &state.filter);

    const char* tabs[] = { "Assets", "Scene" };
    for (int i = 0; i < 2; ++i) {
        if (i) ImGui::SameLine(0, 2);
        bool active = state.tab == i;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? IM_COL32(44, 46, 49, 255) : IM_COL32(26, 28, 31, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 52, 55, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255));
        if (ImGui::Button(std::format("{} ({})", tabs[i], state.items[i].size()).c_str(), { 110, 0 })) state.tab = i;
        ImGui::PopStyleColor(3);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Select %s", desc.typeName.c_str());

    // None comes first; the search filters everything else
    std::vector<const PickerItem*> visible;
    static PickerItem noneItem;
    noneItem.key.clear();
    noneItem.name = "None";
    noneItem.detail = "Clear this field";
    if (desc.allowNone) visible.push_back(&noneItem);
    ImGuiTextFilter filter(state.filter.c_str());
    for (const PickerItem& item : state.items[state.tab])
        if (filter.PassFilter(item.name.c_str()) || filter.PassFilter(item.detail.c_str())) visible.push_back(&item);

    ImGui::BeginChild("##pickerGrid", { width, 320 }, ImGuiChildFlags_Borders);
    const float gap = 6.0f, cellWidth = state.tile, cellHeight = state.tile + ImGui::GetTextLineHeight() + 6.0f;
    const int columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cellWidth + gap)));
    const int rowCount = ((int)visible.size() + columns - 1) / columns;
    std::string hovered;
    ImGuiListClipper clipper;
    clipper.Begin(rowCount, cellHeight + gap);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int col = 0; col < columns; ++col) {
                int index = row * columns + col;
                if (index >= (int)visible.size()) break;
                const PickerItem& item = *visible[index];
                if (col) ImGui::SameLine(0, gap);
                ImGui::PushID(index);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##tile", { cellWidth, cellHeight });
                bool selected = item.key == desc.currentKey, hover = ImGui::IsItemHovered();
                auto* draw = ImGui::GetWindowDrawList();
                if (selected) draw->AddRectFilled(p, { p.x + cellWidth, p.y + cellHeight }, IM_COL32(44, 93, 135, 255), 3);
                else if (hover) draw->AddRectFilled(p, { p.x + cellWidth, p.y + cellHeight }, IM_COL32(43, 45, 48, 255), 3);
                ImVec2 a(p.x + 6, p.y + 4), b(p.x + cellWidth - 6, p.y + state.tile - 4);
                if (item.key.empty()) {
                    ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                    draw->AddCircle(c, (b.x - a.x) * 0.3f, IM_COL32(130, 130, 130, 255), 0, 2);
                    draw->AddLine({ c.x - (b.x - a.x) * 0.21f, c.y + (b.x - a.x) * 0.21f }, { c.x + (b.x - a.x) * 0.21f, c.y - (b.x - a.x) * 0.21f }, IM_COL32(130, 130, 130, 255), 2);
                } else DrawThumbnail(draw, a, b, item, false);
                const char* begin = item.name.c_str(), *end = begin + item.name.size();
                float textWidth = ImGui::CalcTextSize(begin, end).x, textX = p.x + std::max(3.0f, (cellWidth - textWidth) * 0.5f);
                float textY = p.y + state.tile;
                ImGui::RenderTextEllipsis(draw, { textX, textY }, { p.x + cellWidth - 3, textY + ImGui::GetTextLineHeight() }, p.x + cellWidth - 3, p.x + cellWidth - 3, begin, end, nullptr);
                if (hover) hovered = item.detail.empty() ? item.name : item.detail;
                if (ImGui::IsItemClicked()) { picked = item.key; result = true; }
                if (hover && ImGui::IsMouseDoubleClicked(0)) ImGui::CloseCurrentPopup();
                ImGui::PopID();
            }
        }
    }
    clipper.End();
    if (visible.size() <= (desc.allowNone ? 1u : 0u)) {
        const char* message = state.tab == 0 && !desc.assetsHint.empty() ? desc.assetsHint.c_str() : "Nothing to show here.";
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 24);
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(message).x) * 0.5f);
        ImGui::TextDisabled("%s", message);
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%s", hovered.empty() ? (desc.currentKey.empty() ? "None" : desc.currentKey.c_str()) : hovered.c_str());
    ImGui::SameLine(width - 112);
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("##pickerZoom", &state.tile, 48.0f, 150.0f, "");
    ImGui::EndPopup();
    return result;
}

bool ObjectSlot(const ObjectSlotDesc& desc, std::string& picked, bool* doubleClicked) {
    const float width = std::max(48.0f, ImGui::CalcItemWidth()), height = ImGui::GetFrameHeight();
    const float targetWidth = height;
    ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + width, p.y + height);
    ImGui::InvisibleButton("##objectField", { width, height });
    bool hovered = ImGui::IsItemHovered(), overTarget = hovered && ImGui::GetMousePos().x >= q.x - targetWidth;
    auto* draw = ImGui::GetWindowDrawList();
    float rounding = ImGui::GetStyle().FrameRounding;
    draw->AddRectFilled(p, q, hovered ? IM_COL32(27, 29, 32, 255) : IM_COL32(23, 25, 28, 255), rounding);
    draw->AddRect(p, q, ImGui::IsItemActive() ? IM_COL32(47, 128, 237, 255) : IM_COL32(14, 16, 19, 255), rounding);

    float textX = p.x + 5;
    if (!desc.iconItem.key.empty() || desc.iconItem.isEntity) {
        const float icon = height - 6;
        DrawThumbnail(draw, { p.x + 4, p.y + 3 }, { p.x + 4 + icon, p.y + 3 + icon }, desc.iconItem, true);
        textX = p.x + 4 + icon + 5;
    }
    const char* begin = desc.text.c_str(), *end = begin + desc.text.size();
    float textMax = q.x - targetWidth - 2;
    ImGui::RenderTextEllipsis(draw, { textX, p.y + ImGui::GetStyle().FramePadding.y }, { textMax, q.y }, textMax, textMax, begin, end, nullptr);

    // Target button: ring with a dot, the picker's entry point
    ImVec2 c(q.x - targetWidth * 0.5f, p.y + height * 0.5f);
    ImU32 targetColor = overTarget ? IM_COL32(235, 235, 235, 255) : IM_COL32(165, 165, 165, 255);
    if (overTarget) draw->AddRectFilled({ q.x - targetWidth, p.y + 1 }, { q.x - 1, q.y - 1 }, IM_COL32(41, 43, 46, 255), rounding);
    draw->AddCircle(c, height * 0.25f, targetColor, 0, 1.3f);
    draw->AddCircleFilled(c, height * 0.1f, targetColor);

    if (ImGui::IsItemClicked() && overTarget) ImGui::OpenPopup("##objectPicker");
    if (doubleClicked) *doubleClicked = hovered && !overTarget && ImGui::IsMouseDoubleClicked(0);

    bool changed = false;
    if (desc.allowNone && ImGui::BeginPopupContextItem("##objectMenu")) {
        if (ImGui::MenuItem("Clear", nullptr, false, !desc.currentKey.empty())) { picked.clear(); changed = true; }
        ImGui::EndPopup();
    }
    changed |= DrawPickerPopup(desc, picked);
    return changed;
}

std::vector<PickerItem> AssetsUsedInScene(const std::vector<std::string>& extensions, bool includeBuiltins) {
    std::vector<PickerItem> items;
    if (!s_Scene) return items;
    std::set<std::string> found;
    std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& node) {
        if (node.is_string()) {
            const auto& text = node.get_ref<const std::string&>();
            if (text.starts_with("builtin:") ? includeBuiltins
                : std::find(extensions.begin(), extensions.end(), LowerExtension(text)) != extensions.end()) found.insert(text);
        } else if (node.is_structured()) {
            for (const auto& child : node) walk(child);
        }
    };
    walk(nlohmann::json::parse(SceneSerializer::ToJson(*s_Scene), nullptr, false));
    for (const std::string& path : found) {
        PickerItem item;
        item.key = path;
        item.name = path.starts_with("builtin:") ? path.substr(8) : FileSystem::Utf8(fs::u8path(path).stem());
        item.detail = path;
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace Stardust::AssetUI
