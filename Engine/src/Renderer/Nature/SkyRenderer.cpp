#include "Renderer/Quality.h"
#include "Renderer/Nature/SkyRenderer.h"

namespace Stardust {

using namespace RHI;

static constexpr u32 kTransW = 256, kTransH = 64;
static constexpr u32 kSkyW = 192, kSkyH = 108;
static constexpr u32 kNoiseSize = 128;

SkyRenderer::SkyRenderer(Device& device) : m_Device(device) {
    m_TransmittancePipe = device.CreateComputePipeline({ "TransmittanceLUT.cs", "TransmittanceLUT" });
    m_SkyViewPipe = device.CreateComputePipeline({ "SkyViewLUT.cs", "SkyViewLUT" });
    m_AmbientPipe = device.CreateComputePipeline({ "SkyAmbient.cs", "SkyAmbient" });
    m_NoisePipe = device.CreateComputePipeline({ "CloudNoise.cs", "CloudNoise" });
    m_CloudsPipe = device.CreateComputePipeline({ "Clouds.cs", "Clouds" });

    TextureDesc d;
    d.usage = TextureUsage::Storage | TextureUsage::Sampled;
    d.format = Format::RGBA16F;
    d.name = "TransmittanceLUT"; d.width = kTransW; d.height = kTransH;
    m_Transmittance = device.CreateTexture(d);
    d.name = "SkyViewLUT"; d.width = kSkyW; d.height = kSkyH;
    m_SkyView = device.CreateTexture(d);
    d.name = "CloudNoise"; d.width = d.height = d.depth = kNoiseSize; d.dim = TextureDim::Tex3D; d.format = Format::RGBA8_UNORM;
    m_CloudNoise = device.CreateTexture(d);

    m_Ambient = device.CreateBuffer({ 64, BufferUsage::Storage, MemoryUsage::GPUOnly, "SkyAmbient" });
    Resize(1280, 720);
}

void SkyRenderer::Resize(u32 width, u32 height) {
    const u32 downscale = std::max(1u, GraphicsQuality::Current().cloudDownscale);
    u32 w = std::max(1u, width / downscale), h = std::max(1u, height / downscale);
    if (w == m_CloudW && h == m_CloudH) return;
    m_CloudW = w; m_CloudH = h;
    TextureDesc d;
    d.name = "Clouds";
    d.width = w; d.height = h;
    d.format = Format::RGBA16F;
    d.usage = TextureUsage::Storage | TextureUsage::Sampled;
    m_Clouds = m_Device.CreateTexture(d);
}

void SkyRenderer::Update(CommandList& cmd, Buffer* frameBuffer, bool clouds) {
    ScopedGpuTimer timer(m_Device, "Sky+Clouds");
    if (!m_StaticReady) {
        // One-time: transmittance LUT (fixed atmosphere) and the 3D cloud noise
        cmd.Barrier(m_Transmittance.get(), ResourceState::UnorderedAccess);
        cmd.SetPipeline(m_TransmittancePipe.get());
        struct { u32 out; u32 w, h; } tp{ m_Transmittance->UAV(), kTransW, kTransH };
        cmd.Push(tp);
        cmd.Dispatch((kTransW + 7) / 8, (kTransH + 7) / 8, 1);
        cmd.Barrier(m_Transmittance.get(), ResourceState::ShaderRead);

        cmd.Barrier(m_CloudNoise.get(), ResourceState::UnorderedAccess);
        cmd.SetPipeline(m_NoisePipe.get());
        struct { u32 out; u32 size; } np{ m_CloudNoise->UAV(), kNoiseSize };
        cmd.Push(np);
        cmd.Dispatch(kNoiseSize / 8, kNoiseSize / 8, kNoiseSize / 8);
        cmd.Barrier(m_CloudNoise.get(), ResourceState::ShaderRead);
        m_StaticReady = true;
    }

    cmd.Barrier(m_SkyView.get(), ResourceState::UnorderedAccess);
    cmd.SetPipeline(m_SkyViewPipe.get());
    struct { u32 frame, out, w, h; } sp{ frameBuffer->Index(), m_SkyView->UAV(), kSkyW, kSkyH };
    cmd.Push(sp);
    cmd.Dispatch((kSkyW + 7) / 8, (kSkyH + 7) / 8, 1);
    cmd.Barrier(m_SkyView.get(), ResourceState::ShaderRead);

    cmd.MemoryBarrier(); // previous readers of the ambient buffer
    cmd.SetPipeline(m_AmbientPipe.get());
    struct { u32 frame, out; } ap{ frameBuffer->Index(), m_Ambient->Index() };
    cmd.Push(ap);
    cmd.Dispatch(1, 1, 1);
    cmd.MemoryBarrier();

    cmd.Barrier(m_Clouds.get(), ResourceState::UnorderedAccess);
    if (clouds) {
        cmd.SetPipeline(m_CloudsPipe.get());
        struct { u32 frame, noise, out, pad, w, h; } cp{ frameBuffer->Index(), m_CloudNoise->SRV(), m_Clouds->UAV(), 0, m_CloudW, m_CloudH };
        cmd.Push(cp);
        cmd.Dispatch((m_CloudW + 7) / 8, (m_CloudH + 7) / 8, 1);
    }
    cmd.Barrier(m_Clouds.get(), ResourceState::ShaderRead);
}

} // namespace Stardust
