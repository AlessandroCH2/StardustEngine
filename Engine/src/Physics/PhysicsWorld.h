#pragma once
#include "Core/Base.h"
#include "Scene/Registry.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <mutex>

namespace JPH { class PhysicsSystem; class TempAllocatorImpl; class JobSystemThreadPool; class BodyInterface; }

namespace Stardust {

class Scene;

enum class ForceMode : int { Force = 0, Impulse = 1, VelocityChange = 2, Acceleration = 3 };

struct RaycastHit {
    EntityID entity = kNullEntity;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    float distance = 0.0f;
};

enum class ContactKind : int { CollisionEnter = 0, CollisionExit = 1, TriggerEnter = 2, TriggerExit = 3, JointBreak = 4 };
struct CollisionEvent { EntityID a, b; ContactKind kind; };

// Jolt Physics wrapper for one running scene (created when entering play mode)
class PhysicsWorld {
public:
    static void InitJolt();
    static void ShutdownJolt();

    explicit PhysicsWorld(Scene& scene);
    ~PhysicsWorld();

    void CreateBody(EntityID e);     // from the entity's Rigidbody / Collider components
    void DestroyBody(EntityID e);
    void Step(float dt);             // one fixed step + transform sync

    // dt = the time step of the caller (frame dt in Update, fixed dt in FixedUpdate): forces stay frame-rate independent
    void AddForce(EntityID e, const glm::vec3& f, ForceMode mode, float dt);
    void AddForceAtPosition(EntityID e, const glm::vec3& f, const glm::vec3& point, ForceMode mode, float dt);
    void AddTorque(EntityID e, const glm::vec3& t, float dt);
    void RecreateBody(EntityID e);   // after component changes, keeps the velocity
    bool HasBody(EntityID e) const;
    glm::vec3 GetVelocity(EntityID e);
    void SetVelocity(EntityID e, const glm::vec3& v);
    glm::vec3 GetAngularVelocity(EntityID e);
    void SetAngularVelocity(EntityID e, const glm::vec3& v);
    void Teleport(EntityID e, const glm::vec3& pos, const glm::quat& rot);
    bool Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance, RaycastHit& hit, EntityID ignore = kNullEntity);

    std::vector<CollisionEvent> TakeCollisionEvents();

    // Character Controller: moves the capsule by `motion` now (slides, steps, slopes); returns the
    // collision flags (1 Sides, 2 Above, 4 Below). dt is the caller's time step.
    int MoveCharacter(EntityID e, const glm::vec3& motion, float dt);
    bool HasCharacter(EntityID e) const;
    // Joints: (re)built on the next step after a change
    void RebuildJoint(EntityID e);
    glm::vec3 Gravity() const;
    void SetGravity(const glm::vec3& g);

    // internal (contact listener)
    void PushCollision(u32 bodyA, u32 bodyB, bool added);

private:
    void CreateTerrainBody(EntityID e);
    void CreateVehicle(EntityID e);
    void DestroyVehicle(EntityID e);
    void CreateCharacter(EntityID e);
    void DestroyCharacter(EntityID e);
    void CreateJoint(EntityID e);
    void DestroyJoint(EntityID e);
    void UpdateJoints(float dt);
    void RegisterBody(EntityID e, u32 bodyIndexAndSequence, bool sensor);

    Scene& m_Scene;
    Scope<JPH::TempAllocatorImpl> m_Temp;
    Scope<JPH::JobSystemThreadPool> m_Jobs;
    Scope<JPH::PhysicsSystem> m_System;
    struct Impl;
    Scope<Impl> m_Impl;
    std::mutex m_EventMutex;
    std::vector<CollisionEvent> m_Events;
};

} // namespace Stardust
