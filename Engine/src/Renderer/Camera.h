#pragma once
#include "Core/Base.h"
#include <glm/glm.hpp>

namespace Stardust {

// Reverse-Z infinite perspective (depth 1 at near plane, 0 at infinity), right-handed, Y-up clip space.
inline glm::mat4 PerspectiveReverseZ(float fovYRadians, float aspect, float nearPlane,float farPlane=0) {
    float f = 1.0f / std::tan(fovYRadians * 0.5f);
    glm::mat4 p(0.0f);
    p[0][0] = f / aspect;
    p[1][1] = f;
    p[2][3] = -1.0f;
    p[3][2] = nearPlane;if(farPlane>nearPlane){p[2][2]=nearPlane/(farPlane-nearPlane);p[3][2]=farPlane*nearPlane/(farPlane-nearPlane);}
    return p;
}

// Reverse-Z orthographic (near -> 1, far -> 0), used by shadow cascades
inline glm::mat4 OrthoReverseZ(float l, float r, float b, float t, float n, float f) {
    glm::mat4 p(1.0f);
    p[0][0] = 2.0f / (r - l);
    p[1][1] = 2.0f / (t - b);
    p[2][2] = 1.0f / (f - n);
    p[3][0] = -(r + l) / (r - l);
    p[3][1] = -(t + b) / (t - b);
    p[3][2] = f / (f - n);
    return p;
}

// Side planes of a view frustum (the reverse-Z infinite projection has no usable far plane;
// the near plane is irrelevant for culling at our scales).
struct Frustum {
    glm::vec4 planes[4]{};

    static Frustum FromViewProj(const glm::mat4& m) {
        auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
        Frustum f;
        f.planes[0] = row(3) + row(0); // left
        f.planes[1] = row(3) - row(0); // right
        f.planes[2] = row(3) + row(1); // bottom
        f.planes[3] = row(3) - row(1); // top
        for (auto& p : f.planes) p /= glm::length(glm::vec3(p));
        return f;
    }
    bool Sphere(const glm::vec3& c, float r) const {
        for (auto& p : planes) if (glm::dot(glm::vec3(p), c) + p.w < -r) return false;
        return true;
    }
    bool Box(const glm::vec3& mn, const glm::vec3& mx) const {
        for (auto& p : planes) {
            glm::vec3 v(p.x > 0 ? mx.x : mn.x, p.y > 0 ? mx.y : mn.y, p.z > 0 ? mx.z : mn.z);
            if (glm::dot(glm::vec3(p), v) + p.w < 0) return false;
        }
        return true;
    }
};

struct RenderCamera {
    u32 cullingMask=~0u;
    bool solidColor=false; glm::vec4 background{0,0,0,1};
    bool shadows=true, postProcessing=true;
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::vec3 position{ 0.0f };
    bool orthographic=false;float orthographicSize=5,farPlane=1000;
    float nearPlane = 0.1f;
    float fovY = glm::radians(60.0f);
};

} // namespace Stardust
