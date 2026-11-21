#include "Renderer/ReflectionProbes.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Scene.h"
#include "Core/Log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <unordered_map>
#include <bit>
#include <cstring>

namespace Stardust::ReflectionProbes {
using namespace RHI;

namespace {
struct Capture {
    Ref<Texture> raw, filtered;
    int resolution = 0; u32 mips = 0;
    glm::vec3 position{ 0.0f };
    bool valid = false;
    u64 lastFrame = 0;
};
struct SceneProbes { std::unordered_map<u64, Capture> probes; u64 capturing = 0; u32 face = 0; };
std::unordered_map<Scene*, SceneProbes> s_Scenes;
Scope<SceneRenderer> s_Renderer;
Ref<Pipeline> s_Copy, s_Filter;
RHI::Device* s_Device = nullptr;
u64 s_Frame = 0;

void Pipelines(Device& device) {
    if (s_Device == &device && s_Copy) return;
    s_Device = &device;
    s_Copy = device.CreateComputePipeline({ "ProbeCopy.cs", "ProbeCopy" });
    s_Filter = device.CreateComputePipeline({ "ProbeFilter.cs", "ProbeFilter" });
    s_Renderer = MakeScope<SceneRenderer>(device);
    s_Renderer->captureMode = true;
}

void Allocate(Device& device, Capture& c, int resolution) {
    if (c.resolution == resolution && c.raw) return;
    c.resolution = resolution;
    c.mips = std::max(1u, (u32)std::bit_width((u32)resolution) - 2); // down to 4x4
    TextureDesc d;
    d.width = d.height = resolution; d.mipLevels = c.mips; d.dim = TextureDim::Cube;
    d.format = Format::RGBA16F; d.usage = TextureUsage::Storage | TextureUsage::Sampled;
    d.name = "ProbeCapture"; c.raw = device.CreateTexture(d);
    d.name = "ProbeFiltered"; c.filtered = device.CreateTexture(d);
    c.valid = false;
}

// Face orientations: forward and up of each cube face (+X, -X, +Y, -Y, +Z, -Z)
const glm::vec3 kForward[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
const glm::vec3 kUp[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
}

void Render(Scene& scene, Device& device, CommandList& cmd, float time) {
    Registry& reg = scene.Reg();
    bool any = false;
    reg.Each<ReflectionProbeComponent>([&](EntityID, ReflectionProbeComponent&) { any = true; });
    auto& state = s_Scenes[&scene];
    if (!any) { state.probes.clear(); return; }
    ++s_Frame;
    Pipelines(device);
    const float dt = 1.0f / 60.0f;
    // Pick the probe that needs a capture most: never captured first, then the oldest realtime one
    EntityID chosen = kNullEntity; u64 oldest = ~0ull;
    std::unordered_map<u64, bool> live;
    reg.Each<ReflectionProbeComponent>([&](EntityID e, ReflectionProbeComponent& c) {
        if (!reg.IsEnabled<ReflectionProbeComponent>(e) || !scene.IsActiveInHierarchy(e)) return;
        const u64 uuid = reg.Get<IDComponent>(e).uuid;
        live[uuid] = true;
        auto& cap = state.probes[uuid];
        const glm::vec3 position = glm::vec3(scene.GetWorldMatrix(e)[3]) + c.center;
        if (cap.valid && glm::length(position - cap.position) > 0.01f && !scene.IsRunning()) c.dirty = true; // moved in the editor
        if (c.mode == 1) { c.timer += dt; if (c.timer >= c.interval) c.dirty = true; }
        if (!c.dirty && cap.valid) return;
        const u64 age = cap.valid ? cap.lastFrame : 0;
        if (age < oldest) { oldest = age; chosen = e; }
    });
    std::erase_if(state.probes, [&](auto& p) { return !live.contains(p.first); });
    // One face per frame: the capture renderer keeps one view per frame in its buffers
    if (state.capturing && !live.contains(state.capturing)) state.capturing = 0;
    if (!state.capturing) { if (!chosen) return; state.capturing = reg.Get<IDComponent>(chosen).uuid; state.face = 0; }
    EntityID target = scene.FindByUUID(state.capturing);
    if (!target) { state.capturing = 0; return; }
    auto& c = reg.Get<ReflectionProbeComponent>(target);
    auto& cap = state.probes[state.capturing];
    Allocate(device, cap, c.resolution);
    const glm::vec3 position = glm::vec3(scene.GetWorldMatrix(target)[3]) + c.center;
    ScopedGpuTimer timer(device, "Reflection Probe");
    SceneRenderer& r = *s_Renderer;
    r.Resize((u32)c.resolution, (u32)c.resolution);
    r.SetDisplaySize(0, 0);
    EnvironmentSettings env = scene.GatherEnvironment();
    env.taa = false; env.depthOfField = false; env.motionBlur = 0; env.aoIntensity = std::min(env.aoIntensity, 0.5f);
    const float fov = glm::radians(95.0f);
    const glm::mat4 proj = PerspectiveReverseZ(fov, 1.0f, std::max(0.01f, c.nearClip), std::max(c.nearClip + 1.0f, c.farClip));
    {
        const u32 face = state.face;
        RenderCamera camera;
        camera.position = position;
        camera.view = glm::lookAt(position, position + kForward[face], kUp[face]);
        camera.projection = proj;
        camera.fovY = fov; camera.nearPlane = c.nearClip; camera.farPlane = c.farClip;
        camera.shadows = c.shadows; camera.postProcessing = false;
        r.BeginScene(camera, env, time);
        scene.SubmitToRenderer(r, device, false, true);
        r.Render(cmd);
        cmd.Barrier(r.HDRTarget(), ResourceState::ShaderRead);
        cmd.Barrier(cap.raw.get(), ResourceState::UnorderedAccess);
        cmd.SetPipeline(s_Copy.get());
        const glm::mat4 rotation = proj * glm::lookAt(glm::vec3(0), kForward[face], kUp[face]);
        for (u32 m = 0; m < cap.mips; ++m) {
            struct { u32 src, dst, face, size; glm::mat4 viewProj; } p{ r.HDRTarget()->SRV(), cap.raw->UAV(m), face, std::max(1u, (u32)c.resolution >> m), rotation };
            cmd.Push(p);
            cmd.Dispatch((p.size + 7) / 8, (p.size + 7) / 8, 1);
        }
    }
    cmd.Barrier(cap.raw.get(), ResourceState::ShaderRead);
    if (++state.face < 6) return;
    // All six faces: filter into the cube the shaders read
    cmd.Barrier(cap.filtered.get(), ResourceState::UnorderedAccess);
    cmd.SetPipeline(s_Filter.get());
    for (u32 m = 0; m < cap.mips; ++m) {
        const u32 size = std::max(1u, (u32)c.resolution >> m);
        struct { u32 src, dst, size, samples; float roughness, srcSize; float pad[2]; } p{ cap.raw->SRV(), cap.filtered->UAV(m), size, m == 0 ? 1u : 64u, cap.mips > 1 ? (float)m / (cap.mips - 1) : 0.0f, (float)c.resolution, {} };
        cmd.Push(p);
        cmd.Dispatch((size + 7) / 8, (size + 7) / 8, 6);
    }
    cmd.Barrier(cap.filtered.get(), ResourceState::ShaderRead);
    cap.valid = true; cap.position = position; cap.lastFrame = s_Frame;
    c.dirty = false; c.timer = 0;
    state.capturing = 0; state.face = 0;
}

void Submit(Scene& scene, SceneRenderer& renderer) {
    auto it = s_Scenes.find(&scene);
    if (it == s_Scenes.end() || it->second.probes.empty()) return;
    Registry& reg = scene.Reg();
    reg.Each<ReflectionProbeComponent>([&](EntityID e, ReflectionProbeComponent& c) {
        if (!reg.IsEnabled<ReflectionProbeComponent>(e) || !scene.IsActiveInHierarchy(e)) return;
        auto found = it->second.probes.find(reg.Get<IDComponent>(e).uuid);
        if (found == it->second.probes.end() || !found->second.valid) return;
        const Capture& cap = found->second;
        glm::vec3 scale, translation, skew; glm::vec4 persp; glm::quat rot;
        glm::decompose(scene.GetWorldMatrix(e), scale, rot, translation, skew, persp);
        const glm::vec3 center = translation + c.center, half = c.size * glm::abs(scale) * 0.5f;
        ProbeGPU p;
        p.boxMin = glm::vec4(center - half, std::max(0.0f, c.intensity));
        p.boxMax = glm::vec4(center + half, std::max(0.0f, c.blendDistance));
        const u32 srv = cap.filtered->SRV();
        float srvBits; std::memcpy(&srvBits, &srv, 4);
        p.capture = glm::vec4(cap.position, srvBits);
        p.params = glm::vec4(c.boxProjection ? 1.0f : 0.0f, (float)cap.mips, 0, 0);
        const float volume = half.x * half.y * half.z * 8.0f;
        renderer.SubmitProbe(p, volume - c.importance * 1e12f);
    });
}

void Release(Scene& scene) { s_Scenes.erase(&scene); }
void Shutdown() { s_Scenes.clear(); s_Renderer.reset(); s_Copy.reset(); s_Filter.reset(); s_Device = nullptr; }

} // namespace Stardust::ReflectionProbes
