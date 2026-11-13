#include "Renderer/Quality.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Stardust {

void GraphicsQuality::Clamp() {
    renderScale = std::clamp(renderScale, 0.25f, 2.0f);
    u32 size = 256;
    while (size < shadowMapSize && size < 8192) size *= 2; // power of two, 256..8192
    shadowMapSize = size;
    shadowDistance = std::clamp(shadowDistance, 0.1f, 4.0f);
    cloudDownscale = std::clamp(cloudDownscale, 1u, 8u);
    grassDensity = std::clamp(grassDensity, 0.0f, 4.0f);
    grassDistance = std::clamp(grassDistance, 0.0f, 4.0f);
    lodBias = std::clamp(lodBias, 0.1f, 8.0f);
}

const char* GraphicsQuality::LevelName(int level) {
    static const char* names[] = { "Low", "Medium", "High", "Custom" };
    return names[std::clamp(level, 0, 3)];
}

void GraphicsQuality::ToJson(nlohmann::json& j) const {
    j = { { "renderScale", renderScale }, { "shadows", shadows }, { "shadowMapSize", shadowMapSize }, { "shadowDistance", shadowDistance },
          { "cloudDownscale", cloudDownscale }, { "grassDensity", grassDensity }, { "grassDistance", grassDistance }, { "lodBias", lodBias },
          { "bloom", bloom }, { "fxaa", fxaa } };
}

void GraphicsQuality::FromJson(const nlohmann::json& j) {
    if (!j.is_object()) return;
    auto number = [&](const char* key, auto& out) { auto it = j.find(key); if (it != j.end() && it->is_number()) out = it->get<std::decay_t<decltype(out)>>(); };
    auto flag = [&](const char* key, bool& out) { auto it = j.find(key); if (it != j.end() && it->is_boolean()) out = it->get<bool>(); };
    number("renderScale", renderScale); flag("shadows", shadows); number("shadowMapSize", shadowMapSize); number("shadowDistance", shadowDistance);
    number("cloudDownscale", cloudDownscale); number("grassDensity", grassDensity); number("grassDistance", grassDistance); number("lodBias", lodBias);
    flag("bloom", bloom); flag("fxaa", fxaa);
    Clamp();
}

} // namespace Stardust
