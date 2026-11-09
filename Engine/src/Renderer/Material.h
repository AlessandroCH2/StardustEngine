#pragma once
#include "Core/Base.h"
#include "RHI/RHI.h"
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

namespace Stardust {

// Shaders a material can use (Unity-style shader dropdown). Each maps to a pixel shader + pipeline state.
enum class ShaderType : u8 { Standard, Unlit, Toon, Transparent, UI, Water, Count };
const char* ShaderName(ShaderType t);          // e.g. "Stardust/Standard"
ShaderType ShaderFromName(const std::string& name);

struct Material {
    std::string name = "Material";
    ShaderType shader = ShaderType::Standard;
    std::string propertiesJson="{}";
    const nlohmann::json& PropertyLayout() const;
    std::vector<glm::vec4> PropertyValues() const;
    std::string customPixelShader; // compiled asset-relative .spv, using MeshCommon.hlsli interface
    Ref<RHI::Texture> runtimeAlbedo; // dynamic UI/font atlases; never serialized
    Ref<RHI::Texture> runtimeMask;   // UI Mask image, read through the normal slot

    glm::vec4 color{ 1.0f };          // albedo tint (alpha used by Transparent)
    std::string albedoMap;            // asset-relative paths, empty = none
    std::string normalMap;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float normalStrength = 1.0f;
    float alphaCutoff = 0.0f;         // > 0 enables alpha testing (foliage, fences)
    glm::vec3 emissive{ 0.0f };
    float emissiveIntensity = 1.0f;
    glm::vec2 tiling{ 1.0f };
    glm::vec2 offset{ 0.0f };
    bool doubleSided = false;
    // Toon
    float toonSteps = 3.0f;
    float rimStrength = 0.35f;

    std::string assetPath;            // set when loaded from / saved to a .mat file
    bool dirty = false;

    void ToJson(nlohmann::json& j) const;
    void FromJson(const nlohmann::json& j);
};

} // namespace Stardust
