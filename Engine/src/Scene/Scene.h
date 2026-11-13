#pragma once
#include "Scene/Registry.h"
#include "Scene/Components.h"
#include "Renderer/SceneRenderer.h"
#include <unordered_map>
#include <unordered_set>

namespace Stardust {

class PhysicsWorld;

class Scene {
public:
    Scene();
    ~Scene();

    EntityID CreateEntity(const std::string& name = "GameObject", u64 uuid = 0);
    EntityID CreatePrimitive(PrimitiveType type, const std::string& name = {});
    void DestroyEntity(EntityID e);            // also destroys children
    EntityID DuplicateEntity(EntityID e);      // deep copy incl. children, placed next to the original
    EntityID Instantiate(EntityID source, const glm::vec3* position = nullptr); // runtime-aware copy (bodies, scripts, audio)
    EntityID InstantiatePrefab(const std::string& prefabAsset, const glm::vec3* position = nullptr);
    EntityID FindWithTag(const std::string& tag);
    void SetActive(EntityID e, bool active);
    void RefreshComponentState(EntityID e,int type);
    void StartRuntimeObjects(EntityID root);  // creates physics bodies / script instances / audio for a new subtree
    EntityID FindByName(const std::string& name);
    EntityID FindByUUID(u64 uuid);

    // Hierarchy
    void SetParent(EntityID child, EntityID parent, bool keepWorldTransform = true, int index = -1);
    EntityID GetParent(EntityID e);
    const std::vector<EntityID>& GetChildren(EntityID e);
    std::vector<EntityID> GetRoots();          // root entities in hierarchy order
    bool IsDescendantOf(EntityID e, EntityID ancestor);
    void MoveInParent(EntityID e, int newIndex);
    glm::mat4 GetWorldMatrix(EntityID e);
    void SetWorldMatrix(EntityID e, const glm::mat4& world);
    bool IsActiveInHierarchy(EntityID e);

    Registry& Reg() { return m_Registry; }
    const std::string& Name() const { return m_Name; }
    void SetName(const std::string& n) { m_Name = n; }

    // Runtime
    void DontDestroyOnLoad(EntityID e);
    void ReplaceContents(Scene& next,RHI::Device& device);
    void OnRuntimeStart();
    void OnRuntimeStop();
    void OnUpdate(float dt);
    bool IsRunning() const { return m_Running; }
    PhysicsWorld* Physics() { return m_Physics.get(); }
    void QueueDestroy(EntityID e) { m_PendingDestroy.push_back(e); } // destroyed after the current update
    float timeScale = 1.0f;

    // Rendering
    EnvironmentSettings GatherEnvironment();
    bool GetPrimaryCamera(RenderCamera& out, float aspect);
    bool GetCamera(EntityID entity,RenderCamera& out,float aspect);
    void RenderCameraTargets(RHI::Device& device,RHI::CommandList& cmd,float time);
    void SubmitToRenderer(SceneRenderer& renderer, RHI::Device& device,bool animationPreview=false);
    // LOD Groups: the share of the view's height the group covers (quality LOD bias included), the level that
    // gives (-1 = culled), and the bounds measured from its renderers
    float LODScreenHeight(EntityID group, const RenderCamera& camera);
    int SelectLOD(EntityID group, const RenderCamera& camera);
    void RecalculateLODBounds(EntityID group);
    void PrepareTerrains(RHI::Device& device); // make sure terrain data exists (physics heightfields need it before play)

    // Deep copy (entering play mode works on a copy)
    static Ref<Scene> Copy(const Ref<Scene>& other);

    // Default content of a new scene: sun, environment, camera, ground, a few props
    void PopulateDefault();

private:
    void CopyComponents(EntityID src, EntityID dst);
    void ComputeWorldMatrices();

    Registry m_Registry;
    std::string m_Name = "Untitled";
    bool m_Running = false;
    std::vector<EntityID> m_RootOrder;          // ordering of root entities
    std::vector<glm::mat4> m_WorldCache;        // per-frame world matrices, indexed by EntityID
    std::unordered_map<u64, EntityID> m_UUIDMap;
    Scope<PhysicsWorld> m_Physics;
    std::unordered_map<u64,Scope<SceneRenderer>> m_CameraTargets;
    std::unordered_map<u64,std::string> m_CameraTexturePaths;
    std::unordered_map<u64,std::pair<std::string,Ref<Mesh>>> m_SpriteMeshes;
    struct SpriteBatchPart { Ref<RHI::Texture> texture; Ref<Mesh> mesh; glm::vec4 color{1}; std::string path; };
    struct SpriteBatchChunk { u64 version = 0; std::vector<SpriteBatchPart> parts; };
    struct SpriteBatchCache {
        u64 version = 0;
        std::vector<Ref<RHI::Texture>> textures; std::vector<bool> point;
        std::vector<SpriteBatchChunk> chunks;
        std::vector<SpriteBatchPart> animated; std::vector<u32> animatedFrames; bool animatedChecked = false;
    };
    std::unordered_map<u64,SpriteBatchCache> m_SpriteBatchMeshes;
    // Particle quads: CPU-written vertex buffers, reused once the GPU is done with the frame that drew them
    struct ParticleMeshSlot { Ref<Mesh> mesh; Ref<RHI::Buffer> vertices; u32 capacity = 0; u64 frame = 0; };
    std::unordered_map<u64,std::vector<ParticleMeshSlot>> m_ParticleMeshes;
    std::vector<Vertex> m_ParticleScratch;
    std::vector<EntityID> m_PendingDestroy;
    float m_FixedAccumulator = 0.0f;
    std::unordered_set<u64> m_PersistentRoots;
};

u64 GenerateUUID();

} // namespace Stardust
