#include "Renderer/Quality.h"
#include "Renderer/Nature/GrassRenderer.h"
#include <cmath>
#include <algorithm>

namespace Stardust {

using namespace RHI;

static constexpr u32 kBladeIndexCount = 21;

struct GrassGenPush {
    u32 frameBuffer, instanceBuffer, argsBuffer, heightmap;
    glm::vec3 terrainOrigin; float terrainSize;
    glm::vec2 gridOrigin; float spacing; u32 gridDim;
    float radius, bladeHeight, flowerAmount; u32 capacity;
    u32 splatmap, pad[3];
};
static_assert(sizeof(GrassGenPush) == 80);

struct GrassDrawPush {
    u32 frameBuffer, instanceBuffer, entityId; float radius;
    glm::vec4 grassColor, dryColor;
};

GrassRenderer::GrassRenderer(Device& device, Format hdr, Format id, Format depth) : m_Device(device) {
    m_GenPipeline = device.CreateComputePipeline({ "GrassGen.cs", "GrassGen" });

    GraphicsPipelineDesc d;
    d.name = "Grass";
    d.vs = "Grass.vs"; d.ps = "Grass.ps";
    d.colorFormats = { hdr, id };
    d.depthFormat = depth;
    d.cull = CullMode::None;
    m_DrawPipeline = device.CreateGraphicsPipeline(d);

    m_Instances = device.CreateBuffer({ (u64)kCapacity * 32, BufferUsage::Storage, MemoryUsage::GPUOnly, "GrassInstances" });
    u32 args[5] = { kBladeIndexCount, 0, 0, 0, 0 };
    m_Args = device.CreateBuffer({ sizeof(args), BufferUsage::Storage | BufferUsage::Indirect, MemoryUsage::GPUOnly, "GrassArgs" }, args);

    // 3 quads + tip triangle
    std::vector<u32> idx;
    for (u32 l = 0; l < 3; ++l) {
        u32 a = l * 2;
        idx.insert(idx.end(), { a, a + 1, a + 3, a, a + 3, a + 2 });
    }
    idx.insert(idx.end(), { 6, 7, 8 });
    m_Indices = device.CreateBuffer({ idx.size() * 4, BufferUsage::Index, MemoryUsage::GPUOnly, "GrassBladeIB" }, idx.data());
}

void GrassRenderer::Generate(CommandList& cmd, Buffer* frameBuffer, const TerrainData& terrain,
                             const glm::vec3& origin, const glm::vec3& cameraPos) {
    const TerrainSettings& s = terrain.Settings();
    m_Active = s.grass && s.grassDensity > 0.0f && s.grassDistance > 1.0f;
    if (!m_Active) return;

    const GraphicsQuality& quality = GraphicsQuality::Current();
    float spacing = 0.085f / std::sqrt(std::clamp(s.grassDensity * quality.grassDensity, 0.05f, 4.0f));
    float radius = std::clamp(s.grassDistance * quality.grassDistance, 5.0f, 150.0f);
    u32 dim = (u32)std::ceil(2.0f * radius / spacing);
    glm::vec2 cam(cameraPos.x, cameraPos.z);
    glm::vec2 gridOrigin = glm::floor((cam - radius) / spacing) * spacing;

    cmd.MemoryBarrier();                   // previous frame's indirect draw must be done with these buffers
    cmd.FillBuffer(m_Args.get(), 4, 4, 0); // instanceCount = 0
    cmd.MemoryBarrier();
    cmd.SetPipeline(m_GenPipeline.get());
    GrassGenPush p{};
    p.frameBuffer = frameBuffer->Index();
    p.instanceBuffer = m_Instances->Index();
    p.argsBuffer = m_Args->Index();
    p.heightmap = terrain.Heightmap()->SRV();
    p.terrainOrigin = origin;
    p.terrainSize = s.size;
    p.gridOrigin = gridOrigin;
    p.spacing = spacing;
    p.gridDim = dim;
    p.radius = radius;
    p.bladeHeight = s.grassHeight;
    p.flowerAmount = s.flowerAmount;
    p.capacity = kCapacity;
    p.splatmap = terrain.Splatmap()->SRV();
    cmd.Push(p);
    cmd.Dispatch((dim + 15) / 16, (dim + 15) / 16, 1);
    cmd.MemoryBarrier();
}

void GrassRenderer::Draw(CommandList& cmd, Buffer* frameBuffer, const TerrainData& terrain, u32 entityId) {
    if (!m_Active) return;
    const TerrainSettings& s = terrain.Settings();
    cmd.SetPipeline(m_DrawPipeline.get());
    GrassDrawPush p{ frameBuffer->Index(), m_Instances->Index(), entityId, s.grassDistance,
                     glm::vec4(s.grassColor, 1), glm::vec4(s.dryGrassColor, 1) };
    cmd.Push(p);
    cmd.SetIndexBuffer(m_Indices.get());
    // GrassGen undoes its increment for blades over capacity, so instanceCount never exceeds the buffer
    cmd.DrawIndexedIndirect(m_Args.get(), 0, 1);
}

} // namespace Stardust
