#pragma once
#include "Scene/Scene.h"

namespace Stardust {

struct AnimatedProperty {
    std::string id;     // "Canvas Group.Alpha", "Script:Player.speed"
    std::string group;  // component shown in the Add Property list
    std::string label;
    int dimension = 1;  // 1..4 values
    char kind = 'f';    // f number/vector, i integer, b boolean, c color

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
};

} // namespace Stardust
