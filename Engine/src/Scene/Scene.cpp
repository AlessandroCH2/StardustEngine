#include "Scene/Scene.h"
#include "Scene/ComponentState.h"
#include "Core/FileSystem.h"
#include <nlohmann/json.hpp>
#include "UI/UIAssets.h"
#include "UI/CanvasRenderer.h"
#include "UI/VideoPlayer.h"
#include "Core/Log.h"
#include "Assets/AssetManager.h"
#include "Physics/PhysicsWorld.h"
#include "Scripting/ScriptEngine.h"
#include "Scene/SceneSerializer.h"
#include "Scene/SceneManager.h"
#include "Audio/AudioEngine.h"
#include "Animation/AnimatorSystem.h"
#include "Animation/ModelSkeleton.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <random>
#include <functional>
#include <unordered_set>
#include <glm/gtx/component_wise.hpp>
#include "Renderer/Quality.h"

namespace Stardust {

u64 GenerateUUID() {
    static std::mt19937_64 rng(std::random_device{}());
    u64 v;
    do { v = rng(); } while (v == 0);
    return v;
}

static void EraseValue(std::vector<EntityID>& v, EntityID e) {
    v.erase(std::remove(v.begin(), v.end(), e), v.end());
}

static void DecomposeWorld(const glm::mat4& m, glm::vec3& pos, glm::quat& rot, glm::vec3& scale) {
    glm::vec3 skew; glm::vec4 persp;
    glm::decompose(m, scale, rot, pos, skew, persp);
    rot = glm::normalize(rot);
}

Scene::Scene() = default;
Scene::~Scene() { for(auto& [uuid,path]:m_CameraTexturePaths)AssetManager::SetRuntimeTexture(path,nullptr);VideoPlayerSystem::Clear(*this);if (m_Running) OnRuntimeStop(); }

// ------------------------------------------------------------------------------------------------ entities

EntityID Scene::CreateEntity(const std::string& name, u64 uuid) {
    EntityID e = m_Registry.Create();
    u64 id = uuid ? uuid : GenerateUUID();
    m_Registry.Add<IDComponent>(e, id);
    m_UUIDMap[id] = e;
    m_Registry.Add<NameComponent>(e, name);
    m_Registry.Add<TransformComponent>(e);
    m_Registry.Add<RelationshipComponent>(e);
    m_RootOrder.push_back(e);
    return e;
}

EntityID Scene::CreatePrimitive(PrimitiveType type, const std::string& name) {
    EntityID e = CreateEntity(name.empty() ? PrimitiveName(type) : name);
    m_Registry.Add<MeshRendererComponent>(e).mesh = AssetManager::BuiltinRef(type);
    return e;
}

void Scene::DestroyEntity(EntityID e) {
    VideoPlayerSystem::Remove(*this,e);
    if (!m_Registry.Valid(e)) return;
    std::vector<EntityID> children = GetChildren(e); // copy: destroying mutates the list
    for (EntityID c : children) DestroyEntity(c);
    EntityID parent = GetParent(e);
    if (parent) EraseValue(m_Registry.Get<RelationshipComponent>(parent).children, e);
    else EraseValue(m_RootOrder, e);
    if (m_Running) {
        ScriptEngine::DestroyInstance(e);
        AudioEngine::RemoveEntity(e);
        if (m_Physics) m_Physics->DestroyBody(e);
    }
    auto uuid=m_Registry.Get<IDComponent>(e).uuid;m_SpriteMeshes.erase(uuid);
    if(auto path=m_CameraTexturePaths.find(uuid);path!=m_CameraTexturePaths.end()){AssetManager::SetRuntimeTexture(path->second,nullptr);m_CameraTexturePaths.erase(path);}m_CameraTargets.erase(uuid);
    m_UUIDMap.erase(m_Registry.Get<IDComponent>(e).uuid);
    m_Registry.Destroy(e);
}

void Scene::CopyComponents(EntityID src, EntityID dst) {
    m_Registry.Get<NameComponent>(dst) = m_Registry.Get<NameComponent>(src);
    m_Registry.Get<TransformComponent>(dst) = m_Registry.Get<TransformComponent>(src);
    if (auto* c = m_Registry.TryGet<SpriteRendererComponent>(src)) m_Registry.Add<SpriteRendererComponent>(dst,*c);
    if (auto* c = m_Registry.TryGet<MeshRendererComponent>(src)) m_Registry.Add<MeshRendererComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<CameraComponent>(src)) m_Registry.Add<CameraComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<DirectionalLightComponent>(src)) m_Registry.Add<DirectionalLightComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<EnvironmentComponent>(src)) m_Registry.Add<EnvironmentComponent>(dst, *c);
    if(auto* c=m_Registry.TryGet<ProceduralTerrainComponent>(src))m_Registry.Add<ProceduralTerrainComponent>(dst,*c);
    if(auto* c=m_Registry.TryGet<TerrainVegetationComponent>(src))m_Registry.Add<TerrainVegetationComponent>(dst,*c);
    if(auto* c=m_Registry.TryGet<LODGroupComponent>(src))m_Registry.Add<LODGroupComponent>(dst,*c);
    if (auto* c = m_Registry.TryGet<TerrainComponent>(src)) m_Registry.Add<TerrainComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<RigidbodyComponent>(src)) m_Registry.Add<RigidbodyComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<BoxColliderComponent>(src)) m_Registry.Add<BoxColliderComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<SphereColliderComponent>(src)) m_Registry.Add<SphereColliderComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<CapsuleColliderComponent>(src)) m_Registry.Add<CapsuleColliderComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<MeshColliderComponent>(src)) m_Registry.Add<MeshColliderComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<ScriptComponent>(src)) m_Registry.Add<ScriptComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<AnimatorComponent>(src)) m_Registry.Add<AnimatorComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<UIButtonComponent>(src)) m_Registry.Add<UIButtonComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<UITextComponent>(src)) m_Registry.Add<UITextComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<UIImageComponent>(src)) m_Registry.Add<UIImageComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<RectTransformComponent>(src)) m_Registry.Add<RectTransformComponent>(dst, *c);
    if (auto* c = m_Registry.TryGet<CanvasComponent>(src)) m_Registry.Add<CanvasComponent>(dst, *c);
    if(auto* c=m_Registry.TryGet<CanvasGroupComponent>(src))m_Registry.Add<CanvasGroupComponent>(dst,*c);
    if(auto* c=m_Registry.TryGet<FbxRigComponent>(src))m_Registry.Add<FbxRigComponent>(dst,*c);if(auto* c=m_Registry.TryGet<BoneComponent>(src))m_Registry.Add<BoneComponent>(dst,*c);
    if(auto* c=m_Registry.TryGet<UIInputFieldComponent>(src))m_Registry.Add<UIInputFieldComponent>(dst,*c);
    if(auto* c=m_Registry.TryGet<UILayoutGroupComponent>(src))m_Registry.Add<UILayoutGroupComponent>(dst,*c);
    if(m_Registry.Has<RectMaskComponent>(src))m_Registry.Add<RectMaskComponent>(dst);
    if(auto* c=m_Registry.TryGet<UIMaskComponent>(src))m_Registry.Add<UIMaskComponent>(dst,*c);
    VisitToggleComponents([&]<typename T>(int,const char*){if(m_Registry.Has<T>(src))m_Registry.SetEnabled<T>(dst,m_Registry.IsEnabled<T>(src));});
}

// Copies go through the serializer, so every component (current and future) is duplicated
EntityID Scene::DuplicateEntity(EntityID src) {
    if (!m_Registry.Valid(src)) return kNullEntity;
    EntityID parent = GetParent(src);
    EntityID e = SceneSerializer::InstantiateJson(*this, SceneSerializer::EntityToJson(*this, src), parent);
    if (!e) return kNullEntity;
    m_Registry.Get<NameComponent>(e).prefab = m_Registry.Get<NameComponent>(src).prefab;
    // place right after the original, under the same parent
    auto& siblings = parent ? m_Registry.Get<RelationshipComponent>(parent).children : m_RootOrder;
    int idx = (int)(std::find(siblings.begin(), siblings.end(), src) - siblings.begin());
    SetParent(e, parent, false, idx + 1);
    return e;
}

EntityID Scene::Instantiate(EntityID source, const glm::vec3* position) {
    EntityID e = DuplicateEntity(source);
    if (!e) return e;
    if (position) {
        glm::mat4 w = GetWorldMatrix(e);
        w[3] = glm::vec4(*position, 1.0f);
        SetWorldMatrix(e, w);
    }
    if (m_Running) StartRuntimeObjects(e);
    return e;
}

EntityID Scene::InstantiatePrefab(const std::string& prefabAsset, const glm::vec3* position) {
    EntityID e = SceneSerializer::InstantiatePrefab(*this, AssetManager::ToAbsolute(prefabAsset));
    if (!e) return e;
    if (position) m_Registry.Get<TransformComponent>(e).position = *position;
    if (m_Running) StartRuntimeObjects(e);
    return e;
}

void Scene::StartRuntimeObjects(EntityID root) {
    std::vector<EntityID> stack{ root }, all;
    while (!stack.empty()) {
        EntityID e = stack.back(); stack.pop_back();
        all.push_back(e);
        for (EntityID c : GetChildren(e)) stack.push_back(c);
    }
    for (EntityID e : all) if (m_Physics && IsActiveInHierarchy(e)) m_Physics->CreateBody(e);
    for (EntityID e : all) ScriptEngine::Instantiate(e);
    for (EntityID e : all)
        if (auto* a = m_Registry.TryGet<AudioSourceComponent>(e); a && a->playOnStart && IsActiveInHierarchy(e)) AudioEngine::Play(e);
}

EntityID Scene::FindWithTag(const std::string& tag) {
    EntityID found = kNullEntity;
    m_Registry.Each<NameComponent>([&](EntityID e, NameComponent& n) { if (!found && n.tag == tag && IsActiveInHierarchy(e)) found = e; });
    return found;
}

void Scene::RefreshComponentState(EntityID e,int type){
 if(!m_Registry.Valid(e))return;
 if(type==24&&m_Registry.IsEnabled<TerrainComponent>(e))if(auto* device=AssetManager::Device())PrepareTerrains(*device);
 if(m_Physics&&(type==2||type==3||type==4||type==5||type==9||type==24)){m_Physics->DestroyBody(e);if(IsActiveInHierarchy(e))m_Physics->CreateBody(e);}
 if(m_Running&&type==11){AudioEngine::Stop(e);if(auto* audio=m_Registry.TryGetEnabled<AudioSourceComponent>(e);audio&&audio->playOnStart&&IsActiveInHierarchy(e))AudioEngine::Play(e);}
}

void Scene::SetActive(EntityID e, bool active) {
    auto& n = m_Registry.Get<NameComponent>(e);
    if (n.active == active) return;
    n.active = active;
    if (!m_Running || !m_Physics) return;
    // bodies follow the active state of the whole subtree
    std::vector<EntityID> stack{ e };
    while (!stack.empty()) {
        EntityID x = stack.back(); stack.pop_back();
        if (IsActiveInHierarchy(x)) m_Physics->CreateBody(x); else m_Physics->DestroyBody(x);
        for (EntityID c : GetChildren(x)) stack.push_back(c);
    }
}

EntityID Scene::FindByName(const std::string& name) {
    EntityID found = kNullEntity;
    m_Registry.Each<NameComponent>([&](EntityID e, NameComponent& n) { if (!found && n.name == name) found = e; });
    return found;
}

EntityID Scene::FindByUUID(u64 uuid) {
    auto it = m_UUIDMap.find(uuid);
    return it != m_UUIDMap.end() && m_Registry.Valid(it->second) ? it->second : kNullEntity;
}

// ------------------------------------------------------------------------------------------------ hierarchy

EntityID Scene::GetParent(EntityID e) {
    auto* r = m_Registry.TryGet<RelationshipComponent>(e);
    return r ? r->parent : kNullEntity;
}

const std::vector<EntityID>& Scene::GetChildren(EntityID e) {
    static const std::vector<EntityID> kEmpty;
    auto* r = m_Registry.TryGet<RelationshipComponent>(e);
    return r ? r->children : kEmpty;
}

std::vector<EntityID> Scene::GetRoots() {
    std::vector<EntityID> out;
    for (EntityID e : m_RootOrder) if (m_Registry.Valid(e)) out.push_back(e);
    return out;
}

bool Scene::IsDescendantOf(EntityID e, EntityID ancestor) {
    for (EntityID p = GetParent(e); p; p = GetParent(p))
        if (p == ancestor) return true;
    return false;
}

void Scene::SetParent(EntityID child, EntityID parent, bool keepWorld, int index) {
    if (!m_Registry.Valid(child) || child == parent) return;
    if (parent && IsDescendantOf(parent, child)) return; // would create a cycle

    glm::mat4 world = GetWorldMatrix(child);
    auto& rel = m_Registry.Get<RelationshipComponent>(child);
    if (rel.parent) EraseValue(m_Registry.Get<RelationshipComponent>(rel.parent).children, child);
    else EraseValue(m_RootOrder, child);

    rel.parent = parent;
    auto& list = parent ? m_Registry.Get<RelationshipComponent>(parent).children : m_RootOrder;
    if (index < 0 || index > (int)list.size()) list.push_back(child);
    else list.insert(list.begin() + index, child);

    if (keepWorld) SetWorldMatrix(child, world);
}

void Scene::MoveInParent(EntityID e, int newIndex) {
    EntityID parent = GetParent(e);
    auto& list = parent ? m_Registry.Get<RelationshipComponent>(parent).children : m_RootOrder;
    EraseValue(list, e);
    newIndex = std::clamp(newIndex, 0, (int)list.size());
    list.insert(list.begin() + newIndex, e);
}

glm::mat4 Scene::GetWorldMatrix(EntityID e) {
    glm::mat4 local = m_Registry.Get<TransformComponent>(e).Matrix();
    EntityID p = GetParent(e);
    return p ? GetWorldMatrix(p) * local : local;
}

void Scene::SetWorldMatrix(EntityID e, const glm::mat4& world) {
    EntityID p = GetParent(e);
    glm::mat4 local = p ? glm::inverse(GetWorldMatrix(p)) * world : world;
    auto& t = m_Registry.Get<TransformComponent>(e);
    DecomposeWorld(local, t.position, t.rotation, t.scale);
    t.eulerHint = glm::degrees(glm::eulerAngles(t.rotation));
}

bool Scene::IsActiveInHierarchy(EntityID e) {
    for (EntityID x = e; x; x = GetParent(x))
        if (!m_Registry.Get<NameComponent>(x).active) return false;
    return true;
}

void Scene::ComputeWorldMatrices() {
    // Depth-first from the roots: each world matrix is computed exactly once
    std::vector<EntityID> all = m_Registry.AllEntities();
    m_WorldCache.assign(all.empty() ? 0 : all.back() + 1, glm::mat4(1.0f));
    std::vector<std::pair<EntityID, glm::mat4>> stack;
    for (EntityID r : GetRoots()) stack.push_back({ r, glm::mat4(1.0f) });
    while (!stack.empty()) {
        auto [e, parentWorld] = stack.back();
        stack.pop_back();
        glm::mat4 w = parentWorld * m_Registry.Get<TransformComponent>(e).Matrix();
        m_WorldCache[e] = w;
        for (EntityID c : GetChildren(e)) stack.push_back({ c, w });
    }
}

// ------------------------------------------------------------------------------------------------ runtime

void Scene::DontDestroyOnLoad(EntityID e){if(!m_Running||!m_Registry.Valid(e))return;while(GetParent(e))e=GetParent(e);m_PersistentRoots.insert(m_Registry.Get<IDComponent>(e).uuid);}
void Scene::ReplaceContents(Scene& next,RHI::Device& device){
 if(!m_Running)return;
 std::vector<EntityID> preserve;
 for(auto uuid:m_PersistentRoots){auto e=FindByUUID(uuid);if(e){SetParent(e,0,true);preserve.push_back(e);}}
 for(auto e:GetRoots())if(std::find(preserve.begin(),preserve.end(),e)==preserve.end())DestroyEntity(e);
 m_PendingDestroy.clear();
 if(m_Physics)m_Physics->TakeCollisionEvents();
 auto roots=SceneSerializer::AppendScene(*this,SceneSerializer::ToJson(next));m_Name=next.Name();SceneManager::SetActiveSceneName(m_Name);m_FixedAccumulator=0;ComputeWorldMatrices();PrepareTerrains(device);
 std::vector<EntityID> added;std::function<void(EntityID)> visit=[&](auto e){added.push_back(e);for(auto c:GetChildren(e))visit(c);};for(auto root:roots)visit(root);
 for(auto e:added)if(m_Physics&&IsActiveInHierarchy(e))m_Physics->CreateBody(e);
 ScriptEngine::OnRuntimeStart(this,false);
 for(auto e:added)if(auto* audio=m_Registry.TryGet<AudioSourceComponent>(e);audio&&audio->playOnStart&&IsActiveInHierarchy(e))AudioEngine::Play(e);
 AudioEngine::Update(*this);
}

void Scene::OnRuntimeStart() {
    SceneManager::SetActiveSceneName(m_Name);
    m_Running = true;
    m_FixedAccumulator = 0.0f;
    // Video Players start from the beginning, playing only with Play On Awake
    m_Registry.Each<VideoPlayerComponent>([](EntityID,VideoPlayerComponent& c){c.playing=c.playOnStart;c.time=0;});
    ComputeWorldMatrices();
    m_Physics = MakeScope<PhysicsWorld>(*this);
    timeScale = 1.0f;
    m_Registry.Each<VideoPlayerComponent>([](EntityID,VideoPlayerComponent& c){c.playing=c.playOnStart;});
    ScriptEngine::OnRuntimeStart(this);
    AudioEngine::OnRuntimeStart(*this);
}

void Scene::OnRuntimeStop() {
    if (!m_Running) return;
    VideoPlayerSystem::Clear(*this);
    AudioEngine::OnRuntimeStop();
    ScriptEngine::OnRuntimeStop();
    m_Physics.reset();
    m_Running = false;m_PersistentRoots.clear();
}

// Unity-like order: FixedUpdate + physics at 60 Hz, collision callbacks, then Update once per frame
void Scene::OnUpdate(float dt) {
    if (!m_Running) return;
    dt *= std::max(timeScale, 0.0f);
    VideoPlayerSystem::Update(*this,dt);
    AnimatorSystem::Update(*this, dt);
    const float fixedDt = 1.0f / 60.0f;
    m_FixedAccumulator = std::min(m_FixedAccumulator + dt, fixedDt * 5.0f);
    while (m_FixedAccumulator >= fixedDt) {
        ScriptEngine::FixedUpdate(fixedDt);
        if (m_Physics) {
            m_Physics->Step(fixedDt);
            for (auto& c : m_Physics->TakeCollisionEvents())
                if (m_Registry.Valid(c.a) && m_Registry.Valid(c.b)) ScriptEngine::OnContact(c.a, c.b, (int)c.kind);
        }
        m_FixedAccumulator -= fixedDt;
    }
    ScriptEngine::Update(dt);

    // Entities created by scripts this frame get their bodies / instances; destroyed ones are removed
    auto pending = std::move(m_PendingDestroy);
    m_PendingDestroy.clear();
    for (EntityID e : pending) if (m_Registry.Valid(e)) DestroyEntity(e);
    ComputeWorldMatrices();
    AudioEngine::Update(*this);
}

// ------------------------------------------------------------------------------------------------ rendering

EnvironmentSettings Scene::GatherEnvironment() {
    EnvironmentSettings env;
    m_Registry.Each<DirectionalLightComponent>([&](EntityID e, DirectionalLightComponent& l) {
        if (!IsActiveInHierarchy(e)) return;
        glm::mat4 w = GetWorldMatrix(e);
        glm::vec3 forward = glm::normalize(glm::vec3(w * glm::vec4(0, 0, -1, 0)));
        env.sunDirection = -forward; // light travels along forward, so "towards the sun" is backwards
        env.sunColor = l.color;
        env.sunIntensity = l.intensity;
        env.castShadows = l.castShadows;
        env.shadowDistance = l.shadowDistance;
    });
    m_Registry.Each<EnvironmentComponent>([&](EntityID, EnvironmentComponent& c) {
        env.skyMode=c.skyMode;env.skyMaterial=c.skyMaterial;env.skyFaces=c.skyFaces;env.skyColor=c.skyColor;env.skyRotation=c.skyRotation;env.skyExposure=c.skyExposure;
        env.shadowStrength=c.shadowStrength;env.minimumAmbient=c.minimumAmbient;
        env.exposure = c.exposure;
        env.ambientIntensity = c.ambientIntensity;
        env.ambientSky=c.ambientSky;env.ambientHorizon=c.ambientHorizon;env.ambientGround=c.ambientGround;
        env.cloudCoverage = c.cloudCoverage;
        env.cloudDensity = c.cloudDensity;
        env.fogDensity = c.fogDensity;
        env.fogHeightFalloff = c.fogHeightFalloff;
        float a = glm::radians(c.windDirectionDeg);
        env.windDirection = { std::cos(a), std::sin(a) };
        env.windStrength = c.windStrength;
        env.bloomIntensity = c.bloomIntensity;
        env.contrast = c.contrast;
        env.saturation = c.saturation;
        env.vignette = c.vignette;
        env.fxaa = c.fxaa;
    });
    return env;
}

bool Scene::GetCamera(EntityID e,RenderCamera& out,float aspect){
    if(!m_Registry.IsEnabled<CameraComponent>(e))return false;
    auto* camera=m_Registry.TryGet<CameraComponent>(e);if(!camera||!IsActiveInHierarchy(e))return false;auto& c=*camera;
    glm::vec3 pos, scale; glm::quat rot;
    DecomposeWorld(GetWorldMatrix(e), pos, rot, scale);
    out.position = pos;
    out.view = glm::inverse(glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot));
    out.fovY = glm::radians(c.fovDegrees);
    out.nearPlane = c.nearPlane;
    out.orthographic=c.orthographic;out.orthographicSize=std::max(.001f,c.orthographicSize);out.farPlane=std::max(c.nearPlane+.01f,c.farPlane);
    float h=out.orthographicSize;out.projection=c.orthographic?OrthoReverseZ(-h*aspect,h*aspect,-h,h,c.nearPlane,out.farPlane):PerspectiveReverseZ(out.fovY, aspect, c.nearPlane,out.farPlane);
    out.cullingMask=c.cullingMask;out.solidColor=c.clearFlags==1;out.background=c.background;out.shadows=c.renderShadows;out.postProcessing=c.postProcessing;return true;
}
bool Scene::GetPrimaryCamera(RenderCamera& out,float aspect){
    for(auto e:m_Registry.AllEntities())if(auto* c=m_Registry.TryGet<CameraComponent>(e);c&&c->primary&&c->targetTexture.empty()&&GetCamera(e,out,aspect))return true;
    return false;
}
void Scene::RenderCameraTargets(RHI::Device& device,RHI::CommandList& cmd,float time){
    std::unordered_set<u64> live;std::unordered_set<std::string> livePaths;
    m_Registry.Each<CameraComponent>([&](EntityID e,CameraComponent& c){
        if(c.targetTexture.empty()||!IsActiveInHierarchy(e))return;
        auto uuid=m_Registry.Get<IDComponent>(e).uuid;live.insert(uuid);livePaths.insert(c.targetTexture);auto& renderer=m_CameraTargets[uuid];
        if(!renderer)renderer=MakeScope<SceneRenderer>(device);
        auto width=std::clamp(c.targetWidth,16,8192),height=std::clamp(c.targetHeight,16,8192);
        if(c.targetTexture.ends_with(".rendertexture")){
            // Cached by modification time: reading it from disk every frame costs more than it should
            static std::unordered_map<std::string,std::pair<std::filesystem::file_time_type,glm::ivec2>> sizes;
            auto file=AssetManager::ToAbsolute(c.targetTexture);std::error_code ec;auto stamp=std::filesystem::last_write_time(file,ec);auto& cached=sizes[c.targetTexture];
            if(cached.second.x==0||cached.first!=stamp){cached.first=stamp;cached.second={width,height};auto settings=nlohmann::json::parse(FileSystem::ReadText(file),nullptr,false);if(settings.is_object())cached.second={std::clamp(settings.value("width",width),16,8192),std::clamp(settings.value("height",height),16,8192)};}
            width=cached.second.x;height=cached.second.y;}

        renderer->Resize(width,height);RenderCamera camera;if(!GetCamera(e,camera,(float)width/height))return;
        renderer->BeginScene(camera,GatherEnvironment(),time);SubmitToRenderer(*renderer,device);CanvasRenderer::Submit(*this,*renderer,device,{width,height},false,false,false);renderer->Render(cmd);
        auto old=m_CameraTexturePaths.find(uuid);if(old!=m_CameraTexturePaths.end()&&old->second!=c.targetTexture)AssetManager::SetRuntimeTexture(old->second,nullptr);m_CameraTexturePaths[uuid]=c.targetTexture;
        AssetManager::SetRuntimeTexture(c.targetTexture,renderer->OutputRef());
    });
    for(auto& [uuid,path]:m_CameraTexturePaths)if(!live.contains(uuid)&&!livePaths.contains(path))AssetManager::SetRuntimeTexture(path,nullptr);
    std::erase_if(m_CameraTexturePaths,[&](auto& item){return !live.contains(item.first);});
    std::erase_if(m_CameraTargets,[&](auto& item){return !live.contains(item.first);});
}

static Ref<TerrainData> AcquireTerrain(Scene& scene,EntityID e,TerrainComponent& terrain,RHI::Device& device) {
    auto settings=terrain.settings;auto* generator=scene.Reg().TryGetEnabled<ProceduralTerrainComponent>(e);
    TerrainData::ResolveLayers(settings);
    if(generator){settings.mountainHeight=generator->mountainHeight;settings.hillHeight=generator->hillHeight;settings.meadowRadius=generator->meadowRadius;settings.seed=generator->seed;settings.proceduralAppearance=generator->proceduralSurface;}
    else settings.proceduralAppearance=false;
    // Grass and scattered trees exist only through the Terrain Vegetation component
    if(auto* vegetation=scene.Reg().TryGetEnabled<TerrainVegetationComponent>(e)){
        settings.grass=vegetation->grass;settings.grassDensity=vegetation->grassDensity;settings.grassHeight=vegetation->grassHeight;settings.grassDistance=vegetation->grassDistance;
        settings.flowerAmount=vegetation->flowerAmount;settings.grassColor=vegetation->grassColor;settings.dryGrassColor=vegetation->dryGrassColor;
        settings.treeDensity=vegetation->trees?vegetation->treeDensity:0;settings.maxTrees=vegetation->maxTrees;settings.treeSeed=vegetation->treeSeed;
    }else{settings.grass=false;settings.treeDensity=0;}
    return TerrainData::Acquire(device,scene.Reg().Get<IDComponent>(e).uuid,settings,terrain.dataPath,generator&&generator->generateOnMissing,generator?generator->layers:std::array<u32,4>{0,1,2,3});
}

void Scene::PrepareTerrains(RHI::Device& device) {
    m_Registry.Each<TerrainComponent>([&](EntityID e, TerrainComponent& t) {
        AcquireTerrain(*this,e,t,device);
    });
}

void Scene::RecalculateLODBounds(EntityID group) {
    auto* g = m_Registry.TryGet<LODGroupComponent>(group);
    if (!g) return;
    AABB world{ glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX) };
    std::function<void(EntityID)> add = [&](EntityID e) {
        if (auto* mr = m_Registry.TryGet<MeshRendererComponent>(e))
            if (auto mesh = AssetManager::GetMesh(mr->mesh)) {
                const AABB& b = mesh->Bounds(); const glm::mat4 m = GetWorldMatrix(e);
                for (int c = 0; c < 8; ++c) {
                    glm::vec3 p(c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z);
                    p = glm::vec3(m * glm::vec4(p, 1)); world.min = glm::min(world.min, p); world.max = glm::max(world.max, p);
                }
            }
        for (EntityID child : GetChildren(e)) add(child);
    };
    for (auto& level : g->levels) for (u64 id : level.renderers) if (EntityID e = FindByUUID(id)) add(e);
    const glm::mat4 groupWorld = GetWorldMatrix(group);
    const float scale = std::max({ glm::length(glm::vec3(groupWorld[0])), glm::length(glm::vec3(groupWorld[1])), glm::length(glm::vec3(groupWorld[2])), 1e-4f });
    if (world.min.x > world.max.x) { g->size = 1; g->localCenter = glm::vec3(0); return; }
    g->size = glm::compMax(world.max - world.min) / scale;
    g->localCenter = glm::vec3(glm::inverse(groupWorld) * glm::vec4(world.Center(), 1));
}

float Scene::LODScreenHeight(EntityID group, const RenderCamera& camera) {
    auto* g = m_Registry.TryGet<LODGroupComponent>(group);
    if (!g) return 0;
    if (g->size <= 0) RecalculateLODBounds(group);
    const glm::mat4 world = GetWorldMatrix(group);
    const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
    const float size = g->size * scale;
    float height;
    if (camera.orthographic) height = size / std::max(1e-4f, 2 * camera.orthographicSize);
    else {
        const float distance = std::max(1e-3f, glm::distance(camera.position, glm::vec3(world * glm::vec4(g->localCenter, 1))));
        height = size / (2 * distance * std::tan(camera.fovY * 0.5f));
    }
    return height * GraphicsQuality::Current().lodBias;
}

int Scene::SelectLOD(EntityID group, const RenderCamera& camera) {
    auto* g = m_Registry.TryGet<LODGroupComponent>(group);
    if (!g || g->levels.empty()) return 0;
    const float height = LODScreenHeight(group, camera);
    for (int i = 0; i < (int)g->levels.size(); ++i) if (height >= g->levels[i].screenHeight) return i;
    return -1;
}

void Scene::SubmitToRenderer(SceneRenderer& renderer, RHI::Device& device,bool animationPreview) {
    ComputeWorldMatrices();
    renderer.SyncQuality();
    // LOD Groups: the levels not chosen for this camera are left out, children included
    std::unordered_set<EntityID> lodHidden;
    m_Registry.Each<LODGroupComponent>([&](EntityID e, LODGroupComponent& g) {
        if (!m_Registry.IsEnabled<LODGroupComponent>(e) || !IsActiveInHierarchy(e) || g.levels.empty()) return;
        const int level = SelectLOD(e, renderer.Camera());
        g.currentLevel = level;
        std::function<void(EntityID, bool)> mark = [&](EntityID r, bool hide) { if (hide) lodHidden.insert(r); else lodHidden.erase(r); for (EntityID child : GetChildren(r)) mark(child, hide); };
        for (int i = 0; i < (int)g.levels.size(); ++i) if (i != level) for (u64 id : g.levels[i].renderers) if (EntityID r = FindByUUID(id)) mark(r, true);
        if (level >= 0) for (u64 id : g.levels[level].renderers) if (EntityID r = FindByUUID(id)) mark(r, false); // also listed by another level
    });
    m_Registry.Each<TerrainComponent>([&](EntityID e, TerrainComponent& t) {
        if (!IsActiveInHierarchy(e)||!(renderer.Camera().cullingMask&(1u<<std::clamp(m_Registry.Get<NameComponent>(e).layer,0,31)))) return;
        u64 uuid = m_Registry.Get<IDComponent>(e).uuid;
        renderer.SetTerrain(AcquireTerrain(*this,e,t,device), glm::vec3(m_WorldCache[e][3]), e);
    });
    m_Registry.Each<MeshRendererComponent>([&](EntityID e, MeshRendererComponent& mr) {
        if (lodHidden.count(e)||!IsActiveInHierarchy(e)||!(renderer.Camera().cullingMask&(1u<<std::clamp(m_Registry.Get<NameComponent>(e).layer,0,31)))) return;
        const Material* mat = &mr.material;
        Ref<Material> loadedMaterial;
        if (!mr.materialPath.empty())
            if ((loadedMaterial = AssetManager::GetMaterial(mr.materialPath))) mat = loadedMaterial.get();
        Material tintedCopy;
        Ref<RHI::Texture> video;
        if(auto* player=m_Registry.TryGetEnabled<VideoPlayerComponent>(e);player&&player->renderMode==1)video=VideoPlayerSystem::Output(*this,e);
        auto tinted=[&](const Material& source)->const Material&{if(mr.tint==glm::vec4(1)&&!video)return source;tintedCopy=source;tintedCopy.color*=mr.tint;if(video)tintedCopy.runtimeAlbedo=video;return tintedCopy;};
        auto* animator = m_Registry.TryGet<AnimatorComponent>(e);
        auto mesh = m_Registry.Has<FbxRigComponent>(e)?AssetManager::GetPosedFbx(mr.mesh,m_Registry.Get<IDComponent>(e).uuid^((u64)(uintptr_t)this*0x9E3779B97F4A7C15ull),ModelSkeleton::Pose(*this,e)):animator && (m_Running||animationPreview) ? AssetManager::GetAnimatedFbx(mr.mesh,animator->time,animator->state) : AssetManager::GetMesh(mr.mesh);
        if(mesh&&!mesh->SubMeshes().empty()){for(auto& part:mesh->SubMeshes()){auto material=part.materialSlot<mr.materials.size()?AssetManager::GetMaterial(mr.materials[part.materialSlot]):nullptr;renderer.Submit(mesh,m_WorldCache[e],tinted(material?*material:*mat),e,mr.castShadows,part.firstIndex,part.indexCount,mr.receiveShadows);}}
        else {auto material=!mr.materials.empty()?AssetManager::GetMaterial(mr.materials[0]):nullptr;renderer.Submit(mesh,m_WorldCache[e],tinted(material?*material:*mat),e,mr.castShadows,0,0,mr.receiveShadows);}
    });
    std::vector<EntityID> sprites;
    m_Registry.Each<SpriteRendererComponent>([&](EntityID e,SpriteRendererComponent&){sprites.push_back(e);});
    std::stable_sort(sprites.begin(),sprites.end(),[&](auto a,auto b){return m_Registry.Get<SpriteRendererComponent>(a).sortingOrder<m_Registry.Get<SpriteRendererComponent>(b).sortingOrder;});
    for(auto e:sprites){
        auto& c=m_Registry.Get<SpriteRendererComponent>(e);
        if(!IsActiveInHierarchy(e)||!(renderer.Camera().cullingMask&(1u<<std::clamp(m_Registry.Get<NameComponent>(e).layer,0,31))))continue;
        auto sprite=SpriteAsset::Load(c.sprite);auto texture=AssetManager::GetTexture(sprite.texture,true);if(!texture)continue;
        float tw=texture->Desc().width,th=texture->Desc().height;auto r=sprite.rect;if(r.z<=0||r.w<=0)r={0,0,tw,th};
        float ppu=std::max(.01f,c.pixelsPerUnit>0?c.pixelsPerUnit:sprite.pixelsPerUnit);glm::vec2 size=glm::vec2(r.z,r.w)/ppu;
        auto stamp=c.sprite+std::to_string(ppu)+std::to_string(c.flipX)+std::to_string(c.flipY)+std::to_string(r.x)+std::to_string(r.y)+std::to_string(r.z)+std::to_string(r.w)+std::to_string(sprite.pivot.x)+std::to_string(sprite.pivot.y);
        auto& cache=m_SpriteMeshes[m_Registry.Get<IDComponent>(e).uuid];
        if(cache.first!=stamp||!cache.second){
            float u0=r.x/tw,u1=(r.x+r.z)/tw,v0=r.y/th,v1=(r.y+r.w)/th;
            if(c.flipX)std::swap(u0,u1);if(c.flipY)std::swap(v0,v1);
            glm::vec2 lo=-sprite.pivot*size,hi=lo+size;
            std::vector<Vertex> vertices={{{lo.x,lo.y,0},{0,0,1},{u0,v1}},{{hi.x,lo.y,0},{0,0,1},{u1,v1}},{{hi.x,hi.y,0},{0,0,1},{u1,v0}},{{lo.x,hi.y,0},{0,0,1},{u0,v0}}};
            cache={stamp,MakeRef<Mesh>(device,vertices,std::vector<u32>{0,1,2,2,3,0},"Sprite")};
        }
        Material material;if(auto custom=AssetManager::GetMaterial(c.material))material=*custom;else material.shader=ShaderType::UI;
        material.runtimeAlbedo=texture;material.color*=c.color;material.doubleSided=true;
        renderer.Submit(cache.second,m_WorldCache[e],material,e,false,0,0,true,c.sortingOrder);
    }
    m_Registry.Each<PointLightComponent>([&](EntityID e, PointLightComponent& l) {
        if (!IsActiveInHierarchy(e)||!(renderer.Camera().cullingMask&(1u<<std::clamp(m_Registry.Get<NameComponent>(e).layer,0,31)))) return;
        renderer.SubmitLight({ glm::vec3(m_WorldCache[e][3]), l.range, l.color * l.intensity, 0.0f, glm::vec3(0, -1, 0), 0.0f, 1.0f, {} });
    });
    m_Registry.Each<SpotLightComponent>([&](EntityID e, SpotLightComponent& l) {
        if (!IsActiveInHierarchy(e)||!(renderer.Camera().cullingMask&(1u<<std::clamp(m_Registry.Get<NameComponent>(e).layer,0,31)))) return;
        glm::vec3 dir = glm::normalize(-glm::vec3(m_WorldCache[e][2]));
        float outer = glm::radians(std::max(l.outerAngle, 0.5f)), inner = glm::radians(std::min(l.innerAngle, l.outerAngle));
        renderer.SubmitLight({ glm::vec3(m_WorldCache[e][3]), l.range, l.color * l.intensity, 1.0f, dir, std::cos(outer), std::cos(inner), {} });
    });
}

Ref<Scene> Scene::Copy(const Ref<Scene>& other) {
    auto s = MakeRef<Scene>();
    s->m_Registry = other->m_Registry;
    s->m_Name = other->m_Name;
    s->m_RootOrder = other->m_RootOrder;
    s->m_UUIDMap = other->m_UUIDMap;
    return s;
}

void Scene::PopulateDefault() {
    EntityID sun = CreateEntity("Directional Light");
    m_Registry.Add<DirectionalLightComponent>(sun);
    m_Registry.Get<TransformComponent>(sun).SetEuler({ -38.0f, 150.0f, 0.0f });

    EntityID env = CreateEntity("Environment");
    m_Registry.Add<EnvironmentComponent>(env);

    EntityID cam = CreateEntity("Main Camera");
    m_Registry.Add<CameraComponent>(cam);
    m_Registry.Add<ScriptComponent>(cam, "FlyCamera");
    auto& ct = m_Registry.Get<TransformComponent>(cam);
    ct.position = { 0.0f, 2.0f, 8.0f };
    ct.SetEuler({ 4.0f, 0.0f, 0.0f });

    EntityID terrain = CreateEntity("Terrain");
    m_Registry.Add<TerrainComponent>(terrain);

    EntityID props = CreateEntity("Props");
    m_Registry.Get<TransformComponent>(props).position = { 0.0f, 0.0f, -6.0f };

    EntityID cube = CreatePrimitive(PrimitiveType::Cube);
    m_Registry.Get<TransformComponent>(cube).position = { -2.0f, 2.5f, 0.0f };
    m_Registry.Add<BoxColliderComponent>(cube);
    m_Registry.Add<RigidbodyComponent>(cube);
    m_Registry.Get<MeshRendererComponent>(cube).material.color = { 0.8f, 0.3f, 0.2f, 1.0f };
    SetParent(cube, props);

    EntityID sphere = CreatePrimitive(PrimitiveType::Sphere);
    m_Registry.Get<TransformComponent>(sphere).position = { 0.0f, 0.5f, 0.0f };
    auto& sm = m_Registry.Get<MeshRendererComponent>(sphere).material;
    sm.color = { 0.95f, 0.75f, 0.35f, 1.0f };
    sm.metallic = 1.0f;
    sm.roughness = 0.25f;
    m_Registry.Get<TransformComponent>(sphere).position = { 0.0f, 4.0f, 0.0f };
    m_Registry.Add<SphereColliderComponent>(sphere).material.restitution = 0.6f;
    m_Registry.Add<RigidbodyComponent>(sphere);
    m_Registry.Add<ScriptComponent>(sphere, "ColorPulse");
    SetParent(sphere, props);

    EntityID cyl = CreatePrimitive(PrimitiveType::Cylinder);
    auto& cyt = m_Registry.Get<TransformComponent>(cyl);
    cyt.position = { 2.0f, 1.0f, 0.0f };
    cyt.scale = { 1.0f, 2.0f, 1.0f };
    m_Registry.Get<MeshRendererComponent>(cyl).material.color = { 0.2f, 0.45f, 0.85f, 1.0f };
    m_Registry.Add<MeshColliderComponent>(cyl);  // convex hull of the cylinder mesh (static)
    m_Registry.Add<ScriptComponent>(cyl, "Rotator");
    m_Registry.Get<MeshRendererComponent>(cyl).material.shader = ShaderType::Toon;

    // A stack of crates to knock over
    EntityID crates = CreateEntity("Crates");
    m_Registry.Get<TransformComponent>(crates).position = { 4.0f, 0.0f, -4.0f };
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4 - y; ++x) {
            EntityID c = CreatePrimitive(PrimitiveType::Cube, "Crate");
            auto& t = m_Registry.Get<TransformComponent>(c);
            t.position = { 4.0f + (x - (3 - y) * 0.5f) * 1.02f, 0.6f + y * 1.02f, -4.0f };
            auto& m = m_Registry.Get<MeshRendererComponent>(c).material;
            m.color = { 0.62f, 0.45f, 0.28f, 1.0f };
            m.roughness = 0.8f;
            m_Registry.Add<BoxColliderComponent>(c);
            m_Registry.Add<RigidbodyComponent>(c).mass = 2.0f;
            SetParent(c, crates);
        }
    SetParent(cyl, props);
}

} // namespace Stardust
