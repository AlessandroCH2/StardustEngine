#include "UI/UIControls.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Stardust::UIControls {

using json = nlohmann::json;

namespace {
json Vec(glm::vec4 v) { return { v.x, v.y, v.z, v.w }; }
template<typename T> void Read(const json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    if constexpr (std::is_same_v<T, bool>) { if (it->is_boolean()) out = it->get<bool>(); }
    else if constexpr (std::is_same_v<T, std::string>) { if (it->is_string()) out = it->get<std::string>(); }
    else if constexpr (std::is_same_v<T, glm::vec4>) { if (it->is_array() && it->size() == 4) for (int i = 0; i < 4; ++i) if ((*it)[i].is_number()) out[i] = (*it)[i].get<float>(); }
    else { if (it->is_number()) out = it->get<T>(); }
}
}

void ToJson(const UIScrollViewComponent& c, json& j) {
    j = { { "content", std::to_string(c.content) }, { "horizontal", c.horizontal }, { "vertical", c.vertical }, { "movementType", c.movementType },
          { "elasticity", c.elasticity }, { "inertia", c.inertia }, { "decelerationRate", c.decelerationRate }, { "scrollSensitivity", c.scrollSensitivity },
          { "dragToScroll", c.dragToScroll }, { "clipContent", c.clipContent }, { "scrollbars", c.scrollbars }, { "scrollbarWidth", c.scrollbarWidth },
          { "scrollbarPadding", c.scrollbarPadding }, { "minHandleSize", c.minHandleSize }, { "trackColor", Vec(c.trackColor) }, { "handleColor", Vec(c.handleColor) },
          { "handleHoverColor", Vec(c.handleHoverColor) }, { "trackSprite", c.trackSprite }, { "handleSprite", c.handleSprite } };
}
void FromJson(const json& j, UIScrollViewComponent& c) {
    if (!j.is_object()) return;
    if (auto it = j.find("content"); it != j.end()) { if (it->is_string()) c.content = std::strtoull(it->get<std::string>().c_str(), nullptr, 10); else if (it->is_number_unsigned()) c.content = it->get<u64>(); }
    Read(j, "horizontal", c.horizontal); Read(j, "vertical", c.vertical); Read(j, "movementType", c.movementType); Read(j, "elasticity", c.elasticity);
    Read(j, "inertia", c.inertia); Read(j, "decelerationRate", c.decelerationRate); Read(j, "scrollSensitivity", c.scrollSensitivity);
    Read(j, "dragToScroll", c.dragToScroll); Read(j, "clipContent", c.clipContent); Read(j, "scrollbars", c.scrollbars); Read(j, "scrollbarWidth", c.scrollbarWidth);
    Read(j, "scrollbarPadding", c.scrollbarPadding); Read(j, "minHandleSize", c.minHandleSize); Read(j, "trackColor", c.trackColor); Read(j, "handleColor", c.handleColor);
    Read(j, "handleHoverColor", c.handleHoverColor); Read(j, "trackSprite", c.trackSprite); Read(j, "handleSprite", c.handleSprite);
    c.movementType = std::clamp(c.movementType, 0, 2); c.scrollbars = std::clamp(c.scrollbars, 0, 2);
}

void ToJson(const UIDropdownComponent& c, json& j) {
    json options = json::array();
    for (const auto& o : c.options) options.push_back({ { "text", o.text }, { "sprite", o.sprite } });
    j = { { "options", options }, { "value", c.value }, { "interactable", c.interactable }, { "font", c.font }, { "fontSize", c.fontSize },
          { "textColor", Vec(c.textColor) }, { "textPadding", c.textPadding }, { "placeholder", c.placeholder }, { "hoverTint", Vec(c.hoverTint) },
          { "pressedTint", Vec(c.pressedTint) }, { "disabledTint", Vec(c.disabledTint) }, { "arrowColor", Vec(c.arrowColor) }, { "arrowSprite", c.arrowSprite },
          { "arrowSize", c.arrowSize }, { "direction", c.direction }, { "itemHeight", c.itemHeight }, { "maxListHeight", c.maxListHeight },
          { "listOffset", c.listOffset }, { "listPadding", c.listPadding }, { "listColor", Vec(c.listColor) }, { "listSprite", c.listSprite },
          { "itemColor", Vec(c.itemColor) }, { "itemHoverColor", Vec(c.itemHoverColor) }, { "itemSelectedColor", Vec(c.itemSelectedColor) },
          { "itemSprite", c.itemSprite }, { "itemTextColor", Vec(c.itemTextColor) }, { "showCheckmark", c.showCheckmark }, { "checkmarkColor", Vec(c.checkmarkColor) } };
}
void FromJson(const json& j, UIDropdownComponent& c) {
    if (!j.is_object()) return;
    if (auto it = j.find("options"); it != j.end() && it->is_array()) {
        c.options.clear();
        for (const auto& o : *it) {
            if (o.is_string()) c.options.push_back({ o.get<std::string>(), "" });
            else if (o.is_object()) c.options.push_back({ o.value("text", std::string()), o.value("sprite", std::string()) });
        }
    }
    Read(j, "value", c.value); Read(j, "interactable", c.interactable); Read(j, "font", c.font); Read(j, "fontSize", c.fontSize);
    Read(j, "textColor", c.textColor); Read(j, "textPadding", c.textPadding); Read(j, "placeholder", c.placeholder); Read(j, "hoverTint", c.hoverTint);
    Read(j, "pressedTint", c.pressedTint); Read(j, "disabledTint", c.disabledTint); Read(j, "arrowColor", c.arrowColor); Read(j, "arrowSprite", c.arrowSprite);
    Read(j, "arrowSize", c.arrowSize); Read(j, "direction", c.direction); Read(j, "itemHeight", c.itemHeight); Read(j, "maxListHeight", c.maxListHeight);
    Read(j, "listOffset", c.listOffset); Read(j, "listPadding", c.listPadding); Read(j, "listColor", c.listColor); Read(j, "listSprite", c.listSprite);
    Read(j, "itemColor", c.itemColor); Read(j, "itemHoverColor", c.itemHoverColor); Read(j, "itemSelectedColor", c.itemSelectedColor);
    Read(j, "itemSprite", c.itemSprite); Read(j, "itemTextColor", c.itemTextColor); Read(j, "showCheckmark", c.showCheckmark); Read(j, "checkmarkColor", c.checkmarkColor);
    c.itemHeight = std::max(4.0f, c.itemHeight); c.direction = std::clamp(c.direction, 0, 2);
    c.value = c.options.empty() ? -1 : std::clamp(c.value, -1, (int)c.options.size() - 1);
}

} // namespace Stardust::UIControls
