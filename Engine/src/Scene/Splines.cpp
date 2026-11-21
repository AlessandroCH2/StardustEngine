#include "Scene/Splines.h"
#include "Scene/Scene.h"
#include "Renderer/Mesh.h"
#include "Renderer/Camera.h"
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <cfloat>

namespace Stardust::Splines {

static int SegmentCount(const SplineComponent& c) {
    const int n = (int)c.knots.size();
    return n < 2 ? 0 : c.closed ? n : n - 1;
}
static const SplineKnot& Knot(const SplineComponent& c, int i) {
    const int n = (int)c.knots.size();
    return c.knots[c.closed ? ((i % n) + n) % n : std::clamp(i, 0, n - 1)];
}
// Control points of segment s (cubic Bezier)
static void Controls(const SplineComponent& c, int s, glm::vec3& p0, glm::vec3& p1, glm::vec3& p2, glm::vec3& p3) {
    const SplineKnot& a = Knot(c, s), & b = Knot(c, s + 1);
    p0 = a.position; p3 = b.position;
    if (c.mode == 2) { p1 = p0 + (p3 - p0) / 3.0f; p2 = p0 + (p3 - p0) * (2.0f / 3.0f); return; }
    if (c.mode == 1) { p1 = p0 + a.tangentOut; p2 = p3 + b.tangentIn; return; }
    // Auto: Catmull-Rom through the neighbours (the ends mirror their neighbour)
    const int n = (int)c.knots.size();
    glm::vec3 prev = (s > 0 || c.closed) ? Knot(c, s - 1).position : p0 * 2.0f - p3;
    glm::vec3 next = (s + 2 < n || c.closed) ? Knot(c, s + 2).position : p3 * 2.0f - p0;
    p1 = p0 + (p3 - prev) / 6.0f;
    p2 = p3 - (next - p0) / 6.0f;
}
static void Locate(const SplineComponent& c, float t, int& s, float& u) {
    const int segments = SegmentCount(c);
    t = c.closed ? t - std::floor(t) : std::clamp(t, 0.0f, 1.0f);
    const float x = t * segments;
    s = std::min((int)x, segments - 1); u = x - s;
}

glm::vec3 Position(const SplineComponent& c, float t) {
    if (c.knots.empty()) return glm::vec3(0);
    if (SegmentCount(c) == 0) return c.knots[0].position;
    int s; float u; Locate(c, t, s, u);
    glm::vec3 p0, p1, p2, p3; Controls(c, s, p0, p1, p2, p3);
    const float v = 1 - u;
    return v * v * v * p0 + 3 * v * v * u * p1 + 3 * v * u * u * p2 + u * u * u * p3;
}

glm::vec3 Tangent(const SplineComponent& c, float t) {
    if (SegmentCount(c) == 0) return glm::vec3(0, 0, 1);
    int s; float u; Locate(c, t, s, u);
    glm::vec3 p0, p1, p2, p3; Controls(c, s, p0, p1, p2, p3);
    const float v = 1 - u;
    glm::vec3 d = 3 * v * v * (p1 - p0) + 6 * v * u * (p2 - p1) + 3 * u * u * (p3 - p2);
    if (glm::length2(d) < 1e-10f) d = p3 - p0;
    return glm::length2(d) < 1e-10f ? glm::vec3(0, 0, 1) : glm::normalize(d);
}

glm::vec3 Up(const SplineComponent& c, float t) {
    glm::vec3 tangent = Tangent(c, t);
    glm::vec3 side = glm::cross(glm::vec3(0, 1, 0), tangent);
    if (glm::length2(side) < 1e-8f) side = glm::vec3(1, 0, 0);
    glm::vec3 up = glm::normalize(glm::cross(tangent, glm::normalize(side)));
    float roll = 0;
    if (SegmentCount(c) > 0) { int s; float u; Locate(c, t, s, u); roll = glm::mix(Knot(c, s).roll, Knot(c, s + 1).roll, u); }
    return glm::angleAxis(glm::radians(roll), tangent) * up;
}

// Cumulative lengths at evenly spaced parameters
static void Table(const SplineComponent& c, std::vector<float>& lengths, int steps) {
    lengths.assign(steps + 1, 0.0f);
    glm::vec3 prev = Position(c, 0.0f);
    for (int i = 1; i <= steps; ++i) { glm::vec3 p = Position(c, (float)i / steps); lengths[i] = lengths[i - 1] + glm::length(p - prev); prev = p; }
}
static int Steps(const SplineComponent& c) { return std::max(16, SegmentCount(c) * 32); }

float Length(const SplineComponent& c) { std::vector<float> l; Table(c, l, Steps(c)); return l.back(); }

float ParameterAtDistance(const SplineComponent& c, float distance) {
    std::vector<float> l; const int steps = Steps(c); Table(c, l, steps);
    const float total = l.back();
    if (total <= 1e-6f) return 0.0f;
    distance = c.closed ? distance - std::floor(distance / total) * total : std::clamp(distance, 0.0f, total);
    auto it = std::lower_bound(l.begin(), l.end(), distance);
    const int i = std::clamp((int)(it - l.begin()), 1, steps);
    const float span = l[i] - l[i - 1];
    return ((i - 1) + (span > 1e-6f ? (distance - l[i - 1]) / span : 0.0f)) / steps;
}

float Nearest(const SplineComponent& c, const glm::vec3& p, glm::vec3* point) {
    const int steps = Steps(c) * 2;
    float bestT = 0, best = FLT_MAX;
    for (int i = 0; i <= steps; ++i) { float t = (float)i / steps; float d = glm::length2(Position(c, t) - p); if (d < best) { best = d; bestT = t; } }
    // Refine around the best sample
    float step = 1.0f / steps;
    for (int k = 0; k < 12; ++k) {
        step *= 0.5f;
        for (float t : { bestT - step, bestT + step }) { float tt = c.closed ? t : std::clamp(t, 0.0f, 1.0f); float d = glm::length2(Position(c, tt) - p); if (d < best) { best = d; bestT = tt; } }
    }
    if (point) *point = Position(c, bestT);
    return bestT;
}

void Sample(const SplineComponent& c, std::vector<glm::vec3>& out, int perSegment) {
    out.clear();
    const int segments = SegmentCount(c);
    if (segments == 0) { for (auto& k : c.knots) out.push_back(k.position); return; }
    const int steps = segments * std::max(1, c.mode == 2 ? 1 : perSegment);
    for (int i = 0; i <= steps; ++i) out.push_back(Position(c, (float)i / steps));
}

void AutoTangents(SplineComponent& c) {
    const int n = (int)c.knots.size();
    for (int i = 0; i < n; ++i) {
        glm::vec3 prev = (i > 0 || c.closed) ? Knot(c, i - 1).position : c.knots[i].position;
        glm::vec3 next = (i + 1 < n || c.closed) ? Knot(c, i + 1).position : c.knots[i].position;
        glm::vec3 d = (next - prev) / 6.0f;
        c.knots[i].tangentOut = d; c.knots[i].tangentIn = -d;
    }
    c.mode = 1;
}

void Update(Scene& scene, float dt) {
    Registry& reg = scene.Reg();
    reg.Each<SplineComponent>([&](EntityID e, SplineComponent& c) {
        if (!c.follower || c.followSpeed == 0.0f || !reg.IsEnabled<SplineComponent>(e) || !scene.IsActiveInHierarchy(e)) return;
        EntityID f = scene.FindByUUID(c.follower);
        if (!f || f == e) return;
        const float length = Length(c);
        if (length <= 1e-4f) return;
        c.followDistance += c.followSpeed * dt * c.followDirection;
        if (!c.closed || c.followLoop != 0) {
            if (c.followLoop == 0) c.followDistance = c.followDistance - std::floor(c.followDistance / length) * length;
            else if (c.followLoop == 1) { if (c.followDistance > length) { c.followDistance = 2 * length - c.followDistance; c.followDirection = -c.followDirection; } else if (c.followDistance < 0) { c.followDistance = -c.followDistance; c.followDirection = -c.followDirection; } }
            else c.followDistance = std::clamp(c.followDistance, 0.0f, length);
        }
        const float t = ParameterAtDistance(c, c.followDistance);
        const glm::mat4 world = scene.GetWorldMatrix(e);
        glm::vec3 position = glm::vec3(world * glm::vec4(Position(c, t), 1.0f));
        glm::mat4 m = scene.GetWorldMatrix(f);
        glm::vec3 scale, translation, skew; glm::vec4 persp; glm::quat rot;
        glm::decompose(m, scale, rot, translation, skew, persp);
        if (c.alignToPath) {
            glm::vec3 forward = glm::normalize(glm::mat3(world) * Tangent(c, t)) * (float)(c.followSpeed * c.followDirection < 0 ? -1 : 1);
            glm::vec3 up = glm::normalize(glm::mat3(world) * Up(c, t));
            rot = glm::quatLookAt(forward, up); // the engine's forward is -Z
        }
        m = glm::translate(glm::mat4(1), position) * glm::toMat4(rot) * glm::scale(glm::mat4(1), scale);
        scene.SetWorldMatrix(f, m);
    });
}

} // namespace Stardust::Splines

namespace Stardust::LineRenderers {

u32 BuildVertices(const LineRendererComponent& c, const glm::mat4& world, const RenderCamera& camera,
                  std::vector<Vertex>& out, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    out.clear();
    if (c.positions.size() < 2) return 0;
    std::vector<glm::vec3> pts;
    pts.reserve(c.positions.size() + 1);
    for (auto& p : c.positions) pts.push_back(c.useWorldSpace ? p : glm::vec3(world * glm::vec4(p, 1.0f)));
    if (c.loop) pts.push_back(pts.front());
    // Rounded corners: each inner point becomes a short curve
    const float maxWidth = std::max(c.startWidth, c.endWidth) * 0.5f;
    if (c.cornerVertices > 0 && pts.size() > 2) {
        std::vector<glm::vec3> rounded{ pts.front() };
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            glm::vec3 a = pts[i - 1], p = pts[i], b = pts[i + 1];
            float r = std::min({ maxWidth, glm::length(p - a) * 0.5f, glm::length(b - p) * 0.5f });
            glm::vec3 in = p + glm::normalize(a - p + glm::vec3(1e-9f)) * r, outP = p + glm::normalize(b - p + glm::vec3(1e-9f)) * r;
            for (int k = 0; k <= c.cornerVertices + 1; ++k) { float u = (float)k / (c.cornerVertices + 1); float v = 1 - u; rounded.push_back(v * v * in + 2 * v * u * p + u * u * outP); }
        }
        rounded.push_back(pts.back());
        pts.swap(rounded);
    }
    const size_t n = pts.size();
    std::vector<float> along(n, 0.0f);
    for (size_t i = 1; i < n; ++i) along[i] = along[i - 1] + glm::length(pts[i] - pts[i - 1]);
    const float total = std::max(along.back(), 1e-5f);
    const glm::vec3 camBack(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    const glm::vec3 localZ = glm::length2(glm::vec3(world[2])) > 1e-12f ? glm::normalize(glm::vec3(world[2])) : glm::vec3(0, 0, 1);
    const float tile = std::max(c.tileLength, 0.001f);
    std::vector<glm::vec3> left(n), right(n), normals(n);
    std::vector<glm::vec4> colors(n);
    std::vector<float> us(n);
    for (size_t k = 0; k < n; ++k) {
        glm::vec3 dir = pts[std::min(k + 1, n - 1)] - pts[k ? k - 1 : 0];
        if (c.loop && (k == 0 || k == n - 1)) dir = pts[1] - pts[n - 2];
        dir = glm::length2(dir) > 1e-12f ? glm::normalize(dir) : glm::vec3(0, 0, 1);
        glm::vec3 normal = c.alignment == 1 ? localZ : (camera.orthographic ? camBack : camera.position - pts[k]);
        normal = glm::length2(normal) > 1e-12f ? glm::normalize(normal) : camBack;
        glm::vec3 side = glm::cross(dir, normal);
        side = glm::length2(side) > 1e-12f ? glm::normalize(side) : glm::vec3(1, 0, 0);
        const float t = along[k] / total;
        const float half = std::max(0.0f, c.startWidth + (c.endWidth - c.startWidth) * t) * 0.5f;
        left[k] = pts[k] - side * half; right[k] = pts[k] + side * half;
        glm::vec4 color = c.startColor + (c.endColor - c.startColor) * t;
        colors[k] = glm::vec4(glm::vec3(color) * std::max(0.0f, c.intensity), std::clamp(color.a, 0.0f, 1.0f));
        us[k] = c.textureMode == 1 ? along[k] / tile : t;
        normals[k] = normal;
    }
    boundsMin = glm::vec3(FLT_MAX); boundsMax = glm::vec3(-FLT_MAX);
    out.reserve(n * 4);
    for (size_t k = 0; k + 1 < n; ++k) {
        const glm::vec3 corners[4] = { left[k], right[k], right[k + 1], left[k + 1] };
        const glm::vec2 uv[4] = { { us[k], 0 }, { us[k], 1 }, { us[k + 1], 1 }, { us[k + 1], 0 } };
        const size_t at[4] = { k, k, k + 1, k + 1 };
        for (int q = 0; q < 4; ++q) {
            out.push_back({ corners[q], normals[at[q]], uv[q], colors[at[q]] });
            boundsMin = glm::min(boundsMin, corners[q]); boundsMax = glm::max(boundsMax, corners[q]);
        }
    }
    return (u32)(out.size() / 4);
}

} // namespace Stardust::LineRenderers
