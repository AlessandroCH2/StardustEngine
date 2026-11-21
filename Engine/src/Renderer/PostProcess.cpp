#include "Renderer/PostProcess.h"

namespace Stardust {

using namespace RHI;

struct TonemapPush {
    u32 hdr, bloom;
    float exposure, bloomIntensity, contrast, saturation, vignette, pad;
};

PostProcess::PostProcess(Device& device, Format outputFormat) : m_Device(device) {
    m_Down = device.CreateComputePipeline({ "BloomDown.cs", "BloomDown" });
    m_Up = device.CreateComputePipeline({ "BloomUp.cs", "BloomUp" });

    GraphicsPipelineDesc d;
    d.vs = "Fullscreen.vs";
    d.cull = CullMode::None;
    d.colorFormats = { Format::RGBA8_UNORM };
    d.name = "Tonemap"; d.ps = "Tonemap.ps";
    m_Tonemap = device.CreateGraphicsPipeline(d);
    d.colorFormats = { outputFormat };
    m_TonemapDirect = device.CreateGraphicsPipeline(d);
    d.name = "FXAA"; d.ps = "FXAA.ps";
    m_Fxaa = device.CreateGraphicsPipeline(d);
    m_TAA = device.CreateComputePipeline({ "TAA.cs", "TAA" });
    m_DoF = device.CreateComputePipeline({ "DepthOfField.cs", "DepthOfField" });
    m_MotionBlur = device.CreateComputePipeline({ "MotionBlur.cs", "MotionBlur" });
}

void PostProcess::Resize(u32 width, u32 height) {
    if (width == m_Width && height == m_Height) return;
    m_Width = width; m_Height = height;
    TextureDesc d;
    d.format = Format::RGBA16F;
    d.usage = TextureUsage::Storage | TextureUsage::Sampled;
    for (u32 i = 0; i < kBloomLevels; ++i) {
        d.width = std::max(1u, width >> (i + 1));
        d.height = std::max(1u, height >> (i + 1));
        d.name = "BloomDown" + std::to_string(i);
        m_DownChain[i] = m_Device.CreateTexture(d);
        if (i < kBloomLevels - 1) { d.name = "BloomUp" + std::to_string(i); m_UpChain[i] = m_Device.CreateTexture(d); }
    }
    TextureDesc l;
    l.name = "LDR";
    l.width = width; l.height = height;
    l.format = Format::RGBA8_UNORM;
    l.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
    m_LDR = m_Device.CreateTexture(l);
    TextureDesc h;
    h.width = width; h.height = height; h.format = Format::RGBA16F;
    h.usage = TextureUsage::Storage | TextureUsage::Sampled;
    for (int i = 0; i < 2; ++i) { h.name = "TAAHistory" + std::to_string(i); m_History[i] = m_Device.CreateTexture(h); }
    h.name = "PostTempA"; m_TempA = m_Device.CreateTexture(h);
    h.name = "PostTempB"; m_TempB = m_Device.CreateTexture(h);
}

void PostProcess::Execute(CommandList& cmd, Texture* hdr, Texture* output, const PostSettings& s) {
    ScopedGpuTimer timer(m_Device, "Post");
    cmd.Barrier(hdr, ResourceState::ShaderRead);
    // HDR effects: each reads the previous result and writes a storage image
    const bool depthEffects = s.depth != 0 && s.frameBuffer != 0;
    auto groups = [&](Texture* t) { return std::pair((t->Width() + 7) / 8, (t->Height() + 7) / 8); };
    if (depthEffects && s.taa) {
        Texture* history = m_History[m_HistoryIndex].get();
        Texture* out = m_History[m_HistoryIndex ^ 1].get();
        cmd.Barrier(history, ResourceState::ShaderRead);
        cmd.Barrier(out, ResourceState::UnorderedAccess);
        cmd.SetPipeline(m_TAA.get());
        struct { u32 frame, current, history, depth, dst, w, h, reset; float blend, pad[3]; } p{ s.frameBuffer, hdr->SRV(), history->SRV(), s.depth, out->UAV(), out->Width(), out->Height(), s.resetHistory ? 1u : 0u, 0.1f, {} };
        cmd.Push(p);
        auto [gx, gy] = groups(out); cmd.Dispatch(gx, gy, 1);
        cmd.Barrier(out, ResourceState::ShaderRead);
        m_HistoryIndex ^= 1;
        hdr = out;
    }
    if (depthEffects && s.depthOfField && s.bokehRadius > 0.0f) {
        Texture* out = hdr == m_TempA.get() ? m_TempB.get() : m_TempA.get();
        cmd.Barrier(out, ResourceState::UnorderedAccess);
        cmd.SetPipeline(m_DoF.get());
        const float scale = m_Height / 1080.0f;
        struct { u32 frame, src, depth, dst, w, h; float focus, range, radius; u32 samples; float pad[2]; } p{ s.frameBuffer, hdr->SRV(), s.depth, out->UAV(), out->Width(), out->Height(), s.focusDistance, s.focusRange, std::clamp(s.bokehRadius * scale, 0.0f, 32.0f), 48u, {} };
        cmd.Push(p);
        auto [gx, gy] = groups(out); cmd.Dispatch(gx, gy, 1);
        cmd.Barrier(out, ResourceState::ShaderRead);
        hdr = out;
    }
    if (depthEffects && s.motionBlur > 0.0f) {
        Texture* out = hdr == m_TempA.get() ? m_TempB.get() : m_TempA.get();
        cmd.Barrier(out, ResourceState::UnorderedAccess);
        cmd.SetPipeline(m_MotionBlur.get());
        struct { u32 frame, src, depth, dst, w, h; float intensity; u32 samples; } p{ s.frameBuffer, hdr->SRV(), s.depth, out->UAV(), out->Width(), out->Height(), s.motionBlur, 12u };
        cmd.Push(p);
        auto [gx, gy] = groups(out); cmd.Dispatch(gx, gy, 1);
        cmd.Barrier(out, ResourceState::ShaderRead);
        hdr = out;
    }

    // No bloom: skip the whole chain, the tone map multiplies it by zero anyway
    if (s.bloomIntensity <= 0.0f) cmd.Barrier(m_UpChain[0].get(), ResourceState::ShaderRead);
    else {
    // Bloom down chain
    cmd.SetPipeline(m_Down.get());
    Texture* src = hdr;
    for (u32 i = 0; i < kBloomLevels; ++i) {
        Texture* dst = m_DownChain[i].get();
        cmd.Barrier(dst, ResourceState::UnorderedAccess);
        struct { u32 src, dst, w, h, first; } p{ src->SRV(), dst->UAV(), dst->Width(), dst->Height(), i == 0 ? 1u : 0u };
        cmd.Push(p);
        cmd.Dispatch((dst->Width() + 7) / 8, (dst->Height() + 7) / 8, 1);
        cmd.Barrier(dst, ResourceState::ShaderRead);
        src = dst;
    }
    // Up chain: up[i] = down[i] + tent(lower)
    cmd.SetPipeline(m_Up.get());
    Texture* low = m_DownChain[kBloomLevels - 1].get();
    for (int i = (int)kBloomLevels - 2; i >= 0; --i) {
        Texture* dst = m_UpChain[i].get();
        cmd.Barrier(dst, ResourceState::UnorderedAccess);
        struct { u32 low, cur, dst; float radius; u32 w, h; } p{ low->SRV(), m_DownChain[i]->SRV(), dst->UAV(), 1.0f, dst->Width(), dst->Height() };
        cmd.Push(p);
        cmd.Dispatch((dst->Width() + 7) / 8, (dst->Height() + 7) / 8, 1);
        cmd.Barrier(dst, ResourceState::ShaderRead);
        low = dst;
    }
    }

    TonemapPush tp{ hdr->SRV(), m_UpChain[0]->SRV(), s.exposure, s.bloomIntensity, s.contrast, s.saturation, s.vignette, s.fxaa ? 1.0f : 0.0f };
    RenderingDesc rd;
    if (s.fxaa) {
        rd.colors.push_back({ m_LDR.get(), LoadOp::DontCare });
        cmd.BeginRendering(rd);
        cmd.SetPipeline(m_Tonemap.get());
        cmd.Push(tp);
        cmd.Draw(3);
        cmd.EndRendering();
        cmd.Barrier(m_LDR.get(), ResourceState::ShaderRead);

        RenderingDesc fd;
        fd.colors.push_back({ output, LoadOp::DontCare });
        cmd.BeginRendering(fd);
        cmd.SetPipeline(m_Fxaa.get());
        cmd.Push(m_LDR->SRV());
        cmd.Draw(3);
        cmd.EndRendering();
    } else {
        rd.colors.push_back({ output, LoadOp::DontCare });
        cmd.BeginRendering(rd);
        cmd.SetPipeline(m_TonemapDirect.get());
        cmd.Push(tp);
        cmd.Draw(3);
        cmd.EndRendering();
    }
}

} // namespace Stardust
