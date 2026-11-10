#pragma once
#include "Core/Base.h"

namespace Stardust {

// Global graphics quality, chosen per project. Lower levels trade resolution and density for speed on older GPUs.
struct GraphicsQuality {
    int level = 2;               // 0 Low, 1 Medium, 2 High
    float renderScale = 1.0f;    // game view / player 3D resolution relative to the window
    u32 shadowMapSize = 2048;
    u32 cloudDownscale = 2;      // clouds render at 1/N of the resolution
    float grassDensity = 1.0f;   // multiplies each terrain's grass density
    float grassDistance = 1.0f;  // multiplies each terrain's grass distance
    float lodBias = 1.0f;        // below 1 LOD Groups switch to simpler levels sooner

    static GraphicsQuality& Current() { static GraphicsQuality quality; return quality; }
    static void Apply(int level) {
        GraphicsQuality q;
        q.level = level < 0 ? 0 : level > 2 ? 2 : level;
        if (q.level == 0) { q.renderScale = 0.67f; q.shadowMapSize = 1024; q.cloudDownscale = 4; q.grassDensity = 0.4f; q.grassDistance = 0.6f; q.lodBias = 0.6f; }
        if (q.level == 1) { q.renderScale = 0.85f; q.shadowMapSize = 2048; q.cloudDownscale = 3; q.grassDensity = 0.7f; q.grassDistance = 0.8f; q.lodBias = 0.8f; }
        Current() = q;
    }
};

} // namespace Stardust
