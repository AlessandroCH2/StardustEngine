#include "Scene/TrailRenderer.h"
#include "Scene/Scene.h"
#include "Renderer/Mesh.h"
#include "Renderer/Camera.h"
#include "Assets/AssetManager.h"
#include <nlohmann/json.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cfloat>

namespace Stardust::TrailRenderers {

using json = nlohmann::json;

void Clear(TrailRendererComponent& c) {
    c.points.clear();
    c.open = false;
}

void Update(Scene& scene, float dt) {
    auto& reg = scene.Reg();
    reg.Each<TrailRendererComponent>([&](EntityID e, TrailRendererComponent& c) {
        c.clock += dt;
        // Points older than the trail's time go, from the tail
        const float life = std::max(c.time, 0.001f);
        size_t dead = 0;
        while (dead < c.points.size() && c.clock - c.points[dead].time > life) ++dead;
        if (dead) {
            c.points.erase(c.points.begin(), c.points.begin() + (std::ptrdiff_t)dead);
            if (!c.points.empty()) c.points.front().start = true;
        }
        const bool active = reg.IsEnabled<TrailRendererComponent>(e) && scene.IsActiveInHierarchy(e);
        if (!active || !c.emitting) { c.open = false; return; }

        const glm::mat4 world = scene.GetWorldMatrix(e);
        const glm::vec3 position(world[3]);
        glm::vec3 up(world[1]);
        up = glm::length2(up) > 1e-10f ? glm::normalize(up) : glm::vec3(0, 1, 0);
        c.headPosition = position; c.headUp = up;
        const float minDistance = std::max(c.minVertexDistance, 0.001f);
        if (!c.open || c.points.empty()) {
            c.points.push_back({ position, up, c.clock, c.distance, true });
            c.open = true;
        } else if (const float d2 = glm::distance2(position, c.points.back().position); d2 >= minDistance * minDistance) {
            c.distance += std::sqrt(d2);
            c.points.push_back({ position, up, c.clock, c.distance, false });
        }
        const size_t maxPoints = (size_t)std::max(2, c.maxPoints);
        if (c.points.size() > maxPoints) {
            c.points.erase(c.points.begin(), c.points.begin() + (std::ptrdiff_t)(c.points.size() - maxPoints));
            c.points.front().start = true;
        }
    });
}

u32 BuildVertices(const TrailRendererComponent& c, const RenderCamera& camera,
                  std::vector<Vertex>& out, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    out.clear();
    if (c.points.empty()) return 0;
    const glm::vec3 camRight(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    const glm::vec3 camBack(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    const float life = std::max(c.time, 0.001f);
    const bool flat = c.alignment == 1;
    const float tile = std::max(c.tileLength, 0.001f);
    boundsMin = glm::vec3(FLT_MAX); boundsMax = glm::vec3(-FLT_MAX);
    out.reserve(c.points.size() * 4 + 8);

    struct Node { glm::vec3 position, up; float age, distance; };
    std::vector<Node> strip;
    std::vector<glm::vec3> left, right;
    auto flush = [&]() {
        const size_t n = strip.size();
        if (n < 2) { strip.clear(); return; }
        left.resize(n); right.resize(n);
        std::vector<glm::vec4> colors(n); std::vector<float> us(n); std::vector<glm::vec3> normals(n);
        const float head = strip.back().distance, tail = strip.front().distance;
        for (size_t k = 0; k < n; ++k) {
            const Node& p = strip[k];
            glm::vec3 dir = strip[std::min(k + 1, n - 1)].position - strip[k ? k - 1 : 0].position;
            dir = glm::length2(dir) > 1e-12f ? glm::normalize(dir) : glm::vec3(0, 0, 1);
            glm::vec3 normal, side;
            if (flat) { normal = p.up; side = glm::cross(p.up, dir); }
            else {
                normal = camera.orthographic ? camBack : camera.position - p.position;
                normal = glm::length2(normal) > 1e-12f ? glm::normalize(normal) : camBack;
                side = glm::cross(dir, normal);
            }
            side = glm::length2(side) > 1e-12f ? glm::normalize(side) : camRight;
            const float t = std::clamp(p.age / life, 0.0f, 1.0f);
            const float half = std::max(0.0f, c.startWidth + (c.endWidth - c.startWidth) * t) * 0.5f;
            left[k] = p.position - side * half; right[k] = p.position + side * half;
            glm::vec4 color = c.startColor + (c.endColor - c.startColor) * t;
            colors[k] = glm::vec4(glm::vec3(color) * std::max(0.0f, c.intensity), std::clamp(color.a, 0.0f, 1.0f));
            us[k] = c.textureMode == 1 ? p.distance / tile : (head - p.distance) / std::max(head - tail, 1e-4f);
            normals[k] = normal;
        }
        for (size_t k = 0; k + 1 < n; ++k) {
            if (colors[k].a <= 0.002f && colors[k + 1].a <= 0.002f) continue;
            const glm::vec3 corners[4] = { left[k], right[k], right[k + 1], left[k + 1] };
            const glm::vec2 uv[4] = { { us[k], 0 }, { us[k], 1 }, { us[k + 1], 1 }, { us[k + 1], 0 } };
            const size_t at[4] = { k, k, k + 1, k + 1 };
            for (int q = 0; q < 4; ++q) {
                out.push_back({ corners[q], normals[at[q]], uv[q], colors[at[q]] });
                boundsMin = glm::min(boundsMin, corners[q]); boundsMax = glm::max(boundsMax, corners[q]);
            }
        }
        strip.clear();
    };
    for (size_t i = 0; i < c.points.size(); ++i) {
        const TrailPoint& p = c.points[i];
        if (p.start) flush();
        strip.push_back({ p.position, p.up, c.clock - p.time, p.distance });
    }
    // The open strip reaches the object itself
    if (c.open && !strip.empty()) {
        const float extra = glm::distance(c.headPosition, strip.back().position);
        if (extra > 1e-4f) strip.push_back({ c.headPosition, c.headUp, 0.0f, strip.back().distance + extra });
    }
    flush();
    return (u32)(out.size() / 4);
}

Ref<RHI::Texture> DefaultTexture(RHI::Device& device) {
    static std::weak_ptr<RHI::Texture> cached;
    if (auto texture = cached.lock()) return texture;
    constexpr u32 size = 32;
    std::vector<u8> pixels(size * size * 4);
    for (u32 y = 0; y < size; ++y)
        for (u32 x = 0; x < size; ++x) {
            const float v = (y + 0.5f) / size;
            const float edge = std::clamp(std::min(v, 1.0f - v) / 0.18f, 0.0f, 1.0f);
            u8* px = &pixels[(y * size + x) * 4];
            px[0] = px[1] = px[2] = 255;
            px[3] = (u8)std::lround(255.0f * edge * edge * (3 - 2 * edge));
        }
    RHI::TextureDesc desc; desc.width = desc.height = size; desc.format = RHI::Format::RGBA8_UNORM; desc.name = "DefaultTrail";
    auto texture = device.CreateTexture(desc);
    device.UploadTexture(texture.get(), pixels.data(), pixels.size());
    AssetManager::SetRuntimeTexture("builtin:trail", texture); // owned by the asset manager, released at shutdown
    cached = texture;
    return texture;
}

namespace {
json Vec(glm::vec4 v) { return { v.x, v.y, v.z, v.w }; }
void ReadVec(const json& j, const char* key, glm::vec4& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 4) return;
    for (int i = 0; i < 4; ++i) if ((*it)[i].is_number()) out[i] = (*it)[i].get<float>();
}
template<typename T> void Read(const json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    if constexpr (std::is_same_v<T, bool>) { if (it->is_boolean()) out = it->get<bool>(); else if (it->is_number()) out = it->get<double>() != 0; }
    else if constexpr (std::is_same_v<T, std::string>) { if (it->is_string()) out = it->get<std::string>(); }
    else { if (it->is_number()) out = it->get<T>(); }
}
}

void ToJson(const TrailRendererComponent& c, json& j) {
    j = json::object();
    j["time"] = c.time; j["minVertexDistance"] = c.minVertexDistance;
    j["startWidth"] = c.startWidth; j["endWidth"] = c.endWidth;
    j["startColor"] = Vec(c.startColor); j["endColor"] = Vec(c.endColor);
    j["alignment"] = c.alignment; j["textureMode"] = c.textureMode; j["tileLength"] = c.tileLength;
    j["texture"] = c.texture; j["material"] = c.material; j["blendMode"] = c.blendMode; j["intensity"] = c.intensity;
    j["emitting"] = c.emitting; j["maxPoints"] = c.maxPoints; j["sortingOrder"] = c.sortingOrder;
}

void FromJson(const json& j, TrailRendererComponent& c) {
    if (!j.is_object()) return;
    Read(j, "time", c.time); Read(j, "minVertexDistance", c.minVertexDistance);
    Read(j, "startWidth", c.startWidth); Read(j, "endWidth", c.endWidth);
    ReadVec(j, "startColor", c.startColor); ReadVec(j, "endColor", c.endColor);
    Read(j, "alignment", c.alignment); Read(j, "textureMode", c.textureMode); Read(j, "tileLength", c.tileLength);
    Read(j, "texture", c.texture); Read(j, "material", c.material); Read(j, "blendMode", c.blendMode); Read(j, "intensity", c.intensity);
    Read(j, "emitting", c.emitting); Read(j, "maxPoints", c.maxPoints); Read(j, "sortingOrder", c.sortingOrder);
    c.time = std::max(c.time, 0.001f); c.minVertexDistance = std::max(c.minVertexDistance, 0.001f);
    c.alignment = std::clamp(c.alignment, 0, 1); c.textureMode = std::clamp(c.textureMode, 0, 1); c.blendMode = std::clamp(c.blendMode, 0, 1);
    c.maxPoints = std::clamp(c.maxPoints, 2, 100000);
}

} // namespace Stardust::TrailRenderers
