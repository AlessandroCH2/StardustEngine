#pragma once
#include "Renderer/Nature/TerrainData.h"

namespace Stardust {

// Fully GPU-driven grass: a compute pass scatters, culls and LODs blades around the camera every frame,
// then a single indirect instanced draw renders them.
class GrassRenderer {
public:
    GrassRenderer(RHI::Device& device, RHI::Format hdr, RHI::Format id, RHI::Format depth);

    // Compute pass (outside of rendering)
    void Generate(RHI::CommandList& cmd, RHI::Buffer* frameBuffer, const TerrainData& terrain,
                  const glm::vec3& origin, const glm::vec3& cameraPos);
    // Draw pass (inside the opaque rendering pass)
    void Draw(RHI::CommandList& cmd, RHI::Buffer* frameBuffer, const TerrainData& terrain, u32 entityId);

    bool Active() const { return m_Active; }
    static constexpr u32 kCapacity = 1'200'000;

private:
    RHI::Device& m_Device;
    Ref<RHI::Pipeline> m_GenPipeline, m_DrawPipeline;
    Ref<RHI::Buffer> m_Instances, m_Args, m_Indices;
    bool m_Active = false;
};

} // namespace Stardust
