#include "Scene/InverseKinematics.h"
#include "Scene/Scene.h"
#include "Physics/PhysicsWorld.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

namespace Stardust::InverseKinematics {
namespace {
// World transforms from the local ones, walking up the hierarchy: the Animator has just written the
// local poses, so the scene's cached world matrices may be a frame old.
glm::mat4 World(Scene& s, EntityID e) {
    glm::mat4 m = s.Reg().Get<TransformComponent>(e).Matrix();
    for (EntityID p = s.GetParent(e); p; p = s.GetParent(p)) m = s.Reg().Get<TransformComponent>(p).Matrix() * m;
    return m;
}
glm::quat WorldRotation(Scene& s, EntityID e) {
    glm::quat q = s.Reg().Get<TransformComponent>(e).rotation;
    for (EntityID p = s.GetParent(e); p; p = s.GetParent(p)) q = s.Reg().Get<TransformComponent>(p).rotation * q;
    return glm::normalize(q);
}
glm::vec3 Position(Scene& s, EntityID e) { return glm::vec3(World(s, e)[3]); }
// Turns the bone by `delta` in world space
void Rotate(Scene& s, EntityID e, const glm::quat& delta) {
    auto& t = s.Reg().Get<TransformComponent>(e);
    const EntityID p = s.GetParent(e);
    const glm::quat parent = p ? WorldRotation(s, p) : glm::quat(1, 0, 0, 0);
    t.rotation = glm::normalize(glm::inverse(parent) * delta * parent * t.rotation);
}
void SetWorldRotation(Scene& s, EntityID e, const glm::quat& world) {
    auto& t = s.Reg().Get<TransformComponent>(e);
    const EntityID p = s.GetParent(e);
    t.rotation = glm::normalize(glm::inverse(p ? WorldRotation(s, p) : glm::quat(1, 0, 0, 0)) * world);
}
glm::quat FromTo(glm::vec3 a, glm::vec3 b) {
    if (glm::length2(a) < 1e-12f || glm::length2(b) < 1e-12f) return glm::quat(1, 0, 0, 0);
    return glm::rotation(glm::normalize(a), glm::normalize(b));
}
glm::quat Weighted(const glm::quat& q, float w) { return glm::slerp(glm::quat(1, 0, 0, 0), q, std::clamp(w, 0.0f, 1.0f)); }
float SafeAcos(float x) { return std::acos(std::clamp(x, -1.0f, 1.0f)); }

// Two-bone analytic solve: a (root), b (mid), c (tip) reach t, bending towards hint
void SolveTwoBone(Scene& s, EntityID a, EntityID b, EntityID c, glm::vec3 t, const glm::vec3* hint, float weight) {
    const glm::vec3 pa = Position(s, a), pb = Position(s, b), pc = Position(s, c);
    const float lab = glm::length(pb - pa), lcb = glm::length(pb - pc);
    if (lab < 1e-5f || lcb < 1e-5f) return;
    const float lat = std::clamp(glm::length(t - pa), 1e-4f, (lab + lcb) * 0.9999f);
    // Current and wanted inner angles at the root and at the middle joint
    const float ac_ab0 = SafeAcos(glm::dot(glm::normalize(pc - pa), glm::normalize(pb - pa)));
    const float ba_bc0 = SafeAcos(glm::dot(glm::normalize(pa - pb), glm::normalize(pc - pb)));
    const float ac_ab1 = SafeAcos((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat));
    const float ba_bc1 = SafeAcos((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb));
    // Bend plane: from the hint, else from the current bend
    glm::vec3 bendDir = hint ? *hint - pa : pb - pa;
    glm::vec3 axis = glm::cross(pc - pa, bendDir);
    if (glm::length2(axis) < 1e-10f) axis = glm::cross(pc - pa, glm::vec3(0, 1, 0));
    if (glm::length2(axis) < 1e-10f) axis = glm::vec3(1, 0, 0);
    axis = glm::normalize(axis);
    Rotate(s, a, Weighted(glm::angleAxis(ac_ab1 - ac_ab0, axis), weight));
    Rotate(s, b, Weighted(glm::angleAxis(ba_bc1 - ba_bc0, axis), weight));
    // Swing the chain so the tip points at the target
    const glm::vec3 pa2 = Position(s, a), pc2 = Position(s, c);
    Rotate(s, a, Weighted(FromTo(pc2 - pa2, t - pa2), weight));
    // Twist around the root-target line so the middle joint lies towards the hint
    if (hint) {
        const glm::vec3 dir = glm::normalize(t - pa2);
        glm::vec3 mid = Position(s, b) - pa2, want = *hint - pa2;
        mid -= dir * glm::dot(mid, dir); want -= dir * glm::dot(want, dir);
        if (glm::length2(mid) > 1e-8f && glm::length2(want) > 1e-8f) {
            const float angle = std::atan2(glm::dot(glm::cross(glm::normalize(mid), glm::normalize(want)), dir), glm::dot(glm::normalize(mid), glm::normalize(want)));
            Rotate(s, a, Weighted(glm::angleAxis(angle, dir), weight));
        }
    }
}

glm::vec3 Axis(int index) {
    static const glm::vec3 axes[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    return axes[std::clamp(index, 0, 5)];
}
}

void Update(Scene& scene) {
    Registry& reg = scene.Reg();
    bool any = false;
    reg.Each<IKConstraintComponent>([&](EntityID, IKConstraintComponent&) { any = true; });
    if (!any) return;
    auto find = [&](u64 id) { EntityID e = id ? scene.FindByUUID(id) : kNullEntity; return e && reg.Valid(e) ? e : kNullEntity; };
    reg.Each<IKConstraintComponent>([&](EntityID e, IKConstraintComponent& c) {
        if (!reg.IsEnabled<IKConstraintComponent>(e) || !scene.IsActiveInHierarchy(e) || c.weight <= 0.0f) return;
        const EntityID tip = find(c.tip);
        if (!tip) return;
        const EntityID target = find(c.target), hintEntity = find(c.hint);
        if (c.type == (int)IKType::LookAt) {
            if (!target) return;
            const glm::vec3 from = Position(scene, tip), to = Position(scene, target);
            const glm::quat rot = WorldRotation(scene, tip);
            const glm::vec3 current = rot * Axis(c.aimAxis);
            glm::vec3 wanted = to - from;
            if (glm::length2(wanted) < 1e-8f) return;
            wanted = glm::normalize(wanted);
            float angle = SafeAcos(glm::dot(glm::normalize(current), wanted));
            glm::quat delta = FromTo(current, wanted);
            const float limit = glm::radians(std::clamp(c.maxAngle, 0.0f, 180.0f));
            if (angle > limit && angle > 1e-5f) delta = glm::slerp(glm::quat(1, 0, 0, 0), delta, limit / angle);
            Rotate(scene, tip, Weighted(delta, c.weight));
            return;
        }
        const EntityID root = find(c.root), mid = find(c.mid);
        if (!root || !mid) return;
        // The animated knee/elbow direction, when no hint object is given
        const glm::vec3 pa = Position(scene, root), pb = Position(scene, mid), pc = Position(scene, tip);
        glm::vec3 hint = hintEntity ? Position(scene, hintEntity) : pb + (pb - (pa + pc) * 0.5f);
        if (c.type == (int)IKType::TwoBone) {
            if (!target) return;
            const glm::mat4 tw = World(scene, target);
            SolveTwoBone(scene, root, mid, tip, glm::vec3(tw[3]), &hint, c.weight);
            if (c.matchTargetRotation) {
                glm::vec3 scale, translation, skew; glm::vec4 persp; glm::quat targetRot;
                glm::decompose(tw, scale, targetRot, translation, skew, persp);
                SetWorldRotation(scene, tip, glm::slerp(WorldRotation(scene, tip), glm::normalize(targetRot), c.weight));
            }
            c.lastTarget = glm::vec3(tw[3]);
            return;
        }
        // Foot Placement: the ground under the animated foot
        PhysicsWorld* physics = scene.Physics();
        c.grounded = false;
        if (!physics) return;
        const glm::vec3 up(0, 1, 0);
        const float step = std::max(0.01f, c.maxStep);
        RaycastHit hit;
        const EntityID ignore = find(c.ignore);
        if (!physics->Raycast(pc + up * step, -up, step * 2.0f + c.footOffset, hit, ignore)) return;
        if (ignore && hit.entity == ignore) return;
        // The animated foot height above the character's base stays (walk cycles lift the foot)
        glm::vec3 goal = hit.point + up * c.footOffset;
        const float animatedLift = ignore ? std::max(0.0f, pc.y - glm::vec3(scene.GetWorldMatrix(ignore)[3]).y - c.footOffset) : 0.0f;
        goal.y += animatedLift;
        if (std::abs(goal.y - pc.y) > step) return;
        const glm::quat footBefore = WorldRotation(scene, tip);
        SolveTwoBone(scene, root, mid, tip, glm::vec3(pc.x, goal.y, pc.z), &hint, c.weight);
        if (c.alignToGround) {
            // Keep the animated foot orientation, tilted by the slope
            const glm::quat slope = FromTo(up, hit.normal);
            SetWorldRotation(scene, tip, glm::slerp(footBefore, glm::normalize(slope * footBefore), c.weight));
        }
        c.grounded = true; c.lastTarget = goal;
    });
}

} // namespace Stardust::InverseKinematics
