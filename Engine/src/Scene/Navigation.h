#pragma once
// Navigation meshes: baked by Nav Mesh Surface components (Recast), used by Nav Mesh Agents (DetourCrowd)
// and by path queries from scripts (Detour). One navigation mesh per scene: the first enabled surface.
#include "Core/Base.h"
#include "Scene/Registry.h"
#include "Scene/GameplayComponents.h"
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace Stardust { class Scene; }

namespace Stardust::Navigation {
// Editor: builds the nav mesh of `surface` and writes it next to the scene (<Assets>/NavMesh/...). Sets c.data.
bool Bake(Scene& scene, EntityID surface, std::string* error = nullptr);
// Play mode: builds it in memory only (procedural levels)
bool BakeRuntime(Scene& scene, EntityID surface, std::string* error = nullptr);
void Unload(Scene& scene);
void Release(Scene& scene);          // scene destroyed
bool HasNavMesh(Scene& scene);
std::string Info(Scene& scene);
bool& DebugDraw();
// Triangles of the loaded nav mesh (world space, 3 points each), for the editor overlay
const std::vector<glm::vec3>& DebugTriangles(Scene& scene);

// Per frame in play mode: loads the baked data if needed and moves the agents
void Update(Scene& scene, float dt);
void OnRuntimeStop(Scene& scene);
void RemoveAgent(Scene& scene, EntityID e);
void AgentState(Scene& scene, EntityID e, const NavMeshAgentComponent& c, nlohmann::json& out);
void AgentCommand(Scene& scene, EntityID e, NavMeshAgentComponent& c, const nlohmann::json& in);

// Queries (C# NavMesh class). status: 0 complete, 1 partial, 2 invalid
bool CalculatePath(Scene& scene, const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& corners, int& status);
bool SamplePosition(Scene& scene, const glm::vec3& position, float maxDistance, glm::vec3& result);
// true when the straight line is blocked; hit = where it leaves the nav mesh
bool Raycast(Scene& scene, const glm::vec3& from, const glm::vec3& to, glm::vec3& hit);
}
