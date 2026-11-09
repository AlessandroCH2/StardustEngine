#include "Physics/PhysicsWorld.h"
#include "Scene/Scene.h"
#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <unordered_map>
#include <thread>
#include <cstdarg>

#pragma warning(push, 0)
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#pragma warning(pop)

namespace Stardust {

using namespace JPH;

namespace Layers {
static constexpr ObjectLayer NonMoving = 0;
static constexpr ObjectLayer Moving = 1;
static constexpr ObjectLayer Count = 2;
}
namespace BPLayers {
static constexpr BroadPhaseLayer NonMoving(0);
static constexpr BroadPhaseLayer Moving(1);
static constexpr uint Count = 2;
}

class BPLayerInterface final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return BPLayers::Count; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return l == Layers::NonMoving ? BPLayers::NonMoving : BPLayers::Moving; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer l) const override { return l == BPLayers::NonMoving ? "NonMoving" : "Moving"; }
#endif
};

class ObjectVsBPFilter final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer l, BroadPhaseLayer bp) const override {
        return l == Layers::Moving || bp == BPLayers::Moving;
    }
};

class ObjectPairFilter final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        return a == Layers::Moving || b == Layers::Moving;
    }
};

class ContactForwarder final : public ContactListener {
public:
    explicit ContactForwarder(PhysicsWorld& w) : m_World(w) {}
    void OnContactAdded(const Body& a, const Body& b, const ContactManifold&, ContactSettings&) override {
        m_World.PushCollision(a.GetID().GetIndexAndSequenceNumber(), b.GetID().GetIndexAndSequenceNumber(), true);
    }
    void OnContactRemoved(const SubShapeIDPair& pair) override {
        m_World.PushCollision(pair.GetBody1ID().GetIndexAndSequenceNumber(), pair.GetBody2ID().GetIndexAndSequenceNumber(), false);
    }
private:
    PhysicsWorld& m_World;
};

struct PhysicsWorld::Impl {
    BPLayerInterface bpInterface;
    ObjectVsBPFilter objVsBp;
    ObjectPairFilter objPair;
    Scope<ContactForwarder> contacts;
    std::unordered_map<EntityID, BodyID> bodies;
    std::unordered_map<EntityID, BodyType> types;
    std::unordered_map<EntityID, glm::vec3> scales; // world scale at creation (kept when writing transforms back)
    std::unordered_map<EntityID, glm::mat4> transforms;
    std::unordered_map<u32, EntityID> bodyToEntity;  // only modified outside PhysicsSystem::Update
    std::unordered_map<u32, bool> sensors;
};

static Vec3 ToJ(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
static Quat ToJ(const glm::quat& q) { return Quat(q.x, q.y, q.z, q.w); }
static glm::vec3 ToG(Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
static glm::quat ToG(QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

static bool s_JoltReady = false;

void PhysicsWorld::InitJolt() {
    if (s_JoltReady) return;
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
    s_JoltReady = true;
}

void PhysicsWorld::ShutdownJolt() {
    if (!s_JoltReady) return;
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
    s_JoltReady = false;
}

PhysicsWorld::PhysicsWorld(Scene& scene) : m_Scene(scene), m_Impl(MakeScope<Impl>()) {
    InitJolt();
    m_Temp = MakeScope<TempAllocatorImpl>(16 * 1024 * 1024);
    int threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
    m_Jobs = MakeScope<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads);
    m_System = MakeScope<PhysicsSystem>();
    m_System->Init(65536, 0, 65536, 16384, m_Impl->bpInterface, m_Impl->objVsBp, m_Impl->objPair);
    m_System->SetGravity(Vec3(0, -9.81f, 0));
    m_Impl->contacts = MakeScope<ContactForwarder>(*this);
    m_System->SetContactListener(m_Impl->contacts.get());

    Registry& reg = scene.Reg();
    for (EntityID e : reg.AllEntities()) {
        if (!scene.IsActiveInHierarchy(e)) continue;
        CreateBody(e);
    }
    m_System->OptimizeBroadPhase();
    SD_INFO("Physics: {} bodies", m_Impl->bodies.size());
}

PhysicsWorld::~PhysicsWorld() {
    BodyInterface& bi = m_System->GetBodyInterface();
    for (auto& [e, id] : m_Impl->bodies) { bi.RemoveBody(id); bi.DestroyBody(id); }
    m_Impl->bodies.clear();
    m_System.reset();
    m_Jobs.reset();
    m_Temp.reset();
}

bool PhysicsWorld::HasBody(EntityID e) const { return m_Impl->bodies.contains(e); }

void PhysicsWorld::CreateTerrainBody(EntityID e) {
    Registry& reg = m_Scene.Reg();
    auto data = TerrainData::Find(reg.Get<IDComponent>(e).uuid);
    if (!data) { SD_WARN("Terrain collider: terrain data not loaded yet"); return; }
    u32 res = data->Resolution();
    float size = data->Settings().size;
    glm::vec3 origin = glm::vec3(m_Scene.GetWorldMatrix(e)[3]);
    float step = size / (res - 1);
    HeightFieldShapeSettings hs(data->Heights().data(), ToJ(origin + glm::vec3(-size * 0.5f, 0.0f, -size * 0.5f)),
                                Vec3(step, 1.0f, step), res);
    hs.mBlockSize = 4;
    auto result = hs.Create();
    if (result.HasError()) { SD_ERROR("Terrain collider failed: {}", result.GetError().c_str()); return; }
    BodyCreationSettings bs(result.Get(), RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, Layers::NonMoving);
    bs.mFriction = 0.8f;
    bs.mUserData = e;
    BodyID id = m_System->GetBodyInterface().CreateAndAddBody(bs, EActivation::DontActivate);
    m_Impl->bodies[e] = id;
    m_Impl->types[e] = BodyType::Static;
    RegisterBody(e, id.GetIndexAndSequenceNumber(), false);
}

void PhysicsWorld::CreateBody(EntityID e) {
    Registry& reg = m_Scene.Reg();
    if (HasBody(e)) return;
    if(reg.Has<RigidbodyComponent>(e)&&!reg.IsEnabled<RigidbodyComponent>(e))return;
    // Initial startup, scene replacement and SetActive/RecreateBody must use
    // the same terrain path. Scene activation prepares height data first.
    if (auto* terrain = reg.TryGetEnabled<TerrainComponent>(e); terrain && terrain->collider) {
        CreateTerrainBody(e);
        return;
    }
    auto* rb = reg.TryGetEnabled<RigidbodyComponent>(e);
    auto* box = reg.TryGetEnabled<BoxColliderComponent>(e);
    auto* sphere = reg.TryGetEnabled<SphereColliderComponent>(e);
    auto* capsule = reg.TryGetEnabled<CapsuleColliderComponent>(e);
    auto* meshCol = reg.TryGetEnabled<MeshColliderComponent>(e);
    if (!box && !sphere && !capsule && !meshCol) {
        if (rb) SD_WARN("'{}' has a Rigidbody but no collider", reg.Get<NameComponent>(e).name);
        return;
    }

    glm::vec3 pos, scale, skew; glm::vec4 persp; glm::quat rot;
    glm::decompose(m_Scene.GetWorldMatrix(e), scale, rot, pos, skew, persp);
    rot = glm::normalize(rot);
    scale = glm::abs(scale);
    BodyType type = rb ? rb->type : BodyType::Static;

    RefConst<Shape> shape;
    glm::vec3 center(0.0f);
    PhysicsMaterialProps mat;
    if (box) {
        glm::vec3 he = glm::max(box->size * scale * 0.5f, glm::vec3(0.01f));
        shape = new BoxShape(ToJ(he), std::min(0.05f, std::min({ he.x, he.y, he.z }) * 0.5f));
        center = box->center * scale; mat = box->material;
    } else if (sphere) {
        shape = new SphereShape(std::max(0.01f, sphere->radius * std::max({ scale.x, scale.y, scale.z })));
        center = sphere->center * scale; mat = sphere->material;
    } else if (capsule) {
        float r = std::max(0.01f, capsule->radius * std::max(scale.x, scale.z));
        float halfH = std::max(0.01f, capsule->height * 0.5f * scale.y - r);
        shape = new CapsuleShape(halfH, r);
        center = capsule->center * scale; mat = capsule->material;
    } else if (meshCol) {
        auto* mr = reg.TryGet<MeshRendererComponent>(e);
        auto mesh = mr ? AssetManager::GetMesh(mr->mesh) : nullptr;
        if (!mesh) { SD_WARN("Mesh Collider on '{}' needs a Mesh Renderer", reg.Get<NameComponent>(e).name); return; }
        mat = meshCol->material;
        if (meshCol->convex || type != BodyType::Static) {
            Array<Vec3> pts;
            for (auto& p : mesh->Positions()) pts.push_back(ToJ(p * scale));
            auto r = ConvexHullShapeSettings(pts).Create();
            if (r.HasError()) { SD_ERROR("Convex hull failed: {}", r.GetError().c_str()); return; }
            shape = r.Get();
        } else {
            VertexList verts;
            for (auto& p : mesh->Positions()) verts.push_back(Float3(p.x * scale.x, p.y * scale.y, p.z * scale.z));
            IndexedTriangleList tris;
            auto& idx = mesh->Indices();
            for (size_t i = 0; i + 2 < idx.size(); i += 3) tris.push_back(IndexedTriangle(idx[i], idx[i + 1], idx[i + 2], 0));
            auto r = MeshShapeSettings(verts, tris).Create();
            if (r.HasError()) { SD_ERROR("Mesh collider failed: {}", r.GetError().c_str()); return; }
            shape = r.Get();
        }
    }
    if (glm::length(center) > 1e-5f) shape = new RotatedTranslatedShape(ToJ(center), Quat::sIdentity(), shape);

    EMotionType motion = type == BodyType::Dynamic ? EMotionType::Dynamic : type == BodyType::Kinematic ? EMotionType::Kinematic : EMotionType::Static;
    BodyCreationSettings bs(shape, RVec3(pos.x, pos.y, pos.z), ToJ(rot), motion, motion == EMotionType::Static ? Layers::NonMoving : Layers::Moving);
    bs.mFriction = mat.friction;
    bs.mRestitution = mat.restitution;
    bs.mIsSensor = mat.isTrigger;
    bs.mUserData = e;
    if (rb) {
        bs.mLinearDamping = rb->linearDamping;
        bs.mAngularDamping = rb->angularDamping;
        bs.mGravityFactor = rb->useGravity ? 1.0f : 0.0f;
        if (motion == EMotionType::Dynamic) {
            bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            bs.mMassPropertiesOverride.mMass = std::max(rb->mass, 0.001f);
            if (rb->freezeRotation) bs.mAllowedDOFs = EAllowedDOFs::TranslationX | EAllowedDOFs::TranslationY | EAllowedDOFs::TranslationZ;
        }
    }
    BodyID id = m_System->GetBodyInterface().CreateAndAddBody(bs, motion == EMotionType::Static ? EActivation::DontActivate : EActivation::Activate);
    if (id.IsInvalid()) { SD_ERROR("Physics body limit reached"); return; }
    m_Impl->bodies[e] = id;
    m_Impl->types[e] = type;
    m_Impl->scales[e] = scale;
    m_Impl->transforms[e] = m_Scene.GetWorldMatrix(e);
    RegisterBody(e, id.GetIndexAndSequenceNumber(), mat.isTrigger);
}

void PhysicsWorld::RegisterBody(EntityID e, u32 id, bool sensor) {
    m_Impl->bodyToEntity[id] = e;
    m_Impl->sensors[id] = sensor;
}

void PhysicsWorld::RecreateBody(EntityID e) {
    glm::vec3 v = GetVelocity(e), w = GetAngularVelocity(e);
    DestroyBody(e);
    CreateBody(e);
    SetVelocity(e, v);
    SetAngularVelocity(e, w);
}

void PhysicsWorld::DestroyBody(EntityID e) {
    auto it = m_Impl->bodies.find(e);
    if (it == m_Impl->bodies.end()) return;
    BodyInterface& bi = m_System->GetBodyInterface();
    m_Impl->bodyToEntity.erase(it->second.GetIndexAndSequenceNumber());
    m_Impl->sensors.erase(it->second.GetIndexAndSequenceNumber());
    bi.RemoveBody(it->second);
    bi.DestroyBody(it->second);
    m_Impl->bodies.erase(it);
    m_Impl->types.erase(e);
    m_Impl->scales.erase(e);
    m_Impl->transforms.erase(e);
}

void PhysicsWorld::Step(float dt) {
    Registry& reg = m_Scene.Reg();
    BodyInterface& bi = m_System->GetBodyInterface();

    // Compare world matrices, including inherited changes. Collider-only children are static bodies
    // in Jolt, but their parent can still move, rotate, or scale them from gameplay code.
    std::vector<EntityID> resize;
    for (auto& [e, previous] : m_Impl->transforms) {
        if (!reg.Valid(e)) continue;
        glm::vec3 p, s, skew; glm::vec4 perspective; glm::quat r;
        glm::decompose(m_Scene.GetWorldMatrix(e), s, r, p, skew, perspective);
        if (glm::length(glm::abs(s) - m_Impl->scales[e]) > 0.0001f) resize.push_back(e);
    }
    for (EntityID e : resize) RecreateBody(e);
    for (auto& [e, id] : m_Impl->bodies) {
        if (!reg.Valid(e) || !m_Impl->transforms.contains(e)) continue;
        glm::mat4 world = m_Scene.GetWorldMatrix(e);
        bool changed = false;
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            changed |= std::abs(world[c][r] - m_Impl->transforms[e][c][r]) > 0.00001f;
        if (!changed && m_Impl->types[e] != BodyType::Kinematic) continue;
        glm::vec3 pos, scale, skew; glm::vec4 persp; glm::quat rot;
        glm::decompose(world, scale, rot, pos, skew, persp);
        if (m_Impl->types[e] == BodyType::Kinematic)
            bi.MoveKinematic(id, RVec3(pos.x, pos.y, pos.z), ToJ(glm::normalize(rot)), dt);
        else bi.SetPositionAndRotation(id, RVec3(pos.x, pos.y, pos.z), ToJ(glm::normalize(rot)), EActivation::Activate);
    }

    m_System->Update(dt, 1, m_Temp.get(), m_Jobs.get());

    // Parents must be written before children; unordered body iteration can otherwise apply an
    // ancestor's movement twice to a dynamic child.
    std::vector<EntityID> ordered;
    for (auto& [e, id] : m_Impl->bodies) if (reg.Valid(e)) ordered.push_back(e);
    auto depth = [&](EntityID e) { int d = 0; for (; m_Scene.GetParent(e); e = m_Scene.GetParent(e)) ++d; return d; };
    std::stable_sort(ordered.begin(), ordered.end(), [&](EntityID a, EntityID b) { return depth(a) < depth(b); });
    for (EntityID e : ordered) {
        auto id = m_Impl->bodies[e];
        if (m_Impl->types[e] != BodyType::Dynamic || !reg.Valid(e)) continue;
        RVec3 p; Quat q;
        bi.GetPositionAndRotation(id, p, q);
        glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(p.GetX(), p.GetY(), p.GetZ())) * glm::toMat4(ToG(q))
                        * glm::scale(glm::mat4(1.0f), m_Impl->scales[e]);
        m_Scene.SetWorldMatrix(e, world);
    }
    for (EntityID e : ordered) {
        if (!m_Impl->transforms.contains(e)) continue;
        auto world = m_Scene.GetWorldMatrix(e);
        if (m_Impl->types[e] != BodyType::Dynamic) {
            glm::vec3 p, s, skew; glm::vec4 perspective; glm::quat r;
            glm::decompose(world, s, r, p, skew, perspective);
            bi.SetPositionAndRotation(m_Impl->bodies[e], RVec3(p.x, p.y, p.z), ToJ(glm::normalize(r)), EActivation::DontActivate);
        }
        m_Impl->transforms[e] = world;
    }
}

// ------------------------------------------------------------------------------------------------ API

// Forces are converted into impulses using the caller's time step, so AddForce gives the same result
// whether it is called from Update (any frame rate) or FixedUpdate.
void PhysicsWorld::AddForce(EntityID e, const glm::vec3& f, ForceMode mode, float dt) {
    auto it = m_Impl->bodies.find(e);
    if (it == m_Impl->bodies.end() || m_Impl->types[e] != BodyType::Dynamic) return;
    BodyInterface& bi = m_System->GetBodyInterface();
    switch (mode) {
    case ForceMode::Force: bi.AddImpulse(it->second, ToJ(f * dt)); break;
    case ForceMode::Impulse: bi.AddImpulse(it->second, ToJ(f)); break;
    case ForceMode::VelocityChange: bi.AddLinearVelocity(it->second, ToJ(f)); break;
    case ForceMode::Acceleration: bi.AddLinearVelocity(it->second, ToJ(f * dt)); break;
    }
    bi.ActivateBody(it->second);
}

void PhysicsWorld::AddForceAtPosition(EntityID e, const glm::vec3& f, const glm::vec3& point, ForceMode mode, float dt) {
    auto it = m_Impl->bodies.find(e);
    if (it == m_Impl->bodies.end() || m_Impl->types[e] != BodyType::Dynamic) return;
    glm::vec3 impulse = (mode == ForceMode::Force || mode == ForceMode::Acceleration) ? f * dt : f;
    if (mode == ForceMode::Acceleration || mode == ForceMode::VelocityChange) {
        JPH::BodyLockRead lock(m_System->GetBodyLockInterface(), it->second);
        if (!lock.Succeeded()) return;
        float inverseMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
        if (inverseMass <= 0.0f) return;
        impulse /= inverseMass;
    }
    m_System->GetBodyInterface().AddImpulse(it->second, ToJ(impulse), RVec3(point.x, point.y, point.z));
    m_System->GetBodyInterface().ActivateBody(it->second);
}

void PhysicsWorld::AddTorque(EntityID e, const glm::vec3& t, float dt) {
    auto it = m_Impl->bodies.find(e);
    if (it == m_Impl->bodies.end() || m_Impl->types[e] != BodyType::Dynamic) return;
    m_System->GetBodyInterface().AddAngularImpulse(it->second, ToJ(t * dt));
    m_System->GetBodyInterface().ActivateBody(it->second);
}

glm::vec3 PhysicsWorld::GetVelocity(EntityID e) {
    auto it = m_Impl->bodies.find(e);
    return it == m_Impl->bodies.end() ? glm::vec3(0) : ToG(m_System->GetBodyInterface().GetLinearVelocity(it->second));
}

void PhysicsWorld::SetVelocity(EntityID e, const glm::vec3& v) {
    if (auto it = m_Impl->bodies.find(e); it != m_Impl->bodies.end()) m_System->GetBodyInterface().SetLinearVelocity(it->second, ToJ(v));
}

glm::vec3 PhysicsWorld::GetAngularVelocity(EntityID e) {
    auto it = m_Impl->bodies.find(e);
    return it == m_Impl->bodies.end() ? glm::vec3(0) : ToG(m_System->GetBodyInterface().GetAngularVelocity(it->second));
}

void PhysicsWorld::SetAngularVelocity(EntityID e, const glm::vec3& v) {
    if (auto it = m_Impl->bodies.find(e); it != m_Impl->bodies.end()) m_System->GetBodyInterface().SetAngularVelocity(it->second, ToJ(v));
}

void PhysicsWorld::Teleport(EntityID e, const glm::vec3& pos, const glm::quat& rot) {
    if (auto it = m_Impl->bodies.find(e); it != m_Impl->bodies.end())
        m_System->GetBodyInterface().SetPositionAndRotation(it->second, RVec3(pos.x, pos.y, pos.z), ToJ(rot), EActivation::Activate);
}

bool PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance, RaycastHit& hit, EntityID ignore) {
    if (glm::length(dir) < 1e-6f) return false;
    glm::vec3 d = glm::normalize(dir) * maxDistance;
    RRayCast ray{ RVec3(origin.x, origin.y, origin.z), ToJ(d) };
    RayCastResult result;
    auto ig = m_Impl->bodies.find(ignore);
    IgnoreSingleBodyFilter ignoreFilter(ig != m_Impl->bodies.end() ? ig->second : BodyID());
    BodyFilter anyBody;
    const BodyFilter& filter = ig != m_Impl->bodies.end() ? (const BodyFilter&)ignoreFilter : anyBody;
    if (!m_System->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, filter)) return false;
    hit.distance = result.mFraction * maxDistance;
    hit.point = origin + glm::normalize(dir) * hit.distance;
    BodyLockRead lock(m_System->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        const Body& body = lock.GetBody();
        hit.entity = (EntityID)body.GetUserData();
        hit.normal = ToG(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, RVec3(hit.point.x, hit.point.y, hit.point.z)));
    }
    return true;
}

// Called from Jolt worker threads during Update: the body maps are not modified while it runs
void PhysicsWorld::PushCollision(u32 bodyA, u32 bodyB, bool added) {
    auto ia = m_Impl->bodyToEntity.find(bodyA), ib = m_Impl->bodyToEntity.find(bodyB);
    if (ia == m_Impl->bodyToEntity.end() || ib == m_Impl->bodyToEntity.end()) return;
    auto isSensor = [&](u32 id) { auto it = m_Impl->sensors.find(id); return it != m_Impl->sensors.end() && it->second; };
    bool trigger = isSensor(bodyA) || isSensor(bodyB); // read-only access: safe from worker threads
    ContactKind kind = trigger ? (added ? ContactKind::TriggerEnter : ContactKind::TriggerExit)
                               : (added ? ContactKind::CollisionEnter : ContactKind::CollisionExit);
    std::lock_guard lock(m_EventMutex);
    m_Events.push_back({ ia->second, ib->second, kind });
}

std::vector<CollisionEvent> PhysicsWorld::TakeCollisionEvents() {
    std::lock_guard lock(m_EventMutex);
    std::vector<CollisionEvent> out;
    out.swap(m_Events);
    return out;
}

} // namespace Stardust
