#include "Core/Base64.h"
#include "Scripting/ScriptEngine.h"
#include "Scene/ComponentState.h"
#include "Scene/Scene.h"
#include "Physics/PhysicsWorld.h"
#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "Core/Input.h"
#include "Core/FileSystem.h"
#include "Core/Application.h"
#include "Project/Project.h"
#include "Audio/AudioEngine.h"
#include "UI/VideoPlayer.h"
#include "Scene/SceneManager.h"
#include "Assets/Pak.h"
#include "Core/Process.h"
#include <nlohmann/json.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <unordered_map>
#include <fstream>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

namespace Stardust {

namespace fs = std::filesystem;

// ------------------------------------------------------------------------------------------------ native API for C#
// Layout must match ScriptCore/Source/Interop/NativeApi.cs exactly.

struct RaycastHitNative { u64 entity; glm::vec3 point; glm::vec3 normal; float distance; };
struct RigidbodyNative { int type; float mass; float drag; float angularDrag; int useGravity; int freezeRotation; };

enum class ComponentType : int {
    Transform = 0, MeshRenderer, Rigidbody, BoxCollider, SphereCollider, CapsuleCollider, Camera, Light, Script, MeshCollider,
    AnyLight = 10, AudioSource = 11, Animator = 12, Canvas = 13, RectTransform = 14, UIImage = 15, UIText = 16, UIButton = 17, CanvasGroup = 18, RectMask = 19, UILayout = 20, UIInputField = 21, SpriteRenderer=22, Environment=23, Terrain=24, SpotLight=25, VideoPlayer=26, SpriteBatch=27, Rigidbody2D=28, BoxCollider2D=29, CircleCollider2D=30, CapsuleCollider2D=31,
};

struct NativeApi {
    void (*Log)(int, const char*);
    u64 (*FindEntityByName)(const char*);
    int (*EntityExists)(u64);
    const char* (*GetName)(u64);
    void (*SetName)(u64, const char*);
    u64 (*CreateEntity)(const char*);
    u64 (*CreatePrimitive)(int, const char*);
    void (*DestroyEntity)(u64);
    int (*HasComponent)(u64, int);
    void (*AddComponent)(u64, int);
    void (*RemoveComponent)(u64, int);
    u64 (*GetParent)(u64);
    void (*SetParent)(u64, u64);
    void (*GetPosition)(u64, glm::vec3*);
    void (*SetPosition)(u64, const glm::vec3*);
    void (*GetRotation)(u64, glm::vec3*);
    void (*SetRotation)(u64, const glm::vec3*);
    void (*GetScale)(u64, glm::vec3*);
    void (*SetScale)(u64, const glm::vec3*);
    void (*GetWorldPosition)(u64, glm::vec3*);
    void (*GetForward)(u64, glm::vec3*);
    void (*GetRight)(u64, glm::vec3*);
    void (*GetUp)(u64, glm::vec3*);
    void (*LookAt)(u64, const glm::vec3*);
    int (*IsKeyDown)(int);
    int (*IsKeyPressed)(int);
    int (*IsMouseDown)(int);
    int (*IsMousePressed)(int);
    void (*GetMousePosition)(glm::vec2*);
    void (*GetMouseDelta)(glm::vec2*);
    float (*GetScroll)();
    void (*SetCursorLocked)(int);
    void (*AddForce)(u64, const glm::vec3*, int);
    void (*AddTorque)(u64, const glm::vec3*);
    void (*GetVelocity)(u64, glm::vec3*);
    void (*SetVelocity)(u64, const glm::vec3*);
    void (*GetAngularVelocity)(u64, glm::vec3*);
    void (*SetAngularVelocity)(u64, const glm::vec3*);
    int (*Raycast)(const glm::vec3*, const glm::vec3*, float, u64, RaycastHitNative*);
    void (*GetColor)(u64, glm::vec4*);
    void (*SetColor)(u64, const glm::vec4*);
    void* (*GetScriptInstance)(u64);
    // ---- phase 5 additions (order matters, see NativeApi.cs)
    void (*GetWorldEuler)(u64, glm::vec3*);
    void (*SetWorldPosition)(u64, const glm::vec3*);
    void (*SetWorldEuler)(u64, const glm::vec3*);
    void (*AddForceAtPosition)(u64, const glm::vec3*, const glm::vec3*, int);
    void (*GetRigidbody)(u64, RigidbodyNative*);
    void (*SetRigidbody)(u64, const RigidbodyNative*);
    const char* (*GetTag)(u64);
    void (*SetTag)(u64, const char*);
    u64 (*FindWithTag)(const char*);
    int (*GetActive)(u64);
    void (*SetActive)(u64, int);
    int (*GetActiveInHierarchy)(u64);
    u64 (*Instantiate)(u64, const glm::vec3*);
    u64 (*InstantiatePrefab)(const char*, const glm::vec3*);
    int (*LoadScene)(const char*);
    const char* (*GetActiveSceneName)();
    void (*Quit)();
    void (*SetTimeScale)(float);
    float (*GetTimeScale)();
    void (*AudioPlay)(u64);
    void (*AudioStop)(u64);
    int (*AudioIsPlaying)(u64);
    void (*AudioSetVolume)(u64, float);
    void (*AudioSetPitch)(u64, float);
    void (*PlayClipAtPoint)(const char*, const glm::vec3*, float);
    void (*GetLight)(u64, glm::vec4*, float*);
    void (*SetLight)(u64, const glm::vec4*, float);
    int (*GetChildCount)(u64);
    u64 (*GetChild)(u64, int);
    void (*GetScreenSize)(glm::vec2*);
    // Application
    const char* (*GetVersion)();
    const char* (*GetProductName)();
    const char* (*GetCompanyName)();
    const char* (*GetDataPath)();
    const char* (*GetPersistentDataPath)();
    const char* (*GetProjectPath)();
    int (*IsPlaying)();
    int (*MountPak)(const char*);
    void* (*GetScriptInstanceByType)(u64, const char*);
    const char* (*GetComponentJson)(u64, int);
    void (*SetComponentJson)(u64, int, const char*);
    u64 (*LoadSceneAsync)(const char*);
    float (*SceneLoadProgress)(u64);
    int (*SceneLoadStatus)(u64);
    const char* (*SceneLoadError)(u64);
    void (*SceneLoadActivation)(u64,int);
    void (*DontDestroyOnLoad)(u64);
    int (*UnmountPak)(const char*);
    i64 (*AssetSize)(const char*);
    i64 (*ReadAsset)(const char*,u64,u8*,u64);
    const char* (*QueryJson)(const char*); // {"kind":...} requests that need no component: texture sizes...
};

// Managed entry points ([UnmanagedCallersOnly] in Stardust.Interop.Bridge)
struct ManagedApi {
    int (*LoadEditorAssembly)(const char*);
    char* (*GetEditorCommands)();
    char* (*DrawEditorUI)(const char*);
    void (*InvokeEditorCommand)(const char*);
    void (*InvokeButton)(void*);
    void (*PumpSceneLoads)();
    void (*DispatchUIInput)(u64,const char*,int);
    void (*InvokeNamed)(void*,const char*);
    void (*InvokeAction)(void*,u64,const char*,const char*,const char*);
    char* (*GetComponentActions)(const char*);
    void (*DispatchUIButton)(u64);
    void (*ClearUIEvents)();
    int (*Initialize)(NativeApi*, int);
    int (*LoadAssembly)(const char*);
    void (*UnloadAssembly)();
    char* (*GetClasses)();
    void* (*CreateInstance)(const char*, u64, const char*);
    void (*ResolveReferences)(void*, const char*);
    void (*DestroyInstance)(void*);
    void (*InvokeCreate)(void*);
    void (*InvokeUpdate)(void*, float);
    void (*InvokeFixedUpdate)(void*, float);
    void (*InvokeContact)(void*, u64, int);
    void (*InvokeDestroy)(void*);
    char* (*GetFields)(void*);
    void (*SetField)(void*, const char*, const char*);
    void (*FreeString)(char*);
    void (*SetTime)(float, float, float);
};

static NativeApi s_Native{};
static ManagedApi s_Managed{};
static std::unordered_map<EntityID, std::vector<void*>> s_Instances;
static std::unordered_map<EntityID, std::vector<char>> s_Started; // slots whose OnCreate has run
static fs::file_time_type s_LoadedTime{};
static fs::file_time_type s_SourceTime{};
static size_t s_SourceCount = 0;

// Newest write time + number of .cs files under the project's Assets folder
static void ScanSources(fs::file_time_type& newest, size_t& count) {
    newest = {};
    count = 0;
    std::error_code ec;
    fs::path assets = Project::AssetsDir();
    if (!Project::IsLoaded()) return;
    for (const auto& root : {assets, Project::Dir() / "Packages"}) {
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".cs") continue;
        ++count;
        newest = std::max(newest, it->last_write_time(ec));
    }
        ec.clear();
    }
}
static std::atomic<int> s_BuildResult{ -1 }; // -1 none, 0 failed, 1 success
static std::string s_NameBuffer;
static float s_ElapsedTime = 0.0f;
static float s_CurrentDt = 1.0f / 60.0f;
static glm::vec2 s_ScreenSize{ 1280, 720 }; // dt of the callback being executed (Update or FixedUpdate)

static void DecomposeWorld(EntityID e, glm::vec3& pos, glm::quat& rot, glm::vec3& scale);

static Scene* S() { return ScriptEngine::ContextScene(); }
static EntityID E(u64 uuid) { return S() ? S()->FindByUUID(uuid) : kNullEntity; }
static u64 U(EntityID e) { return S() && S()->Reg().Valid(e) ? S()->Reg().Get<IDComponent>(e).uuid : 0; }
static TransformComponent* T(u64 uuid) {
    EntityID e = E(uuid);
    return e ? S()->Reg().TryGet<TransformComponent>(e) : nullptr;
}
static glm::mat4 WorldOf(u64 uuid) { EntityID e = E(uuid); return e ? S()->GetWorldMatrix(e) : glm::mat4(1.0f); }

// Keep physics bodies in sync when scripts move objects directly
static void DecomposeWorld(EntityID e, glm::vec3& pos, glm::quat& rot, glm::vec3& scale) {
    glm::vec3 skew; glm::vec4 persp;
    glm::decompose(S()->GetWorldMatrix(e), scale, rot, pos, skew, persp);
    rot = glm::normalize(rot);
}

static void SyncBody(EntityID e) {
    if (!S() || !S()->Physics()) return;
    glm::vec3 pos, scale, skew; glm::vec4 persp; glm::quat rot;
    glm::decompose(S()->GetWorldMatrix(e), scale, rot, pos, skew, persp);
    S()->Physics()->Teleport(e, pos, glm::normalize(rot));
}

static void RebuildBody(EntityID e) {
    if (!S() || !S()->Physics()) return;
    S()->Physics()->DestroyBody(e);
    S()->Physics()->CreateBody(e);
}

static void InitNativeApi() {
    NativeApi& a = s_Native;
    a.Log = [](int level, const char* msg) {
        switch (level) { case 2: SD_WARN("[C#] {}", msg); break; case 3: SD_ERROR("[C#] {}", msg); break; default: SD_INFO("[C#] {}", msg); }
    };
    a.FindEntityByName = [](const char* n) -> u64 { return S() ? U(S()->FindByName(n)) : 0; };
    a.EntityExists = [](u64 id) -> int { return E(id) != kNullEntity; };
    a.GetName = [](u64 id) -> const char* {
        EntityID e = E(id);
        s_NameBuffer = e ? S()->Reg().Get<NameComponent>(e).name : std::string();
        return s_NameBuffer.c_str();
    };
    a.SetName = [](u64 id, const char* n) { if (EntityID e = E(id)) S()->Reg().Get<NameComponent>(e).name = n; };
    a.CreateEntity = [](const char* n) -> u64 { return S() ? U(S()->CreateEntity(n)) : 0; };
    a.CreatePrimitive = [](int type, const char* n) -> u64 {
        if (!S()) return 0;
        type = std::clamp(type, 0, (int)PrimitiveType::Count - 1);
        return U(S()->CreatePrimitive((PrimitiveType)type, n));
    };
    a.DestroyEntity = [](u64 id) { if (EntityID e = E(id)) S()->QueueDestroy(e); };
    a.HasComponent = [](u64 id, int t) -> int {
        EntityID e = E(id);
        if (!e) return 0;
        Registry& r = S()->Reg();
        switch ((ComponentType)t) {
        case ComponentType::SpriteBatch:return r.Has<SpriteBatchComponent>(e);
        case ComponentType::Rigidbody2D:return r.Has<Rigidbody2DComponent>(e);case ComponentType::BoxCollider2D:return r.Has<BoxCollider2DComponent>(e);
        case ComponentType::CircleCollider2D:return r.Has<CircleCollider2DComponent>(e);case ComponentType::CapsuleCollider2D:return r.Has<CapsuleCollider2DComponent>(e);
        case ComponentType::VideoPlayer:return r.Has<VideoPlayerComponent>(e);case ComponentType::SpriteRenderer:return r.Has<SpriteRendererComponent>(e);case ComponentType::Environment:return r.Has<EnvironmentComponent>(e);case ComponentType::Terrain:return r.Has<TerrainComponent>(e);case ComponentType::SpotLight:return r.Has<SpotLightComponent>(e);case ComponentType::Transform: return 1;
        case ComponentType::MeshRenderer: return r.Has<MeshRendererComponent>(e);
        case ComponentType::Rigidbody: return r.Has<RigidbodyComponent>(e);
        case ComponentType::BoxCollider: return r.Has<BoxColliderComponent>(e);
        case ComponentType::SphereCollider: return r.Has<SphereColliderComponent>(e);
        case ComponentType::CapsuleCollider: return r.Has<CapsuleColliderComponent>(e);
        case ComponentType::Camera: return r.Has<CameraComponent>(e);
        case ComponentType::Light: return r.Has<DirectionalLightComponent>(e);
        case ComponentType::Script: return r.Has<ScriptComponent>(e);
        case ComponentType::MeshCollider: return r.Has<MeshColliderComponent>(e);
        case ComponentType::AnyLight: return r.Has<PointLightComponent>(e) || r.Has<SpotLightComponent>(e) || r.Has<DirectionalLightComponent>(e);
        case ComponentType::Animator: return r.Has<AnimatorComponent>(e);
        case ComponentType::Canvas: return r.Has<CanvasComponent>(e);
        case ComponentType::RectTransform: return r.Has<RectTransformComponent>(e);
        case ComponentType::UIImage: return r.Has<UIImageComponent>(e);
        case ComponentType::UIText: return r.Has<UITextComponent>(e);
        case ComponentType::UIButton: return r.Has<UIButtonComponent>(e);
        case ComponentType::CanvasGroup: return r.Has<CanvasGroupComponent>(e);
        case ComponentType::RectMask: return r.Has<RectMaskComponent>(e);
        case ComponentType::UIInputField: return r.Has<UIInputFieldComponent>(e);
        case ComponentType::UILayout: return r.Has<UILayoutGroupComponent>(e);
        case ComponentType::AudioSource: return r.Has<AudioSourceComponent>(e);
        }
        return 0;
    };
    a.AddComponent = [](u64 id, int t) {
        EntityID e = E(id);
        if (!e) return;
        Registry& r = S()->Reg();
        switch ((ComponentType)t) {
        case ComponentType::SpriteBatch:if(!r.Has<SpriteBatchComponent>(e))r.Add<SpriteBatchComponent>(e);break;
        case ComponentType::Rigidbody2D:if(!r.Has<Rigidbody2DComponent>(e))r.Add<Rigidbody2DComponent>(e);RebuildBody(e);break;
        case ComponentType::BoxCollider2D:if(!r.Has<BoxCollider2DComponent>(e))r.Add<BoxCollider2DComponent>(e);RebuildBody(e);break;
        case ComponentType::CircleCollider2D:if(!r.Has<CircleCollider2DComponent>(e))r.Add<CircleCollider2DComponent>(e);RebuildBody(e);break;
        case ComponentType::CapsuleCollider2D:if(!r.Has<CapsuleCollider2DComponent>(e))r.Add<CapsuleCollider2DComponent>(e);RebuildBody(e);break;
        case ComponentType::VideoPlayer:if(!r.Has<VideoPlayerComponent>(e))r.Add<VideoPlayerComponent>(e);break;case ComponentType::SpriteRenderer: if(!r.Has<SpriteRendererComponent>(e))r.Add<SpriteRendererComponent>(e);break;case ComponentType::Environment:if(!r.Has<EnvironmentComponent>(e))r.Add<EnvironmentComponent>(e);break;case ComponentType::Terrain:if(!r.Has<TerrainComponent>(e))r.Add<TerrainComponent>(e);break;case ComponentType::SpotLight:if(!r.Has<SpotLightComponent>(e))r.Add<SpotLightComponent>(e);break;case ComponentType::MeshRenderer: if (!r.Has<MeshRendererComponent>(e)) r.Add<MeshRendererComponent>(e); break;
        case ComponentType::Rigidbody: if (!r.Has<RigidbodyComponent>(e)) r.Add<RigidbodyComponent>(e); RebuildBody(e); break;
        case ComponentType::BoxCollider: if (!r.Has<BoxColliderComponent>(e)) r.Add<BoxColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::SphereCollider: if (!r.Has<SphereColliderComponent>(e)) r.Add<SphereColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::CapsuleCollider: if (!r.Has<CapsuleColliderComponent>(e)) r.Add<CapsuleColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::MeshCollider: if (!r.Has<MeshColliderComponent>(e)) r.Add<MeshColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::Camera: if (!r.Has<CameraComponent>(e)) r.Add<CameraComponent>(e); break;
        case ComponentType::Light: if (!r.Has<DirectionalLightComponent>(e)) r.Add<DirectionalLightComponent>(e); break;
        case ComponentType::AnyLight: if (!r.Has<PointLightComponent>(e)) r.Add<PointLightComponent>(e); break;
        case ComponentType::Animator: if (!r.Has<AnimatorComponent>(e)) r.Add<AnimatorComponent>(e); break;
        case ComponentType::Canvas: if (!r.Has<CanvasComponent>(e)) r.Add<CanvasComponent>(e); break;
        case ComponentType::RectTransform: if (!r.Has<RectTransformComponent>(e)) r.Add<RectTransformComponent>(e); break;
        case ComponentType::UIImage: if (!r.Has<UIImageComponent>(e)) r.Add<UIImageComponent>(e); break;
        case ComponentType::UIText: if (!r.Has<UITextComponent>(e)) r.Add<UITextComponent>(e); break;
        case ComponentType::UIButton: if (!r.Has<UIButtonComponent>(e)) r.Add<UIButtonComponent>(e); break;
        case ComponentType::CanvasGroup: if(!r.Has<CanvasGroupComponent>(e))r.Add<CanvasGroupComponent>(e);break;
        case ComponentType::RectMask: if(!r.Has<RectMaskComponent>(e))r.Add<RectMaskComponent>(e);break;
        case ComponentType::UIInputField: if(!r.Has<UIInputFieldComponent>(e))r.Add<UIInputFieldComponent>(e);break;
        case ComponentType::UILayout: if(!r.Has<UILayoutGroupComponent>(e))r.Add<UILayoutGroupComponent>(e);break;
        case ComponentType::AudioSource: if (!r.Has<AudioSourceComponent>(e)) r.Add<AudioSourceComponent>(e); break;
        default: break;
        }
    };
    a.RemoveComponent = [](u64 id, int t) {
        EntityID e = E(id);
        if (!e) return;
        Registry& r = S()->Reg();
        switch ((ComponentType)t) {
        case ComponentType::SpriteRenderer:r.Remove<SpriteRendererComponent>(e);break;
        case ComponentType::SpriteBatch:r.Remove<SpriteBatchComponent>(e);break;
        case ComponentType::Rigidbody2D:r.Remove<Rigidbody2DComponent>(e);RebuildBody(e);break;
        case ComponentType::BoxCollider2D:r.Remove<BoxCollider2DComponent>(e);RebuildBody(e);break;
        case ComponentType::CircleCollider2D:r.Remove<CircleCollider2DComponent>(e);RebuildBody(e);break;
        case ComponentType::CapsuleCollider2D:r.Remove<CapsuleCollider2DComponent>(e);RebuildBody(e);break;
        case ComponentType::Environment:r.Remove<EnvironmentComponent>(e);break;
        case ComponentType::Terrain:r.Remove<TerrainComponent>(e);RebuildBody(e);break;
        case ComponentType::SpotLight:r.Remove<SpotLightComponent>(e);break;
        case ComponentType::VideoPlayer:VideoPlayerSystem::Remove(*S(),e);r.Remove<VideoPlayerComponent>(e);break;
        case ComponentType::MeshRenderer: r.Remove<MeshRendererComponent>(e); break;
        case ComponentType::Rigidbody: r.Remove<RigidbodyComponent>(e); RebuildBody(e); break;
        case ComponentType::BoxCollider: r.Remove<BoxColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::SphereCollider: r.Remove<SphereColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::CapsuleCollider: r.Remove<CapsuleColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::MeshCollider: r.Remove<MeshColliderComponent>(e); RebuildBody(e); break;
        case ComponentType::Camera: r.Remove<CameraComponent>(e); break;
        case ComponentType::Light: r.Remove<DirectionalLightComponent>(e); break;
        case ComponentType::Animator: r.Remove<AnimatorComponent>(e); break;
        case ComponentType::Canvas: r.Remove<CanvasComponent>(e); break;
        case ComponentType::RectTransform: r.Remove<RectTransformComponent>(e); break;
        case ComponentType::UIImage: r.Remove<UIImageComponent>(e); break;
        case ComponentType::UIText: r.Remove<UITextComponent>(e); break;
        case ComponentType::UIButton: r.Remove<UIButtonComponent>(e); break;
        case ComponentType::CanvasGroup: r.Remove<CanvasGroupComponent>(e);break;
        case ComponentType::RectMask: r.Remove<RectMaskComponent>(e);break;
        case ComponentType::UIInputField: r.Remove<UIInputFieldComponent>(e);break;
        case ComponentType::UILayout: r.Remove<UILayoutGroupComponent>(e);break;
        default: break;
        }
    };
    a.GetParent = [](u64 id) -> u64 { EntityID e = E(id); return e ? U(S()->GetParent(e)) : 0; };
    a.SetParent = [](u64 id, u64 parent) { if (EntityID e = E(id)) S()->SetParent(e, E(parent), true); };

    a.GetPosition = [](u64 id, glm::vec3* o) { if (auto* t = T(id)) *o = t->position; };
    a.SetPosition = [](u64 id, const glm::vec3* v) { if (auto* t = T(id)) { t->position = *v; SyncBody(E(id)); } };
    a.GetRotation = [](u64 id, glm::vec3* o) { if (auto* t = T(id)) *o = t->eulerHint; };
    a.SetRotation = [](u64 id, const glm::vec3* v) { if (auto* t = T(id)) { t->SetEuler(*v); SyncBody(E(id)); } };
    a.GetScale = [](u64 id, glm::vec3* o) { if (auto* t = T(id)) *o = t->scale; };
    a.SetScale = [](u64 id, const glm::vec3* v) { if (auto* t = T(id)) t->scale = *v; };
    a.GetWorldPosition = [](u64 id, glm::vec3* o) { *o = glm::vec3(WorldOf(id)[3]); };
    a.GetForward = [](u64 id, glm::vec3* o) { *o = glm::normalize(-glm::vec3(WorldOf(id)[2])); };
    a.GetRight = [](u64 id, glm::vec3* o) { *o = glm::normalize(glm::vec3(WorldOf(id)[0])); };
    a.GetUp = [](u64 id, glm::vec3* o) { *o = glm::normalize(glm::vec3(WorldOf(id)[1])); };
    a.LookAt = [](u64 id, const glm::vec3* target) {
        EntityID e = E(id);
        if (!e) return;
        glm::vec3 pos = glm::vec3(S()->GetWorldMatrix(e)[3]);
        glm::vec3 dir = *target - pos;
        if (glm::length(dir) < 1e-5f) return;
        dir = glm::normalize(dir);
        float yaw = std::atan2(-dir.x, -dir.z);
        float pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
        auto& t = S()->Reg().Get<TransformComponent>(e);
        t.SetEuler(glm::degrees(glm::vec3(pitch, yaw, 0.0f)));
        SyncBody(e);
    };

    a.IsKeyDown = [](int k) -> int { return Input::RuntimeKeyboard && Input::IsKeyDown((Key)k); };
    a.IsKeyPressed = [](int k) -> int { return Input::RuntimeKeyboard && Input::IsKeyPressed((Key)k); };
    a.IsMouseDown = [](int b) -> int { return Input::RuntimeMouse && Input::IsMouseDown((MouseButton)b); };
    a.IsMousePressed = [](int b) -> int { return Input::RuntimeMouse && Input::IsMousePressed((MouseButton)b); };
    a.GetMousePosition = [](glm::vec2* o) { *o = Input::MousePosition()-Input::RuntimeOrigin; };
    a.GetMouseDelta = [](glm::vec2* o) { *o = Input::RuntimeMouse?Input::MouseDelta():glm::vec2(0); };
    a.GetScroll = []() -> float { return Input::RuntimeMouse?Input::ScrollDelta():0.f; };
    a.SetCursorLocked = [](int l) { if(Input::RuntimeMouse)Input::SetCursorMode(l ? CursorMode::Locked : CursorMode::Normal); };

    a.AddForce = [](u64 id, const glm::vec3* f, int mode) { if (S() && S()->Physics()) S()->Physics()->AddForce(E(id), *f, (ForceMode)mode, s_CurrentDt); };
    a.AddTorque = [](u64 id, const glm::vec3* t) { if (S() && S()->Physics()) S()->Physics()->AddTorque(E(id), *t, s_CurrentDt); };
    a.GetVelocity = [](u64 id, glm::vec3* o) { *o = S() && S()->Physics() ? S()->Physics()->GetVelocity(E(id)) : glm::vec3(0); };
    a.SetVelocity = [](u64 id, const glm::vec3* v) { if (S() && S()->Physics()) S()->Physics()->SetVelocity(E(id), *v); };
    a.GetAngularVelocity = [](u64 id, glm::vec3* o) { *o = S() && S()->Physics() ? S()->Physics()->GetAngularVelocity(E(id)) : glm::vec3(0); };
    a.SetAngularVelocity = [](u64 id, const glm::vec3* v) { if (S() && S()->Physics()) S()->Physics()->SetAngularVelocity(E(id), *v); };
    a.Raycast = [](const glm::vec3* o, const glm::vec3* d, float maxDist, u64 ignore, RaycastHitNative* out) -> int {
        if (!S() || !S()->Physics()) return 0;
        RaycastHit hit;
        if (!S()->Physics()->Raycast(*o, *d, maxDist, hit, ignore ? E(ignore) : kNullEntity)) return 0;
        *out = { U(hit.entity), hit.point, hit.normal, hit.distance };
        return 1;
    };
    a.GetColor = [](u64 id, glm::vec4* o) {
        EntityID e = E(id);
        auto* mr = e ? S()->Reg().TryGet<MeshRendererComponent>(e) : nullptr;
        *o = mr ? mr->material.color : glm::vec4(1);
    };
    a.SetColor = [](u64 id, const glm::vec4* c) {
        EntityID e = E(id);
        if (auto* mr = e ? S()->Reg().TryGet<MeshRendererComponent>(e) : nullptr) { mr->materialPath.clear(); mr->materials.clear(); mr->material.color = *c; }
    };
    a.GetScriptInstance = [](u64 id) -> void* {
        auto it = s_Instances.find(E(id));
        return it == s_Instances.end() || it->second.empty() ? nullptr : it->second.front();
    };

    // ---- world-space transform
    a.GetWorldEuler = [](u64 id, glm::vec3* o) {
        EntityID e = E(id);
        if (!e) return;
        glm::vec3 p, sc; glm::quat r;
        DecomposeWorld(e, p, r, sc);
        auto* t = S()->Reg().TryGet<TransformComponent>(e);
        // root objects keep the editor-friendly euler values
        *o = S()->GetParent(e) == kNullEntity && t ? t->eulerHint : glm::degrees(glm::eulerAngles(r));
    };
    a.SetWorldPosition = [](u64 id, const glm::vec3* v) {
        EntityID e = E(id);
        if (!e) return;
        glm::mat4 w = S()->GetWorldMatrix(e);
        w[3] = glm::vec4(*v, 1.0f);
        S()->SetWorldMatrix(e, w);
        SyncBody(e);
    };
    a.SetWorldEuler = [](u64 id, const glm::vec3* v) {
        EntityID e = E(id);
        if (!e) return;
        if (S()->GetParent(e) == kNullEntity) { S()->Reg().Get<TransformComponent>(e).SetEuler(*v); SyncBody(e); return; }
        glm::vec3 p, sc; glm::quat r;
        DecomposeWorld(e, p, r, sc);
        glm::mat4 w = glm::translate(glm::mat4(1.0f), p) * glm::toMat4(glm::quat(glm::radians(*v))) * glm::scale(glm::mat4(1.0f), sc);
        S()->SetWorldMatrix(e, w);
        SyncBody(e);
    };
    a.AddForceAtPosition = [](u64 id, const glm::vec3* f, const glm::vec3* p, int mode) {
        if (S() && S()->Physics()) S()->Physics()->AddForceAtPosition(E(id), *f, *p, (ForceMode)mode, s_CurrentDt);
    };
    a.GetRigidbody = [](u64 id, RigidbodyNative* o) {
        EntityID e = E(id);
        auto* rb = e ? S()->Reg().TryGet<RigidbodyComponent>(e) : nullptr;
        if (!rb) { *o = {}; return; }
        *o = { (int)rb->type, rb->mass, rb->linearDamping, rb->angularDamping, rb->useGravity, rb->freezeRotation };
    };
    a.SetRigidbody = [](u64 id, const RigidbodyNative* v) {
        EntityID e = E(id);
        auto* rb = e ? S()->Reg().TryGet<RigidbodyComponent>(e) : nullptr;
        if (!rb) return;
        rb->type = (BodyType)v->type; rb->mass = v->mass; rb->linearDamping = v->drag; rb->angularDamping = v->angularDrag;
        rb->useGravity = v->useGravity != 0; rb->freezeRotation = v->freezeRotation != 0;
        if (S()->Physics()) S()->Physics()->RecreateBody(e);
    };
    a.GetTag = [](u64 id) -> const char* {
        EntityID e = E(id);
        s_NameBuffer = e ? S()->Reg().Get<NameComponent>(e).tag : std::string();
        return s_NameBuffer.c_str();
    };
    a.SetTag = [](u64 id, const char* t) { if (EntityID e = E(id)) S()->Reg().Get<NameComponent>(e).tag = t; };
    a.FindWithTag = [](const char* t) -> u64 { return S() ? U(S()->FindWithTag(t)) : 0; };
    a.GetActive = [](u64 id) -> int { EntityID e = E(id); return e && S()->Reg().Get<NameComponent>(e).active; };
    a.SetActive = [](u64 id, int v) { if (EntityID e = E(id)) S()->SetActive(e, v != 0); };
    a.GetActiveInHierarchy = [](u64 id) -> int { EntityID e = E(id); return e && S()->IsActiveInHierarchy(e); };
    a.Instantiate = [](u64 id, const glm::vec3* pos) -> u64 { EntityID e = E(id); return e ? U(S()->Instantiate(e, pos)) : 0; };
    a.InstantiatePrefab = [](const char* path, const glm::vec3* pos) -> u64 { return S() ? U(S()->InstantiatePrefab(path, pos)) : 0; };
    a.LoadSceneAsync=[](const char* name)->u64{auto path=Project::ResolveScene(name);return path.empty()?0:SceneManager::LoadAsync(path);};
    a.SceneLoadProgress=[](u64 id){return SceneManager::LoadProgress(id);};a.SceneLoadStatus=[](u64 id){return SceneManager::LoadStatus(id);};a.SceneLoadError=[](u64 id)->const char*{static thread_local std::string error;error=SceneManager::LoadError(id);return error.c_str();};a.SceneLoadActivation=[](u64 id,int allowed){SceneManager::AllowActivation(id,allowed!=0);};
    a.LoadScene = [](const char* name) -> int {
        std::string path = Project::ResolveScene(name);
        if (path.empty()) { SD_ERROR("SceneManager.LoadScene: '{}' is not in Scenes In Build", name); return 0; }
        SceneManager::RequestLoad(path);
        return 1;
    };
    a.GetActiveSceneName = []() -> const char* { s_NameBuffer = S() ? S()->Name() : std::string(); return s_NameBuffer.c_str(); };
    

    // ---- Application
    a.Quit = []() { SceneManager::RequestQuit(); };
    a.GetVersion = []() -> const char* {
        return Project::Config().version.c_str();
    };

    a.GetProductName = []() -> const char* {
        return Project::Config().name.c_str();
    };

    a.GetCompanyName = []() -> const char* {
        return Project::Config().company.c_str();
    };

    a.GetDataPath = []() -> const char* {
        static std::string path;
        path = Project::AssetsDir().string();
        return path.c_str();
    };

    a.GetPersistentDataPath = []() -> const char* {
        static std::string path;

        fs::path persistentPath =
            FileSystem::ExecutableDir() / "PersistentData";
#ifndef _WIN32
        if(const char* xdg=std::getenv("XDG_DATA_HOME"))persistentPath=fs::path(xdg)/"Stardust"/Project::Config().name;
        else if(const char* home=std::getenv("HOME"))persistentPath=fs::path(home)/".local/share/Stardust"/Project::Config().name;
#endif

        std::error_code ec;
        fs::create_directories(persistentPath, ec);

        path = persistentPath.string();
        return path.c_str();
    };

    a.GetProjectPath = []() -> const char* {
        static std::string path;
        path = Project::Dir().string();
        return path.c_str();
    };

    a.IsPlaying = []() -> int {
        return ScriptEngine::RuntimeScene() != nullptr;
    };
    a.GetScriptInstanceByType = [](u64 uuid, const char* type) -> void* {
        EntityID e = E(uuid);
        auto it = s_Instances.find(e);
        auto* scripts = e ? S()->Reg().TryGet<ScriptComponent>(e) : nullptr;
        if (it == s_Instances.end() || !scripts) return nullptr;
        for (size_t i = 0; i < it->second.size() && i < scripts->Count(); ++i)
            if (scripts->Slot(i).className == type) return it->second[i];
        return nullptr;
    };
    a.GetComponentJson=[](u64 id,int type)->const char* {
        using j=nlohmann::json;j data=j::object();auto e=E(id);
        auto vec2=[](glm::vec2 v){return j::array({v.x,v.y});};auto vec4=[](glm::vec4 v){return j::array({v.x,v.y,v.z,v.w});};
        if(type==104)data={{"cursorLocked",Input::RuntimeMouse&&Input::GetCursorMode()==CursorMode::Locked}};
        if(type==105)data={{"runInBackground",Application::RunInBackground()}};
        if(type==101)data={{"layers",Project::Config().layers}};
        if(type==100){data["connected"]=Input::GamepadConnected((int)id);data["axes"]=j::array();data["buttons"]=j::array();data["pressed"]=j::array();for(int i=0;i<6;++i)data["axes"].push_back(Input::GamepadAxis((int)id,i));for(int i=0;i<15;++i){data["buttons"].push_back(Input::GamepadButton((int)id,i));data["pressed"].push_back(Input::GamepadButton((int)id,i,true));}}
        if(e){auto& r=S()->Reg();
            // Quads are not in here (they can be megabytes): QueryJson "spriteBatchQuads" reads them
            if(type==27)if(auto* c=r.TryGet<SpriteBatchComponent>(e)){u64 v=c->version;for(u64 chunk:c->chunkVersions)v=v*1099511628211ull^chunk;data={{"textures",c->textures},{"count",c->quads.size()},{"material",c->material},{"color",vec4(c->color)},{"sortingOrder",c->sortingOrder},{"version",std::to_string(v)},{"animations",c->animations.size()}};}
            if(type==26)if(auto* c=r.TryGet<VideoPlayerComponent>(e))data={{"clip",c->clip},{"targetTexture",c->targetTexture},{"renderMode",c->renderMode},{"playing",c->playing},{"loop",c->loop},{"time",c->time},{"duration",c->duration},{"speed",c->speed},{"audio",c->audio},{"volume",c->volume}};
            auto vec3=[](glm::vec3 v){return j::array({v.x,v.y,v.z});};
            auto collider=[&](const auto* c){if(c)data={{"isTrigger",c->material.isTrigger},{"friction",c->material.friction},{"restitution",c->material.restitution}};};
            if(type==3)if(auto* c=r.TryGet<BoxColliderComponent>(e)){collider(c);data["size"]=vec3(c->size);data["center"]=vec3(c->center);}
            if(type==28)if(auto* c=r.TryGet<Rigidbody2DComponent>(e))data={{"bodyType",(int)c->type},{"mass",c->mass},{"linearDamping",c->linearDamping},{"angularDamping",c->angularDamping},{"gravityScale",c->gravityScale},{"freezeRotation",c->freezeRotation}};
            if(type==29)if(auto* c=r.TryGet<BoxCollider2DComponent>(e)){collider(c);data["size"]=vec2(c->size);data["offset"]=vec2(c->offset);}
            if(type==30)if(auto* c=r.TryGet<CircleCollider2DComponent>(e)){collider(c);data["radius"]=c->radius;data["offset"]=vec2(c->offset);}
            if(type==31)if(auto* c=r.TryGet<CapsuleCollider2DComponent>(e)){collider(c);data["size"]=vec2(c->size);data["offset"]=vec2(c->offset);}
            if(type==4)if(auto* c=r.TryGet<SphereColliderComponent>(e)){collider(c);data["radius"]=c->radius;data["center"]=vec3(c->center);}
            if(type==5)if(auto* c=r.TryGet<CapsuleColliderComponent>(e)){collider(c);data["radius"]=c->radius;data["height"]=c->height;data["center"]=vec3(c->center);}
            if(type==9)if(auto* c=r.TryGet<MeshColliderComponent>(e)){collider(c);data["convex"]=c->convex;}
            if(type==7)if(auto* c=r.TryGet<DirectionalLightComponent>(e))data={{"color",vec3(c->color)},{"intensity",c->intensity},{"castShadows",c->castShadows},{"shadowDistance",c->shadowDistance}};
            if(type==25)if(auto* c=r.TryGet<SpotLightComponent>(e))data={{"color",vec3(c->color)},{"intensity",c->intensity},{"range",c->range},{"innerAngle",c->innerAngle},{"outerAngle",c->outerAngle}};
            if(type==24)if(auto* c=r.TryGet<TerrainComponent>(e))data={{"size",c->settings.size},{"resolution",c->settings.resolution},{"dataPath",c->dataPath},{"collider",c->collider},{"height",c->queryHeight}};
            if(type==103){data["disabled"]=j::array();VisitToggleComponents([&]<typename T>(int component,const char*){if(r.Has<T>(e)&&!r.IsEnabled<T>(e))data["disabled"].push_back(component);});}
            if(type==102)if(auto* scripts=r.TryGet<ScriptComponent>(e)){data["slots"]=j::array();for(size_t i=0;i<scripts->Count();++i)data["slots"].push_back(scripts->Slot(i).enabled);}
            if(type==99)data={{"layer",r.Get<NameComponent>(e).layer}};
            if(type==1)if(auto* c=r.TryGet<MeshRendererComponent>(e)){data={{"mesh",c->mesh},{"materials",c->materials},{"material",c->materialPath},{"castShadows",c->castShadows},{"receiveShadows",c->receiveShadows}};j mat;auto effective=!c->materialPath.empty()?AssetManager::GetMaterial(c->materialPath):!c->materials.empty()?AssetManager::GetMaterial(c->materials.front()):nullptr;(effective?*effective:c->material).ToJson(mat);data["embeddedMaterial"]=mat;}
            if(type==22)if(auto* c=r.TryGet<SpriteRendererComponent>(e))data={{"sprite",c->sprite},{"material",c->material},{"color",vec4(c->color)},{"pixelsPerUnit",c->pixelsPerUnit},{"flipX",c->flipX},{"flipY",c->flipY},{"sortingOrder",c->sortingOrder}};
            if(type==23)if(auto* c=r.TryGet<EnvironmentComponent>(e))data={{"shadowStrength",c->shadowStrength},{"minimumAmbient",c->minimumAmbient},{"exposure",c->exposure},{"ambientIntensity",c->ambientIntensity},{"bloomIntensity",c->bloomIntensity},{"contrast",c->contrast},{"saturation",c->saturation},{"vignette",c->vignette},{"fogDensity",c->fogDensity},{"fxaa",c->fxaa},{"skyMode",c->skyMode},{"skyMaterial",c->skyMaterial},{"skyColor",vec4(c->skyColor)}};
            if(type==11)if(auto* c=r.TryGet<AudioSourceComponent>(e))data={{"clip",c->clip},{"volume",c->volume},{"pitch",c->pitch},{"loop",c->loop},{"spatial",c->spatial}};

            if(type==6)if(auto* c=r.TryGet<CameraComponent>(e))data={{"orthographic",c->orthographic},{"orthographicSize",c->orthographicSize},{"fieldOfView",c->fovDegrees},{"nearClipPlane",c->nearPlane},{"farClipPlane",c->farPlane},{"cullingMask",c->cullingMask},{"targetTexture",c->targetTexture},{"targetWidth",c->targetWidth},{"targetHeight",c->targetHeight}};
            if(type==13)if(auto* c=r.TryGet<CanvasComponent>(e))data={{"referenceResolution",vec2(c->referenceResolution)},{"renderMode",c->renderMode},{"pixelsPerUnit",c->pixelsPerUnit},{"scaleWithScreen",c->scaleWithScreen}};
            if(type==14)if(auto* c=r.TryGet<RectTransformComponent>(e))data={{"anchorMin",vec2(c->anchorMin)},{"anchorMax",vec2(c->anchorMax)},{"pivot",vec2(c->pivot)},{"anchoredPosition",vec2(c->anchoredPosition)},{"sizeDelta",vec2(c->sizeDelta)},{"scale",vec2(c->scale)},{"rotation",c->rotation}};
            if(type==15)if(auto* c=r.TryGet<UIImageComponent>(e))data={{"texture",c->texture},{"sprite",c->sprite},{"material",c->material},{"color",vec4(c->color)},{"type",c->type},{"preserveAspect",c->preserveAspect},{"pixelsPerUnitMultiplier",c->pixelsPerUnitMultiplier},{"fillAmount",c->fillAmount}};
            if(type==16)if(auto* c=r.TryGet<UITextComponent>(e))data={{"text",c->text},{"fontSize",c->fontSize},{"font",c->font},{"material",c->material},{"color",vec4(c->color)},{"richText",c->richText},{"wordWrap",c->wordWrap},{"autoSize",c->autoSize},{"alignment",c->alignment},{"outlineWidth",c->outlineWidth},{"outlineColor",vec4(c->outlineColor)}};
            if(type==18)if(auto* c=r.TryGet<CanvasGroupComponent>(e))data={{"alpha",c->alpha},{"interactable",c->interactable},{"blocksRaycasts",c->blocksRaycasts}};
            if(type==21)if(auto* c=r.TryGet<UIInputFieldComponent>(e))data={{"text",c->text},{"placeholder",c->placeholder},{"interactable",c->interactable},{"readOnly",c->readOnly},{"password",c->password},{"multiline",c->multiline},{"characterLimit",c->characterLimit}};
            if(type==20)if(auto* c=r.TryGet<UILayoutGroupComponent>(e))data={{"mode",c->mode},{"columns",c->columns},{"spacing",vec2(c->spacing)},{"cellSize",vec2(c->cellSize)},{"fitContent",c->fitContent}};
            if(type==17)if(auto* c=r.TryGet<UIButtonComponent>(e))data={{"interactable",c->interactable},{"transition",c->transition},{"fadeDuration",c->fadeDuration},{"highlightedColor",vec4(c->highlightedColor)},{"pressedColor",vec4(c->pressedColor)},{"disabledColor",vec4(c->disabledColor)}};
            if(type==12)if(auto* c=r.TryGet<AnimatorComponent>(e))data={{"state",c->state},{"speed",c->speed},{"playing",c->playing},{"parameters",c->parameters}};
        }s_NameBuffer=data.dump();return s_NameBuffer.c_str();
    };
    a.SetComponentJson=[](u64 id,int type,const char* value){
        if(type==105){auto settings=nlohmann::json::parse(value,nullptr,false);if(settings.is_object()&&settings.contains("runInBackground"))Application::SetRunInBackground(settings.value("runInBackground",false));return;}
        auto e=E(id);if(!e)return;auto data=nlohmann::json::parse(value,nullptr,false);if(!data.is_object())return;
        auto v2=[&](const char* name,glm::vec2& v){if(data.contains(name)&&data[name].is_array()&&data[name].size()==2)v={data[name][0].get<float>(),data[name][1].get<float>()};};
        auto v4=[&](const char* name,glm::vec4& v){if(data.contains(name)&&data[name].is_array()&&data[name].size()==4)v={data[name][0].get<float>(),data[name][1].get<float>(),data[name][2].get<float>(),data[name][3].get<float>()};};
        try{auto& r=S()->Reg();
            if(type==26)if(auto* c=r.TryGet<VideoPlayerComponent>(e)){c->clip=data.value("clip",c->clip);c->targetTexture=data.value("targetTexture",c->targetTexture);c->playing=data.value("playing",c->playing);c->loop=data.value("loop",c->loop);c->time=data.value("time",c->time);c->speed=data.value("speed",c->speed);c->renderMode=std::clamp(data.value("renderMode",c->renderMode),0,1);c->audio=data.value("audio",c->audio);c->volume=data.value("volume",c->volume);}
            auto v3=[&](const char* name,glm::vec3& v){if(data.contains(name)&&data[name].is_array()&&data[name].size()==3)v={data[name][0].get<float>(),data[name][1].get<float>(),data[name][2].get<float>()};};
            auto collider=[&](auto* c){if(c){c->material.isTrigger=data.value("isTrigger",c->material.isTrigger);c->material.friction=std::max(0.f,data.value("friction",c->material.friction));c->material.restitution=std::clamp(data.value("restitution",c->material.restitution),0.f,1.f);}};
            if(type==3)if(auto* c=r.TryGet<BoxColliderComponent>(e)){collider(c);v3("size",c->size);v3("center",c->center);RebuildBody(e);}
            if(type==28)if(auto* c=r.TryGet<Rigidbody2DComponent>(e)){c->type=(BodyType)std::clamp(data.value("bodyType",(int)c->type),0,2);c->mass=std::max(.001f,data.value("mass",c->mass));c->linearDamping=std::max(0.f,data.value("linearDamping",c->linearDamping));c->angularDamping=std::max(0.f,data.value("angularDamping",c->angularDamping));c->gravityScale=data.value("gravityScale",c->gravityScale);c->freezeRotation=data.value("freezeRotation",c->freezeRotation);RebuildBody(e);}
            if(type==29)if(auto* c=r.TryGet<BoxCollider2DComponent>(e)){collider(c);v2("size",c->size);v2("offset",c->offset);RebuildBody(e);}
            if(type==30)if(auto* c=r.TryGet<CircleCollider2DComponent>(e)){collider(c);c->radius=std::max(.001f,data.value("radius",c->radius));v2("offset",c->offset);RebuildBody(e);}
            if(type==31)if(auto* c=r.TryGet<CapsuleCollider2DComponent>(e)){collider(c);v2("size",c->size);v2("offset",c->offset);RebuildBody(e);}
            if(type==4)if(auto* c=r.TryGet<SphereColliderComponent>(e)){collider(c);c->radius=std::max(.001f,data.value("radius",c->radius));v3("center",c->center);RebuildBody(e);}
            if(type==5)if(auto* c=r.TryGet<CapsuleColliderComponent>(e)){collider(c);c->radius=std::max(.001f,data.value("radius",c->radius));c->height=std::max(.002f,data.value("height",c->height));v3("center",c->center);RebuildBody(e);}
            if(type==9)if(auto* c=r.TryGet<MeshColliderComponent>(e)){collider(c);c->convex=data.value("convex",c->convex);RebuildBody(e);}
            if(type==7)if(auto* c=r.TryGet<DirectionalLightComponent>(e)){v3("color",c->color);c->intensity=data.value("intensity",c->intensity);c->castShadows=data.value("castShadows",c->castShadows);c->shadowDistance=data.value("shadowDistance",c->shadowDistance);}
            if(type==25)if(auto* c=r.TryGet<SpotLightComponent>(e)){v3("color",c->color);c->intensity=data.value("intensity",c->intensity);c->range=data.value("range",c->range);c->innerAngle=data.value("innerAngle",c->innerAngle);c->outerAngle=data.value("outerAngle",c->outerAngle);}
            if(type==24)if(auto* c=r.TryGet<TerrainComponent>(e)){
                c->collider=data.value("collider",c->collider);
                auto terrain=TerrainData::Find(r.Get<IDComponent>(e).uuid);if(!terrain&&AssetManager::Device()){S()->PrepareTerrains(*AssetManager::Device());terrain=TerrainData::Find(r.Get<IDComponent>(e).uuid);}
                if(terrain){
                    if(data.contains("query")){auto q=data["query"];c->queryHeight=terrain->HeightAt(q[0],q[1]);}
                    if(data.contains("flat"))terrain->MakeFlat(data["flat"].get<float>(),terrain->Resolution());
                    if(data.contains("brush")){auto q=data["brush"];terrain->ApplyBrush((TerrainBrush)std::clamp(q.value("mode",1),1,6),{q.value("x",0.f),q.value("z",0.f)},std::max(.01f,q.value("radius",1.f)),q.value("strength",1.f),q.value("hardness",.5f),q.value("height",0.f),q.value("layer",0u),q.value("invert",false),q.value("dt",1.f));}
                }
                if(data.value("rebuildCollider",false)||data.contains("collider")||data.contains("flat"))RebuildBody(e);
            }
            if(type==103){int component=data.value("type",-1);VisitToggleComponents([&]<typename T>(int id,const char*){if(id==component&&r.Has<T>(e)){r.SetEnabled<T>(e,data.value("enabled",true));S()->RefreshComponentState(e,id);}});}
            if(type==102)if(auto* scripts=r.TryGet<ScriptComponent>(e)){int index=data.value("slot",0);if(index>=0&&index<scripts->Count())scripts->Slot(index).enabled=data.value("enabled",true);}
            if(type==99)r.Get<NameComponent>(e).layer=std::clamp(data.value("layer",0),0,31);
            if(type==1)if(auto* c=r.TryGet<MeshRendererComponent>(e)){
                c->mesh=data.value("mesh",c->mesh);c->materials=data.value("materials",c->materials);c->materialPath=data.value("material",c->materialPath);c->castShadows=data.value("castShadows",c->castShadows);c->receiveShadows=data.value("receiveShadows",c->receiveShadows);
                if(data.contains("embeddedMaterial")){c->materialPath.clear();c->materials.clear();c->material.FromJson(data["embeddedMaterial"]);}
                if(data.contains("vertices")&&data.contains("triangles")){
                    std::vector<Vertex> vertices;std::vector<u32> indices=data["triangles"].get<std::vector<u32>>();
                    for(auto& v:data["vertices"])vertices.push_back({{v[0],v[1],v[2]},{0,0,0},{v.size()>3?v[3].get<float>():0,v.size()>4?v[4].get<float>():0}});
                    if(vertices.empty()||indices.empty()||indices.size()%3||vertices.size()>1000000||std::any_of(indices.begin(),indices.end(),[&](auto i){return i>=vertices.size();}))return;
                    ComputeNormalsAndTangents(vertices,indices,true);c->mesh="runtime:mesh:"+std::to_string(id);AssetManager::SetRuntimeMesh(c->mesh,MakeRef<Mesh>(*AssetManager::Device(),vertices,indices,c->mesh));RebuildBody(e);
                }
            }
            if(type==27)if(auto* c=r.TryGet<SpriteBatchComponent>(e)){
                bool structure=false;
                auto decode=[](const std::string& text){auto bytes=Base64::Decode(text);std::vector<SpriteQuad> quads(bytes.size()/sizeof(SpriteQuad));if(!quads.empty())std::memcpy(quads.data(),bytes.data(),quads.size()*sizeof(SpriteQuad));return quads;};
                if(data.contains("textures")&&data["textures"].is_array()){c->textures.clear();for(auto& t:data["textures"])if(t.is_string())c->textures.push_back(t.get<std::string>());structure=true;}
                if(data.contains("animations")&&data["animations"].is_array()){c->animations.clear();for(auto& a:data["animations"]){SpriteAnimation anim;anim.fps=a.value("fps",8.f);for(auto& f:a.value("frames",nlohmann::json::array()))if(f.is_array()&&f.size()==4)anim.frames.push_back({f[0].get<float>(),f[1].get<float>(),f[2].get<float>(),f[3].get<float>()});c->animations.push_back(anim);}structure=true;}
                if(data.contains("quads")&&data["quads"].is_string()){c->quads=decode(data["quads"].get<std::string>());structure=true;}
                if(data.contains("count")&&data["count"].is_number_integer()){const size_t count=(size_t)std::clamp<i64>(data["count"].get<i64>(),0,4000000);if(count!=c->quads.size()){SpriteQuad empty;empty.size={0,0};c->quads.resize(count,empty);structure=true;}}
                // Partial update: overwrite quads from "start" (the quad count grows if needed)
                if(data.contains("replace")&&data["replace"].is_object()){auto& rep=data["replace"];const size_t start=(size_t)std::max<i64>(0,rep.value("start",(i64)0));auto quads=decode(rep.value("quads",std::string()));
                    if(!quads.empty()&&start+quads.size()<=4000000){const bool grew=start+quads.size()>c->quads.size();if(grew){SpriteQuad empty;empty.size={0,0};c->quads.resize(start+quads.size(),empty);}std::memcpy(c->quads.data()+start,quads.data(),quads.size()*sizeof(SpriteQuad));if(grew)structure=true;else c->TouchRange(start,quads.size());}}
                c->material=data.value("material",c->material);v4("color",c->color);c->sortingOrder=data.value("sortingOrder",c->sortingOrder);
                if(structure)c->Touch();
            }
            if(type==22)if(auto* c=r.TryGet<SpriteRendererComponent>(e)){c->sprite=data.value("sprite",c->sprite);c->material=data.value("material",c->material);v4("color",c->color);c->pixelsPerUnit=std::max(.01f,data.value("pixelsPerUnit",c->pixelsPerUnit));c->flipX=data.value("flipX",c->flipX);c->flipY=data.value("flipY",c->flipY);c->sortingOrder=data.value("sortingOrder",c->sortingOrder);}
            if(type==23)if(auto* c=r.TryGet<EnvironmentComponent>(e)){c->shadowStrength=std::clamp(data.value("shadowStrength",c->shadowStrength),0.f,1.f);c->minimumAmbient=std::max(0.f,data.value("minimumAmbient",c->minimumAmbient));c->exposure=data.value("exposure",c->exposure);c->ambientIntensity=data.value("ambientIntensity",c->ambientIntensity);c->bloomIntensity=data.value("bloomIntensity",c->bloomIntensity);c->contrast=data.value("contrast",c->contrast);c->saturation=data.value("saturation",c->saturation);c->vignette=data.value("vignette",c->vignette);c->fogDensity=data.value("fogDensity",c->fogDensity);c->fxaa=data.value("fxaa",c->fxaa);c->skyMode=data.value("skyMode",c->skyMode);c->skyMaterial=data.value("skyMaterial",c->skyMaterial);v4("skyColor",c->skyColor);}
            if(type==11)if(auto* c=r.TryGet<AudioSourceComponent>(e)){c->clip=data.value("clip",c->clip);c->volume=data.value("volume",c->volume);c->pitch=data.value("pitch",c->pitch);c->loop=data.value("loop",c->loop);c->spatial=data.value("spatial",c->spatial);AudioEngine::Stop(e);}

            if(type==6)if(auto* c=r.TryGet<CameraComponent>(e)){c->cullingMask=data.value("cullingMask",c->cullingMask);c->targetTexture=data.value("targetTexture",c->targetTexture);c->targetWidth=std::clamp(data.value("targetWidth",c->targetWidth),16,8192);c->targetHeight=std::clamp(data.value("targetHeight",c->targetHeight),16,8192);c->orthographic=data.value("orthographic",c->orthographic);c->orthographicSize=std::max(.001f,data.value("orthographicSize",c->orthographicSize));c->fovDegrees=std::clamp(data.value("fieldOfView",c->fovDegrees),1.f,179.f);c->nearPlane=std::max(.001f,data.value("nearClipPlane",c->nearPlane));c->farPlane=std::max(c->nearPlane+.001f,data.value("farClipPlane",c->farPlane));}
            if(type==13)if(auto* c=r.TryGet<CanvasComponent>(e)){v2("referenceResolution",c->referenceResolution);c->renderMode=data.value("renderMode",c->renderMode);c->pixelsPerUnit=data.value("pixelsPerUnit",c->pixelsPerUnit);c->scaleWithScreen=data.value("scaleWithScreen",c->scaleWithScreen);}
            if(type==14)if(auto* c=r.TryGet<RectTransformComponent>(e)){v2("anchorMin",c->anchorMin);v2("anchorMax",c->anchorMax);v2("pivot",c->pivot);v2("anchoredPosition",c->anchoredPosition);v2("sizeDelta",c->sizeDelta);v2("scale",c->scale);c->rotation=data.value("rotation",c->rotation);}
            if(type==15)if(auto* c=r.TryGet<UIImageComponent>(e)){c->sprite=data.value("sprite",c->sprite);c->texture=data.value("texture",c->texture);c->material=data.value("material",c->material);v4("color",c->color);c->type=data.value("type",c->type);c->preserveAspect=data.value("preserveAspect",c->preserveAspect);c->pixelsPerUnitMultiplier=std::max(.01f,data.value("pixelsPerUnitMultiplier",c->pixelsPerUnitMultiplier));c->fillAmount=data.value("fillAmount",c->fillAmount);}
            if(type==16)if(auto* c=r.TryGet<UITextComponent>(e)){c->text=data.value("text",c->text);c->fontSize=data.value("fontSize",c->fontSize);c->font=data.value("font",c->font);c->material=data.value("material",c->material);v4("color",c->color);v4("outlineColor",c->outlineColor);c->outlineWidth=data.value("outlineWidth",c->outlineWidth);c->richText=data.value("richText",c->richText);c->wordWrap=data.value("wordWrap",c->wordWrap);c->autoSize=data.value("autoSize",c->autoSize);c->alignment=data.value("alignment",c->alignment);}
            if(type==18)if(auto* c=r.TryGet<CanvasGroupComponent>(e)){c->alpha=data.value("alpha",c->alpha);c->interactable=data.value("interactable",c->interactable);c->blocksRaycasts=data.value("blocksRaycasts",c->blocksRaycasts);}
            if(type==21)if(auto* c=r.TryGet<UIInputFieldComponent>(e)){c->text=data.value("text",c->text);c->placeholder=data.value("placeholder",c->placeholder);c->interactable=data.value("interactable",c->interactable);c->readOnly=data.value("readOnly",c->readOnly);c->password=data.value("password",c->password);c->multiline=data.value("multiline",c->multiline);c->characterLimit=data.value("characterLimit",c->characterLimit);}
            if(type==20)if(auto* c=r.TryGet<UILayoutGroupComponent>(e)){c->mode=data.value("mode",c->mode);c->columns=data.value("columns",c->columns);v2("spacing",c->spacing);v2("cellSize",c->cellSize);c->fitContent=data.value("fitContent",c->fitContent);}
            if(type==17)if(auto* c=r.TryGet<UIButtonComponent>(e)){c->interactable=data.value("interactable",c->interactable);c->transition=data.value("transition",c->transition);c->fadeDuration=data.value("fadeDuration",c->fadeDuration);v4("highlightedColor",c->highlightedColor);v4("pressedColor",c->pressedColor);v4("disabledColor",c->disabledColor);}
            if(type==12)if(auto* c=r.TryGet<AnimatorComponent>(e)){if(data.contains("state")){c->state=data["state"].get<std::string>();c->time=0;}c->speed=data.value("speed",c->speed);c->playing=data.value("playing",c->playing);if(data.contains("parameter"))c->parameters[data.at("parameter").get<std::string>()]=data.value("value",0.f);}
        }catch(const std::exception& ex){SD_ERROR("Component property: {}",ex.what());}
    };
    a.MountPak = [](const char* path) -> int { return Pak::Mount(fs::u8path(path), Project::AssetsDir()) ? 1 : 0; };

    a.DontDestroyOnLoad=[](u64 id){if(S())S()->DontDestroyOnLoad(E(id));};
    a.UnmountPak=[](const char* path)->int{try{return Pak::Unmount(fs::u8path(path))?1:0;}catch(...){return 0;}};
    a.AssetSize=[](const char* path)->i64{try{auto p=AssetManager::ToAbsolute(path);if(Pak::Contains(p))return (i64)Pak::Size(p);std::error_code ec;auto size=fs::file_size(p,ec);return ec?-1:(i64)size;}catch(...){return -1;}};
    a.QueryJson=[](const char* request)->const char*{
        static thread_local std::string out;
        nlohmann::json result=nlohmann::json::object();
        auto q=nlohmann::json::parse(request?request:"",nullptr,false);
        if(q.is_object()&&q.value("kind","")=="spriteBatchQuads"&&S()){
            const u64 id=std::strtoull(q.value("entity","0").c_str(),nullptr,16);
            if(EntityID e=S()->FindByUUID(id))if(auto* c=S()->Reg().TryGet<SpriteBatchComponent>(e))result["quads"]=Base64::Encode(c->quads.data(),c->quads.size()*sizeof(SpriteQuad));
        }
        if(q.is_object()&&q.value("kind","")=="textureSize")
            if(auto texture=AssetManager::GetTexture(q.value("path",""),true)){result["width"]=texture->Desc().width;result["height"]=texture->Desc().height;}
        out=result.dump();return out.c_str();
    };
    a.ReadAsset=[](const char* path,u64 offset,u8* buffer,u64 count)->i64{try{auto p=AssetManager::ToAbsolute(path);if(!buffer&&count)return -1;if(Pak::Contains(p)){auto size=Pak::Size(p);if(offset==0&&count>=size){auto data=FileSystem::ReadBinary(p);if(!data.empty())memcpy(buffer,data.data(),data.size());return (i64)data.size();}return (i64)Pak::ReadRange(p,offset,buffer,count);}std::ifstream in(p,std::ios::binary);if(!in)return -1;in.seekg(offset);in.read((char*)buffer,count);return (i64)in.gcount();}catch(const std::exception& e){SD_ERROR("Asset read: {}",e.what());return -1;}};

    a.SetTimeScale = [](float t) { if (S()) S()->timeScale = std::max(t, 0.0f); };
    a.GetTimeScale = []() -> float { return S() ? S()->timeScale : 1.0f; };
    a.AudioPlay = [](u64 id) { AudioEngine::Play(E(id)); };
    a.AudioStop = [](u64 id) { AudioEngine::Stop(E(id)); };
    a.AudioIsPlaying = [](u64 id) -> int { return AudioEngine::IsPlaying(E(id)); };
    a.AudioSetVolume = [](u64 id, float v) { AudioEngine::SetVolume(E(id), v); };
    a.AudioSetPitch = [](u64 id, float v) { AudioEngine::SetPitch(E(id), v); };
    a.PlayClipAtPoint = [](const char* clip, const glm::vec3* p, float vol) { AudioEngine::PlayOneShot(clip, *p, vol, true); };
    a.GetLight = [](u64 id, glm::vec4* c, float* range) {
        EntityID e = E(id);
        if (!e) return;
        Registry& r = S()->Reg();
        if (auto* l = r.TryGet<PointLightComponent>(e)) { *c = glm::vec4(l->color, l->intensity); *range = l->range; }
        else if (auto* l2 = r.TryGet<SpotLightComponent>(e)) { *c = glm::vec4(l2->color, l2->intensity); *range = l2->range; }
        else if (auto* l3 = r.TryGet<DirectionalLightComponent>(e)) { *c = glm::vec4(l3->color, l3->intensity); *range = 0.0f; }
    };
    a.SetLight = [](u64 id, const glm::vec4* c, float range) {
        EntityID e = E(id);
        if (!e) return;
        Registry& r = S()->Reg();
        if (auto* l = r.TryGet<PointLightComponent>(e)) { l->color = glm::vec3(*c); l->intensity = c->w; l->range = range; }
        else if (auto* l2 = r.TryGet<SpotLightComponent>(e)) { l2->color = glm::vec3(*c); l2->intensity = c->w; l2->range = range; }
        else if (auto* l3 = r.TryGet<DirectionalLightComponent>(e)) { l3->color = glm::vec3(*c); l3->intensity = c->w; }
    };
    a.GetChildCount = [](u64 id) -> int { EntityID e = E(id); return e ? (int)S()->GetChildren(e).size() : 0; };
    a.GetChild = [](u64 id, int i) -> u64 {
        EntityID e = E(id);
        if (!e) return 0;
        auto& c = S()->GetChildren(e);
        return i >= 0 && i < (int)c.size() ? U(c[i]) : 0;
    };
    a.GetScreenSize = [](glm::vec2* o) { *o = s_ScreenSize; };
}

// ------------------------------------------------------------------------------------------------ hosting

template<typename T>
static bool GetExport(load_assembly_and_get_function_pointer_fn load, const fs::path& assembly, const wchar_t* method, T& out) {
    auto typeName=fs::path(L"Stardust.Interop.Bridge, Stardust.ScriptCore").native();auto methodName=fs::path(method).native();
    int rc = load(assembly.c_str(), typeName.c_str(), methodName.c_str(), UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&out);
    if (rc != 0 || !out) { SD_ERROR("ScriptCore export not found: {} (0x{:x})", fs::path(method).string(), (u32)rc); return false; }
    return true;
}

bool ScriptEngine::Init() {
    fs::path exeDir = FileSystem::ExecutableDir();
    fs::path coreDll = exeDir / "Stardust.ScriptCore.dll";
    fs::path config = exeDir / "Stardust.ScriptCore.runtimeconfig.json";
    if (!fs::exists(coreDll) || !fs::exists(config)) { SD_ERROR("Stardust.ScriptCore.dll not found next to the executable"); return false; }

    char_t buffer[4096];
    size_t size = 4096;
    if (get_hostfxr_path(buffer, &size, nullptr) != 0) { SD_ERROR("Could not locate hostfxr: install the .NET 8 runtime"); return false; }
#ifdef _WIN32
    HMODULE lib = LoadLibraryW(buffer);
    if (!lib) { SD_ERROR("Could not load hostfxr"); return false; }
    auto init = (hostfxr_initialize_for_runtime_config_fn)GetProcAddress(lib, "hostfxr_initialize_for_runtime_config");
    auto getDelegate = (hostfxr_get_runtime_delegate_fn)GetProcAddress(lib, "hostfxr_get_runtime_delegate");
    auto close = (hostfxr_close_fn)GetProcAddress(lib, "hostfxr_close");
#else
    void* lib=dlopen(buffer,RTLD_LAZY|RTLD_LOCAL);if(!lib){SD_ERROR("Could not load hostfxr: {}",dlerror());return false;}
    auto init=(hostfxr_initialize_for_runtime_config_fn)dlsym(lib,"hostfxr_initialize_for_runtime_config");
    auto getDelegate=(hostfxr_get_runtime_delegate_fn)dlsym(lib,"hostfxr_get_runtime_delegate");
    auto close=(hostfxr_close_fn)dlsym(lib,"hostfxr_close");
#endif
    if (!init || !getDelegate || !close) { SD_ERROR("hostfxr exports missing"); return false; }

    hostfxr_handle ctx = nullptr;
    int rc = init(config.c_str(), nullptr, &ctx);
    if (rc < 0 || !ctx) { SD_ERROR("hostfxr init failed (0x{:x})", (u32)rc); if (ctx) close(ctx); return false; }
    load_assembly_and_get_function_pointer_fn load = nullptr;
    rc = getDelegate(ctx, hdt_load_assembly_and_get_function_pointer, (void**)&load);
    close(ctx);
    if (rc != 0 || !load) { SD_ERROR("hostfxr delegate failed (0x{:x})", (u32)rc); return false; }

    ManagedApi& m = s_Managed;
    bool ok = GetExport(load, coreDll, L"Initialize", m.Initialize) && GetExport(load, coreDll, L"LoadAssembly", m.LoadAssembly) &&
              GetExport(load, coreDll, L"UnloadAssembly", m.UnloadAssembly) && GetExport(load, coreDll, L"GetClasses", m.GetClasses) &&
              GetExport(load, coreDll, L"CreateInstance", m.CreateInstance) && GetExport(load, coreDll, L"DestroyInstance", m.DestroyInstance) &&
              GetExport(load, coreDll, L"ResolveReferences", m.ResolveReferences) &&
              GetExport(load, coreDll, L"InvokeCreate", m.InvokeCreate) && GetExport(load, coreDll, L"InvokeUpdate", m.InvokeUpdate) &&
              GetExport(load, coreDll, L"InvokeFixedUpdate", m.InvokeFixedUpdate) && GetExport(load, coreDll, L"InvokeContact", m.InvokeContact) &&
              GetExport(load, coreDll, L"InvokeDestroy", m.InvokeDestroy) && GetExport(load, coreDll, L"GetFields", m.GetFields) &&
              GetExport(load, coreDll, L"SetField", m.SetField) && GetExport(load, coreDll, L"FreeString", m.FreeString) &&
              GetExport(load, coreDll, L"SetTime", m.SetTime);
    if (!ok || !GetExport(load, coreDll, L"InvokeButton", m.InvokeButton) ||
        !GetExport(load, coreDll, L"LoadEditorAssembly",m.LoadEditorAssembly) ||
        !GetExport(load, coreDll, L"DrawEditorUI",m.DrawEditorUI) || !GetExport(load, coreDll, L"GetEditorCommands",m.GetEditorCommands) ||
        !GetExport(load, coreDll, L"InvokeEditorCommand",m.InvokeEditorCommand) || !GetExport(load,coreDll,L"InvokeNamed",m.InvokeNamed) || !GetExport(load,coreDll,L"DispatchUIButton",m.DispatchUIButton) || !GetExport(load,coreDll,L"ClearUIEvents",m.ClearUIEvents) || !GetExport(load,coreDll,L"InvokeAction",m.InvokeAction) || !GetExport(load,coreDll,L"GetComponentActions",m.GetComponentActions) || !GetExport(load,coreDll,L"DispatchUIInput",m.DispatchUIInput) || !GetExport(load,coreDll,L"PumpSceneLoads",m.PumpSceneLoads)) return false;

    InitNativeApi();
    ScanSources(s_SourceTime, s_SourceCount);
    if (!m.Initialize(&s_Native, (int)sizeof(NativeApi))) { SD_ERROR("ScriptCore rejected the native API table (version mismatch)"); return false; }
    s_Ready = true;
    SD_INFO("C# scripting ready (.NET hosted)");
    ReloadGameAssembly();
    return true;
}

void ScriptEngine::Shutdown() {
    if (!s_Ready) return;
    OnRuntimeStop();
    s_Managed.UnloadAssembly();
    s_Ready = false;
}

fs::path ScriptEngine::ProjectDir() { return Project::Dir(); }
fs::path ScriptEngine::GameAssemblyPath() { return Project::ScriptAssembly(); }

bool ScriptEngine::ReloadGameAssembly() {
    if (!s_Ready) return false;
    if (s_Scene) { SD_WARN("Scripts are reloaded after leaving play mode"); return false; }
    s_Managed.UnloadAssembly();
    s_Classes.clear();
    s_EditorCommands.clear();
    fs::path dll = GameAssemblyPath();
    if (!fs::exists(dll)) { SD_WARN("Game scripts not built yet ({}). Use Scripts > Build.", dll.string()); return false; }
    s_LoadedTime = fs::last_write_time(dll);
    if (!s_Managed.LoadAssembly(FileSystem::Utf8(dll).c_str())) { SD_ERROR("Could not load {}", dll.string()); return false; }

    char* json = s_Managed.GetClasses();
    if (json) {
        auto j = nlohmann::json::parse(json, nullptr, false);
        s_Managed.FreeString(json);
        if (j.is_array())
            for (auto& c : j) {
                ScriptClassInfo info;
                info.name = c.value("name", "");
                for (auto& f : c["fields"]) info.fields.push_back({ f.value("name", ""), f.value("type", ""), f["default"].dump(),f.value("schema",nlohmann::json::object()).dump() });
                for(auto& m:c.value("methods",nlohmann::json::array())) {ScriptMethodInfo method{m.value("name",""),m.value("signature","")};for(auto& p:m["parameters"])method.parameters.push_back({p.value("name",""),p.value("type",""),p["default"].dump()});info.methods.push_back(std::move(method));}
                std::string desc;
                for (auto& f : info.fields) desc += std::format(" {} {};", f.type, f.name);
                SD_TRACE("Script {}:{}", info.name, desc);
                s_Classes.push_back(std::move(info));
            }
    }
    SD_INFO("Game scripts loaded: {} classes", s_Classes.size());
    auto editorDll = ProjectDir() / "bin" / (Project::Config().assemblyName + ".Editor.dll");
    if (s_EditorMode && fs::exists(editorDll) && s_Managed.LoadEditorAssembly(FileSystem::Utf8(editorDll).c_str())) {
        char* commands = s_Managed.GetEditorCommands();
        auto list = nlohmann::json::parse(commands ? commands : "[]",nullptr,false);
        if (commands) s_Managed.FreeString(commands);
        if (list.is_array()) for (auto& command : list) s_EditorCommands.push_back(command.get<std::string>());
    }
    return true;
}
std::string ScriptEngine::DrawEditorUI(const std::string& request){if(!s_Ready||!s_EditorMode)return "{}";auto* response=s_Managed.DrawEditorUI(request.c_str());std::string result=response?response:"{}";if(response)s_Managed.FreeString(response);return result;}
void ScriptEngine::RunEditorCommand(const std::string& path) { if (s_EditorMode && s_Ready) s_Managed.InvokeEditorCommand(path.c_str()); }

bool ScriptEngine::CheckForSourceChange() {
    fs::file_time_type newest;
    size_t count;
    ScanSources(newest, count);
    if (newest == s_SourceTime && count == s_SourceCount) return false;
    s_SourceTime = newest;
    s_SourceCount = count;
    return true;
}

bool ScriptEngine::NeedsBuild() {
    std::error_code ec;
    fs::path dll = GameAssemblyPath();
    if (!fs::exists(dll, ec)) return s_SourceCount > 0;
    auto built=fs::last_write_time(dll,ec);if(ec)return true;
    auto core=fs::last_write_time(FileSystem::ExecutableDir()/"Stardust.ScriptCore.dll",ec);
    return built<s_SourceTime||(!ec&&built<core);
}

bool ScriptEngine::CheckForAssemblyChange() {
    std::error_code ec;
    fs::path dll = GameAssemblyPath();
    if (!fs::exists(dll, ec)) return false;
    auto t = fs::last_write_time(dll, ec);
    return !ec && t != s_LoadedTime;
}

const ScriptClassInfo* ScriptEngine::FindClass(const std::string& name) {
    for (auto& c : s_Classes) if (c.name == name) return &c;
    return nullptr;
}

// ------------------------------------------------------------------------------------------------ build

void ScriptEngine::BuildAsync() {
    if (s_Building) return;
    s_Building = true;
    s_BuildResult = -1;
    std::thread([] {
        fs::path proj = Project::ScriptProjectFile();
        if (!fs::exists(proj)) Project::WriteScriptProject(proj, Project::Config().assemblyName);
        std::wstring cmd = L"dotnet build \"" + proj.wstring() + L"\" -c Debug -o \"" + (ProjectDir() / "bin").wstring() +
                           L"\" -p:StardustBin=\"" + FileSystem::ExecutableDir().wstring() + L"\" --nologo -v:q -clp:NoSummary";
        SD_INFO("Building scripts...");
        int code=RunProcess(cmd,ProjectDir(),[](const std::string& line){if(line.find(": error")!=std::string::npos)SD_ERROR("{}",line);else SD_INFO("{}",line);});
        if (code == 0 && s_EditorMode) {
            Project::WriteEditorScriptProject();
            auto editorProject = ProjectDir() / (Project::Config().assemblyName + ".Editor.csproj");
            std::wstring editorCmd = L"dotnet build \"" + editorProject.wstring() + L"\" -c Debug -o \"" + (ProjectDir()/"bin").wstring() + L"\" -p:StardustBin=\"" + FileSystem::ExecutableDir().wstring() + L"\" --nologo -v:q";
            code = RunProcess(editorCmd, ProjectDir(), [](const std::string& line) { SD_INFO("{}",line); });
        }
        if (code == 0) SD_INFO("Scripts built successfully"); else SD_ERROR("Script build failed (exit code {})", code);
        s_BuildResult = code == 0 ? 1 : 0;
        s_Building = false;
    }).detach();
}

bool ScriptEngine::TakeBuildFinished(bool& success) {
    int r = s_BuildResult.exchange(-1);
    if (r < 0) return false;
    success = r == 1;
    return true;
}

// ------------------------------------------------------------------------------------------------ runtime

void ScriptEngine::OnRuntimeStart(Scene* scene,bool resetTime) {
    s_Scene = scene;
    if(resetTime)s_ElapsedTime = 0.0f;
    if (!s_Ready) return;
    // Like Unity's Awake/Start split: create every instance, then resolve references between them, then OnCreate
    std::vector<EntityID> ids;
    scene->Reg().Each<ScriptComponent>([&](EntityID e, ScriptComponent&) { ids.push_back(e); });
    std::vector<EntityID> created;
    for (EntityID e : ids) if (CreateRaw(e)) created.push_back(e);
    for (EntityID e : created) {
        auto& scripts = scene->Reg().Get<ScriptComponent>(e);
        for (size_t i = 0; i < s_Instances[e].size(); ++i)
            if (s_Instances[e][i]) s_Managed.ResolveReferences(s_Instances[e][i], scripts.Slot(i).fieldsJson.c_str());
    }
    for (EntityID e : created) StartSlots(e);
}

// A script runs only while it is enabled and its object is active in the hierarchy
bool ScriptEngine::SlotRunnable(EntityID e, size_t slot) {
    auto* scripts = s_Scene->Reg().TryGet<ScriptComponent>(e);
    return scripts && slot < scripts->Count() && scripts->Slot(slot).enabled && s_Scene->IsActiveInHierarchy(e);
}
static bool SlotStarted(EntityID e, size_t slot) {
    auto it = s_Started.find(e);
    return it != s_Started.end() && slot < it->second.size() && it->second[slot];
}
// OnCreate waits for the first moment the script may run, and happens once
void ScriptEngine::StartSlots(EntityID e) {
    auto found = s_Instances.find(e);
    if (found == s_Instances.end()) return;
    auto handles = found->second;
    for (size_t i = 0; i < handles.size(); ++i) {
        if (!s_Instances.contains(e)) return; // a previous OnCreate destroyed this object
        if (!handles[i] || SlotStarted(e, i) || !SlotRunnable(e, i)) continue;
        auto& started = s_Started[e];
        started.resize(handles.size(), 0);
        started[i] = 1;
        s_Managed.InvokeCreate(handles[i]);
    }
}

bool ScriptEngine::CreateRaw(EntityID e) {
    if (!s_Ready || !s_Scene) return false;
    auto* scripts = s_Scene->Reg().TryGet<ScriptComponent>(e);
    if (!scripts) return false;
    auto& handles = s_Instances[e];
    if (handles.size() >= scripts->Count()) return false;
    for (size_t i = handles.size(); i < scripts->Count(); ++i) {
        auto& slot = scripts->Slot(i);
        auto instanceFields=nlohmann::json::parse(slot.fieldsJson,nullptr,false);if(!instanceFields.is_object())instanceFields=nlohmann::json::object();instanceFields["__sdScriptSlot"]=i;auto instanceJson=instanceFields.dump();
        void* h = slot.className.empty() ? nullptr : s_Managed.CreateInstance(slot.className.c_str(), s_Scene->Reg().Get<IDComponent>(e).uuid, instanceJson.c_str());
        handles.push_back(h);
        if (!h && !slot.className.empty()) SD_ERROR("Script class '{}' could not be instantiated", slot.className);
    }
    return true;
}

void ScriptEngine::OnRuntimeStop() {
    auto instances = std::move(s_Instances);
    auto started = std::move(s_Started);
    s_Instances.clear();
    s_Started.clear();
    if (s_Ready) for (auto& [e, handles] : instances) for (size_t i = 0; i < handles.size(); ++i) {
        if (!handles[i]) continue;
        auto flags = started.find(e);
        if (flags != started.end() && i < flags->second.size() && flags->second[i]) s_Managed.InvokeDestroy(handles[i]);
        s_Managed.DestroyInstance(handles[i]);
    }
    if(s_Ready)s_Managed.ClearUIEvents();
    s_Scene = nullptr;
}

void ScriptEngine::Instantiate(EntityID e) {
    size_t previous=s_Instances.contains(e) ? s_Instances[e].size() : 0;
    if (!CreateRaw(e)) return;
    auto handles = s_Instances[e];
    auto& scripts = s_Scene->Reg().Get<ScriptComponent>(e);
    for (size_t i = previous; i < handles.size(); ++i)
        if (handles[i]) s_Managed.ResolveReferences(handles[i], scripts.Slot(i).fieldsJson.c_str());
    StartSlots(e);
}

void ScriptEngine::DestroyInstance(EntityID e) {
    auto it = s_Instances.find(e);
    if (it == s_Instances.end()) return;
    auto handles = std::move(it->second);
    s_Instances.erase(it);
    std::vector<char> started;
    if (auto flags = s_Started.find(e); flags != s_Started.end()) { started = std::move(flags->second); s_Started.erase(flags); }
    for (size_t i = 0; i < handles.size(); ++i) {
        if (!handles[i]) continue;
        if (i < started.size() && started[i]) s_Managed.InvokeDestroy(handles[i]);
        s_Managed.DestroyInstance(handles[i]);
    }
}

void ScriptEngine::Update(float dt) {
    if(s_Ready)s_Managed.PumpSceneLoads();
    if (!s_Ready || !s_Scene) return;
    s_ElapsedTime += dt;
    s_CurrentDt = dt;
    s_Managed.SetTime(dt, s_ElapsedTime, 1.0f / 60.0f);
    auto list = s_Instances;
    for (auto& [e, handles] : list) StartSlots(e);
    for (auto& [e, handles] : list) for(size_t i=0;i<handles.size();++i)
        if(handles[i]&&s_Instances.contains(e)&&SlotStarted(e,i)&&SlotRunnable(e,i))s_Managed.InvokeUpdate(handles[i],dt);
}

void ScriptEngine::SetScreenSize(float w, float h) { s_ScreenSize = { w, h }; }
float ScriptEngine::CurrentDeltaTime() { return s_CurrentDt; }

void ScriptEngine::FixedUpdate(float dt) {
    if (!s_Ready || !s_Scene) return;
    s_CurrentDt = dt;
    s_Managed.SetTime(dt, s_ElapsedTime, dt);
    auto list = s_Instances;
    for (auto& [e, handles] : list) StartSlots(e);
    for (auto& [e, handles] : list) for(size_t i=0;i<handles.size();++i)
        if(handles[i]&&s_Instances.contains(e)&&SlotStarted(e,i)&&SlotRunnable(e,i))s_Managed.InvokeFixedUpdate(handles[i],dt);
}

void ScriptEngine::OnContact(EntityID a, EntityID b, int kind) {
    if (!s_Ready || !s_Scene) return;
    auto invoke = [&](EntityID e, EntityID other) {
        auto it = s_Instances.find(e);
        if (it == s_Instances.end()) return;
        auto handles = it->second;
        for (size_t i = 0; i < handles.size(); ++i) if (handles[i] && s_Instances.contains(e) && SlotStarted(e, i) && SlotRunnable(e, i)) s_Managed.InvokeContact(handles[i], U(other), kind);
    };
    invoke(a, b); invoke(b, a);
}

bool ScriptEngine::HasInstance(EntityID e, size_t slot) {
    auto it = s_Instances.find(e);
    return it != s_Instances.end() && slot < it->second.size() && it->second[slot];
}
std::string ScriptEngine::RemapFieldReferences(const std::string& className,const std::string& fieldsText,const std::string& mappingText){using json=nlohmann::json;auto fields=json::parse(fieldsText,nullptr,false),mapping=json::parse(mappingText,nullptr,false);if(!fields.is_object()||!mapping.is_object())return fieldsText;std::function<void(json&)> generic=[&](json& value){if(value.is_object()&&value.contains("asset"))return;if(value.is_object()||value.is_array())for(auto& v:value)generic(v);else if(value.is_string()&&mapping.contains(value.get<std::string>()))value=mapping[value.get<std::string>()];};std::function<void(json&,const json&)> typed=[&](json& value,const json& schema){auto kind=schema.value("kind","");if(kind=="List"&&value.is_array()){for(auto& v:value)typed(v,schema["element"]);}else if(kind=="Dictionary"&&value.is_array()){for(auto& pair:value)if(pair.is_object()){typed(pair["key"],schema["key"]);typed(pair["value"],schema["value"]);}}else if(kind=="Entity"||kind.starts_with("Component:")||kind.starts_with("Script:"))generic(value);};if(auto* info=FindClass(className)){for(auto& field:info->fields)if(fields.contains(field.name)){auto schema=json::parse(field.schemaJson,nullptr,false);if(!schema.is_object()||!schema.contains("kind"))schema={{"kind",field.type}};typed(fields[field.name],schema);}}else generic(fields);return fields.dump();}
std::vector<ScriptMethodInfo> ScriptEngine::ActionMethods(const std::string& component,const std::string& scriptClass){
    if(component=="Script"){auto* info=FindClass(scriptClass);return info?info->methods:std::vector<ScriptMethodInfo>{};}
    if(!s_Ready)return {};char* raw=s_Managed.GetComponentActions(component.c_str());auto json=nlohmann::json::parse(raw?raw:"[]",nullptr,false);if(raw)s_Managed.FreeString(raw);std::vector<ScriptMethodInfo> result;if(json.is_array())for(auto& m:json){ScriptMethodInfo info{m.value("name",""),m.value("signature","")};for(auto& p:m["parameters"])info.parameters.push_back({p.value("name",""),p.value("type",""),p["default"].dump()});result.push_back(std::move(info));}return result;
}
void ScriptEngine::FireAnimationEvent(EntityID e, const std::string& script, const std::string& function, const std::string& signature, const std::string& argumentsJson) {
    if (!s_Ready || !s_Scene || function.empty()) return;
    auto found = s_Instances.find(e);
    auto* scripts = s_Scene->Reg().TryGet<ScriptComponent>(e);
    if (found == s_Instances.end() || !scripts) return;
    auto handles = found->second;
    for (size_t i = 0; i < handles.size(); ++i) {
        if (!handles[i] || !s_Instances.contains(e) || !SlotStarted(e, i) || !SlotRunnable(e, i)) continue;
        scripts = s_Scene->Reg().TryGet<ScriptComponent>(e);
        if (!scripts || (!script.empty() && scripts->Slot(i).className != script)) continue;
        if (signature.empty()) s_Managed.InvokeNamed(handles[i], function.c_str());
        else s_Managed.InvokeAction(handles[i], U(e), "Script", signature.c_str(), argumentsJson.c_str());
    }
}

void ScriptEngine::OnUIInput(EntityID e,const std::string& text,bool submit){if(s_Ready&&s_Scene)s_Managed.DispatchUIInput(U(e),text.c_str(),submit?1:0);}
void ScriptEngine::OnUIButton(EntityID e) {
    if(!s_Ready||!s_Scene)return;
    s_Managed.DispatchUIButton(U(e));
    if(auto* button=s_Scene->Reg().TryGet<UIButtonComponent>(e);button&&!button->actions.empty()){
        auto actions=button->actions;
        for(const auto& action:actions){if(!action.enabled||action.method.empty())continue;auto target=action.target?s_Scene->FindByUUID(action.target):e;if(!target)continue;void* handle=nullptr;if(action.component=="Script"){auto found=s_Instances.find(target);if(found==s_Instances.end()||action.scriptSlot<0||action.scriptSlot>=found->second.size())continue;handle=found->second[action.scriptSlot];if(!handle||!SlotStarted(target,action.scriptSlot)||!SlotRunnable(target,action.scriptSlot))continue;}s_Managed.InvokeAction(handle,U(target),action.component.c_str(),action.method.c_str(),action.argumentsJson.c_str());}
        return;
    }
    EntityID target=e;std::string method="OnClick";
    if(auto* button=s_Scene->Reg().TryGet<UIButtonComponent>(e)){if(button->target)target=s_Scene->FindByUUID(button->target);method=button->method;}
    auto it=s_Instances.find(target);if(it==s_Instances.end())return;auto handles=it->second;
    for(size_t i=0;i<handles.size();++i)if(handles[i]&&s_Instances.contains(target)&&SlotStarted(target,i)&&SlotRunnable(target,i))s_Managed.InvokeNamed(handles[i],method.c_str());
}

std::string ScriptEngine::GetInstanceFields(EntityID e, size_t slot) {
    if (!HasInstance(e, slot)) return "{}";
    char* value = s_Managed.GetFields(s_Instances[e][slot]);
    std::string out = value ? value : "{}";
    if (value) s_Managed.FreeString(value);
    return out;
}
void ScriptEngine::SetInstanceField(EntityID e, const std::string& name, const std::string& json, size_t slot) {
    if (HasInstance(e, slot)) s_Managed.SetField(s_Instances[e][slot], name.c_str(), json.c_str());
}

} // namespace Stardust
