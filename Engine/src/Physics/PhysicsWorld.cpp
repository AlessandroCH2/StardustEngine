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
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
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
    struct CharacterBody { JPH::Ref<CharacterVirtual> character; glm::vec3 written{ 0.0f }; u32 innerBody = 0; };
    std::unordered_map<EntityID, CharacterBody> characters;
    struct JointBody { JPH::Ref<TwoBodyConstraint> constraint; EntityID other = kNullEntity; int type = 0; };
    std::unordered_map<EntityID, JointBody> joints;
    std::vector<EntityID> pendingJoints;
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
    reg.Each<JointComponent>([&](EntityID e, JointComponent&) { if (scene.IsActiveInHierarchy(e)) m_Impl->pendingJoints.push_back(e); });
    SD_INFO("Physics: {} bodies", m_Impl->bodies.size());
}

PhysicsWorld::~PhysicsWorld() {
    BodyInterface& bi = m_System->GetBodyInterface();
    for (auto& [e, v] : m_Impl->vehicles) { m_System->RemoveStepListener(v.constraint); m_System->RemoveConstraint(v.constraint); }
    m_Impl->vehicles.clear();
    for (auto& [e, j] : m_Impl->joints) m_System->RemoveConstraint(j.constraint);
    m_Impl->joints.clear();
    m_Impl->characters.clear();
    for (auto& [e, id] : m_Impl->bodies) { bi.RemoveBody(id); bi.DestroyBody(id); }
    m_Impl->bodies.clear();
    m_System.reset();
    m_Jobs.reset();
    m_Temp.reset();
}

bool PhysicsWorld::HasBody(EntityID e) const { return m_Impl->bodies.contains(e); }
bool PhysicsWorld::HasCharacter(EntityID e) const { return m_Impl->characters.contains(e); }
glm::vec3 PhysicsWorld::Gravity() const { return ToG(m_System->GetGravity()); }
void PhysicsWorld::SetGravity(const glm::vec3& g) { m_System->SetGravity(ToJ(g)); }

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
    if (reg.TryGetEnabled<CharacterControllerComponent>(e) && !reg.TryGetEnabled<RigidbodyComponent>(e)) { CreateCharacter(e); return; }
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
    if (mat.isTrigger) bs.mCollideKinematicVsNonDynamic = true; // Character Controllers fire triggers
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
    DestroyCharacter(e);
    // Joints on this body or connected to it: rebuilt on the next step if both bodies exist again
    std::vector<EntityID> affected;
    for (auto& [owner, j] : m_Impl->joints) if (owner == e || j.other == e) affected.push_back(owner);
    for (EntityID owner : affected) { DestroyJoint(owner); m_Impl->pendingJoints.push_back(owner); }
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
    auto changed=[](const glm::mat4& a,const glm::mat4& b){
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>.00001f)return true;
        return false;
    };
    std::vector<EntityID> resize;
    for(auto& [e,previous]:m_Impl->transforms){
        if(!reg.Valid(e))continue;
        glm::mat4 world=m_Scene.GetWorldMatrix(e);
        const bool moved=changed(world,previous);auto type=m_Impl->types[e];
        if(!moved&&type!=BodyType::Kinematic)continue;
        glm::vec3 pos,scale,skew;glm::vec4 perspective;glm::quat rot;
        glm::decompose(world,scale,rot,pos,skew,perspective);
        if(moved&&glm::length(glm::abs(scale)-m_Impl->scales[e])>.0001f){resize.push_back(e);continue;}
        if(type==BodyType::Kinematic)bi.MoveKinematic(m_Impl->bodies[e],RVec3(pos.x,pos.y,pos.z),ToJ(glm::normalize(rot)),dt);
        else bi.SetPositionAndRotation(m_Impl->bodies[e],RVec3(pos.x,pos.y,pos.z),ToJ(glm::normalize(rot)),EActivation::Activate);
        previous=world;
    }
    for(EntityID e:resize)RecreateBody(e);

    // Vehicle inputs from the components (set by scripts)
    for (auto& [e, v] : m_Impl->vehicles) {
        auto* vc = reg.TryGet<VehicleControllerComponent>(e);
        if (!vc || !m_Impl->bodies.contains(e)) continue;
        auto* controller = static_cast<WheeledVehicleController*>(v.constraint->GetController());
        controller->SetDriverInput(std::clamp(vc->throttle, -1.0f, 1.0f), std::clamp(vc->steer, -1.0f, 1.0f), std::clamp(vc->brake, 0.0f, 1.0f), std::clamp(vc->handbrake, 0.0f, 1.0f));
        if (vc->throttle != 0 || vc->steer != 0) bi.ActivateBody(m_Impl->bodies[e]);
    }

    if (!m_Impl->pendingJoints.empty()) {
        auto pending = std::move(m_Impl->pendingJoints); m_Impl->pendingJoints.clear();
        std::sort(pending.begin(), pending.end()); pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
        for (EntityID e : pending) CreateJoint(e);
    }

    m_System->Update(dt, 1, m_Temp.get(), m_Jobs.get());
    UpdateJoints(dt);

    // Parents must be written before children; unordered body iteration can otherwise apply an
    // ancestor's movement twice to a dynamic child.
    std::vector<std::pair<int,EntityID>> ordered;
    for(auto& [e,id]:m_Impl->bodies)if(reg.Valid(e)&&m_Impl->types[e]==BodyType::Dynamic){
        int depth=0;for(EntityID p=m_Scene.GetParent(e);p;p=m_Scene.GetParent(p))++depth;
        ordered.emplace_back(depth,e);
    }
    std::sort(ordered.begin(),ordered.end());
    for(auto [depth,e]:ordered){
        auto id=m_Impl->bodies[e];RVec3 p;Quat q;bi.GetPositionAndRotation(id,p,q);
        glm::mat4 world=glm::translate(glm::mat4(1),glm::vec3(p.GetX(),p.GetY(),p.GetZ()))*glm::toMat4(ToG(q))*glm::scale(glm::mat4(1),m_Impl->scales[e]);
        if(changed(world,m_Impl->transforms[e]))m_Scene.SetWorldMatrix(e,world);
    }
    // Static children of moving bodies still follow their parents; unchanged city geometry needs no Jolt write.
    for(auto& [e,previous]:m_Impl->transforms){
        if(!reg.Valid(e))continue;auto world=m_Scene.GetWorldMatrix(e);
        if(m_Impl->types[e]!=BodyType::Dynamic&&changed(world,previous)){
            glm::vec3 p,s,skew;glm::vec4 perspective;glm::quat r;glm::decompose(world,s,r,p,skew,perspective);
            bi.SetPositionAndRotation(m_Impl->bodies[e],RVec3(p.x,p.y,p.z),ToJ(glm::normalize(r)),EActivation::DontActivate);
        }
        previous=world;
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

// ------------------------------------------------------------------------------------------------ character controller

void PhysicsWorld::CreateCharacter(EntityID e) {
    Registry& reg = m_Scene.Reg();
    auto& cc = reg.Get<CharacterControllerComponent>(e);
    glm::vec3 pos, scale, skew; glm::vec4 persp; glm::quat rot;
    glm::decompose(m_Scene.GetWorldMatrix(e), scale, rot, pos, skew, persp);
    scale = glm::abs(scale);
    const float radius = std::max(0.02f, cc.radius * std::max(scale.x, scale.z));
    const float halfHeight = std::max(0.01f, cc.height * 0.5f * scale.y - radius);
    RefConst<Shape> shape = new CapsuleShape(halfHeight, radius);
    shape = new RotatedTranslatedShape(ToJ(cc.center * scale), Quat::sIdentity(), shape);
    JPH::Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings;
    settings->mShape = shape;
    settings->mInnerBodyShape = shape;           // ray casts and triggers see the character
    settings->mInnerBodyLayer = Layers::Moving;
    settings->mMaxSlopeAngle = glm::radians(std::clamp(cc.slopeLimit, 0.0f, 89.0f));
    settings->mCharacterPadding = std::clamp(cc.skinWidth, 0.001f, 0.2f);
    settings->mMass = std::max(1.0f, cc.mass);
    settings->mMaxStrength = std::max(0.0f, cc.pushForce);
    settings->mSupportingVolume = Plane(Vec3::sAxisY(), -radius + cc.center.y * scale.y - halfHeight + radius * 0.5f);
    settings->mEnhancedInternalEdgeRemoval = true;
    Impl::CharacterBody body;
    body.character = new CharacterVirtual(settings, RVec3(pos.x, pos.y, pos.z), Quat::sIdentity(), e, m_System.get());
    body.written = pos;
    BodyID inner = body.character->GetInnerBodyID();
    if (!inner.IsInvalid()) {
        m_System->GetBodyInterface().SetUserData(inner, e);
        body.innerBody = inner.GetIndexAndSequenceNumber();
        RegisterBody(e, body.innerBody, false);
    }
    m_Impl->characters[e] = std::move(body);
}

void PhysicsWorld::DestroyCharacter(EntityID e) {
    auto it = m_Impl->characters.find(e);
    if (it == m_Impl->characters.end()) return;
    if (it->second.innerBody) { m_Impl->bodyToEntity.erase(it->second.innerBody); m_Impl->sensors.erase(it->second.innerBody); }
    m_Impl->characters.erase(it); // the inner body is removed with the character
}

int PhysicsWorld::MoveCharacter(EntityID e, const glm::vec3& motion, float dt) {
    auto it = m_Impl->characters.find(e);
    if (it == m_Impl->characters.end()) return 0;
    Registry& reg = m_Scene.Reg();
    auto* cc = reg.TryGet<CharacterControllerComponent>(e);
    if (!cc) return 0;
    auto& body = it->second;
    CharacterVirtual& ch = *body.character;
    // Moved from a script or the editor since the last move: start from there
    glm::vec3 world = glm::vec3(m_Scene.GetWorldMatrix(e)[3]);
    if (glm::length(world - body.written) > 1e-4f) ch.SetPosition(RVec3(world.x, world.y, world.z));
    if (glm::length(motion) < std::max(cc->minMoveDistance, 1e-7f) || dt <= 0.0f) return cc->collisionFlags;
    dt = std::max(dt, 1e-4f);
    const glm::vec3 before = ToG(Vec3(ch.GetPosition()));
    ch.SetLinearVelocity(ToJ(motion / dt));
    CharacterVirtual::ExtendedUpdateSettings update;
    update.mWalkStairsStepUp = Vec3(0, std::max(0.0f, cc->stepOffset), 0);
    update.mStickToFloorStepDown = Vec3(0, motion.y > 0.0f ? 0.0f : -std::max(0.0f, cc->stepOffset), 0);
    ch.ExtendedUpdate(dt, Vec3::sZero(), update,
        m_System->GetDefaultBroadPhaseLayerFilter(Layers::Moving), m_System->GetDefaultLayerFilter(Layers::Moving),
        {}, {}, *m_Temp);
    const glm::vec3 after = ToG(Vec3(ch.GetPosition()));
    int flags = 0;
    for (const auto& contact : ch.GetActiveContacts()) {
        if (!contact.mHadCollision || contact.mIsSensorB) continue;
        const float up = contact.mContactNormal.GetY();
        flags |= up > 0.55f ? 4 : up < -0.55f ? 2 : 1;
    }
    cc->grounded = ch.IsSupported() || (flags & 4);
    if (cc->grounded) flags |= 4;
    cc->groundNormal = ch.IsSupported() ? ToG(ch.GetGroundNormal()) : glm::vec3(0, 1, 0);
    cc->collisionFlags = flags;
    cc->velocity = (after - before) / dt;
    // Write the new position, keeping rotation and scale
    glm::mat4 m = m_Scene.GetWorldMatrix(e);
    m[3] = glm::vec4(after, 1.0f);
    m_Scene.SetWorldMatrix(e, m);
    body.written = after;
    return flags;
}

// ------------------------------------------------------------------------------------------------ joints

void PhysicsWorld::RebuildJoint(EntityID e) { DestroyJoint(e); m_Impl->pendingJoints.push_back(e); }

void PhysicsWorld::DestroyJoint(EntityID e) {
    auto it = m_Impl->joints.find(e);
    if (it == m_Impl->joints.end()) return;
    m_System->RemoveConstraint(it->second.constraint);
    m_Impl->joints.erase(it);
}

static Vec3 Perpendicular(Vec3Arg v) { return v.GetNormalizedPerpendicular(); }

void PhysicsWorld::CreateJoint(EntityID e) {
    Registry& reg = m_Scene.Reg();
    if (!reg.Valid(e) || m_Impl->joints.contains(e)) return;
    auto* j = reg.TryGetEnabled<JointComponent>(e);
    if (!j || j->broken || !m_Scene.IsActiveInHierarchy(e)) return;
    auto self = m_Impl->bodies.find(e);
    if (self == m_Impl->bodies.end()) return;
    EntityID other = j->connectedBody ? m_Scene.FindByUUID(j->connectedBody) : kNullEntity;
    if (j->connectedBody && (!other || !m_Impl->bodies.contains(other))) return; // waits for the other body
    const glm::mat4 world = m_Scene.GetWorldMatrix(e);
    const glm::vec3 anchor = glm::vec3(world * glm::vec4(j->anchor, 1.0f));
    glm::vec3 axisG = glm::mat3(world) * j->axis;
    if (glm::length(axisG) < 1e-6f) axisG = glm::vec3(1, 0, 0);
    const Vec3 axis = ToJ(glm::normalize(axisG));
    const Vec3 normal = Perpendicular(axis);
    const RVec3 point(anchor.x, anchor.y, anchor.z);
    JPH::Ref<TwoBodyConstraintSettings> settings;
    SpringSettings spring(ESpringMode::FrequencyAndDamping, std::max(0.0f, j->springFrequency), std::clamp(j->springDamping, 0.0f, 1.0f));
    switch ((JointType)j->type) {
    case JointType::Fixed: {
        auto* s = new FixedConstraintSettings; s->mSpace = EConstraintSpace::WorldSpace; s->mAutoDetectPoint = true; settings = s; break;
    }
    case JointType::Hinge: {
        auto* s = new HingeConstraintSettings; s->mSpace = EConstraintSpace::WorldSpace;
        s->mPoint1 = s->mPoint2 = point; s->mHingeAxis1 = s->mHingeAxis2 = axis; s->mNormalAxis1 = s->mNormalAxis2 = normal;
        if (j->useLimits) { s->mLimitsMin = glm::radians(std::clamp(std::min(j->minLimit, j->maxLimit), -179.0f, 0.0f)); s->mLimitsMax = glm::radians(std::clamp(std::max(j->minLimit, j->maxLimit), 0.0f, 179.0f)); s->mLimitsSpringSettings = spring; }
        s->mMaxFrictionTorque = std::max(0.0f, j->friction);
        s->mMotorSettings = MotorSettings(0.0f, 0.0f, std::max(0.0f, j->motorForce), std::max(0.0f, j->motorForce));
        settings = s; break;
    }
    case JointType::Spring: {
        auto* s = new DistanceConstraintSettings; s->mSpace = EConstraintSpace::WorldSpace;
        Vec3 otherPoint = other ? m_System->GetBodyInterface().GetCenterOfMassPosition(m_Impl->bodies[other]) : point;
        s->mPoint1 = point; s->mPoint2 = other ? RVec3(otherPoint) : point;
        const float rest = other ? (float)(Vec3(otherPoint) - Vec3(point)).Length() : 0.0f;
        s->mMinDistance = j->useLimits ? std::max(0.0f, j->minLimit) : 0.0f;
        s->mMaxDistance = j->useLimits ? std::max(s->mMinDistance, j->maxLimit) : rest;
        if (!j->useLimits) s->mMinDistance = rest;
        s->mLimitsSpringSettings = spring;
        settings = s; break;
    }
    case JointType::Slider: {
        auto* s = new SliderConstraintSettings; s->mSpace = EConstraintSpace::WorldSpace;
        s->mPoint1 = s->mPoint2 = point; s->mSliderAxis1 = s->mSliderAxis2 = axis; s->mNormalAxis1 = s->mNormalAxis2 = normal;
        if (j->useLimits) { s->mLimitsMin = std::min(0.0f, std::min(j->minLimit, j->maxLimit)); s->mLimitsMax = std::max(0.0f, std::max(j->minLimit, j->maxLimit)); s->mLimitsSpringSettings = spring; }
        s->mMaxFrictionForce = std::max(0.0f, j->friction);
        s->mMotorSettings = MotorSettings(0.0f, 0.0f, std::max(0.0f, j->motorForce), std::max(0.0f, j->motorForce));
        settings = s; break;
    }
    case JointType::Ball: default: {
        auto* s = new SwingTwistConstraintSettings; s->mSpace = EConstraintSpace::WorldSpace;
        s->mPosition1 = s->mPosition2 = point; s->mTwistAxis1 = s->mTwistAxis2 = axis; s->mPlaneAxis1 = s->mPlaneAxis2 = normal;
        const float swing = glm::radians(std::clamp(j->swingLimit, 0.0f, 179.0f));
        s->mNormalHalfConeAngle = s->mPlaneHalfConeAngle = j->useLimits ? swing : JPH_PI;
        s->mTwistMinAngle = j->useLimits ? glm::radians(std::clamp(std::min(j->twistMin, j->twistMax), -179.0f, 0.0f)) : -JPH_PI;
        s->mTwistMaxAngle = j->useLimits ? glm::radians(std::clamp(std::max(j->twistMin, j->twistMax), 0.0f, 179.0f)) : JPH_PI;
        s->mMaxFrictionTorque = std::max(0.0f, j->friction);
        settings = s; break;
    }
    }
    const BodyLockInterfaceNoLock& locks = m_System->GetBodyLockInterfaceNoLock();
    Body* a = locks.TryGetBody(self->second);
    Body* b = other ? locks.TryGetBody(m_Impl->bodies[other]) : &Body::sFixedToWorld;
    if (!a || !b) return;
    if (a->IsStatic() && b->IsStatic()) { SD_WARN("Joint on '{}': both bodies are static", reg.Get<NameComponent>(e).name); return; }
    Impl::JointBody joint;
    joint.constraint = settings->Create(*a, *b);
    joint.other = other;
    joint.type = j->type;
    if (auto* hinge = dynamic_cast<HingeConstraint*>(joint.constraint.GetPtr()); hinge && j->useMotor) { hinge->SetMotorState(EMotorState::Velocity); hinge->SetTargetAngularVelocity(glm::radians(j->motorTargetVelocity)); }
    if (auto* slider = dynamic_cast<SliderConstraint*>(joint.constraint.GetPtr()); slider && j->useMotor) { slider->SetMotorState(EMotorState::Velocity); slider->SetTargetVelocity(j->motorTargetVelocity); }
    m_System->AddConstraint(joint.constraint);
    // Without collision between the pair, connected limbs (ragdolls) do not push each other apart
    if (!j->enableCollision && other) {
        // Jolt has no per-pair filter here: a shared collision group with a sub group per body
        // is the usual way; bodies keep colliding with everything else.
        static u32 s_NextGroup = 1;
        auto& bi = m_System->GetBodyInterface();
        const CollisionGroup& ga = a->GetCollisionGroup(); const CollisionGroup& gb = b->GetCollisionGroup();
        if (!ga.GetGroupFilter() && !gb.GetGroupFilter()) {
            JPH::Ref<GroupFilterTable> table = new GroupFilterTable(2);
            table->DisableCollision(0, 1);
            CollisionGroup::GroupID group = s_NextGroup++;
            a->SetCollisionGroup(CollisionGroup(table, group, 0));
            b->SetCollisionGroup(CollisionGroup(table, group, 1));
        }
    }
    bool wake = true;
    if (wake) { m_System->GetBodyInterface().ActivateBody(self->second); if (other) m_System->GetBodyInterface().ActivateBody(m_Impl->bodies[other]); }
    m_Impl->joints[e] = std::move(joint);
}

void PhysicsWorld::UpdateJoints(float dt) {
    if (m_Impl->joints.empty() || dt <= 0.0f) return;
    Registry& reg = m_Scene.Reg();
    std::vector<EntityID> broken;
    for (auto& [e, joint] : m_Impl->joints) {
        auto* j = reg.TryGet<JointComponent>(e);
        if (!j) { broken.push_back(e); continue; }
        // Lambdas are impulses over the step: divide by dt for forces
        Vec3 force = Vec3::sZero(), torque = Vec3::sZero();
        Constraint* c = joint.constraint.GetPtr();
        if (auto* f = dynamic_cast<FixedConstraint*>(c)) { force = f->GetTotalLambdaPosition(); torque = f->GetTotalLambdaRotation(); }
        else if (auto* h = dynamic_cast<HingeConstraint*>(c)) { force = h->GetTotalLambdaPosition(); auto r = h->GetTotalLambdaRotation(); torque = Vec3(r[0], r[1], h->GetTotalLambdaRotationLimits()); j->currentAngle = glm::degrees(h->GetCurrentAngle());
            if (j->useMotor) { h->SetMotorState(EMotorState::Velocity); h->SetTargetAngularVelocity(glm::radians(j->motorTargetVelocity)); } else h->SetMotorState(EMotorState::Off); }
        else if (auto* d = dynamic_cast<DistanceConstraint*>(c)) { force = Vec3(d->GetTotalLambdaPosition(), 0, 0); }
        else if (auto* s = dynamic_cast<SliderConstraint*>(c)) { auto p = s->GetTotalLambdaPosition(); force = Vec3(p[0], p[1], s->GetTotalLambdaPositionLimits()); torque = s->GetTotalLambdaRotation(); j->currentAngle = s->GetCurrentPosition();
            if (j->useMotor) { s->SetMotorState(EMotorState::Velocity); s->SetTargetVelocity(j->motorTargetVelocity); } else s->SetMotorState(EMotorState::Off); }
        else if (auto* t = dynamic_cast<SwingTwistConstraint*>(c)) { force = t->GetTotalLambdaPosition(); torque = Vec3(t->GetTotalLambdaTwist(), t->GetTotalLambdaSwingY(), t->GetTotalLambdaSwingZ()); }
        j->currentForce = ToG(force) / dt; j->currentTorque = ToG(torque) / dt;
        const bool overForce = j->breakForce > 0.0f && glm::length(j->currentForce) > j->breakForce;
        const bool overTorque = j->breakTorque > 0.0f && glm::length(j->currentTorque) > j->breakTorque;
        if (overForce || overTorque) { j->broken = true; broken.push_back(e); }
    }
    for (EntityID e : broken) {
        DestroyJoint(e);
        if (auto* j = reg.TryGet<JointComponent>(e); j && j->broken) {
            std::lock_guard lock(m_EventMutex);
            m_Events.push_back({ e, e, ContactKind::JointBreak });
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
