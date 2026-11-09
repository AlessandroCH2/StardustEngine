#pragma once
#include "Renderer/Camera.h"
#include <glm/glm.hpp>

namespace Stardust {

// Unity-style scene camera: hold right mouse to look around and fly with WASD/QE, scroll to dolly,
// middle mouse to pan, F to frame the selection.
class EditorCamera {
public:
    void Update(float dt, bool viewportHovered, bool viewportFocused);
    void Focus(const glm::vec3& target, float distance = 5.0f);
    void Frame(const glm::vec3& target,float extent){Focus(target,std::max(2.f,extent*1.3f));if(m_2D)m_OrthoSize=std::max(.5f,extent*.65f);}
    void SetFromView(const glm::mat4& view); // e.g. after the view cube rotated the camera

    RenderCamera GetRenderCamera(float aspect) const;
    glm::mat4 View() const;
    glm::mat4 GizmoProjection(float aspect) const; // finite, non-reversed projection for ImGuizmo

    glm::vec3 Position() const { return m_Position; }
    glm::vec3 Forward() const;
    bool IsFlying() const { return m_Flying; }
    bool Is2D() const { return m_2D; }
    void Set2D(bool value) { m_2D = value; }

    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float moveSpeed = 8.0f;

private:
    glm::vec3 m_Position{ 0.0f, 2.2f, 14.0f };
    float m_Yaw = 0.0f, m_Pitch = 2.0f; // degrees
    bool m_Flying = false, m_Panning = false;
    bool m_2D = false;
    float m_OrthoSize = 10.0f;
};

} // namespace Stardust
