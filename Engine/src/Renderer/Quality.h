#pragma once
#include "Core/Base.h"
#include <nlohmann/json_fwd.hpp>

namespace Stardust {

// Global graphics quality, chosen per project (Project Settings) and changeable by games at run time (C# QualitySettings).
// Low, Medium and High are presets; Custom keeps every value as set.
struct GraphicsQuality {
    enum Level : int { Low = 0, Medium = 1, High = 2, Custom = 3 };
    int level = High;
    float renderScale = 1.0f;    // game view / player 3D resolution relative to the window (UI stays at full resolution)
    bool shadows = true;
    u32 shadowMapSize = 2048;
    float shadowDistance = 1.0f; // multiplies the Directional Light's shadow distance
    u32 cloudDownscale = 2;      // clouds render at 1/N of the resolution
    float grassDensity = 1.0f;   // multiplies each terrain's grass density
    float grassDistance = 1.0f;  // multiplies each terrain's grass distance
    float lodBias = 2.0f;        // multiplies the screen share LOD Groups see: 2 on High
    bool bloom = true;
    bool fxaa = true;

    static GraphicsQuality& Current() { static GraphicsQuality quality; return quality; }
    static GraphicsQuality Preset(int level) {
        GraphicsQuality q;
        q.level = level < 0 ? 0 : level > 2 ? 2 : level;
        if (q.level == Low) { q.renderScale = 0.67f; q.shadowMapSize = 1024; q.shadowDistance = 0.6f; q.cloudDownscale = 4; q.grassDensity = 0.4f; q.grassDistance = 0.6f; q.lodBias = 1.4f; q.bloom = false; }
        if (q.level == Medium) { q.renderScale = 0.85f; q.shadowMapSize = 2048; q.shadowDistance = 0.8f; q.cloudDownscale = 3; q.grassDensity = 0.7f; q.grassDistance = 0.8f; q.lodBias = 0.8f; }
        return q;
    }
    // A preset level, or Custom with the given values
    static void Apply(int level, const GraphicsQuality* custom = nullptr) {
        if (level == Custom && custom) { Current() = *custom; Current().level = Custom; Current().Clamp(); }
        else if (level == Custom) Current().level = Custom;
        else Current() = Preset(level);
    }
    void Clamp();
    static const char* LevelName(int level);

    // Values only (no level); FromJson changes only the keys present
    void ToJson(nlohmann::json& j) const;
    void FromJson(const nlohmann::json& j);
};

} // namespace Stardust
