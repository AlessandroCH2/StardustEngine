#include "Scene/GameplaySerialization.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <format>

namespace Stardust::GameplayJson {
using json = nlohmann::json;

namespace {
// One field list per component, used both ways
struct Writer {
    json& j;
    void operator()(const char* k, const float& v) { j[k] = v; }
    void operator()(const char* k, const int& v) { j[k] = v; }
    void operator()(const char* k, const bool& v) { j[k] = v; }
    void operator()(const char* k, const u32& v) { j[k] = v; }
    void operator()(const char* k, const std::string& v) { j[k] = v; }
    void operator()(const char* k, const glm::vec3& v) { j[k] = { v.x, v.y, v.z }; }
    void operator()(const char* k, const glm::vec4& v) { j[k] = { v.x, v.y, v.z, v.w }; }
    // Object references as 16 hex digits, like the rest of the scene (prefab and duplicate remapping finds them)
    void Ref(const char* k, const u64& v) { j[k] = v ? std::format("{:016x}", v) : std::string("0"); }
};
struct Reader {
    const json& j;
    template<typename T> bool Has(const char* k, T check) const { auto it = j.find(k); return it != j.end() && check(*it); }
    void operator()(const char* k, float& v) { if (Has(k, [](auto& x) { return x.is_number(); })) v = j[k].get<float>(); }
    void operator()(const char* k, int& v) { if (Has(k, [](auto& x) { return x.is_number(); })) v = (int)j[k].get<double>(); }
    void operator()(const char* k, bool& v) { if (Has(k, [](auto& x) { return x.is_boolean(); })) v = j[k].get<bool>(); }
    void operator()(const char* k, u32& v) { if (Has(k, [](auto& x) { return x.is_number(); })) v = (u32)j[k].get<double>(); }
    void operator()(const char* k, std::string& v) { if (Has(k, [](auto& x) { return x.is_string(); })) v = j[k].get<std::string>(); }
    void operator()(const char* k, glm::vec3& v) { if (Has(k, [](auto& x) { return x.is_array() && x.size() == 3; })) { auto& a = j[k]; v = { a[0].get<float>(), a[1].get<float>(), a[2].get<float>() }; } }
    void operator()(const char* k, glm::vec4& v) { if (Has(k, [](auto& x) { return x.is_array() && x.size() == 4; })) { auto& a = j[k]; v = { a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>() }; } }
    void Ref(const char* k, u64& v) {
        auto it = j.find(k); if (it == j.end()) return;
        if (it->is_string()) { const auto s = it->get<std::string>(); v = std::strtoull(s.c_str(), nullptr, s.size() == 16 ? 16 : 10); }
        else if (it->is_number_unsigned()) v = it->get<u64>();
    }
};


template<typename A> void Fields(A& a, CharacterControllerComponent& c) {
    a("height", c.height); a("radius", c.radius); a("center", c.center); a("slopeLimit", c.slopeLimit); a("stepOffset", c.stepOffset);
    a("skinWidth", c.skinWidth); a("minMoveDistance", c.minMoveDistance); a("pushForce", c.pushForce); a("mass", c.mass);
}
template<typename A> void Fields(A& a, JointComponent& c) {
    a("type", c.type); a.Ref("connectedBody", c.connectedBody); a("anchor", c.anchor); a("axis", c.axis); a("enableCollision", c.enableCollision);
    a("useLimits", c.useLimits); a("minLimit", c.minLimit); a("maxLimit", c.maxLimit); a("swingLimit", c.swingLimit); a("twistMin", c.twistMin); a("twistMax", c.twistMax);
    a("springFrequency", c.springFrequency); a("springDamping", c.springDamping); a("useMotor", c.useMotor); a("motorTargetVelocity", c.motorTargetVelocity);
    a("motorForce", c.motorForce); a("friction", c.friction); a("breakForce", c.breakForce); a("breakTorque", c.breakTorque);
}
template<typename A> void Fields(A& a, NavMeshSurfaceComponent& c) {
    a("agentRadius", c.agentRadius); a("agentHeight", c.agentHeight); a("maxSlope", c.maxSlope); a("stepHeight", c.stepHeight);
    a("cellSize", c.cellSize); a("cellHeight", c.cellHeight); a("minRegionArea", c.minRegionArea); a("tileSize", c.tileSize);
    a("useBounds", c.useBounds); a("boundsCenter", c.boundsCenter); a("boundsSize", c.boundsSize); a("collectFrom", c.collectFrom);
    a("layerMask", c.layerMask); a("data", c.data);
}
template<typename A> void Fields(A& a, NavMeshAgentComponent& c) {
    a("speed", c.speed); a("angularSpeed", c.angularSpeed); a("acceleration", c.acceleration); a("stoppingDistance", c.stoppingDistance);
    a("autoBraking", c.autoBraking); a("radius", c.radius); a("height", c.height); a("baseOffset", c.baseOffset);
    a("avoidancePriority", c.avoidancePriority); a("updatePosition", c.updatePosition); a("updateRotation", c.updateRotation);
}
template<typename A> void Fields(A& a, LineRendererComponent& c) {
    a("useWorldSpace", c.useWorldSpace); a("loop", c.loop); a("startWidth", c.startWidth); a("endWidth", c.endWidth);
    a("startColor", c.startColor); a("endColor", c.endColor); a("alignment", c.alignment); a("textureMode", c.textureMode);
    a("tileLength", c.tileLength); a("cornerVertices", c.cornerVertices); a("texture", c.texture); a("material", c.material);
    a("blendMode", c.blendMode); a("intensity", c.intensity); a("sortingOrder", c.sortingOrder);
}
template<typename A> void Fields(A& a, SplineComponent& c) {
    a("mode", c.mode); a("closed", c.closed); a.Ref("follower", c.follower); a("followSpeed", c.followSpeed);
    a("followLoop", c.followLoop); a("alignToPath", c.alignToPath);
}
template<typename A> void Fields(A& a, ReflectionProbeComponent& c) {
    a("size", c.size); a("center", c.center); a("boxProjection", c.boxProjection); a("blendDistance", c.blendDistance);
    a("intensity", c.intensity); a("resolution", c.resolution); a("mode", c.mode); a("interval", c.interval);
    a("importance", c.importance); a("nearClip", c.nearClip); a("farClip", c.farClip); a("shadows", c.shadows);
}
template<typename A> void Fields(A& a, DecalComponent& c) {
    a("texture", c.texture); a("normalMap", c.normalMap); a("color", c.color); a("size", c.size); a("opacity", c.opacity);
    a("normalStrength", c.normalStrength); a("angleFade", c.angleFade); a("smoothness", c.smoothness);
    a("tilingOffset", c.tilingOffset); a("emission", c.emission); a("sortingOrder", c.sortingOrder);
}

template<typename A> void Fields(A& a, IKConstraintComponent& c) {
    a("type", c.type); a("weight", c.weight); a.Ref("root", c.root); a.Ref("mid", c.mid); a.Ref("tip", c.tip); a.Ref("target", c.target); a.Ref("hint", c.hint);
    a("matchTargetRotation", c.matchTargetRotation); a("aimAxis", c.aimAxis); a("maxAngle", c.maxAngle);
    a("footOffset", c.footOffset); a("maxStep", c.maxStep); a("alignToGround", c.alignToGround); a.Ref("ignore", c.ignore);
}

template<typename C> void Write(const C& c, json& j) { if (!j.is_object()) j = json::object(); Writer w{ j }; Fields(w, const_cast<C&>(c)); }
template<typename C> void Read(const json& j, C& c) { if (!j.is_object()) return; Reader r{ j }; Fields(r, c); }

json Vec3(const glm::vec3& v) { return { v.x, v.y, v.z }; }
glm::vec3 Vec3(const json& a, glm::vec3 fallback) { return a.is_array() && a.size() == 3 ? glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()) : fallback; }
}

void ToJson(const CharacterControllerComponent& c, json& j) { Write(c, j); }
void FromJson(const json& j, CharacterControllerComponent& c) {
    Read(j, c);
    c.height = std::max(0.02f, c.height); c.radius = std::clamp(c.radius, 0.01f, c.height * 0.5f);
    c.slopeLimit = std::clamp(c.slopeLimit, 0.0f, 89.0f); c.stepOffset = std::max(0.0f, c.stepOffset); c.skinWidth = std::clamp(c.skinWidth, 0.001f, 0.2f);
}
void ToJson(const JointComponent& c, json& j) { Write(c, j); }
void FromJson(const json& j, JointComponent& c) { Read(j, c); c.type = std::clamp(c.type, 0, 4); }
void ToJson(const NavMeshSurfaceComponent& c, json& j) { Write(c, j); }
void FromJson(const json& j, NavMeshSurfaceComponent& c) {
    Read(j, c);
    c.cellSize = std::clamp(c.cellSize, 0.02f, 5.0f); c.cellHeight = std::clamp(c.cellHeight, 0.02f, 5.0f); c.tileSize = std::clamp(c.tileSize, 16, 512);
}
void ToJson(const NavMeshAgentComponent& c, json& j) { Write(c, j); }
void FromJson(const json& j, NavMeshAgentComponent& c) { Read(j, c); c.avoidancePriority = std::clamp(c.avoidancePriority, 0, 99); }
void ToJson(const LineRendererComponent& c, json& j) {
    Write(c, j);
    j["positions"] = json::array(); for (auto& p : c.positions) j["positions"].push_back(Vec3(p));
}
void FromJson(const json& j, LineRendererComponent& c) {
    Read(j, c);
    if (auto it = j.find("positions"); it != j.end() && it->is_array()) {
        c.positions.clear();
        for (auto& p : *it) if (p.is_array() && p.size() == 3) c.positions.push_back(Vec3(p, glm::vec3(0)));
    }
    c.cornerVertices = std::clamp(c.cornerVertices, 0, 16);
}
void ToJson(const SplineComponent& c, json& j) {
    Write(c, j);
    j["knots"] = json::array();
    for (auto& k : c.knots) j["knots"].push_back({ { "position", Vec3(k.position) }, { "tangentIn", Vec3(k.tangentIn) }, { "tangentOut", Vec3(k.tangentOut) }, { "roll", k.roll } });
}
void FromJson(const json& j, SplineComponent& c) {
    Read(j, c);
    if (auto it = j.find("knots"); it != j.end() && it->is_array()) {
        c.knots.clear();
        for (auto& k : *it) {
            if (!k.is_object()) continue;
            SplineKnot knot;
            knot.position = Vec3(k.value("position", json()), glm::vec3(0));
            knot.tangentIn = Vec3(k.value("tangentIn", json()), glm::vec3(0));
            knot.tangentOut = Vec3(k.value("tangentOut", json()), glm::vec3(0));
            knot.roll = k.value("roll", 0.0f);
            c.knots.push_back(knot);
        }
    }
    c.mode = std::clamp(c.mode, 0, 2);
}
void ToJson(const ReflectionProbeComponent& c, json& j) { Write(c, j); }
void FromJson(const json& j, ReflectionProbeComponent& c) {
    Read(j, c);
    c.resolution = c.resolution <= 64 ? 64 : c.resolution <= 128 ? 128 : c.resolution <= 256 ? 256 : 512;
    c.size = glm::max(c.size, glm::vec3(0.01f));
}
void ToJson(const IKConstraintComponent& c, json& j) { Write(c, j); }
void FromJson(const json& j, IKConstraintComponent& c) { Read(j, c); c.type = std::clamp(c.type, 0, 2); c.weight = std::clamp(c.weight, 0.0f, 1.0f); c.aimAxis = std::clamp(c.aimAxis, 0, 5); }
void ToJson(const DecalComponent& c, json& j) { Write(c, j); }
void FromJson(const json& j, DecalComponent& c) { Read(j, c); c.size = glm::max(c.size, glm::vec3(0.001f)); }

} // namespace Stardust::GameplayJson
