#pragma once
#include "Scene/Components.h"
#include "RHI/RHI.h"
#include <nlohmann/json_fwd.hpp>

namespace Stardust {

class Scene;
struct Vertex;
struct RenderCamera;

// Recording and drawing of Trail Renderer components (see TrailRendererComponent in Components.h).
// Points are recorded on the CPU once per frame (Play mode, and edit mode in the editor) and drawn as a ribbon of quads, one draw
// per trail, through the particle pipelines (vertex tangents carry the colours).
namespace TrailRenderers {
    // Ages the points of every trail and records the new positions (after scripts and physics)
    void Update(Scene& scene, float dt);
    // Removes every point (the trail starts again from the object)
    void Clear(TrailRendererComponent& c);

    // Quads of the ribbon for this camera, world space; returns the number of quads written
    u32 BuildVertices(const TrailRendererComponent& c, const RenderCamera& camera,
                      std::vector<Vertex>& out, glm::vec3& boundsMin, glm::vec3& boundsMax);

    // White, fading out towards both edges of the ribbon: used when a trail has no texture
    Ref<RHI::Texture> DefaultTexture(RHI::Device& device);

    // Settings (not the points) to and from JSON; FromJson only changes the keys present
    void ToJson(const TrailRendererComponent& c, nlohmann::json& j);
    void FromJson(const nlohmann::json& j, TrailRendererComponent& c);
}

} // namespace Stardust
