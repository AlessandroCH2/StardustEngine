#pragma once
// Projecting gizmo lines into the Scene view. A line that passes behind the camera is cut where it crosses a plane just
// in front of it: projecting the hidden end directly sends it to the far side of the screen (or to huge coordinates),
// which is what made gizmos jump around when the camera got close or turned.
#include <imgui.h>
#include <glm/glm.hpp>

namespace Stardust::Gizmo {

constexpr float kNearW = 0.05f; // clip-space w (view depth) a line is cut at

inline ImVec2 ToScreen(const glm::vec4& clip, ImVec2 viewMin, ImVec2 viewSize) {
    return { viewMin.x + (clip.x / clip.w * 0.5f + 0.5f) * viewSize.x, viewMin.y + (0.5f - clip.y / clip.w * 0.5f) * viewSize.y };
}

// The visible part of the segment a-b on screen; false when it lies wholly behind the camera
inline bool ProjectSegment(const glm::mat4& viewProj, const glm::vec3& a, const glm::vec3& b, ImVec2 viewMin, ImVec2 viewSize, ImVec2& pa, ImVec2& pb) {
    glm::vec4 ca = viewProj * glm::vec4(a, 1.0f), cb = viewProj * glm::vec4(b, 1.0f);
    if (ca.w < kNearW && cb.w < kNearW) return false;
    if (ca.w < kNearW) ca = glm::mix(ca, cb, (kNearW - ca.w) / (cb.w - ca.w));
    else if (cb.w < kNearW) cb = glm::mix(cb, ca, (kNearW - cb.w) / (ca.w - cb.w));
    pa = ToScreen(ca, viewMin, viewSize);
    pb = ToScreen(cb, viewMin, viewSize);
    return true;
}

// A single point; false when it is behind (or right at) the camera
inline bool ProjectPoint(const glm::mat4& viewProj, const glm::vec3& p, ImVec2 viewMin, ImVec2 viewSize, ImVec2& out) {
    const glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
    if (c.w < kNearW) return false;
    out = ToScreen(c, viewMin, viewSize);
    return true;
}

} // namespace Stardust::Gizmo
