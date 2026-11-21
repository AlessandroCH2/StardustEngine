#pragma once
#include "Scene/Components.h"

namespace Stardust { class Scene; }
namespace Stardust::GameplayInspector {
void Draw(Scene& scene, EntityID e, CharacterControllerComponent& c, bool playing);
void Draw(Scene& scene, EntityID e, JointComponent& c, bool playing);
void Draw(Scene& scene, EntityID e, NavMeshSurfaceComponent& c, bool playing);
void Draw(Scene& scene, EntityID e, NavMeshAgentComponent& c, bool playing);
void Draw(Scene& scene, EntityID e, LineRendererComponent& c, bool playing);
void Draw(Scene& scene, EntityID e, SplineComponent& c, bool playing);
void Draw(Scene& scene, EntityID e, ReflectionProbeComponent& c, bool playing);
void Draw(Scene& scene, EntityID e, DecalComponent& c, bool playing);
}
