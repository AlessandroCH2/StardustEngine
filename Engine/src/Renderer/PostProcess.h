#pragma once
#include "RHI/RHI.h"
#include <algorithm>
#include <array>

namespace Stardust {

struct PostSettings {
    float exposure = 1.0f;
    float bloomIntensity = 0.04f;
    float contrast = 1.06f;
    float saturation = 1.08f;
    float vignette = 0.35f;
    bool fxaa = true;
    // HDR effects before bloom, read the depth buffer and the frame constants
    u32 frameBuffer = 0, depth = 0;
    bool taa = false, resetHistory = false;
    bool depthOfField = false; float focusDistance = 10, focusRange = 8, bokehRadius = 8;
    float motionBlur = 0;
};

// HDR -> LDR chain: physically based bloom (13-tap down / tent up), ACES tonemapping, grading, FXAA.
class PostProcess {
public:
    explicit PostProcess(RHI::Device& device, RHI::Format outputFormat);
    void Resize(u32 width, u32 height);
    // Reads hdr (any state), writes output (left in RenderTarget state)
    void Execute(RHI::CommandList& cmd, RHI::Texture* hdr, RHI::Texture* output, const PostSettings& s);

private:
    static constexpr u32 kBloomLevels = 6;
    RHI::Device& m_Device;
    Ref<RHI::Pipeline> m_Down, m_Up, m_Tonemap, m_Fxaa, m_TonemapDirect;
    Ref<RHI::Pipeline> m_TAA, m_DoF, m_MotionBlur;
    std::array<Ref<RHI::Texture>, 2> m_History;
    Ref<RHI::Texture> m_TempA, m_TempB;
    u32 m_HistoryIndex = 0;
    std::array<Ref<RHI::Texture>, kBloomLevels> m_DownChain;
    std::array<Ref<RHI::Texture>, kBloomLevels - 1> m_UpChain;
    Ref<RHI::Texture> m_LDR;
    u32 m_Width = 0, m_Height = 0;
};

} // namespace Stardust
