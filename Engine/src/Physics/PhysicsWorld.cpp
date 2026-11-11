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
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
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
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
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
    struct Vehicle { JPH::Ref<VehicleConstraint> constraint; JPH::Ref<VehicleCollisionTester> tester; std::vector<EntityID> wheels; };
    std::unordered_map<EntityID, Vehicle> vehicles;
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
    for (auto& [e, v] : m_Impl->vehicles) { m_System->RemoveStepListener(v.constraint); m_System->RemoveConstraint(v.constraint); }
    m_Impl->vehicles.clear();
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
    if(reg.Has<Rigidbody2DComponent>(e)&&!reg.IsEnabled<Rigidbody2DComponent>(e))return;
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
    auto* rb2 = reg.TryGetEnabled<Rigidbody2DComponent>(e);
    auto* box2 = reg.TryGetEnabled<BoxCollider2DComponent>(e);
    auto* circle2 = reg.TryGetEnabled<CircleCollider2DComponent>(e);
    auto* capsule2 = reg.TryGetEnabled<CapsuleCollider2DComponent>(e);
    const bool is2D = !box && !sphere && !capsule && !meshCol && (box2 || circle2 || capsule2);
    if (!box && !sphere && !capsule && !meshCol && !is2D) {
        if (rb || rb2) SD_WARN("'{}' has a Rigidbody but no collider", reg.Get<NameComponent>(e).name);
        return;
    }

    glm::vec3 pos, scale, skew; glm::vec4 persp; glm::quat rot;
    glm::decompose(m_Scene.GetWorldMatrix(e), scale, rot, pos, skew, persp);
    rot = glm::normalize(rot);
    scale = glm::abs(scale);
    BodyType type = is2D ? (rb2 ? rb2->type : BodyType::Static) : rb ? rb->type : BodyType::Static;

    RefConst<Shape> shape;
    glm::vec3 center(0.0f);
    PhysicsMaterialProps mat;
    // 2D shapes are deep slabs, so bodies at any Z meet like in a 2D world
    constexpr float kDepth2D = 50.0f;
    if (is2D && box2) {
        const glm::vec2 he = glm::max(box2->size * glm::vec2(scale) * 0.5f, glm::vec2(0.005f));
        shape = new BoxShape(Vec3(he.x, he.y, kDepth2D), std::min(0.02f, std::min(he.x, he.y) * 0.5f));
        center = glm::vec3(box2->offset * glm::vec2(scale), 0); mat = box2->material;
    } else if (is2D && circle2) {
        const float radius = std::max(0.005f, circle2->radius * std::max(scale.x, scale.y));
        // Cylinders stand on Y: turned to lie along Z they are circles in XY
        shape = new RotatedTranslatedShape(Vec3::sZero(), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), new CylinderShape(kDepth2D, radius, std::min(0.02f, radius * 0.5f)));
        center = glm::vec3(circle2->offset * glm::vec2(scale), 0); mat = circle2->material;
    } else if (is2D && capsule2) {
        const float radius = std::max(0.005f, capsule2->size.x * 0.5f * scale.x);
        const float half = std::max(0.005f, capsule2->size.y * 0.5f * scale.y - radius);
        shape = new CapsuleShape(half, radius);
        center = glm::vec3(capsule2->offset * glm::vec2(scale), 0); mat = capsule2->material;
    } else if (box) {
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
    auto* vehicle = !is2D && rb && rb->type == BodyType::Dynamic ? reg.TryGetEnabled<VehicleControllerComponent>(e) : nullptr;
    if (vehicle && std::abs(vehicle->centerOfMassOffset) > 1e-4f) {
        auto r = OffsetCenterOfMassShapeSettings(Vec3(0, vehicle->centerOfMassOffset, 0), shape).Create();
        if (!r.HasError()) shape = r.Get();
    }

    EMotionType motion = type == BodyType::Dynamic ? EMotionType::Dynamic : type == BodyType::Kinematic ? EMotionType::Kinematic : EMotionType::Static;
    BodyCreationSettings bs(shape, RVec3(pos.x, pos.y, pos.z), ToJ(rot), motion, motion == EMotionType::Static ? Layers::NonMoving : Layers::Moving);
    bs.mFriction = mat.friction;
    bs.mRestitution = mat.restitution;
    bs.mIsSensor = mat.isTrigger;
    bs.mUserData = e;
    if (is2D && rb2) {
        bs.mLinearDamping = rb2->linearDamping;
        bs.mAngularDamping = rb2->angularDamping;
        bs.mGravityFactor = rb2->gravityScale;
        if (motion == EMotionType::Dynamic) {
            bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            bs.mMassPropertiesOverride.mMass = std::max(rb2->mass, 0.001f);
            // Locked to the XY plane, turning only around Z
            bs.mAllowedDOFs = rb2->freezeRotation ? EAllowedDOFs::TranslationX | EAllowedDOFs::TranslationY : EAllowedDOFs::Plane2D;
        }
    } else if (rb) {
        bs.mLinearDamping = rb->linearDamping;
        bs.mAngularDamping = rb->angularDamping;
        bs.mGravityFactor = rb->useGravity ? 1.0f : 0.0f;
        if (motion == EMotionType::Dynamic) {
            bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            bs.mMassPropertiesOverride.mMass = std::max(rb->mass, 0.001f);
            if (rb->freezeRotation && !vehicle) bs.mAllowedDOFs = EAllowedDOFs::TranslationX | EAllowedDOFs::TranslationY | EAllowedDOFs::TranslationZ;
        }
    }
    BodyID id = m_System->GetBodyInterface().CreateAndAddBody(bs, motion == EMotionType::Static ? EActivation::DontActivate : EActivation::Activate);
    if (id.IsInvalid()) { SD_ERROR("Physics body limit reached"); return; }
    m_Impl->bodies[e] = id;
    m_Impl->types[e] = type;
    m_Impl->scales[e] = scale;
    m_Impl->transforms[e] = m_Scene.GetWorldMatrix(e);
    RegisterBody(e, id.GetIndexAndSequenceNumber(), mat.isTrigger);
    if (vehicle) CreateVehicle(e);
}

// ------------------------------------------------------------------------------------------------ vehicles

static void CollectWheels(Scene& scene, EntityID e, std::vector<EntityID>& out) {
    for (EntityID c : scene.GetChildren(e)) {
        if (scene.Reg().TryGetEnabled<WheelColliderComponent>(c) && scene.IsActiveInHierarchy(c)) out.push_back(c);
        CollectWheels(scene, c, out);
    }
}

static void ScaleCurve(LinearCurve& curve, float scale) { for (auto& p : curve.mPoints) p.mY *= scale; }

void PhysicsWorld::CreateVehicle(EntityID e) {
    Registry& reg = m_Scene.Reg();
    auto& vc = reg.Get<VehicleControllerComponent>(e);
    std::vector<EntityID> wheels;
    CollectWheels(m_Scene, e, wheels);
    if (wheels.empty()) { SD_WARN("Vehicle Controller on '{}' has no Wheel Collider children", reg.Get<NameComponent>(e).name); return; }

    const glm::mat4 rootInverse = glm::inverse(m_Scene.GetWorldMatrix(e));
    VehicleConstraintSettings settings;
    settings.mUp = Vec3(0, 1, 0);
    settings.mForward = Vec3(0, 0, -1);       // the engine's forward
    settings.mMaxPitchRollAngle = glm::radians(std::clamp(vc.maxPitchRoll, 1.0f, 180.0f));
    std::vector<glm::vec3> positions;
    for (EntityID w : wheels) {
        auto& wc = reg.Get<WheelColliderComponent>(w);
        if (!wc.restCaptured) { wc.restPosition = glm::vec3(rootInverse * m_Scene.GetWorldMatrix(w)[3]); wc.restCaptured = true; }
        positions.push_back(wc.restPosition);
        JPH::Ref<WheelSettingsWV> ws = new WheelSettingsWV;
        const float travel = std::max(0.02f, wc.suspensionDistance);
        ws->mPosition = ToJ(wc.restPosition + glm::vec3(0, travel, 0));
        ws->mSuspensionDirection = Vec3(0, -1, 0);
        ws->mSteeringAxis = Vec3(0, 1, 0);
        ws->mWheelUp = Vec3(0, 1, 0);
        ws->mWheelForward = Vec3(0, 0, -1);
        ws->mSuspensionMinLength = 0.0f;
        ws->mSuspensionMaxLength = travel;
        ws->mSuspensionSpring.mFrequency = std::max(0.1f, wc.springFrequency);
        ws->mSuspensionSpring.mDamping = std::clamp(wc.springDamping, 0.0f, 1.0f);
        ws->mRadius = std::max(0.05f, wc.radius);
        ws->mWidth = std::max(0.02f, wc.width);
        ws->mMaxSteerAngle = glm::radians(std::clamp(wc.maxSteerAngle, 0.0f, 89.0f));
        ws->mMaxBrakeTorque = std::max(0.0f, wc.brakeTorque);
        ws->mMaxHandBrakeTorque = wc.handbrake ? std::max(0.0f, wc.handbrakeTorque) : 0.0f;
        ScaleCurve(ws->mLongitudinalFriction, std::max(0.0f, wc.forwardFriction));
        ScaleCurve(ws->mLateralFriction, std::max(0.0f, wc.sidewaysFriction));
        settings.mWheels.push_back(ws.GetPtr());
    }
    // Wheels on the same axle (similar Z, opposite sides) form pairs: driven pairs get a differential,
    // every pair an anti-roll bar
    std::vector<std::pair<int, int>> axles;
    std::vector<bool> used(wheels.size(), false);
    for (size_t i = 0; i < wheels.size(); ++i) {
        if (used[i]) continue;
        int partner = -1;
        for (size_t j = i + 1; j < wheels.size(); ++j)
            if (!used[j] && std::abs(positions[j].z - positions[i].z) < 0.3f && (positions[j].x > 0) != (positions[i].x > 0)) { partner = (int)j; break; }
        used[i] = true; if (partner >= 0) used[partner] = true;
        int left = (int)i, right = partner;
        if (partner >= 0 && positions[i].x > positions[partner].x) std::swap(left, right);
        axles.push_back({ left, right });
    }
    JPH::Ref<WheeledVehicleControllerSettings> controller = new WheeledVehicleControllerSettings;
    controller->mEngine.mMaxTorque = std::max(1.0f, vc.engineTorque);
    controller->mEngine.mMinRPM = std::max(100.0f, vc.minRPM);
    controller->mEngine.mMaxRPM = std::max(controller->mEngine.mMinRPM + 100.0f, vc.maxRPM);
    controller->mTransmission.mShiftUpRPM = vc.shiftUpRPM;
    controller->mTransmission.mShiftDownRPM = vc.shiftDownRPM;
    controller->mDifferentialLimitedSlipRatio = std::max(1.0f, vc.limitedSlipRatio);
    for (auto [left, right] : axles) {
        bool driven = reg.Get<WheelColliderComponent>(wheels[left]).motor || (right >= 0 && reg.Get<WheelColliderComponent>(wheels[right]).motor);
        if (driven) { VehicleDifferentialSettings d; d.mLeftWheel = left; d.mRightWheel = right; controller->mDifferentials.push_back(d); }
        if (vc.antiRollBars && right >= 0) { VehicleAntiRollBar bar; bar.mLeftWheel = left; bar.mRightWheel = right; settings.mAntiRollBars.push_back(bar); }
    }
    for (auto& d : controller->mDifferentials) d.mEngineTorqueRatio = 1.0f / controller->mDifferentials.size();
    settings.mController = controller;

    auto id = m_Impl->bodies[e];
    BodyLockWrite lock(m_System->GetBodyLockInterface(), id);
    if (!lock.Succeeded()) return;
    Impl::Vehicle v;
    v.constraint = new VehicleConstraint(lock.GetBody(), settings);
    v.tester = new VehicleCollisionTesterCastCylinder(Layers::Moving);
    v.constraint->SetVehicleCollisionTester(v.tester);
    v.wheels = wheels;
    m_System->AddConstraint(v.constraint);
    m_System->AddStepListener(v.constraint);
    m_Impl->vehicles[e] = std::move(v);
}

void PhysicsWorld::DestroyVehicle(EntityID e) {
    auto it = m_Impl->vehicles.find(e);
    if (it == m_Impl->vehicles.end()) return;
    m_System->RemoveStepListener(it->second.constraint);
    m_System->RemoveConstraint(it->second.constraint);
    m_Impl->vehicles.erase(it);
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
    DestroyVehicle(e);
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

    // Vehicle inputs from the components (set by scripts)
    for (auto& [e, v] : m_Impl->vehicles) {
        auto* vc = reg.TryGet<VehicleControllerComponent>(e);
        if (!vc || !m_Impl->bodies.contains(e)) continue;
        auto* controller = static_cast<WheeledVehicleController*>(v.constraint->GetController());
        controller->SetDriverInput(std::clamp(vc->throttle, -1.0f, 1.0f), std::clamp(vc->steer, -1.0f, 1.0f), std::clamp(vc->brake, 0.0f, 1.0f), std::clamp(vc->handbrake, 0.0f, 1.0f));
        if (vc->throttle != 0 || vc->steer != 0) bi.ActivateBody(m_Impl->bodies[e]);
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

    // Vehicles: read back speed and engine state, place the wheel objects (suspension, steering, rolling)
    for (auto& [e, v] : m_Impl->vehicles) {
        auto* vc = reg.TryGet<VehicleControllerComponent>(e);
        if (!vc || !m_Impl->bodies.contains(e)) continue;
        auto id = m_Impl->bodies[e];
        Vec3 velocity = bi.GetLinearVelocity(id);
        Quat rotation = bi.GetRotation(id);
        vc->speed = velocity.Dot(rotation * Vec3(0, 0, -1));
        auto* controller = static_cast<WheeledVehicleController*>(v.constraint->GetController());
        vc->rpm = controller->GetEngine().GetCurrentRPM();
        vc->gear = controller->GetTransmission().GetCurrentGear();
        for (size_t i = 0; i < v.wheels.size(); ++i) {
            EntityID w = v.wheels[i];
            if (!reg.Valid(w)) continue;
            auto& wc = reg.Get<WheelColliderComponent>(w);
            const Wheel* wheel = v.constraint->GetWheel((uint)i);
            wc.steerAngle = glm::degrees(wheel->GetSteerAngle());
            wc.rotationAngle = glm::degrees(wheel->GetRotationAngle());
            wc.suspensionLength = wheel->GetSuspensionLength();
            wc.compression = 1.0f - wc.suspensionLength / std::max(0.02f, wc.suspensionDistance);
            wc.grounded = wheel->HasContact();
            RMat44 m = v.constraint->GetWheelWorldTransform((uint)i, Vec3(1, 0, 0), Vec3(0, 1, 0));
            glm::mat4 world(1.0f);
            for (int c = 0; c < 4; ++c) { Vec4 col = m.GetColumn4(c); world[c] = glm::vec4(col.GetX(), col.GetY(), col.GetZ(), col.GetW()); }
            glm::vec3 scale = reg.Get<TransformComponent>(w).scale;
            m_Scene.SetWorldMatrix(w, world * glm::scale(glm::mat4(1.0f), scale));
        }
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
