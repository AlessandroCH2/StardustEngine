#pragma once
#include "Core/Base.h"
#include "Renderer/Mesh.h"
#include "Renderer/Material.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Registry.h"
#include "Renderer/Nature/TerrainData.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <unordered_map>

namespace Stardust {

struct IDComponent {
    u64 uuid = 0;
};
struct PrefabInstanceComponent {std::string asset,baseline="{}",mapping="{}";bool revert=false;};

struct NameComponent {
    std::string name = "GameObject";
    bool active = true;
    std::string tag = "Untagged";
    int layer = 0;
    std::string prefab;      // source prefab asset (informational, set when instantiated from a prefab)
};

// Parent/child links. Children order is the order shown in the Hierarchy.
struct RelationshipComponent {
    EntityID parent = kNullEntity;
    std::vector<EntityID> children;
};

struct TransformComponent {
    glm::vec3 position{ 0.0f };
    glm::quat rotation{ 1, 0, 0, 0 };
    glm::vec3 scale{ 1.0f };
    glm::vec3 eulerHint{ 0.0f }; // degrees, keeps the inspector stable across quaternion round-trips

    glm::mat4 Matrix() const { // local matrix (relative to the parent)
        return glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
    }
    glm::vec3 Forward() const { return rotation * glm::vec3(0, 0, -1); }
    glm::vec3 Right() const { return rotation * glm::vec3(1, 0, 0); }
    glm::vec3 Up() const { return rotation * glm::vec3(0, 1, 0); }
    void SetEuler(const glm::vec3& degrees) { eulerHint = degrees; rotation = glm::quat(glm::radians(degrees)); }
};

// Mesh + material (Unity's MeshFilter + MeshRenderer in one component)
struct MeshRendererComponent {
    std::string mesh = "builtin:Cube";   // "builtin:<Primitive>" or an asset path (.obj/.gltf/.glb)
    std::vector<std::string> materials; // submesh material slots
    std::string materialPath;            // .mat asset; empty = use the embedded material below
    Material material;                   // embedded material
    bool castShadows = true,receiveShadows=true;
    glm::vec4 tint{ 1.0f };              // multiplies the material color; driven by animation, not saved
};

struct CameraComponent {
    u32 cullingMask=~0u;
    int clearFlags=0;                          // 0 Skybox, 1 Solid Color: no sky or clouds, much cheaper for render textures
    glm::vec4 background{0.19f,0.30f,0.47f,1}; // Solid Color background
    bool renderShadows=true, postProcessing=true;
    std::string targetTexture;
    int targetWidth=512,targetHeight=512;
    bool orthographic=false;float orthographicSize=5,farPlane=1000;
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    bool primary = true;
};
struct SpriteRendererComponent {
    std::string sprite,material;
    glm::vec4 color{1};
    float pixelsPerUnit=0;
    bool flipX=false,flipY=false;
    int sortingOrder=0;
};
// One textured quad of a Sprite Batch, on the entity's local XY plane
struct SpriteQuad {
    glm::vec2 position{0}, size{1}; // bottom-left corner and size, in local units
    glm::vec4 rect{0};              // texture pixels x, y, width, height from the top-left; width 0 = the whole texture
    u32 color = 0xFFFFFFFF;         // RGBA8, red in the lowest byte
    u16 texture = 0;                // index into SpriteBatchComponent::textures
    u16 flags = 0;                  // 1 flip X, 2 flip Y, bits 2-3: quarter turns counter-clockwise, bits 4-15: animation + 1
};
static_assert(sizeof(SpriteQuad) == 40);
// Frames of an animated quad: texture pixel rectangles shown in turn
struct SpriteAnimation { std::vector<glm::vec4> frames; float fps = 8; };
// Many quads drawn as one object, one draw per texture and colour: tilemaps, decals, 2D effects.
// Meshes are built per chunk of kChunk quads, so changing a few quads rebuilds only their chunk.
struct SpriteBatchComponent {
    static constexpr size_t kChunk = 4096;
    std::vector<std::string> textures;
    std::vector<SpriteQuad> quads;   // a quad with zero size is an empty slot
    std::vector<SpriteAnimation> animations;
    std::string material;
    glm::vec4 color{1};
    int sortingOrder = 0;
    u64 version = 0;                 // textures, animations or the quad count changed
    std::vector<u64> chunkVersions;  // one per chunk
    static u64 NextVersion() { static u64 counter = 0; return ++counter; }
    void Touch() { version = NextVersion(); chunkVersions.assign((quads.size() + kChunk - 1) / kChunk, NextVersion()); }
    void TouchRange(size_t first, size_t count) {
        if (chunkVersions.size() != (quads.size() + kChunk - 1) / kChunk) { Touch(); return; }
        for (size_t c = first / kChunk; c < chunkVersions.size() && c * kChunk < first + count; ++c) chunkVersions[c] = NextVersion();
    }
};
// ------------------------------------------------------------------ Particle System
// A random value between min and max (equal values: constant)
struct ParticleRange { float min = 1, max = 1; };
struct ParticleColorKey { float time = 0; glm::vec4 color{ 1 }; };
struct ParticleFloatKey { float time = 0, value = 1; };
// Emits count particles at time (seconds into the cycle), cycles times every interval (cycles 0 = for ever)
struct ParticleBurst { float time = 0; int count = 30; int cycles = 1; float interval = 0.5f; float probability = 1; };
struct Particle {
    glm::vec3 position{ 0 }, velocity{ 0 };  // simulation space (local or world)
    glm::vec4 color{ 1 };
    float size = 1, rotation = 0, angularVelocity = 0;
    float age = 0, lifetime = 1;
    float seed = 0;                           // random 0..1, fixed for the particle's life
};
enum class ParticleShape : int { Sphere, Hemisphere, Cone, Box, Circle, Edge, Point };
// Unity-style particle emitter: modules for emission, shape and changes over the lifetime of each particle.
// The emitter's up axis (+Y) is its forward: a cone opens upwards, a box and an edge emit upwards.
struct ParticleSystemComponent {
    // Main
    float duration = 5; bool looping = true, prewarm = false, playOnAwake = true;
    ParticleRange startDelay{ 0, 0 }, startLifetime{ 5, 5 }, startSpeed{ 5, 5 }, startSize{ 1, 1 }, startRotation{ 0, 0 }; // rotation in degrees
    glm::vec4 startColor{ 1 }, startColor2{ 1 };    // random between the two
    float gravityModifier = 0, simulationSpeed = 1;
    int simulationSpace = 0;                        // 0 Local (particles follow the object), 1 World (they stay behind)
    int maxParticles = 1000;
    // Emission
    bool emission = true; float rateOverTime = 10, rateOverDistance = 0;
    std::vector<ParticleBurst> bursts;
    // Shape
    bool shapeEnabled = true; ParticleShape shape = ParticleShape::Cone;
    float radius = 1, radiusThickness = 1, angle = 25, arc = 360, randomizeDirection = 0;
    glm::vec3 boxSize{ 1 }, shapeOffset{ 0 };
    // Over lifetime
    bool velocityOverLifetime = false; glm::vec3 velocity{ 0 };
    bool forceOverLifetime = false; glm::vec3 force{ 0 };
    bool limitVelocity = false; float speedLimit = 10, drag = 0;
    bool colorOverLifetime = false; std::vector<ParticleColorKey> colorKeys{ { 0, { 1, 1, 1, 1 } }, { 1, { 1, 1, 1, 0 } } };
    bool sizeOverLifetime = false; std::vector<ParticleFloatKey> sizeKeys{ { 0, 1 }, { 1, 0 } };
    bool rotationOverLifetime = false; ParticleRange angularVelocity{ 45, 45 }; // degrees per second
    bool noise = false; float noiseStrength = 1, noiseFrequency = 0.5f, noiseScrollSpeed = 0.5f;
    // Collision with a horizontal world plane
    bool collision = false; float collisionPlane = 0, bounce = 0.5f, dampen = 0, lifetimeLoss = 0;
    // Texture Sheet Animation: the texture is a grid of frames, read left to right, top to bottom
    bool textureSheet = false; int tilesX = 2, tilesY = 2; float sheetFps = 0, sheetCycles = 1; bool randomStartFrame = false;
    // Renderer
    std::string texture, material;
    int blendMode = 0;      // 0 Alpha, 1 Additive
    int renderMode = 0;     // 0 Billboard, 1 Stretched Billboard, 2 Horizontal, 3 Vertical
    float lengthScale = 2, speedScale = 0; // Stretched: length = size * lengthScale + speed * speedScale
    float intensity = 1;    // HDR colour multiplier (above 1 glows with bloom)
    bool sortByDistance = false;
    int sortingOrder = 0;
    u32 seed = 0;           // 0 = different every run

    // Runtime (not saved)
    std::vector<Particle> particles;
    float time = 0, emitAccumulator = 0, delayLeft = 0;
    bool started = false, playing = false, paused = false, emitting = false;
    glm::vec3 lastPosition{ 0 }; bool hasLastPosition = false;
    u32 random = 1;
};

struct VideoPlayerComponent {
    std::string clip;
    int renderMode=0;          // 0 Render Texture, 1 Material Override (this object's mesh). A UI object always shows it in its Image.
    std::string targetTexture; // Render Texture mode: the .rendertexture asset that receives the frames
    bool playOnStart=true,playing=true,loop=true;
    bool audio=true;float volume=1;
    float speed=1,time=0,duration=0;
};

// The sun. Its transform's forward axis is the light direction (like Unity's Directional Light).
struct DirectionalLightComponent {
    glm::vec3 color{ 1.0f, 0.98f, 0.95f };
    float intensity = 10.0f;
    bool castShadows = true;
    float shadowDistance = 120.0f;
};

// Atmosphere, weather and post-processing of the scene (one per scene)
struct EnvironmentComponent {
    int skyMode=0;std::string skyMaterial;std::array<std::string,6> skyFaces;glm::vec4 skyColor{.15f,.25f,.4f,1};float skyRotation=0,skyExposure=0;
    float shadowStrength=.85f,minimumAmbient=.10f;
    float exposure = 1.0f;
    float ambientIntensity = 1.0f;
    glm::vec3 ambientSky{.12f,.16f,.23f},ambientHorizon{.09f,.11f,.15f},ambientGround{.055f,.05f,.045f};
    float cloudCoverage = 0.4f;
    float cloudDensity = 1.0f;
    float fogDensity = 0.00022f;
    float fogHeightFalloff = 0.0012f;
    float windDirectionDeg = 20.0f;
    float windStrength = 0.5f;
    float bloomIntensity = 0.04f;
    float contrast = 1.06f;
    float saturation = 1.08f;
    float vignette = 0.35f;
    bool fxaa = true;
};

// Procedural landscape with grass and trees (rendered by the nature renderers)
struct TerrainComponent {
    float queryHeight=0;
    std::array<u64,4> neighbors{}; // left,right,bottom,top
    TerrainSettings settings;
    std::string dataPath;   // .terrain file with the sculpted heights / painted layers / trees (empty = procedural)
    bool collider = true;   // static heightfield collider in play mode
};

// Unity's LOD Group: each level lists objects (with their children) shown while the group covers at least
// screenHeight of the view's height. Below the last level the object is culled.
struct LODLevel { float screenHeight = 0.5f; std::vector<u64> renderers; }; // renderers by UUID
struct LODGroupComponent {
    std::vector<LODLevel> levels{ { 0.6f, {} }, { 0.3f, {} }, { 0.1f, {} } };
    float size = 0;                    // world size at scale 1; 0 = measured from the renderers the first time
    glm::vec3 localCenter{ 0 };        // centre of the bounds in the group's space
    int currentLevel = -1;             // last level drawn (runtime, shown by the inspector)
};

// Automatic grass and scattered trees for the Terrain on the same object
struct TerrainVegetationComponent {
    bool grass = true;
    float grassDensity = 1.0f, grassHeight = 0.55f, grassDistance = 55.0f, flowerAmount = 0.03f;
    glm::vec3 grassColor{ 0.13f, 0.24f, 0.04f }, dryGrassColor{ 0.38f, 0.37f, 0.12f };
    bool trees = true;
    float treeDensity = 1.0f;
    u32 maxTrees = 6000, treeSeed = 3;
};

struct ProceduralTerrainComponent {
    float mountainHeight=220,hillHeight=14,meadowRadius=260;
    u32 seed=7;
    bool generateOnMissing=true,proceduralSurface=false;
    std::array<u32,4> layers={0,1,2,3};
};

// Local lights (forward shaded, no shadows)
struct PointLightComponent {
    glm::vec3 color{ 1.0f, 0.85f, 0.6f };
    float intensity = 20.0f;   // luminous power-ish scale
    float range = 10.0f;
};

struct SpotLightComponent {
    glm::vec3 color{ 1.0f, 0.95f, 0.85f };
    float intensity = 40.0f;
    float range = 20.0f;
    float innerAngle = 20.0f;  // degrees (full cone = 2x)
    float outerAngle = 30.0f;
};

// ---------------------------------------------------------------- audio (miniaudio)
struct AudioSourceComponent {
    std::string clip;          // .wav / .mp3 / .flac / .ogg asset
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool spatial = true;       // 3D: attenuated and panned relative to the Audio Listener
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
};

struct AudioListenerComponent {};

struct CanvasComponent {
    glm::vec2 referenceResolution{1920, 1080};
    bool scaleWithScreen = true;
    int sortingOrder = 0;
    int renderMode = 0; // 0 Screen Space Overlay, 1 World Space, 2 Screen Space Camera
    float pixelsPerUnit = 100;
    float planeDistance = 10;
};
struct RectTransformComponent {
    glm::vec2 anchorMin{0.5f}, anchorMax{0.5f}, pivot{0.5f};
    glm::vec2 anchoredPosition{0}, sizeDelta{160, 40};
    float rotation = 0;
    glm::vec2 scale{1};
};
struct UIImageComponent {
    float pixelsPerUnitMultiplier=1;
    std::string texture, sprite, material;
    glm::vec4 color{1};
    int type = 0; // Simple, Sliced, Tiled, Filled
    bool preserveAspect = false, raycastTarget = true;
    float fillAmount = 1;
    int fillMethod = 0; // Horizontal, Vertical
};
struct UITextComponent {
    std::string text = "Text", font, material;
    glm::vec4 color{1}; float fontSize = 24;
    bool richText = true, wordWrap = true, autoSize = false, raycastTarget = true;
    float minFontSize = 12, maxFontSize = 48, characterSpacing = 0, lineSpacing = 1;
    int alignment = 1, verticalAlignment = 1; // Left/Center/Right, Top/Middle/Bottom
    int overflow = 0; // Clip, Overflow, Ellipsis
    glm::vec4 outlineColor{0,0,0,1}, shadowColor{0,0,0,0};
    float outlineWidth = 0;
    glm::vec2 shadowOffset{2,-2};
};
struct UIButtonAction {
    bool enabled=true; u64 target=0; std::string component="Script", method, argumentsJson="[]"; int scriptSlot=0; std::vector<std::string> argumentTypes;
};
struct UIButtonComponent {
    int transition=1; // None, Color Tint, Animation
    float fadeDuration=.1f;
    std::string normalState="Normal",highlightedState="Highlighted",pressedState="Pressed",disabledState="Disabled";
    glm::vec4 currentTint{1};int visualState=-1;
    bool interactable = true;
    glm::vec4 highlightedColor{0.7f,0.8f,1,1}, pressedColor{0.5f,0.6f,0.8f,1}, disabledColor{0.5f,0.5f,0.5f,0.5f};
    u64 target=0;
    std::string method="OnClick";
    std::vector<UIButtonAction> actions;
};
struct UIInputFieldComponent {
    std::string text,placeholder="Enter text...",font;
    float fontSize=24;glm::vec4 color{.1f,.1f,.1f,1},placeholderColor{.5f,.5f,.5f,1};
    bool interactable=true,readOnly=false,multiline=false,password=false;int characterLimit=0;
    int cursor=0,selectionStart=0,selectionEnd=0;float scrollX=0;
};
struct CanvasGroupComponent { float alpha = 1; bool interactable = true, blocksRaycasts = true; };
struct RectMaskComponent {};
// Clips children to this object's Image, following the image's alpha
struct UIMaskComponent { bool showGraphic = true; };
struct UILayoutGroupComponent {
    int mode=0,columns=2; // Horizontal, Vertical, Grid
    glm::vec2 spacing{8},cellSize{100,40};
    glm::vec4 padding{8}; // left,bottom,right,top
    bool fitContent=false;
    int startCorner=0;    // 0 upper left, 1 upper right, 2 lower left, 3 lower right
    int startAxis=0;      // grid only: 0 fills rows first, 1 fills columns first
    int childAlignment=0; // 0..8, left to right then top to bottom
};
struct FbxRigComponent {std::string source;};
struct BoneComponent {u64 root=0;u32 nodeId=0;};
struct AnimatorLayerRuntime {
    std::string state, previousState, eventState;
    float eventTime = 0;
    float time = 0, previousTime = 0, transitionTime = 0, transitionDuration = 0;
    float blendDuration = 0; // length of the current blend tree, to keep its phase when the weights change
};
struct AnimatorComponent {
    std::string avatar;                       // .avatar asset, needed by humanoid clips
    std::vector<AnimatorLayerRuntime> layers; // runtime state of the layers above the base one
    std::string eventState; float eventTime = 0; // where Animation Events were last checked
    std::string previousState;float previousTime=0,transitionTime=0,transitionDuration=0;
    float blendDuration = 0; // length of the current blend tree, to keep its phase when the weights change
    std::string controller, state;
    float speed = 1, time = 0;
    bool playing = true;
    std::unordered_map<std::string, float> parameters;
};

// ---------------------------------------------------------------- physics (Jolt)

enum class BodyType : u8 { Static, Dynamic, Kinematic };

struct RigidbodyComponent {
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool useGravity = true;
    bool freezeRotation = false;  // e.g. character capsules
};

struct PhysicsMaterialProps {
    float friction = 0.5f;
    float restitution = 0.1f;
    bool isTrigger = false;   // sensor: no collision response, sends OnTriggerEnter / OnTriggerExit
};

// 2D physics: bodies move on the XY plane and turn only around Z; 2D colliders ignore depth
struct Rigidbody2DComponent {
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    float gravityScale = 1.0f;
    bool freezeRotation = false;
};
struct BoxCollider2DComponent { glm::vec2 size{ 1.0f }, offset{ 0.0f }; PhysicsMaterialProps material; };
struct CircleCollider2DComponent { float radius = 0.5f; glm::vec2 offset{ 0.0f }; PhysicsMaterialProps material; };
// Vertical capsule: size is the full width and height
struct CapsuleCollider2DComponent { glm::vec2 size{ 1.0f, 2.0f }, offset{ 0.0f }; PhysicsMaterialProps material; };

struct BoxColliderComponent {
    glm::vec3 size{ 1.0f };
    glm::vec3 center{ 0.0f };
    PhysicsMaterialProps material;
};

struct SphereColliderComponent {
    float radius = 0.5f;
    glm::vec3 center{ 0.0f };
    PhysicsMaterialProps material;
};

struct CapsuleColliderComponent {
    float radius = 0.5f;
    float height = 2.0f;          // total height including the caps (Y axis)
    glm::vec3 center{ 0.0f };
    PhysicsMaterialProps material;
};

// Uses the Mesh Renderer's mesh: convex hull (works for dynamic bodies) or exact triangles (static only)
struct MeshColliderComponent {
    bool convex = true;
    PhysicsMaterialProps material;
};

// A wheel of a Vehicle Controller (on a child of the vehicle). The entity's position is the wheel
// centre with the suspension fully extended; in play mode the engine moves the entity along its
// suspension and turns it for steering and rolling, so a wheel mesh can sit on it (axle along X).
struct WheelColliderComponent {
    float radius = 0.35f;
    float width = 0.25f;
    float suspensionDistance = 0.3f;    // travel above the modelled position (m)
    float springFrequency = 1.6f;       // Hz: how stiff the suspension is
    float springDamping = 0.5f;         // 0..1
    float maxSteerAngle = 0.0f;         // degrees; front wheels usually 30..40
    bool motor = false;                 // driven by the engine
    bool handbrake = false;
    float brakeTorque = 1500.0f;        // Nm
    float handbrakeTorque = 4000.0f;    // Nm
    float forwardFriction = 1.0f;       // multipliers of the tyre curves
    float sidewaysFriction = 1.0f;
    // runtime (play mode), not serialized
    glm::vec3 restPosition{ 0.0f }; bool restCaptured = false;
    float steerAngle = 0, rotationAngle = 0, suspensionLength = 0, compression = 0; bool grounded = false;
};

// Car physics (Jolt wheeled vehicle) for a dynamic Rigidbody with colliders: engine, automatic gearbox,
// differentials between the driven wheels and anti-roll bars. Its wheels are the Wheel Colliders on
// child objects. Scripts drive it with throttle / steer / brake / handbrake.
struct VehicleControllerComponent {
    float engineTorque = 600.0f;        // Nm
    float minRPM = 1000.0f, maxRPM = 6500.0f;
    float shiftUpRPM = 5200.0f, shiftDownRPM = 2200.0f;
    float centerOfMassOffset = -0.35f;  // lowers the centre of mass (m) for stability
    float maxPitchRoll = 60.0f;         // degrees the body may tilt before it is held back
    bool antiRollBars = true;
    float limitedSlipRatio = 1.4f;
    // runtime (play mode), not serialized
    float throttle = 0, steer = 0, brake = 0, handbrake = 0;
    float speed = 0, rpm = 0; int gear = 0;
};

// ---------------------------------------------------------------- scripting (C#)

struct ScriptSlot {
    std::string className;          // e.g. "Rotator" or "MyGame.Player"
    std::string fieldsJson = "{}";  // serialized public field values edited in the Inspector
    bool enabled = true;
};
struct ScriptComponent : ScriptSlot {
    std::vector<ScriptSlot> additional;
    explicit ScriptComponent(std::string name = {}) { className = std::move(name); }
    size_t Count() const { return 1 + additional.size(); }
    ScriptSlot& Slot(size_t index) { return index == 0 ? static_cast<ScriptSlot&>(*this) : additional.at(index - 1); }
};

} // namespace Stardust
