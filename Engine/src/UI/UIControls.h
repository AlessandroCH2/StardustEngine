#pragma once
#include "Scene/Components.h"
#include <nlohmann/json_fwd.hpp>

namespace Stardust::UIControls {
// Scroll View and Dropdown settings to and from JSON (scenes, prefabs, scripts). FromJson changes only the keys present.
void ToJson(const UIScrollViewComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, UIScrollViewComponent& c);
void ToJson(const UIDropdownComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, UIDropdownComponent& c);
}
