#pragma once
// Software occlusion culling: the biggest opaque meshes on screen (buildings, walls, cliffs) are drawn into
// a small depth buffer on the CPU, and the bounding boxes of the other objects are tested against it.
// It is conservative: a pixel counts as covered only when a triangle covers all of it, and a triangle's
// depth is its farthest corner, so an object is skipped only when it is certainly hidden.
#include "Core/Base.h"
#include <glm/glm.hpp>
#include <vector>

namespace Stardust {

class OcclusionBuffer {
public:
    static constexpr int kWidth = 256, kHeight = 144;
    void Begin(const glm::mat4& viewProj);
    // Triangles of a mesh (indices into positions), placed with `model`
    void AddOccluder(const std::vector<glm::vec3>& positions, const std::vector<u32>& indices, u32 firstIndex, u32 indexCount, const glm::mat4& model);
    // False when the world-space box is hidden behind the occluders
    bool IsVisible(const glm::vec3& boxMin, const glm::vec3& boxMax) const;
    u32 Triangles() const { return m_Triangles; }
private:
    void Rasterize(const glm::vec4& a, const glm::vec4& b, const glm::vec4& c);
    glm::mat4 m_ViewProj{ 1.0f };
    std::vector<float> m_Depth; // reverse-Z: bigger is closer, 0 = nothing
    u32 m_Triangles = 0;
};

} // namespace Stardust
