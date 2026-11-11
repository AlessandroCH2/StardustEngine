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
