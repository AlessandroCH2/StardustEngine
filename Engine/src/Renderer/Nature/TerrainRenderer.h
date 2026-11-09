#pragma once
#include "Renderer/Nature/TerrainData.h"
#include "Renderer/Camera.h"
#include <array>

namespace Stardust {

// CDLOD-style terrain: a quadtree is refined around the camera every frame and each leaf is drawn as an
// instance of one shared grid patch whose heights come from the heightmap in the vertex shader.
class TerrainRenderer {
public:
    TerrainRenderer(RHI::Device& device, RHI::Format hdr, RHI::Format id, RHI::Format depth);

    void Prepare(const TerrainData& terrain, const glm::vec3& origin, u32 entityId,
                 const glm::vec3& cameraPos, const Frustum& frustum, float shadowDistance);
    void Draw(RHI::CommandList& cmd, RHI::Buffer* frameBuffer);
    void DrawShadow(RHI::CommandList& cmd, RHI::Buffer* frameBuffer, u32 cascade);

    u32 PatchCount() const { return m_MainCount; }
    u64 Triangles() const { return (u64)m_MainCount * m_IndexCount / 3; }

private:
    void Select(float minX, float minZ, float size, bool useFrustum, float maxDistance, std::vector<glm::vec4>& out);
    void PushConstants(RHI::CommandList& cmd, RHI::Buffer* frameBuffer, u32 base, u32 cascade);

    RHI::Device& m_Device;
    Ref<RHI::Pipeline> m_Pipeline, m_ShadowPipeline;
    Ref<RHI::Buffer> m_Indices;
    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_Instances, m_Layers;
    u32 m_IndexCount = 0;

    const TerrainData* m_Terrain = nullptr;
    glm::vec3 m_Origin{ 0.0f }, m_CameraPos{ 0.0f };
    Frustum m_Frustum;
    u32 m_EntityId = 0;
    u32 m_MainCount = 0, m_ShadowBase = 0, m_ShadowCount = 0;
    std::vector<glm::vec4> m_Main, m_Shadow;
};

} // namespace Stardust
