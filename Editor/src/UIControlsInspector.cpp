// Inspectors of the Scroll View and Dropdown UI components, and the GameObject > UI items that build them ready to use
#include "InspectorWidgets.h"
#include "AssetUI.h"
#include "Scene/Scene.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <format>

namespace Stardust {

namespace {
const std::vector<std::string> kSprites = { ".sprite", ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
bool Color4(const char* label, glm::vec4& c) { return Insp::ColorEdit4(label, &c.x); }
void Header(const char* text) { ImGui::Dummy({ 0, 4 }); ImGui::TextDisabled("%s", text); }
}

void DrawScrollViewInspector(Scene& scene, EntityID e, UIScrollViewComponent& c) {
    // Content: a child (or descendant) of this object
    std::string current = "(first child)";
    if (c.content) if (EntityID content = scene.FindByUUID(c.content)) current = scene.Reg().Get<NameComponent>(content).name;
    if (Insp::BeginCombo("Content", current.c_str())) {
        if (ImGui::Selectable("(first child)", c.content == 0)) c.content = 0;
        std::function<void(EntityID, int)> list = [&](EntityID parent, int depth) {
            for (EntityID child : scene.GetChildren(parent)) {
                if (!scene.Reg().Has<RectTransformComponent>(child)) continue;
                const u64 id = scene.Reg().Get<IDComponent>(child).uuid;
                if (ImGui::Selectable(std::format("{}{}##{}", std::string(depth * 2, ' '), scene.Reg().Get<NameComponent>(child).name, id).c_str(), c.content == id)) c.content = id;
                if (depth < 3) list(child, depth + 1);
            }
        };
        list(e, 0);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The object moved by scrolling. Its RectTransform size is the scrollable area\n(a Layout Group with Fit Content makes it follow its children).");
    Insp::Checkbox("Horizontal", &c.horizontal);
    Insp::Checkbox("Vertical", &c.vertical);
    Insp::Combo("Movement Type", &c.movementType, "Unrestricted\0Elastic\0Clamped\0");
    if (c.movementType == 1) Insp::DragFloat("Elasticity", &c.elasticity, 0.005f, 0.01f, 2.0f, "%.2f s");
    Insp::Checkbox("Inertia", &c.inertia);
    if (c.inertia) Insp::SliderFloat("Deceleration Rate", &c.decelerationRate, 0, 0.99f, "%.3f");
    Insp::DragFloat("Scroll Sensitivity", &c.scrollSensitivity, 0.5f, 0, 1000);
    Insp::Checkbox("Drag To Scroll", &c.dragToScroll);
    Insp::Checkbox("Clip Content", &c.clipContent);
    Header("Scroll Bars");
    Insp::Combo("Visibility", &c.scrollbars, "Hidden\0When Needed\0Always\0");
    if (c.scrollbars) {
        Insp::DragFloat("Width", &c.scrollbarWidth, 0.2f, 2, 64);
        Insp::DragFloat("Padding", &c.scrollbarPadding, 0.2f, 0, 64);
        Insp::DragFloat("Min Handle Size", &c.minHandleSize, 0.5f, 4, 512);
        Color4("Track Color", c.trackColor);
        AssetUI::AssetField("Track Sprite", c.trackSprite, kSprites);
        Color4("Handle Color", c.handleColor);
        Color4("Handle Hover", c.handleHoverColor);
        AssetUI::AssetField("Handle Sprite", c.handleSprite, kSprites);
    }
}

void DrawDropdownInspector(UIDropdownComponent& c) {
    Insp::Checkbox("Interactable", &c.interactable);
    Header("Options");
    int remove = -1, moveUp = -1;
    for (int i = 0; i < (int)c.options.size(); ++i) {
        ImGui::PushID(i);
        auto& o = c.options[i];
        if (ImGui::RadioButton("##value", c.value == i)) c.value = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Selected option");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 120));
        ImGui::InputText("##text", &o.text);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(56);
        const std::string spriteLabel = o.sprite.empty() ? "Icon" : "Icon*";
        if (ImGui::Button(spriteLabel.c_str())) ImGui::OpenPopup("icon");
        if (ImGui::BeginPopup("icon")) { ImGui::SetNextItemWidth(260); AssetUI::AssetField("Icon", o.sprite, kSprites); ImGui::EndPopup(); }
        ImGui::SameLine(); if (ImGui::SmallButton("^") && i > 0) moveUp = i;
        ImGui::SameLine(); if (ImGui::SmallButton("x")) remove = i;
        ImGui::PopID();
    }
    if (moveUp > 0) { std::swap(c.options[moveUp], c.options[moveUp - 1]); if (c.value == moveUp) --c.value; else if (c.value == moveUp - 1) ++c.value; }
    if (remove >= 0) { c.options.erase(c.options.begin() + remove); if (c.value >= (int)c.options.size()) c.value = (int)c.options.size() - 1; }
    if (ImGui::Button("Add Option")) c.options.push_back({ std::format("Option {}", c.options.size() + 1), "" });
    Insp::InputText("Placeholder", &c.placeholder);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shown when no option is selected (value -1)");

    Header("Caption");
    AssetUI::AssetField("Font", c.font, { ".ttf", ".otf" }, "Default");
    Insp::DragFloat("Font Size", &c.fontSize, 0.2f, 4, 200);
    Color4("Text Color", c.textColor);
    Insp::DragFloat("Text Padding", &c.textPadding, 0.2f, 0, 100);
    Color4("Hover Tint", c.hoverTint);
    Color4("Pressed Tint", c.pressedTint);
    Color4("Disabled Tint", c.disabledTint);
    Color4("Arrow Color", c.arrowColor);
    AssetUI::AssetField("Arrow Sprite", c.arrowSprite, kSprites, "Triangle");
    Insp::DragFloat("Arrow Size", &c.arrowSize, 0.2f, 2, 100);

    Header("List");
    Insp::Combo("Direction", &c.direction, "Auto\0Down\0Up\0");
    Insp::DragFloat("Item Height", &c.itemHeight, 0.2f, 8, 300);
    Insp::DragFloat("Max Height", &c.maxListHeight, 1, 20, 4000);
    Insp::DragFloat("Offset", &c.listOffset, 0.2f, -100, 100);
    Insp::DragFloat("Padding", &c.listPadding, 0.2f, 0, 100);
    Color4("List Color", c.listColor);
    AssetUI::AssetField("List Sprite", c.listSprite, kSprites, "Plain");
    Color4("Item Color", c.itemColor);
    Color4("Item Hover", c.itemHoverColor);
    Color4("Item Selected", c.itemSelectedColor);
    AssetUI::AssetField("Item Sprite", c.itemSprite, kSprites, "Plain");
    Color4("Item Text", c.itemTextColor);
    Insp::Checkbox("Checkmark", &c.showCheckmark);
    if (c.showCheckmark) Color4("Checkmark Color", c.checkmarkColor);
}

// GameObject > UI > Scroll View: a panel with a Content child that grows with a vertical layout, and a few items
EntityID CreateScrollView(Scene& scene, EntityID parent) {
    EntityID view = scene.CreateEntity("Scroll View");
    scene.SetParent(view, parent, false);
    auto& rt = scene.Reg().Add<RectTransformComponent>(view); rt.sizeDelta = { 360, 300 };
    scene.Reg().Add<UIImageComponent>(view).color = { 0.13f, 0.14f, 0.16f, 0.92f };
    scene.Reg().Add<UIScrollViewComponent>(view);
    EntityID content = scene.CreateEntity("Content");
    scene.SetParent(content, view, false);
    auto& ct = scene.Reg().Add<RectTransformComponent>(content);
    ct.anchorMin = { 0, 1 }; ct.anchorMax = { 1, 1 }; ct.pivot = { 0.5f, 1 }; ct.sizeDelta = { 0, 442 }; // the 8 items with spacing and padding
    auto& layout = scene.Reg().Add<UILayoutGroupComponent>(content); layout.mode = 1; layout.fitContent = true; layout.spacing = { 6, 6 };
    for (int i = 1; i <= 8; ++i) {
        EntityID item = scene.CreateEntity(std::format("Item {}", i));
        scene.SetParent(item, content, false);
        scene.Reg().Add<RectTransformComponent>(item).sizeDelta = { 340, 48 };
        scene.Reg().Add<UIImageComponent>(item).color = { 0.22f, 0.24f, 0.28f, 1 };
        auto& text = scene.Reg().Add<UITextComponent>(item); text.text = std::format("Item {}", i); text.fontSize = 22;
    }
    return view;
}

// GameObject > UI > Dropdown
EntityID CreateDropdown(Scene& scene, EntityID parent) {
    EntityID e = scene.CreateEntity("Dropdown");
    scene.SetParent(e, parent, false);
    scene.Reg().Add<RectTransformComponent>(e).sizeDelta = { 240, 44 };
    scene.Reg().Add<UIImageComponent>(e).color = { 0.96f, 0.96f, 0.97f, 1 };
    scene.Reg().Add<UIDropdownComponent>(e);
    return e;
}

} // namespace Stardust
