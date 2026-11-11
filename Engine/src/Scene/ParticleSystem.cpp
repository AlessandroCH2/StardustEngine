#include "Scene/ParticleSystem.h"
#include "Scene/Scene.h"
#include "Scene/ComponentState.h"
#include "Renderer/Mesh.h"
#include "Renderer/Camera.h"
#include <nlohmann/json.hpp>
#include "Assets/AssetManager.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
#include <cfloat>
#include <algorithm>
#include <random>

namespace Stardust::ParticleSystems {

using json = nlohmann::json;

namespace {

// xorshift32: cheap, and a fixed seed replays the same effect
float Random(ParticleSystemComponent& c) {
    u32 x = c.random ? c.random : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c.random = x;
    return (x >> 8) * (1.0f / 16777216.0f);
}
float Range(ParticleSystemComponent& c, const ParticleRange& r) { return r.min == r.max ? r.min : r.min + (r.max - r.min) * Random(c); }
glm::vec3 RandomUnit(ParticleSystemComponent& c) {
    const float z = Random(c) * 2 - 1, a = Random(c) * glm::two_pi<float>(), r = std::sqrt(std::max(0.0f, 1 - z * z));
    return { r * std::cos(a), z, r * std::sin(a) };
}

glm::vec4 EvaluateColor(const std::vector<ParticleColorKey>& keys, float t) {
    if (keys.empty()) return glm::vec4(1);
    if (t <= keys.front().time) return keys.front().color;
    for (size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].time) {
            const float span = keys[i].time - keys[i - 1].time;
            return glm::mix(keys[i - 1].color, keys[i].color, span > 0 ? (t - keys[i - 1].time) / span : 1.0f);
        }
    return keys.back().color;
}
float EvaluateFloat(const std::vector<ParticleFloatKey>& keys, float t) {
    if (keys.empty()) return 1;
    if (t <= keys.front().time) return keys.front().value;
    for (size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].time) {
            const float span = keys[i].time - keys[i - 1].time;
            return glm::mix(keys[i - 1].value, keys[i].value, span > 0 ? (t - keys[i - 1].time) / span : 1.0f);
        }
    return keys.back().value;
}

// Smooth value noise in [-1, 1]
float Hash(int x, int y, int z) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + (u32)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 8388607.5f - 1.0f;
}
float ValueNoise(glm::vec3 p) {
    const glm::vec3 f = glm::floor(p), t = p - f, s = t * t * (3.0f - 2.0f * t);
    const int x = (int)f.x, y = (int)f.y, z = (int)f.z;
    auto lerp = [](float a, float b, float k) { return a + (b - a) * k; };
    return lerp(lerp(lerp(Hash(x, y, z), Hash(x + 1, y, z), s.x), lerp(Hash(x, y + 1, z), Hash(x + 1, y + 1, z), s.x), s.y),
                lerp(lerp(Hash(x, y, z + 1), Hash(x + 1, y, z + 1), s.x), lerp(Hash(x, y + 1, z + 1), Hash(x + 1, y + 1, z + 1), s.x), s.y), s.z);
}
glm::vec3 NoiseVector(glm::vec3 p) {
    return { ValueNoise(p), ValueNoise(p + glm::vec3(31.7f, 11.3f, 47.1f)), ValueNoise(p + glm::vec3(-19.1f, 53.9f, 7.7f)) };
}

// Position and direction of a new particle in the emitter's local space
void SampleShape(ParticleSystemComponent& c, glm::vec3& position, glm::vec3& direction) {
    position = glm::vec3(0); direction = { 0, 1, 0 };
    if (c.shapeEnabled) {
        const float thickness = std::clamp(c.radiusThickness, 0.0f, 1.0f);
        const float arc = glm::radians(std::clamp(c.arc, 0.0f, 360.0f));
        switch (c.shape) {
        case ParticleShape::Sphere:
        case ParticleShape::Hemisphere: {
            direction = RandomUnit(c);
            if (c.shape == ParticleShape::Hemisphere) direction.y = std::abs(direction.y);
            position = direction * c.radius * glm::mix(1.0f, std::cbrt(Random(c)), thickness);
            break;
        }
        case ParticleShape::Cone: {
            const float a = Random(c) * arc, k = glm::mix(1.0f, std::sqrt(Random(c)), thickness);
            const glm::vec2 ring(std::cos(a), std::sin(a));
            position = glm::vec3(ring.x, 0, ring.y) * c.radius * k;
            // Directions open with the distance from the centre, up to the cone angle at the rim
            const float tilt = glm::radians(std::clamp(c.angle, 0.0f, 89.9f)) * (c.radius > 0 ? k : Random(c));
            direction = { std::sin(tilt) * ring.x, std::cos(tilt), std::sin(tilt) * ring.y };
            break;
        }
        case ParticleShape::Box:
            position = (glm::vec3(Random(c), Random(c), Random(c)) - 0.5f) * c.boxSize;
            break;
        case ParticleShape::Circle: {
            const float a = Random(c) * arc;
            const glm::vec3 ring(std::cos(a), 0, std::sin(a));
            position = ring * c.radius * glm::mix(1.0f, std::sqrt(Random(c)), thickness);
            direction = ring;
            break;
        }
        case ParticleShape::Edge:
            position = { (Random(c) * 2 - 1) * c.radius, 0, 0 };
            break;
        case ParticleShape::Point: break;
        }
        position += c.shapeOffset;
    }
    if (c.randomizeDirection > 0) direction = glm::normalize(glm::mix(direction, RandomUnit(c), std::clamp(c.randomizeDirection, 0.0f, 1.0f)) + glm::vec3(0, 1e-5f, 0));
}

void Spawn(ParticleSystemComponent& c, const glm::mat4& world, int count) {
    count = std::min(count, std::max(0, c.maxParticles) - (int)c.particles.size());
    for (int i = 0; i < count; ++i) {
        Particle p;
        glm::vec3 position, direction;
        SampleShape(c, position, direction);
        const float speed = Range(c, c.startSpeed);
        if (c.simulationSpace == 1) {
            p.position = glm::vec3(world * glm::vec4(position, 1));
            const glm::vec3 d = glm::vec3(world * glm::vec4(direction, 0));
            p.velocity = glm::length(d) > 0 ? glm::normalize(d) * speed : glm::vec3(0);
        } else {
            p.position = position;
            p.velocity = direction * speed;
        }
        p.color = glm::mix(c.startColor, c.startColor2, Random(c));
        p.size = std::max(0.0f, Range(c, c.startSize));
        p.rotation = glm::radians(Range(c, c.startRotation));
        p.angularVelocity = glm::radians(Range(c, c.angularVelocity));
        p.lifetime = std::max(0.001f, Range(c, c.startLifetime));
        p.seed = Random(c);
        c.particles.push_back(p);
    }
}

// Bursts whose time falls in [from, to) of the current cycle
void FireBursts(ParticleSystemComponent& c, const glm::mat4& world, float from, float to) {
    for (const auto& b : c.bursts) {
        const int cycles = b.cycles <= 0 ? 1000000 : b.cycles;
        for (int k = 0; k < cycles; ++k) {
            const float t = b.time + k * std::max(0.001f, b.interval);
            if (t >= to || t >= c.duration) break;
            if (t >= from && (b.probability >= 1 || Random(c) < b.probability)) Spawn(c, world, b.count);
        }
    }
}

void Start(ParticleSystemComponent& c) {
    c.started = true;
    c.particles.clear();
    c.time = 0; c.emitAccumulator = 0; c.hasLastPosition = false;
    c.random = c.seed ? c.seed : (u32)std::random_device{}() | 1u;
    c.playing = c.emitting = c.playOnAwake;
    c.paused = false;
    c.delayLeft = Range(c, c.startDelay);
}

void Step(Scene& scene, EntityID e, ParticleSystemComponent& c, float dt) {
    const glm::mat4 world = scene.GetWorldMatrix(e);
    const glm::vec3 emitterPosition(world[3]);
    const bool local = c.simulationSpace == 0;
    const glm::mat4 toLocal = local ? glm::inverse(world) : glm::mat4(1);

    // Emission
    if (c.emitting && c.emission) {
        if (c.delayLeft > 0) c.delayLeft -= dt;
        if (c.delayLeft <= 0) {
            const float from = c.time;
            c.time += dt;
            float moved = c.hasLastPosition ? glm::distance(emitterPosition, c.lastPosition) : 0.0f;
            c.emitAccumulator += std::max(0.0f, c.rateOverTime) * dt + std::max(0.0f, c.rateOverDistance) * moved;
            const int count = (int)c.emitAccumulator;
            c.emitAccumulator -= count;
            Spawn(c, world, count);
            const float duration = std::max(0.05f, c.duration);
            FireBursts(c, world, from, std::min(c.time, duration));
            if (c.time >= duration) {
                if (c.looping) { c.time = std::fmod(c.time - duration, duration); FireBursts(c, world, 0, c.time); }
                else { c.time = duration; c.emitting = false; }
            }
        }
    } else if (c.emitting) {
        // Emission module off: the cycle still runs so a non-looping system ends
        c.time += dt;
        if (c.time >= c.duration) { if (c.looping) c.time = std::fmod(c.time, std::max(0.05f, c.duration)); else c.emitting = false; }
    }
    c.lastPosition = emitterPosition; c.hasLastPosition = true;

    // Forces in simulation space
    glm::vec3 gravity(0, -9.81f * c.gravityModifier, 0);
    glm::vec3 force = c.forceOverLifetime ? c.force : glm::vec3(0);
    if (local) gravity = glm::vec3(toLocal * glm::vec4(gravity, 0));
    const float dragFactor = c.limitVelocity ? std::max(0.0f, 1.0f - c.drag * dt) : 1.0f;
    const float noiseTime = c.time * c.noiseScrollSpeed;

    for (size_t i = 0; i < c.particles.size();) {
        Particle& p = c.particles[i];
        p.age += dt;
        if (p.age >= p.lifetime) { p = c.particles.back(); c.particles.pop_back(); continue; }
        p.velocity += (gravity + force) * dt;
        if (c.limitVelocity) {
            p.velocity *= dragFactor;
            const float speed = glm::length(p.velocity);
            if (speed > c.speedLimit && speed > 0) p.velocity *= c.speedLimit / speed;
        }
        glm::vec3 motion = p.velocity;
        if (c.velocityOverLifetime) motion += c.velocity;
        if (c.noise && c.noiseStrength != 0)
            motion += NoiseVector(p.position * c.noiseFrequency + glm::vec3(noiseTime, p.seed * 17.0f, -noiseTime)) * c.noiseStrength;
        p.position += motion * dt;
        p.rotation += (c.rotationOverLifetime ? p.angularVelocity : 0.0f) * dt;
        if (c.collision) {
            glm::vec3 wp = local ? glm::vec3(world * glm::vec4(p.position, 1)) : p.position;
            if (wp.y < c.collisionPlane) {
                glm::vec3 wv = local ? glm::vec3(world * glm::vec4(p.velocity, 0)) : p.velocity;
                wp.y = c.collisionPlane;
                if (wv.y < 0) wv.y = -wv.y * c.bounce;
                wv.x *= 1 - std::clamp(c.dampen, 0.0f, 1.0f); wv.z *= 1 - std::clamp(c.dampen, 0.0f, 1.0f);
                p.age += p.lifetime * std::clamp(c.lifetimeLoss, 0.0f, 1.0f);
                p.position = local ? glm::vec3(toLocal * glm::vec4(wp, 1)) : wp;
                p.velocity = local ? glm::vec3(toLocal * glm::vec4(wv, 0)) : wv;
            }
        }
        ++i;
    }
    if (!c.emitting && c.particles.empty()) c.playing = false;
}

} // namespace

void ResetRuntime(ParticleSystemComponent& c) {
    c.particles.clear();
    c.started = false; c.playing = c.paused = c.emitting = false;
    c.time = c.emitAccumulator = c.delayLeft = 0;
    c.hasLastPosition = false;
}

void Simulate(Scene& scene, EntityID e, ParticleSystemComponent& c, float dt) {
    if (!c.started) {
        Start(c);
        // Prewarm: a looping system starts as if it had already run one cycle
        if (c.playing && c.prewarm && c.looping) {
            const float stepSize = 1.0f / 30.0f;
            for (float t = 0; t < c.duration; t += stepSize) Step(scene, e, c, stepSize);
        }
    }
    if (!c.playing || c.paused) return;
    dt *= std::max(0.0f, c.simulationSpeed);
    // Long frames are split so fast particles still collide and bursts keep their order
    while (dt > 0) { const float step = std::min(dt, 0.05f); Step(scene, e, c, step); dt -= step; }
}

void Update(Scene& scene, float dt) {
    scene.Reg().Each<ParticleSystemComponent>([&](EntityID e, ParticleSystemComponent& c) {
        if (!scene.Reg().IsEnabled<ParticleSystemComponent>(e) || !scene.IsActiveInHierarchy(e)) return;
        Simulate(scene, e, c, dt);
    });
}

void Play(ParticleSystemComponent& c) {
    if (!c.started) { Start(c); }
    if (c.paused) { c.paused = false; return; }
    if (!c.emitting) { c.time = 0; c.emitAccumulator = 0; c.delayLeft = Range(c, c.startDelay); }
    c.playing = c.emitting = true;
}
void Pause(ParticleSystemComponent& c) { if (c.playing) c.paused = true; }
void Stop(ParticleSystemComponent& c, bool clear) {
    if (!c.started) Start(c);
    c.emitting = false; c.paused = false;
    if (clear) { c.particles.clear(); c.playing = false; }
}
void Clear(ParticleSystemComponent& c) { c.particles.clear(); }
void Emit(Scene& scene, EntityID e, ParticleSystemComponent& c, int count) {
    if (!c.started) { Start(c); c.playing = c.emitting = false; }
    Spawn(c, scene.GetWorldMatrix(e), std::max(0, count));
    c.playing = true; c.paused = false;
}

u32 BuildVertices(Scene& scene, EntityID e, const ParticleSystemComponent& c, const RenderCamera& camera,
                  std::vector<Vertex>& out, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    out.clear();
    if (c.particles.empty()) return 0;
    const glm::mat4 world = scene.GetWorldMatrix(e);
    const bool local = c.simulationSpace == 0;
    const float scale = local ? std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) }) : 1.0f;
    // Camera axes from the view matrix rows
    const glm::vec3 camRight(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    const glm::vec3 camUp(camera.view[0][1], camera.view[1][1], camera.view[2][1]);
    const glm::vec3 camBack(camera.view[0][2], camera.view[1][2], camera.view[2][2]);

    std::vector<u32> order(c.particles.size());
    for (u32 i = 0; i < (u32)order.size(); ++i) order[i] = i;
    std::vector<glm::vec3> positions(c.particles.size());
    for (size_t i = 0; i < c.particles.size(); ++i)
        positions[i] = local ? glm::vec3(world * glm::vec4(c.particles[i].position, 1)) : c.particles[i].position;
    if (c.sortByDistance) {
        std::vector<float> depth(order.size());
        for (size_t i = 0; i < order.size(); ++i)
            depth[i] = camera.orthographic ? glm::dot(positions[i], -camBack) : glm::distance2(positions[i], camera.position);
        if (camera.orthographic) std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return depth[a] < depth[b]; });
        else std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return depth[a] > depth[b]; });
    }

    const int tilesX = c.textureSheet ? std::max(1, c.tilesX) : 1, tilesY = c.textureSheet ? std::max(1, c.tilesY) : 1;
    const int frames = tilesX * tilesY;
    out.reserve(order.size() * 4);
    boundsMin = glm::vec3(FLT_MAX); boundsMax = glm::vec3(-FLT_MAX);
    for (u32 index : order) {
        const Particle& p = c.particles[index];
        const float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
        float size = p.size * scale * (c.sizeOverLifetime ? std::max(0.0f, EvaluateFloat(c.sizeKeys, t)) : 1.0f);
        if (size <= 0) continue;
        glm::vec4 color = p.color * (c.colorOverLifetime ? EvaluateColor(c.colorKeys, t) : glm::vec4(1));
        color = glm::vec4(glm::vec3(color) * std::max(0.0f, c.intensity), std::clamp(color.a, 0.0f, 1.0f));
        if (color.a <= 0.002f) continue;
        const glm::vec3 center = positions[index];

        // Frame of the texture sheet
        glm::vec2 uvMin(0), uvMax(1);
        if (frames > 1) {
            int frame = c.randomStartFrame ? (int)(p.seed * frames) : 0;
            frame += c.sheetFps > 0 ? (int)(p.age * c.sheetFps) : (int)(t * std::max(0.0f, c.sheetCycles) * frames);
            frame = ((frame % frames) + frames) % frames;
            const glm::vec2 cell(1.0f / tilesX, 1.0f / tilesY);
            uvMin = glm::vec2(frame % tilesX, frame / tilesX) * cell;
            uvMax = uvMin + cell;
        }

        glm::vec3 right, up;
        glm::vec3 corners[4];
        const float half = size * 0.5f;
        if (c.renderMode == 1) {
            // Stretched along the velocity, the particle's tail behind it
            glm::vec3 v = local ? glm::vec3(world * glm::vec4(p.velocity, 0)) : p.velocity;
            if (c.velocityOverLifetime) v += local ? glm::vec3(world * glm::vec4(c.velocity, 0)) : c.velocity;
            const float speed = glm::length(v);
            const glm::vec3 axis = speed > 1e-4f ? v / speed : camUp;
            const glm::vec3 toCamera = camera.orthographic ? camBack : glm::normalize(camera.position - center);
            glm::vec3 side = glm::cross(axis, toCamera);
            side = glm::length(side) > 1e-5f ? glm::normalize(side) * half : camRight * half;
            const float length = size * c.lengthScale + speed * c.speedScale;
            corners[0] = center - side - axis * length; corners[1] = center + side - axis * length;
            corners[2] = center + side; corners[3] = center - side;
        } else {
            if (c.renderMode == 2) { right = { 1, 0, 0 }; up = { 0, 0, -1 }; }
            else if (c.renderMode == 3) {
                up = { 0, 1, 0 };
                const glm::vec3 toCamera = camera.orthographic ? camBack : camera.position - center;
                right = glm::cross(up, toCamera);
                right = glm::length(right) > 1e-5f ? glm::normalize(right) : camRight;
            } else { right = camRight; up = camUp; }
            const float cs = std::cos(p.rotation), sn = std::sin(p.rotation);
            const glm::vec3 r = (right * cs + up * sn) * half, u = (up * cs - right * sn) * half;
            corners[0] = center - r - u; corners[1] = center + r - u; corners[2] = center + r + u; corners[3] = center - r + u;
        }
        // Texture rows go from the top: v grows downwards
        const glm::vec2 uv[4] = { { uvMin.x, uvMax.y }, { uvMax.x, uvMax.y }, { uvMax.x, uvMin.y }, { uvMin.x, uvMin.y } };
        for (int k = 0; k < 4; ++k) {
            out.push_back({ corners[k], camBack, uv[k], color });
            boundsMin = glm::min(boundsMin, corners[k]); boundsMax = glm::max(boundsMax, corners[k]);
        }
    }
    return (u32)(out.size() / 4);
}

Ref<RHI::Texture> DefaultTexture(RHI::Device& device) {
    static std::weak_ptr<RHI::Texture> cached;
    if (auto texture = cached.lock()) return texture;
    constexpr u32 size = 64;
    std::vector<u8> pixels(size * size * 4);
    for (u32 y = 0; y < size; ++y)
        for (u32 x = 0; x < size; ++x) {
            const float dx = (x + 0.5f) / size * 2 - 1, dy = (y + 0.5f) / size * 2 - 1;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(1.0f - d, 0.0f, 1.0f);
            u8* px = &pixels[(y * size + x) * 4];
            px[0] = px[1] = px[2] = 255;
            px[3] = (u8)std::lround(255.0f * a * a * (3 - 2 * a));
        }
    RHI::TextureDesc desc; desc.width = desc.height = size; desc.format = RHI::Format::RGBA8_UNORM; desc.name = "DefaultParticle";
    auto texture = device.CreateTexture(desc);
    device.UploadTexture(texture.get(), pixels.data(), pixels.size());
    AssetManager::SetRuntimeTexture("builtin:particle", texture); // owned by the asset manager, released at shutdown
    cached = texture;
    return texture;
}

// ------------------------------------------------------------------------------------------------ JSON

namespace {
json Vec(glm::vec3 v) { return { v.x, v.y, v.z }; }
json Vec(glm::vec4 v) { return { v.x, v.y, v.z, v.w }; }
json RangeJson(const ParticleRange& r) { return { r.min, r.max }; }
template<int N> void ReadVec(const json& j, const char* key, glm::vec<N, float>& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < N) return;
    for (int i = 0; i < N; ++i) if ((*it)[i].is_number()) out[i] = (*it)[i].get<float>();
}
void ReadRange(const json& j, const char* key, ParticleRange& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    if (it->is_number()) out.min = out.max = it->get<float>();
    else if (it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number()) { out.min = (*it)[0].get<float>(); out.max = (*it)[1].get<float>(); }
}
template<typename T> void Read(const json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    if constexpr (std::is_same_v<T, bool>) { if (it->is_boolean()) out = it->get<bool>(); else if (it->is_number()) out = it->get<double>() != 0; }
    else if constexpr (std::is_same_v<T, std::string>) { if (it->is_string()) out = it->get<std::string>(); }
    else { if (it->is_number()) out = it->get<T>(); }
}
}

void ToJson(const ParticleSystemComponent& c, json& j) {
    j = json::object();
    j["duration"] = c.duration; j["looping"] = c.looping; j["prewarm"] = c.prewarm; j["playOnAwake"] = c.playOnAwake;
    j["startDelay"] = RangeJson(c.startDelay); j["startLifetime"] = RangeJson(c.startLifetime); j["startSpeed"] = RangeJson(c.startSpeed);
    j["startSize"] = RangeJson(c.startSize); j["startRotation"] = RangeJson(c.startRotation);
    j["startColor"] = Vec(c.startColor); j["startColor2"] = Vec(c.startColor2);
    j["gravityModifier"] = c.gravityModifier; j["simulationSpeed"] = c.simulationSpeed; j["simulationSpace"] = c.simulationSpace; j["maxParticles"] = c.maxParticles;
    j["emission"] = c.emission; j["rateOverTime"] = c.rateOverTime; j["rateOverDistance"] = c.rateOverDistance;
    j["bursts"] = json::array();
    for (const auto& b : c.bursts) j["bursts"].push_back({ { "time", b.time }, { "count", b.count }, { "cycles", b.cycles }, { "interval", b.interval }, { "probability", b.probability } });
    j["shapeEnabled"] = c.shapeEnabled; j["shape"] = (int)c.shape; j["radius"] = c.radius; j["radiusThickness"] = c.radiusThickness;
    j["angle"] = c.angle; j["arc"] = c.arc; j["randomizeDirection"] = c.randomizeDirection; j["boxSize"] = Vec(c.boxSize); j["shapeOffset"] = Vec(c.shapeOffset);
    j["velocityOverLifetime"] = c.velocityOverLifetime; j["velocity"] = Vec(c.velocity);
    j["forceOverLifetime"] = c.forceOverLifetime; j["force"] = Vec(c.force);
    j["limitVelocity"] = c.limitVelocity; j["speedLimit"] = c.speedLimit; j["drag"] = c.drag;
    j["colorOverLifetime"] = c.colorOverLifetime;
    j["colorKeys"] = json::array();
    for (const auto& k : c.colorKeys) j["colorKeys"].push_back({ k.time, k.color.r, k.color.g, k.color.b, k.color.a });
    j["sizeOverLifetime"] = c.sizeOverLifetime;
    j["sizeKeys"] = json::array();
    for (const auto& k : c.sizeKeys) j["sizeKeys"].push_back({ k.time, k.value });
    j["rotationOverLifetime"] = c.rotationOverLifetime; j["angularVelocity"] = RangeJson(c.angularVelocity);
    j["noise"] = c.noise; j["noiseStrength"] = c.noiseStrength; j["noiseFrequency"] = c.noiseFrequency; j["noiseScrollSpeed"] = c.noiseScrollSpeed;
    j["collision"] = c.collision; j["collisionPlane"] = c.collisionPlane; j["bounce"] = c.bounce; j["dampen"] = c.dampen; j["lifetimeLoss"] = c.lifetimeLoss;
    j["textureSheet"] = c.textureSheet; j["tilesX"] = c.tilesX; j["tilesY"] = c.tilesY; j["sheetFps"] = c.sheetFps; j["sheetCycles"] = c.sheetCycles; j["randomStartFrame"] = c.randomStartFrame;
    j["texture"] = c.texture; j["material"] = c.material; j["blendMode"] = c.blendMode; j["renderMode"] = c.renderMode;
    j["lengthScale"] = c.lengthScale; j["speedScale"] = c.speedScale; j["intensity"] = c.intensity; j["sortByDistance"] = c.sortByDistance;
    j["sortingOrder"] = c.sortingOrder; j["seed"] = c.seed;
}

void FromJson(const json& j, ParticleSystemComponent& c) {
    if (!j.is_object()) return;
    Read(j, "duration", c.duration); Read(j, "looping", c.looping); Read(j, "prewarm", c.prewarm); Read(j, "playOnAwake", c.playOnAwake);
    ReadRange(j, "startDelay", c.startDelay); ReadRange(j, "startLifetime", c.startLifetime); ReadRange(j, "startSpeed", c.startSpeed);
    ReadRange(j, "startSize", c.startSize); ReadRange(j, "startRotation", c.startRotation);
    ReadVec(j, "startColor", c.startColor); ReadVec(j, "startColor2", c.startColor2);
    Read(j, "gravityModifier", c.gravityModifier); Read(j, "simulationSpeed", c.simulationSpeed); Read(j, "simulationSpace", c.simulationSpace); Read(j, "maxParticles", c.maxParticles);
    Read(j, "emission", c.emission); Read(j, "rateOverTime", c.rateOverTime); Read(j, "rateOverDistance", c.rateOverDistance);
    if (auto it = j.find("bursts"); it != j.end() && it->is_array()) {
        c.bursts.clear();
        for (const auto& b : *it) if (b.is_object()) {
            ParticleBurst burst;
            Read(b, "time", burst.time); Read(b, "count", burst.count); Read(b, "cycles", burst.cycles); Read(b, "interval", burst.interval); Read(b, "probability", burst.probability);
            c.bursts.push_back(burst);
        }
    }
    Read(j, "shapeEnabled", c.shapeEnabled);
    if (auto it = j.find("shape"); it != j.end() && it->is_number_integer()) c.shape = (ParticleShape)std::clamp(it->get<int>(), 0, (int)ParticleShape::Point);
    Read(j, "radius", c.radius); Read(j, "radiusThickness", c.radiusThickness); Read(j, "angle", c.angle); Read(j, "arc", c.arc); Read(j, "randomizeDirection", c.randomizeDirection);
    ReadVec(j, "boxSize", c.boxSize); ReadVec(j, "shapeOffset", c.shapeOffset);
    Read(j, "velocityOverLifetime", c.velocityOverLifetime); ReadVec(j, "velocity", c.velocity);
    Read(j, "forceOverLifetime", c.forceOverLifetime); ReadVec(j, "force", c.force);
    Read(j, "limitVelocity", c.limitVelocity); Read(j, "speedLimit", c.speedLimit); Read(j, "drag", c.drag);
    Read(j, "colorOverLifetime", c.colorOverLifetime);
    if (auto it = j.find("colorKeys"); it != j.end() && it->is_array()) {
        c.colorKeys.clear();
        for (const auto& k : *it) if (k.is_array() && k.size() == 5) c.colorKeys.push_back({ k[0].get<float>(), { k[1].get<float>(), k[2].get<float>(), k[3].get<float>(), k[4].get<float>() } });
        std::stable_sort(c.colorKeys.begin(), c.colorKeys.end(), [](auto& a, auto& b) { return a.time < b.time; });
    }
    Read(j, "sizeOverLifetime", c.sizeOverLifetime);
    if (auto it = j.find("sizeKeys"); it != j.end() && it->is_array()) {
        c.sizeKeys.clear();
        for (const auto& k : *it) if (k.is_array() && k.size() == 2) c.sizeKeys.push_back({ k[0].get<float>(), k[1].get<float>() });
        std::stable_sort(c.sizeKeys.begin(), c.sizeKeys.end(), [](auto& a, auto& b) { return a.time < b.time; });
    }
    Read(j, "rotationOverLifetime", c.rotationOverLifetime); ReadRange(j, "angularVelocity", c.angularVelocity);
    Read(j, "noise", c.noise); Read(j, "noiseStrength", c.noiseStrength); Read(j, "noiseFrequency", c.noiseFrequency); Read(j, "noiseScrollSpeed", c.noiseScrollSpeed);
    Read(j, "collision", c.collision); Read(j, "collisionPlane", c.collisionPlane); Read(j, "bounce", c.bounce); Read(j, "dampen", c.dampen); Read(j, "lifetimeLoss", c.lifetimeLoss);
    Read(j, "textureSheet", c.textureSheet); Read(j, "tilesX", c.tilesX); Read(j, "tilesY", c.tilesY); Read(j, "sheetFps", c.sheetFps); Read(j, "sheetCycles", c.sheetCycles); Read(j, "randomStartFrame", c.randomStartFrame);
    Read(j, "texture", c.texture); Read(j, "material", c.material); Read(j, "blendMode", c.blendMode); Read(j, "renderMode", c.renderMode);
    Read(j, "lengthScale", c.lengthScale); Read(j, "speedScale", c.speedScale); Read(j, "intensity", c.intensity); Read(j, "sortByDistance", c.sortByDistance);
    Read(j, "sortingOrder", c.sortingOrder); Read(j, "seed", c.seed);
    c.maxParticles = std::clamp(c.maxParticles, 0, 100000);
    c.tilesX = std::clamp(c.tilesX, 1, 64); c.tilesY = std::clamp(c.tilesY, 1, 64);
    c.blendMode = std::clamp(c.blendMode, 0, 1); c.renderMode = std::clamp(c.renderMode, 0, 3); c.simulationSpace = std::clamp(c.simulationSpace, 0, 1);
}

} // namespace Stardust::ParticleSystems
