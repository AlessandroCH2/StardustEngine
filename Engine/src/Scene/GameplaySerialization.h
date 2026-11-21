#pragma once
// JSON for the components in GameplayComponents.h: scene files and the C# component properties use the same keys.
#include "Scene/GameplayComponents.h"
#include <nlohmann/json_fwd.hpp>

namespace Stardust::GameplayJson {
void ToJson(const CharacterControllerComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, CharacterControllerComponent& c);
void ToJson(const JointComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, JointComponent& c);
void ToJson(const NavMeshSurfaceComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, NavMeshSurfaceComponent& c);
void ToJson(const NavMeshAgentComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, NavMeshAgentComponent& c);
void ToJson(const LineRendererComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, LineRendererComponent& c);
void ToJson(const SplineComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, SplineComponent& c);
void ToJson(const ReflectionProbeComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, ReflectionProbeComponent& c);
void ToJson(const DecalComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, DecalComponent& c);
void ToJson(const IKConstraintComponent& c, nlohmann::json& j);
void FromJson(const nlohmann::json& j, IKConstraintComponent& c);
}
