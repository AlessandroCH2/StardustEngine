#include "Scene/GpuParticles.h"
#include "Scene/Scene.h"
#include "Scene/ParticleSystem.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Mesh.h"
#include "Assets/AssetManager.h"
#include <unordered_map>
#include <cstring>
#include <array>
#include <cstddef>

namespace Stardust::GpuParticles {
using namespace RHI;

namespace {
constexpr u32 kHeader = 512, kStride = 64;
constexpr u32 kMaxCapacity = 1u << 20;

// Must match GpuParticles.hlsli :: ParticleHeader
struct Header {
    glm::mat4 world;            // simulation space -> world (identity for world space)
    glm::vec4 acceleration;     // xyz gravity + force (simulation space), w drag per second
    glm::vec4 velocity;         // xyz velocity over lifetime, w speed limit (0 = none)
    glm::vec4 noise;            // strength, frequency, scroll, enabled
    glm::vec4 collision;        // plane height (world), bounce, dampen, lifetime loss
    glm::uvec4 flags;           // x local, y collision, z colour keys, w size keys
    glm::vec4 misc;             // x dt, y rotation over lifetime, z intensity, w render mode
    glm::vec4 colorTimes[2];    // 8 colour key times
    glm::vec4 colors[8];
    glm::vec4 sizeTimes[2], sizes[2];
    glm::vec4 stretch;          // length scale, speed scale, tiles x, tiles y
    glm::vec4 sheet;            // enabled, fps, cycles, random start frame
    glm::vec4 time;             // x clock (noise scroll), y world scale
    glm::mat4 inverseWorld;     // world -> simulation space (collision)
};
static_assert(offsetof(Header, inverseWorld) == 432);
static_assert(sizeof(Header) <= kHeader);

struct GpuParticle { glm::vec3 position; float age; glm::vec3 velocity; float lifetime; glm::vec4 color; float size, rotation, angularVelocity, seed; };
static_assert(sizeof(GpuParticle) == kStride);

struct System {
    Ref<Buffer> particles;
    std::array<Ref<Buffer>, kFramesInFlight> upload;
    Ref<Mesh> mesh;
    u32 capacity = 0, head = 0;
};
struct SceneSystems { std::unordered_map<u64, System> systems; };
std::unordered_map<Scene*, SceneSystems> s_Scenes;
Ref<Pipeline> s_Sim;
Device* s_Device = nullptr;

void Allocate(Device& device, System& s, u32 capacity) {
    if (s.capacity == capacity && s.particles) return;
    s.capacity = capacity; s.head = 0;
    s.particles = device.CreateBuffer({ kHeader + (u64)capacity * kStride, BufferUsage::Storage | BufferUsage::CopyDst, MemoryUsage::GPUOnly, "GpuParticles" });
    for (auto& u : s.upload) u.reset();
    // Quads: four vertices per particle, the vertex shader reads the particle buffer instead of vertices
    std::vector<u32> indices; indices.reserve((size_t)capacity * 6);
    for (u32 q = 0; q < capacity; ++q) for (u32 k : { 0u, 1u, 2u, 2u, 3u, 0u }) indices.push_back(q * 4 + k);
    std::vector<Vertex> placeholder(4);
    s.mesh = MakeRef<Mesh>(device, placeholder, indices, "GpuParticles");
    s.mesh->SetVertexBuffer(s.particles);
    s.mesh->SetBounds({ glm::vec3(-1e5f), glm::vec3(1e5f) });
}

Header MakeHeader(Scene& scene, EntityID e, const ParticleSystemComponent& c, float dt) {
    Header h{};
    const glm::mat4 world = scene.GetWorldMatrix(e);
    const bool local = c.simulationSpace == 0;
    h.world = local ? world : glm::mat4(1.0f);
    glm::vec3 gravity(0, -9.81f * c.gravityModifier, 0);
    if (local) gravity = glm::vec3(glm::inverse(world) * glm::vec4(gravity, 0));
    h.acceleration = glm::vec4(gravity + (c.forceOverLifetime ? c.force : glm::vec3(0)), c.limitVelocity ? c.drag : 0.0f);
    h.velocity = glm::vec4(c.velocityOverLifetime ? c.velocity : glm::vec3(0), c.limitVelocity ? std::max(0.001f, c.speedLimit) : 0.0f);
    h.noise = { c.noiseStrength, c.noiseFrequency, c.noiseScrollSpeed, c.noise ? 1.0f : 0.0f };
    h.collision = { c.collisionPlane, c.bounce, std::clamp(c.dampen, 0.0f, 1.0f), std::clamp(c.lifetimeLoss, 0.0f, 1.0f) };
    const u32 colorKeys = c.colorOverLifetime ? (u32)std::min<size_t>(8, c.colorKeys.size()) : 0;
    const u32 sizeKeys = c.sizeOverLifetime ? (u32)std::min<size_t>(8, c.sizeKeys.size()) : 0;
    h.flags = { local ? 1u : 0u, c.collision ? 1u : 0u, colorKeys, sizeKeys };
    h.misc = { dt, c.rotationOverLifetime ? 1.0f : 0.0f, std::max(0.0f, c.intensity), (float)c.renderMode };
    for (u32 i = 0; i < colorKeys; ++i) { h.colorTimes[i / 4][i % 4] = c.colorKeys[i].time; h.colors[i] = c.colorKeys[i].color; }
    for (u32 i = 0; i < sizeKeys; ++i) { h.sizeTimes[i / 4][i % 4] = c.sizeKeys[i].time; h.sizes[i / 4][i % 4] = c.sizeKeys[i].value; }
    h.stretch = { c.lengthScale, c.speedScale, (float)std::max(1, c.tilesX), (float)std::max(1, c.tilesY) };
    h.sheet = { c.textureSheet ? 1.0f : 0.0f, c.sheetFps, c.sheetCycles, c.randomStartFrame ? 1.0f : 0.0f };
    const float scale = local ? std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) }) : 1.0f;
    h.time = { c.gpuClock, scale, 0, 0 };
    h.inverseWorld = glm::inverse(h.world);
    return h;
}
}

void Simulate(Scene& scene, Device& device, CommandList& cmd) {
    Registry& reg = scene.Reg();
    bool any = false;
    reg.Each<ParticleSystemComponent>([&](EntityID, ParticleSystemComponent& c) { any |= c.gpu; });
    auto& state = s_Scenes[&scene];
    if (!any) { state.systems.clear(); return; }
    if (s_Device != &device || !s_Sim) { s_Device = &device; s_Sim = device.CreateComputePipeline({ "GpuParticleSim.cs", "GpuParticleSim" }); }
    std::unordered_map<u64, bool> live;
    const u32 fi = device.FrameIndex();
    bool dispatched = false;
    reg.Each<ParticleSystemComponent>([&](EntityID e, ParticleSystemComponent& c) {
        if (!c.gpu || !reg.IsEnabled<ParticleSystemComponent>(e)) return;
        const u64 uuid = reg.Get<IDComponent>(e).uuid;
        live[uuid] = true;
        System& s = state.systems[uuid];
        Allocate(device, s, std::clamp((u32)std::max(1, c.maxParticles), 16u, kMaxCapacity));
        const u32 newCount = (u32)std::min<size_t>(c.gpuPending.size(), s.capacity);
        const u64 uploadSize = kHeader + (u64)std::max(newCount, 64u) * kStride;
        auto& upload = s.upload[fi];
        if (!upload || upload->Desc().size < uploadSize) {
            u64 size = kHeader + 64ull * kStride; while (size < uploadSize) size *= 2;
            upload = device.CreateBuffer({ size, BufferUsage::CopySrc, MemoryUsage::CPUToGPU, "GpuParticlesUpload" });
        }
        auto* bytes = (u8*)upload->Mapped();
        const Header header = MakeHeader(scene, e, c, c.gpuDt);
        std::memcpy(bytes, &header, sizeof(Header));
        for (u32 i = 0; i < newCount; ++i) {
            const Particle& p = c.gpuPending[c.gpuPending.size() - newCount + i];
            GpuParticle g{ p.position, 0.0f, p.velocity, p.lifetime, p.color, p.size, p.rotation, p.angularVelocity, p.seed };
            std::memcpy(bytes + kHeader + (u64)i * kStride, &g, kStride);
        }
        if (c.gpuClear) { cmd.FillBuffer(s.particles.get(), kHeader, (u64)s.capacity * kStride, 0); s.head = 0; c.gpuClear = false; }
        cmd.CopyBuffer(upload.get(), 0, s.particles.get(), 0, kHeader);
        // New particles overwrite the oldest slots of the ring
        u32 written = 0;
        while (written < newCount) {
            const u32 run = std::min(newCount - written, s.capacity - s.head);
            cmd.CopyBuffer(upload.get(), kHeader + (u64)written * kStride, s.particles.get(), kHeader + (u64)s.head * kStride, (u64)run * kStride);
            written += run; s.head = (s.head + run) % s.capacity;
        }
        c.gpuPending.clear();
        dispatched = true;
    });
    if (dispatched) {
        cmd.MemoryBarrier();
        cmd.SetPipeline(s_Sim.get());
        reg.Each<ParticleSystemComponent>([&](EntityID e, ParticleSystemComponent& c) {
            if (!c.gpu || !reg.IsEnabled<ParticleSystemComponent>(e)) return;
            auto it = state.systems.find(reg.Get<IDComponent>(e).uuid);
            if (it == state.systems.end()) return;
            struct { u32 buffer, capacity; float dt, pad; } push{ it->second.particles->Index(), it->second.capacity, c.gpuDt, 0 };
            cmd.Push(push);
            cmd.Dispatch((it->second.capacity + 63) / 64, 1, 1);
            c.gpuDt = 0;
        });
        cmd.MemoryBarrier();
    }
    std::erase_if(state.systems, [&](auto& s) { return !live.contains(s.first); });
}

void Submit(Scene& scene, EntityID e, const ParticleSystemComponent& c, SceneRenderer& renderer, Device& device) {
    auto it = s_Scenes.find(&scene);
    if (it == s_Scenes.end()) return;
    auto sys = it->second.systems.find(scene.Reg().Get<IDComponent>(e).uuid);
    if (sys == it->second.systems.end() || !sys->second.mesh || c.gpuSpawned == 0) return;
    Material material;
    if (auto custom = AssetManager::GetMaterial(c.material)) material = *custom; else material.shader = ShaderType::Unlit;
    Ref<Texture> texture = c.texture.empty() ? ParticleSystems::DefaultTexture(device) : AssetManager::GetTexture(c.texture, true);
    if (!texture) texture = ParticleSystems::DefaultTexture(device);
    material.runtimeAlbedo = texture;
    material.doubleSided = true;
    material.metallic = c.texture.empty() ? 0.0f : 1.0f; material.albedoMap = c.texture;
    renderer.SubmitParticles(sys->second.mesh, material, e, sys->second.capacity * 6, c.blendMode == 1, c.sortingOrder, true);
}

void Release(Scene& scene) { s_Scenes.erase(&scene); }
void Shutdown() { s_Scenes.clear(); s_Sim.reset(); s_Device = nullptr; }

} // namespace Stardust::GpuParticles
