#include "Renderer/OcclusionCulling.h"
#include <algorithm>
#include <cmath>

namespace Stardust {

void OcclusionBuffer::Begin(const glm::mat4& viewProj) {
    m_ViewProj = viewProj;
    m_Depth.assign((size_t)kWidth * kHeight, 0.0f);
    m_Triangles = 0;
}

void OcclusionBuffer::AddOccluder(const std::vector<glm::vec3>& positions, const std::vector<u32>& indices, u32 firstIndex, u32 indexCount, const glm::mat4& model) {
    const glm::mat4 mvp = m_ViewProj * model;
    const u32 end = std::min<u32>((u32)indices.size(), firstIndex + indexCount);
    for (u32 i = firstIndex; i + 2 < end; i += 3) {
        const u32 ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) continue;
        Rasterize(mvp * glm::vec4(positions[ia], 1), mvp * glm::vec4(positions[ib], 1), mvp * glm::vec4(positions[ic], 1));
    }
}

void OcclusionBuffer::Rasterize(const glm::vec4& a, const glm::vec4& b, const glm::vec4& c) {
    // A triangle crossing the camera plane is left out: fewer occluders is always safe
    constexpr float kNear = 1e-3f;
    if (a.w < kNear || b.w < kNear || c.w < kNear) return;
    auto screen = [](const glm::vec4& v) { return glm::vec3((v.x / v.w * 0.5f + 0.5f) * kWidth, (0.5f - v.y / v.w * 0.5f) * kHeight, v.z / v.w); };
    glm::vec3 p0 = screen(a), p1 = screen(b), p2 = screen(c);
    float area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
    if (std::abs(area) < 1e-6f) return;
    if (area < 0) { std::swap(p1, p2); area = -area; } // both windings occlude
    // The farthest corner (reverse-Z: smallest), so a pixel is never claimed closer than the triangle is
    const float depth = std::max(0.0f, std::min({ p0.z, p1.z, p2.z }));
    if (depth <= 0.0f) return;
    const int x0 = std::max(0, (int)std::floor(std::min({ p0.x, p1.x, p2.x })));
    const int x1 = std::min(kWidth - 1, (int)std::ceil(std::max({ p0.x, p1.x, p2.x })));
    const int y0 = std::max(0, (int)std::floor(std::min({ p0.y, p1.y, p2.y })));
    const int y1 = std::min(kHeight - 1, (int)std::ceil(std::max({ p0.y, p1.y, p2.y })));
    if (x0 > x1 || y0 > y1) return;
    ++m_Triangles;
    auto edge = [](const glm::vec3& a, const glm::vec3& b, float x, float y) { return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x); };
    auto inside = [&](float x, float y) { return edge(p0, p1, x, y) >= 0 && edge(p1, p2, x, y) >= 0 && edge(p2, p0, x, y) >= 0; };
    for (int y = y0; y <= y1; ++y) {
        // Corners of the pixels on this row and the next: a pixel is covered when its four corners are inside
        bool previousTop = inside((float)x0, (float)y), previousBottom = inside((float)x0, (float)y + 1);
        float* row = &m_Depth[(size_t)y * kWidth];
        for (int x = x0; x <= x1; ++x) {
            const bool top = inside((float)x + 1, (float)y), bottom = inside((float)x + 1, (float)y + 1);
            if (previousTop && previousBottom && top && bottom) row[x] = std::max(row[x], depth);
            previousTop = top; previousBottom = bottom;
        }
    }
}

bool OcclusionBuffer::IsVisible(const glm::vec3& mn, const glm::vec3& mx) const {
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f, closest = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 c = m_ViewProj * glm::vec4((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1.0f);
        if (c.w < 1e-3f) return true; // reaches behind the camera
        const float x = (c.x / c.w * 0.5f + 0.5f) * kWidth, y = (0.5f - c.y / c.w * 0.5f) * kHeight;
        minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y);
        closest = std::max(closest, c.z / c.w);
    }
    const int x0 = std::max(0, (int)std::floor(minX)), x1 = std::min(kWidth - 1, (int)std::floor(maxX));
    const int y0 = std::max(0, (int)std::floor(minY)), y1 = std::min(kHeight - 1, (int)std::floor(maxY));
    if (x0 > x1 || y0 > y1) return true; // off screen: the frustum test decides
    for (int y = y0; y <= y1; ++y) {
        const float* row = &m_Depth[(size_t)y * kWidth];
        for (int x = x0; x <= x1; ++x) if (row[x] <= closest) return true; // this pixel shows the box (or nothing covers it)
    }
    return false;
}

} // namespace Stardust
