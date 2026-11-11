#pragma once
#include "Scene/Scene.h"
#include <nlohmann/json_fwd.hpp>

namespace Stardust {

struct AnimatedProperty {
    std::string id;     // "Canvas Group.Alpha", "Script:Player.speed"
    std::string group;  // component shown in the Add Property list
    std::string label;
    int dimension = 1;  // 1..4 values
    char kind = 'f';    // f number/vector, i integer, b boolean, c color, r asset reference (sprite...)

    // Name of the clip channel that stores this property's keys
    std::string ChannelKey() const;
};

// Every animatable value of every component, by name, so a clip can drive any of them.
class AnimatedProperties {
public:
    static std::vector<AnimatedProperty> List(Scene& scene, EntityID entity);
    static bool Get(Scene& scene, EntityID entity, const std::string& id, glm::vec4& value);
    static bool Set(Scene& scene, EntityID entity, const std::string& id, const glm::vec4& value);

    // Decodes "prop:1f:Canvas Group.Alpha"; false for the transform channels
    static bool ParseChannel(const std::string& key, int& dimension, char& kind, std::string& id);

    // Asset references ('r' properties) are keyed by a number: a 24-bit hash of the asset path, exact in a float.
    // A track lists the paths it uses under "references" ({ "<number>": "path" }); they are registered before sampling.
    static float ReferenceKey(const std::string& path);
    static std::string ReferencePath(float key);
    static void RegisterReferences(const nlohmann::json& track);
    // Adds the path of a reference key to a track's "references" (call it when writing an 'r' key)
    static void StoreReference(nlohmann::json& track, float key);
};

} // namespace Stardust
