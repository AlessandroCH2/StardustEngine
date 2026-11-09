#pragma once
#include "Renderer/Nature/TerrainData.h"
#include "Renderer/Camera.h"
#include "Renderer/Mesh.h"
#include <array>

namespace Stardust {

struct FrameConstants;

// Procedurally generated broadleaf trees (bark tubes + alpha-tested leaf cluster cards),
// drawn with GPU instancing, CPU frustum culling and two LODs.
class TreeRenderer {
public:
    static constexpr u32 kTypes = 3;
    static constexpr u32 kLods = 2;

    TreeRenderer(RHI::Device& device, RHI::Format hdr, RHI::Format id, RHI::Format depth);

    // CPU culling for the main view and every shadow cascade; writes this frame's instance buffer
    void Prepare(const TerrainData& terrain, const glm::vec3& origin, u32 entityId,
                 const glm::vec3& cameraPos, const Frustum& frustum, const FrameConstants& fc, bool shadows);
    void Draw(RHI::CommandList& cmd, RHI::Buffer* frameBuffer);
    void DrawShadow(RHI::CommandList& cmd, RHI::Buffer* frameBuffer, u32 cascade);

    u32 VisibleTrees() const { return m_Visible; }
    u64 Triangles() const { return m_Triangles; }

private:
    struct TreeModel {
        Ref<Mesh> bark[kLods], leaves[kLods];
        float height = 10.0f, radius = 4.0f;
    };
    struct Batch { u32 base = 0, count = 0; };
    // list 0 = main view, 1..4 = shadow cascades
    static constexpr u32 kLists = 5;

    void BuildModels();
    void CreateLeafTexture();
    void DrawList(RHI::CommandList& cmd, RHI::Buffer* frameBuffer, u32 list, u32 cascade, bool shadow);

    RHI::Device& m_Device;
    Ref<RHI::Pipeline> m_Bark, m_Leaves, m_BarkShadow, m_LeavesShadow;
    Ref<RHI::Texture> m_LeafTexture;
    std::array<TreeModel, kTypes> m_Models;
    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_Instances;
    Batch m_Batches[kLists][kTypes][kLods]{};
    u32 m_EntityId = 0, m_Visible = 0;
    u64 m_Triangles = 0;
    bool m_HasData = false;
};

} // namespace Stardust
