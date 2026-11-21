#pragma once
// Reflection Probe captures: the scene is rendered six times around each probe into a cube map,
// then filtered into mips of growing roughness. Objects inside a probe's box use it for reflections
// and local ambient light (PBR.hlsli :: SampleProbes).
#include "Core/Base.h"
#include "RHI/RHI.h"

namespace Stardust {
class Scene;
class SceneRenderer;

namespace ReflectionProbes {
// Captures the probes that need it: one face of one probe per frame (a capture takes six frames)
void Render(Scene& scene, RHI::Device& device, RHI::CommandList& cmd, float time);
// Adds the captured probes of the scene to a view
void Submit(Scene& scene, SceneRenderer& renderer);
void Release(Scene& scene);
// Frees the capture renderer and cube maps (before the device goes away)
void Shutdown();
}
}
