#include "EditorCamera.h"
#include "Core/Input.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Stardust {

glm::vec3 EditorCamera::Forward() const {
    if (m_2D) return {0, 0, -1};
    float y = glm::radians(m_Yaw), p = glm::radians(m_Pitch);
    return glm::normalize(glm::vec3(std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)));
}

glm::mat4 EditorCamera::View() const {
    return glm::lookAt(m_Position, m_Position + Forward(), glm::vec3(0, 1, 0));
}

RenderCamera EditorCamera::GetRenderCamera(float aspect) const {
    RenderCamera c;
    c.view = View();
    c.fovY = glm::radians(fovDegrees);
    c.nearPlane = nearPlane;
    c.projection = PerspectiveReverseZ(c.fovY, aspect, nearPlane);
    if (m_2D) c.projection = glm::ortho(-m_OrthoSize * aspect, m_OrthoSize * aspect, -m_OrthoSize, m_OrthoSize, 5000.0f, -5000.0f);
    c.position = m_Position;
    return c;
}

glm::mat4 EditorCamera::GizmoProjection(float aspect) const {
    if (m_2D) return glm::ortho(-m_OrthoSize * aspect, m_OrthoSize * aspect, -m_OrthoSize, m_OrthoSize, -5000.0f, 5000.0f);
    return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, 5000.0f);
}

void EditorCamera::SetFromView(const glm::mat4& view) {
    glm::mat4 inv = glm::inverse(view);
    m_Position = glm::vec3(inv[3]);
    glm::vec3 f = glm::normalize(-glm::vec3(inv[2]));
    m_Pitch = glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f)));
    m_Yaw = glm::degrees(std::atan2(f.x, -f.z));
}

void EditorCamera::Focus(const glm::vec3& target, float distance) {
    m_Position = target - Forward() * distance;
}

void EditorCamera::Update(float dt, bool hovered, bool focused) {
    if (m_2D) {
        if (m_Flying) { m_Flying = false; Input::SetCursorMode(CursorMode::Normal); }
        if (hovered) {
            m_OrthoSize = std::clamp(m_OrthoSize * std::pow(0.85f, Input::ScrollDelta()), 0.05f, 5000.0f);
            if (Input::IsMouseDown(MouseButton::Middle) || Input::IsMouseDown(MouseButton::Right)) {
                auto d = Input::MouseDelta();
                m_Position += glm::vec3(-d.x, d.y, 0) * m_OrthoSize * 0.002f;
            }
        }
        return;
    }
    bool rmb = Input::IsMouseDown(MouseButton::Right);
    bool mmb = Input::IsMouseDown(MouseButton::Middle);

    if (!m_Flying && rmb && hovered) { m_Flying = true; Input::SetCursorMode(CursorMode::Locked); }
    if (m_Flying && !rmb) { m_Flying = false; Input::SetCursorMode(CursorMode::Normal); }
    if (!m_Panning && mmb && hovered) m_Panning = true;
    if (m_Panning && !mmb) m_Panning = false;

    glm::vec2 md = Input::MouseDelta();
    glm::vec3 fwd = Forward();
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::cross(right, fwd);

    if (m_Flying) {
        m_Yaw += md.x * 0.15f;
        m_Pitch = std::clamp(m_Pitch - md.y * 0.15f, -89.0f, 89.0f);

        float speed = moveSpeed * (Input::IsKeyDown(Key::LeftShift) ? 3.0f : 1.0f);
        glm::vec3 move(0.0f);
        if (Input::IsKeyDown(Key::W)) move += fwd;
        if (Input::IsKeyDown(Key::S)) move -= fwd;
        if (Input::IsKeyDown(Key::D)) move += right;
        if (Input::IsKeyDown(Key::A)) move -= right;
        if (Input::IsKeyDown(Key::E)) move += glm::vec3(0, 1, 0);
        if (Input::IsKeyDown(Key::Q)) move -= glm::vec3(0, 1, 0);
        if (glm::length(move) > 0) m_Position += glm::normalize(move) * speed * dt;

        // Scroll while flying changes speed (Unity behaviour)
        float s = Input::ScrollDelta();
        if (s != 0) moveSpeed = std::clamp(moveSpeed * (s > 0 ? 1.2f : 1.0f / 1.2f), 0.5f, 500.0f);
    } else if (hovered) {
        float s = Input::ScrollDelta();
        if (s != 0) m_Position += fwd * s * std::max(1.0f, moveSpeed * 0.25f);
    }

    if (m_Panning) {
        float k = 0.01f * std::max(1.0f, moveSpeed * 0.2f);
        m_Position += (-right * md.x + up * md.y) * k;
    }
}

} // namespace Stardust
