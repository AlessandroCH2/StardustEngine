#pragma once
// Particle Systems with "Simulate on GPU": the emitter spawns on the CPU (ParticleSystem.cpp), the new
// particles are copied into a GPU ring buffer, a compute shader moves them (GpuParticleSim.cs) and the
// vertex shader turns each into a quad (GpuParticle.vs). Buffer: 512-byte header, then 64 bytes per particle.
#include "Scene/Components.h"
#include "RHI/RHI.h"

namespace Stardust {
class Scene;
class SceneRenderer;

namespace GpuParticles {
// Once per frame, before the views: uploads the new particles and runs the simulation step
void Simulate(Scene& scene, RHI::Device& device, RHI::CommandList& cmd);
// Adds the system to a view
void Submit(Scene& scene, EntityID e, const ParticleSystemComponent& c, SceneRenderer& renderer, RHI::Device& device);
void Release(Scene& scene);
void Shutdown(); // before the device goes away
}
}
