#pragma once
// Components added in 0.7: character controller, joints, navigation, line renderer, splines,
// reflection probes and decals. Included at the end of Components.h.
#include "Core/Base.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Stardust {

// ------------------------------------------------------------------ Character Controller
// A capsule moved by scripts with Move(): it slides along walls, climbs steps up to stepOffset and
// slopes up to slopeLimit, and pushes dynamic bodies. It is not moved by forces (no Rigidbody needed).
struct CharacterControllerComponent {
    float height = 2.0f;
    float radius = 0.5f;
    glm::vec3 center{ 0.0f };
    float slopeLimit = 45.0f;       // degrees
    float stepOffset = 0.3f;        // metres
    float skinWidth = 0.02f;        // metres kept between the capsule and what it touches
    float minMoveDistance = 0.0f;   // moves shorter than this are ignored
    float pushForce = 100.0f;       // N: how hard it pushes dynamic bodies
    float mass = 70.0f;             // kg, for pushing
    // runtime (play mode), not serialized
    bool grounded = false;
    int collisionFlags = 0;         // 1 Sides, 2 Above, 4 Below
    glm::vec3 velocity{ 0.0f };
    glm::vec3 groundNormal{ 0, 1, 0 };
};

// ------------------------------------------------------------------ Joints
// Connects this object's Rigidbody to another one (or to the world when connectedBody is 0).
// Anchor and axis are in this object's local space.
enum class JointType : int { Fixed = 0, Hinge = 1, Spring = 2, Slider = 3, Ball = 4 };
struct JointComponent {
    int type = (int)JointType::Hinge;
    u64 connectedBody = 0;          // UUID of the other object; 0 = fixed to the world
    glm::vec3 anchor{ 0.0f };       // local space
    glm::vec3 axis{ 1, 0, 0 };      // hinge / slider axis, local space
    bool enableCollision = false;   // the two bodies collide with each other
    // limits: degrees for Hinge, metres for Slider, metres (min/max distance) for Spring
    bool useLimits = false;
    float minLimit = -45.0f, maxLimit = 45.0f;
    // Ball: cone around the axis and twist around it (degrees)
    float swingLimit = 45.0f, twistMin = -30.0f, twistMax = 30.0f;
    // soft limits / spring (Spring joint: always on)
    float springFrequency = 2.0f;   // Hz; 0 = rigid
    float springDamping = 0.5f;     // 0..1
    // motor (Hinge: degrees per second, Slider: metres per second)
    bool useMotor = false;
    float motorTargetVelocity = 90.0f;
    float motorForce = 500.0f;      // N or N m
    float friction = 0.0f;          // N m (Hinge, Ball) or N (Slider) when the motor is off
    // breaking: Infinity = unbreakable
    float breakForce = 0.0f;        // N, 0 = unbreakable
    float breakTorque = 0.0f;       // N m, 0 = unbreakable
    // runtime, not serialized
    bool broken = false;
    float currentAngle = 0.0f;      // Hinge (degrees) / Slider (metres)
    glm::vec3 currentForce{ 0.0f }, currentTorque{ 0.0f };
};

// ------------------------------------------------------------------ Navigation
// Bakes the walkable surface of the scene (meshes with Mesh Renderers or colliders, terrains) into a
// navigation mesh that Nav Mesh Agents find paths on. The baked data is stored in `data` (.navmesh asset).
struct NavMeshSurfaceComponent {
    float agentRadius = 0.5f;
    float agentHeight = 2.0f;
    float maxSlope = 45.0f;         // degrees
    float stepHeight = 0.4f;
    float cellSize = 0.25f;         // horizontal voxel size (smaller = more accurate, slower)
    float cellHeight = 0.2f;
    float minRegionArea = 4.0f;     // m^2: smaller islands are removed
    int tileSize = 64;              // voxels per tile side
    bool useBounds = false;         // only bake inside boundsCenter/boundsSize (else the whole scene)
    glm::vec3 boundsCenter{ 0.0f }, boundsSize{ 100.0f, 50.0f, 100.0f };
    int collectFrom = 0;            // 0 Render meshes, 1 Colliders
    u32 layerMask = 0xFFFFFFFFu;    // layers included in the bake
    std::string data;               // baked .navmesh asset
    // runtime
    u64 loadedVersion = 0;
};

struct NavMeshAgentComponent {
    float speed = 3.5f;
    float angularSpeed = 360.0f;    // degrees per second
    float acceleration = 8.0f;
    float stoppingDistance = 0.0f;
    bool autoBraking = true;
    float radius = 0.5f;
    float height = 2.0f;
    float baseOffset = 0.0f;        // the object's pivot above the navmesh
    int avoidancePriority = 50;     // 0 most important .. 99 least
    bool updatePosition = true, updateRotation = true;
    // runtime, not serialized
    glm::vec3 destination{ 0.0f };
    bool hasDestination = false, isStopped = false, pathPending = false, hasPath = false, onNavMesh = false;
    int pathStatus = 0;             // 0 Complete, 1 Partial, 2 Invalid
    float remainingDistance = 0.0f;
    glm::vec3 velocity{ 0.0f }, nextPosition{ 0.0f };
    std::vector<glm::vec3> corners;
    int crowdIndex = -1;
    bool warp = false; glm::vec3 warpTo{ 0.0f };
};

// ------------------------------------------------------------------ Line Renderer
struct LineRendererComponent {
    std::vector<glm::vec3> positions{ { 0, 0, 0 }, { 0, 0, 1 } };
    bool useWorldSpace = false;     // positions in world space; otherwise in this object's space
    bool loop = false;
    float startWidth = 0.1f, endWidth = 0.1f;
    glm::vec4 startColor{ 1 }, endColor{ 1 };
    int alignment = 0;              // 0 View (faces the camera), 1 Local (faces the object's Z axis)
    int textureMode = 0;            // 0 Stretch, 1 Tile
    float tileLength = 1.0f;
    int cornerVertices = 0;         // extra vertices that round each corner
    std::string texture, material;
    int blendMode = 0;              // 0 Alpha, 1 Additive
    float intensity = 1.0f;
    int sortingOrder = 0;
};

// ------------------------------------------------------------------ Splines
struct SplineKnot {
    glm::vec3 position{ 0.0f };
    glm::vec3 tangentIn{ 0.0f }, tangentOut{ 0.0f }; // Bezier mode: relative to the knot
    float roll = 0.0f;              // degrees around the curve
};
// A curve through knots (local space). Auto: smooth through the knots (Catmull-Rom). Bezier: each knot
// has its own in/out tangents. Linear: straight segments.
struct SplineComponent {
    std::vector<SplineKnot> knots{ { { 0, 0, 0 } }, { { 0, 0, 5 } }, { { 5, 0, 10 } } };
    int mode = 0;                   // 0 Auto, 1 Bezier, 2 Linear
    bool closed = false;
    // Follow: moves `follower` (an object UUID) along the curve in play mode
    u64 follower = 0;
    float followSpeed = 0.0f;       // metres per second, 0 = off
    int followLoop = 0;             // 0 Loop, 1 Ping-pong, 2 Once
    bool alignToPath = true;
    // runtime
    float followDistance = 0.0f; int followDirection = 1;
};

// ------------------------------------------------------------------ Reflection Probe
// Captures the surroundings into a cube map used for reflections and local ambient light of the
// objects inside its box. Mode: 0 On Load (captured once when the scene starts), 1 Realtime (every
// `interval` seconds), 2 Via Scripting (RenderProbe()).
struct ReflectionProbeComponent {
    glm::vec3 size{ 10.0f }, center{ 0.0f };
    bool boxProjection = true;
    float blendDistance = 1.0f;
    float intensity = 1.0f;
    int resolution = 128;           // 64, 128, 256, 512
    int mode = 0;
    float interval = 0.0f;          // Realtime: seconds between captures (0 = every frame)
    int importance = 1;
    float nearClip = 0.1f, farClip = 1000.0f;
    bool shadows = true;
    // runtime
    bool dirty = true; float timer = 0.0f; u32 slot = ~0u;
};

// ------------------------------------------------------------------ Decal
// Projects a texture onto the opaque surfaces inside its box (along the object's -Z axis): dirt,
// bullet holes, road markings.
struct DecalComponent {
    std::string texture, normalMap;
    glm::vec4 color{ 1 };
    glm::vec3 size{ 1.0f, 1.0f, 1.0f };   // width, height, projection depth
    float opacity = 1.0f;
    float normalStrength = 1.0f;
    float angleFade = 0.5f;          // 0..1: fades on surfaces turned away from the projector
    float smoothness = 0.3f;
    glm::vec4 tilingOffset{ 1, 1, 0, 0 };
    float emission = 0.0f;
    int sortingOrder = 0;
};

} // namespace Stardust
