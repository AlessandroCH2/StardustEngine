#include "Renderer/Nature/TerrainRenderer.h"
#include <cstring>
#include "Assets/AssetManager.h"

namespace Stardust {

using namespace RHI;

static constexpr u32 kPatchRes = 32;          // quads per patch side
static constexpr float kMinPatchSize = 16.0f; // metres -> 0.5 m vertex spacing close to the camera
static constexpr float kLodFactor = 2.2f;     // split while distance < size * factor
static constexpr u32 kMaxInstances = 4096;

struct TerrainPushC {
    u32 frameBuffer, instanceBuffer, instanceBase, heightmap;
    glm::vec3 origin; float size;
    u32 patchRes, entityId, cascade; float skirtDepth;
    glm::vec4 grassColor, dryColor;
    u32 splatmap, layerBuffer, pad0, pad1;
};
static_assert(sizeof(TerrainPushC) == 96);

struct TerrainLayerGPU {
    glm::vec4 tintRoughness;
    u32 texture;   // SRV or ~0 for the procedural look
    float tiling;
    u32 pad[2];
};

TerrainRenderer::TerrainRenderer(Device& device, Format hdr, Format id, Format depth) : m_Device(device) {
    GraphicsPipelineDesc d;
    d.name = "Terrain";
    d.vs = "Terrain.vs"; d.ps = "Terrain.ps";
    d.colorFormats = { hdr, id };
    d.depthFormat = depth;
    m_Pipeline = device.CreateGraphicsPipeline(d);

    GraphicsPipelineDesc s;
    s.name = "TerrainShadow";
    s.vs = "TerrainShadow.vs";
    s.depthFormat = depth;
    s.cull = CullMode::None;
    s.depthClamp = true;
    s.depthBiasConstant = -1.5f;
    s.depthBiasSlope = -2.0f;
    m_ShadowPipeline = device.CreateGraphicsPipeline(s);

    // Index buffer of one patch: grid + skirt (both windings so the skirt is visible from either side)
    const u32 N = kPatchRes, V = N + 1;
    std::vector<u32> idx;
    for (u32 z = 0; z < N; ++z)
        for (u32 x = 0; x < N; ++x) {
            u32 a = z * V + x, b = a + V;
            idx.insert(idx.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
    u32 grid = V * V;
    auto gridIndex = [&](u32 side, u32 k) { return side == 0 ? k : side == 1 ? k * V + N : side == 2 ? N * V + k : k * V; };
    for (u32 side = 0; side < 4; ++side)
        for (u32 k = 0; k < N; ++k) {
            u32 g0 = gridIndex(side, k), g1 = gridIndex(side, k + 1);
            u32 s0 = grid + side * V + k, s1 = s0 + 1;
            idx.insert(idx.end(), { g0, s0, g1, g1, s0, s1, g0, g1, s0, g1, s1, s0 });
        }
    m_IndexCount = (u32)idx.size();
    m_Indices = device.CreateBuffer({ idx.size() * 4, BufferUsage::Index, MemoryUsage::GPUOnly, "TerrainPatchIB" }, idx.data());
    for (auto& b : m_Instances)
        b = device.CreateBuffer({ kMaxInstances * sizeof(glm::vec4), BufferUsage::Storage, MemoryUsage::CPUToGPU, "TerrainPatches" });
    for (auto& b : m_Layers)
        b = device.CreateBuffer({ kTerrainLayers * sizeof(TerrainLayerGPU), BufferUsage::Storage, MemoryUsage::CPUToGPU, "TerrainLayers" });
}

void TerrainRenderer::Select(float minX, float minZ, float size, bool useFrustum, float maxDistance, std::vector<glm::vec4>& out) {
    glm::vec3 mn = m_Origin + glm::vec3(minX, m_Terrain->MinHeight(), minZ);
    glm::vec3 mx = m_Origin + glm::vec3(minX + size, m_Terrain->MaxHeight(), minZ + size);
    glm::vec3 closest = glm::clamp(m_CameraPos, mn, mx);
    float dist = glm::length(closest - m_CameraPos);
    if (dist > maxDistance) return;
    if (useFrustum && !m_Frustum.Box(mn, mx)) return;
    if (size > kMinPatchSize && dist < size * kLodFactor) {
        float h = size * 0.5f;
        Select(minX, minZ, h, useFrustum, maxDistance, out);
        Select(minX + h, minZ, h, useFrustum, maxDistance, out);
        Select(minX, minZ + h, h, useFrustum, maxDistance, out);
        Select(minX + h, minZ + h, h, useFrustum, maxDistance, out);
        return;
    }
    if (out.size() < kMaxInstances / 2) out.push_back({ minX, minZ, size, 0.0f });
}

void TerrainRenderer::Prepare(const TerrainData& terrain, const glm::vec3& origin, u32 entityId,
                              const glm::vec3& cameraPos, const Frustum& frustum, float shadowDistance) {
    m_Terrain = &terrain;
    m_Origin = origin;
    m_EntityId = entityId;
    m_CameraPos = cameraPos;
    m_Frustum = frustum;
    m_Main.clear();
    m_Shadow.clear();
    float half = terrain.Settings().size * 0.5f;
    Select(-half, -half, terrain.Settings().size, true, 1e9f, m_Main);
    Select(-half, -half, terrain.Settings().size, false, shadowDistance * 1.5f + 100.0f, m_Shadow);

    m_MainCount = (u32)m_Main.size();
    m_ShadowBase = m_MainCount;
    m_ShadowCount = (u32)m_Shadow.size();
    auto* dst = m_Instances[m_Device.FrameIndex()]->As<glm::vec4>();
    std::memcpy(dst, m_Main.data(), m_Main.size() * sizeof(glm::vec4));
    std::memcpy(dst + m_ShadowBase, m_Shadow.data(), m_Shadow.size() * sizeof(glm::vec4));

    auto* layers = m_Layers[m_Device.FrameIndex()]->As<TerrainLayerGPU>();
    for (u32 i = 0; i < kTerrainLayers; ++i) {
        const TerrainLayer fallback;const TerrainLayer& l = i<terrain.Settings().layers.size()?terrain.Settings().layers[i]:fallback;
        auto tex = AssetManager::GetTexture(l.albedoMap, true);
        layers[i] = { glm::vec4(l.tint, l.roughness), tex ? tex->SRV() : ~0u, std::max(l.tiling, 0.01f), { 0, 0 } };
    }
}

void TerrainRenderer::PushConstants(CommandList& cmd, Buffer* frameBuffer, u32 base, u32 cascade) {
    const TerrainSettings& s = m_Terrain->Settings();
    TerrainPushC p{};
    p.frameBuffer = frameBuffer->Index();
    p.instanceBuffer = m_Instances[m_Device.FrameIndex()]->Index();
    p.instanceBase = base;
    p.heightmap = m_Terrain->Heightmap()->SRV();
    p.origin = m_Origin;
    p.size = s.size;
    p.patchRes = kPatchRes;
    p.entityId = m_EntityId;
    p.cascade = cascade;
    p.skirtDepth = 2.0f;
    p.grassColor = glm::vec4(s.grassColor, s.proceduralAppearance?1.f:0.f);
    p.dryColor = glm::vec4(s.dryGrassColor, 1.0f);
    p.splatmap = m_Terrain->Splatmap()->SRV();
    p.layerBuffer = m_Layers[m_Device.FrameIndex()]->Index();
    cmd.Push(p);
}

void TerrainRenderer::Draw(CommandList& cmd, Buffer* frameBuffer) {
    if (!m_Terrain || m_MainCount == 0) return;
    cmd.SetPipeline(m_Pipeline.get());
    PushConstants(cmd, frameBuffer, 0, 0);
    cmd.SetIndexBuffer(m_Indices.get());
    cmd.DrawIndexed(m_IndexCount, m_MainCount);
}

void TerrainRenderer::DrawShadow(CommandList& cmd, Buffer* frameBuffer, u32 cascade) {
    if (!m_Terrain || m_ShadowCount == 0) return;
    cmd.SetPipeline(m_ShadowPipeline.get());
    PushConstants(cmd, frameBuffer, m_ShadowBase, cascade);
    cmd.SetIndexBuffer(m_Indices.get());
    cmd.DrawIndexed(m_IndexCount, m_ShadowCount);
}

} // namespace Stardust
