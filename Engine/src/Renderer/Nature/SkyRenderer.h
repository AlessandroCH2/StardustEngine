#pragma once
#include "RHI/RHI.h"
#include <algorithm>

namespace Stardust {

// Physical atmosphere LUTs, per-frame sky ambient and low-resolution volumetric clouds.
class SkyRenderer {
public:
    explicit SkyRenderer(RHI::Device& device);

    void Resize(u32 width, u32 height);
    // Compute work for this frame. Must run after the frame constants are written.
    void Update(RHI::CommandList& cmd, RHI::Buffer* frameBuffer, bool clouds);

    u32 TransmittanceSRV() const { return m_Transmittance->SRV(); }
    u32 SkyViewSRV() const { return m_SkyView->SRV(); }
    u32 AmbientBuffer() const { return m_Ambient->Index(); }
    u32 CloudsSRV() const { return m_Clouds->SRV(); }

    static constexpr u32 kCloudDownscale = 2;

private:
    RHI::Device& m_Device;
    Ref<RHI::Pipeline> m_TransmittancePipe, m_SkyViewPipe, m_AmbientPipe, m_NoisePipe, m_CloudsPipe;
    Ref<RHI::Texture> m_Transmittance, m_SkyView, m_CloudNoise, m_Clouds;
    Ref<RHI::Buffer> m_Ambient;
    bool m_StaticReady = false;
    u32 m_CloudW = 0, m_CloudH = 0;
};

} // namespace Stardust
