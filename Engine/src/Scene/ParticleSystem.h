#pragma once
#include "Scene/Components.h"
#include "RHI/RHI.h"
#include <nlohmann/json_fwd.hpp>

namespace Stardust {

class Scene;
struct Vertex;
struct RenderCamera;

// Simulation and drawing of Particle System components (see ParticleSystemComponent in Components.h).
// Particles are simulated on the CPU and drawn as camera-facing quads, one draw per system.
namespace ParticleSystems {
    // Advances every playing system of the scene (Play mode)
    void Update(Scene& scene, float dt);
    // Advances one system; the editor uses it to preview the selected system outside Play mode
    void Simulate(Scene& scene, EntityID e, ParticleSystemComponent& c, float dt);

    void Play(ParticleSystemComponent& c);
    void Pause(ParticleSystemComponent& c);
    // Stops emitting; with clear, the living particles go too
    void Stop(ParticleSystemComponent& c, bool clear = false);
    void Clear(ParticleSystemComponent& c);
    // Emits particles now, playing or not
    void Emit(Scene& scene, EntityID e, ParticleSystemComponent& c, int count);
    // Back to the state of a system that never ran (the next update starts it again if Play On Awake)
    void ResetRuntime(ParticleSystemComponent& c);

    // Quads of the living particles for this camera, world space; returns the number of quads written.
    // Vertex tangents carry the particle colours (Particle.vs/ps.hlsl).
    u32 BuildVertices(Scene& scene, EntityID e, const ParticleSystemComponent& c, const RenderCamera& camera,
                      std::vector<Vertex>& out, glm::vec3& boundsMin, glm::vec3& boundsMax);

    // Soft round sprite used when a system has no texture
    Ref<RHI::Texture> DefaultTexture(RHI::Device& device);

    // Settings (not the living particles) to and from JSON: scenes, prefabs and scripts.
    // FromJson only changes the keys present, so scripts can set a single value.
    void ToJson(const ParticleSystemComponent& c, nlohmann::json& j);
    void FromJson(const nlohmann::json& j, ParticleSystemComponent& c);
}

} // namespace Stardust
