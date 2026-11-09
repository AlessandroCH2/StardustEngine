#pragma once
#include "RHI/RHI.h"
#include "Renderer/Camera.h"
#include "Renderer/Mesh.h"
#include "Renderer/PostProcess.h"
#include "Renderer/Material.h"
#include "Renderer/Nature/TerrainData.h"
#include <array>
#include <unordered_map>

namespace Stardust {

class SkyRenderer;
class TerrainRenderer;
class GrassRenderer;
class TreeRenderer;

struct EnvironmentSettings {
    int skyMode=0;std::string skyMaterial;std::array<std::string,6> skyFaces;glm::vec4 skyColor{.15f,.25f,.4f,1};float skyRotation=0,skyExposure=0;
    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.4f, 0.55f, 0.35f)); // towards the sun
    glm::vec3 sunColor{ 1.0f, 0.98f, 0.95f };
    float sunIntensity = 10.0f;
    float shadowStrength=.85f,minimumAmbient=.10f;
    float exposure = 1.0f;
    float ambientIntensity = 1.0f;
    glm::vec3 ambientSky{.12f,.16f,.23f},ambientHorizon{.09f,.11f,.15f},ambientGround{.055f,.05f,.045f};
    bool castShadows = true;
    float shadowDistance = 120.0f;
    // atmosphere & weather
    float cloudCoverage = 0.4f;
    float cloudDensity = 1.0f;
    float fogDensity = 0.00022f;      // per metre at height 0
    float fogHeightFalloff = 0.0012f; // per metre
    glm::vec2 windDirection{ 1.0f, 0.3f };
    float windStrength = 0.5f;
    // post
    float bloomIntensity = 0.04f;
    float contrast = 1.06f;
    float saturation = 1.08f;
    float vignette = 0.35f;
    bool fxaa = true;
};

// Per-draw GPU data (Assets/Shaders/MeshCommon.hlsli :: DrawData)
struct DrawDataGPU {
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 params;       // roughness, metallic, normal strength, alpha cutoff
    glm::vec4 emissive;     // rgb, w = toon steps
    glm::vec4 tilingOffset;
    glm::uvec4 ids;         // albedo SRV, normal SRV, entity id, rim strength bits
};
static_assert(sizeof(DrawDataGPU) == 144);

// GPU layout of the per-frame constants (Assets/Shaders/Common.hlsli :: FrameData)
struct FrameConstants {
    glm::mat4 viewProj, view, proj, invViewProj;
    glm::vec3 cameraPos;   float time;
    glm::vec3 sunDir;      float sunIntensity;
    glm::vec3 sunColor;    float exposure;
    glm::vec3 skyZenith;   float ambientIntensity; // skyZenith/horizon/ground: legacy, unused by shaders
    glm::vec3 skyHorizon;  float pad0;
    glm::vec3 groundColor; float pad1;
    glm::vec4 viewport;
    glm::mat4 cascadeViewProj[4];
    glm::vec4 cascadeSplits;     // view-space far distance of each cascade
    glm::vec4 cascadeTexelSize;  // world-space size of one shadow texel per cascade
    glm::uvec4 shadowInfo;       // x = shadow map SRV, y = cascade count, z = enabled, w = resolution
    glm::uvec4 atmosphereInfo;   // x = transmittance LUT, y = sky-view LUT, z = ambient buffer, w = clouds
    glm::vec4 fogParams;         // x = density, y = height falloff, z = cloud coverage, w = cloud density
    glm::vec4 windParams;        // xy = direction, z = strength
    glm::uvec4 lightInfo;        // x = local light buffer, y = count
};
static_assert(sizeof(FrameConstants) == 736, "FrameConstants must match Common.hlsli");

// Point / spot light (64 bytes on the GPU, see PBR.hlsli :: EvaluateLocalLights)
struct LocalLight {
    glm::vec3 position; float range;
    glm::vec3 color;    float isSpot;      // color already multiplied by intensity
    glm::vec3 direction; float cosOuter;
    float cosInner; float pad[3];
};
static_assert(sizeof(LocalLight) == 64);
static constexpr u32 kMaxLocalLights = 256;

static constexpr u32 kShadowCascades = 4;
static constexpr u32 kShadowMapSize = 2048;

struct RenderStats {
    u32 drawCalls = 0;
    u64 triangles = 0;
    u32 terrainPatches = 0;
    u32 visibleTrees = 0;
};

// Renders a scene view (editor scene view, game view, or the player) into its own set of targets.
class SceneRenderer {
public:
    explicit SceneRenderer(RHI::Device& device);
    ~SceneRenderer();

    void Resize(u32 width, u32 height);
    void BeginScene(const RenderCamera& camera, const EnvironmentSettings& env, float time);
    void Submit(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material, u32 entityId, bool castShadows = true,u32 firstIndex=0,u32 indexCount=0,bool receiveShadows=true,int sortingOrder=0);
    const RenderCamera& Camera()const{return m_Camera;}
    void SubmitUI(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material, u32 entityId, bool overlay);
    size_t TerrainCount()const{return m_ActiveTerrains.size();}
    size_t SubmittedDrawCount()const{return m_Draws.size();}
    void SetTerrain(const Ref<TerrainData>& terrain, const glm::vec3& origin, u32 entityId);
    void SubmitLight(const LocalLight& light) { if (m_Lights.size() < kMaxLocalLights) m_Lights.push_back(light); }
    void Render(RHI::CommandList& cmd);

    Ref<RHI::Texture> OutputRef() const { return m_Output; }
    RHI::Texture* Output() const { return m_Output.get(); }        // tonemapped LDR, ShaderRead state
    RHI::Texture* EntityIdTarget() const { return m_EntityId.get(); }
    u32 Width() const { return m_Width; }
    u32 Height() const { return m_Height; }
    const RenderStats& Stats() const { return m_Stats; }

    bool grid2D=false;
    bool drawGrid = false; // editor ground grid, drawn over the tonemapped image with depth test
    u32 selectedEntity = 0; // editor: draws a Unity-style orange outline around this entity id

    // Editor picking: read back the entity id under a pixel. The result arrives a couple of frames later.
    void RequestPick(u32 x, u32 y) { m_PickRequested = true; m_PickX = x; m_PickY = y; }
    bool PollPick(u32& entityId) { if (!m_HasPickResult) return false; entityId = m_PickResult; m_HasPickResult = false; return true; }

private:
    struct DrawItem {
        Ref<Mesh> mesh;
        ShaderType shader;
        bool doubleSided;
        bool castShadows;
        float sortDepth;
        glm::vec3 boundsCenter;
        float boundsRadius;
        Ref<RHI::Pipeline> customPipeline;
        bool uiOverlay = false;
        u32 firstIndex=0,indexCount=0;int sortingOrder=0;u32 propertyOffset=0,propertyCount=0;
    };
    void DrawMeshes(RHI::CommandList& cmd, RHI::Buffer* frameBuf, bool transparent);
    RHI::Pipeline* MeshPipeline(ShaderType s, bool doubleSided) const { return m_MeshPipelines[(int)s][doubleSided ? 1 : 0].get(); }

    void ComputeCascades(FrameConstants& fc, float aspect);
    void RenderShadows(RHI::CommandList& cmd, RHI::Buffer* frameBuf);

    RHI::Device& m_Device;
    std::unordered_map<std::string, Ref<RHI::Pipeline>> m_CustomPipelines;
    u32 m_Width = 0, m_Height = 0;
    Ref<RHI::Texture> m_HDR, m_Depth, m_EntityId, m_Output,m_OpaqueColor,m_OpaqueDepth;
    Ref<RHI::Pipeline> m_MeshPipelines[(int)ShaderType::Count][2];
    Ref<RHI::Pipeline> m_SkyPipeline, m_ShadowPipeline, m_GridPipeline,m_Grid2DPipeline;
    Ref<RHI::Pipeline> m_UIOverlayPipeline;
    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_DrawBuffers;
    std::vector<DrawDataGPU> m_DrawData;
    std::vector<glm::vec4> m_MaterialValues;
    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_MaterialBuffers;
    std::vector<LocalLight> m_Lights;
    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_LightBuffers;
    Ref<RHI::Pipeline> m_OutlinePipeline;
    Ref<RHI::Texture> m_ShadowMap;
    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_FrameBuffers;
    std::array<glm::vec4, kShadowCascades> m_CascadeSpheres{}; // xyz center, w radius (for caster culling)

    Scope<SkyRenderer> m_Sky;
    struct TerrainBatch;
    std::unordered_map<u32,Scope<TerrainBatch>> m_TerrainBatches;
    std::vector<TerrainBatch*> m_ActiveTerrains;
    Scope<PostProcess> m_Post;

    RenderCamera m_Camera;
    EnvironmentSettings m_Env;
    float m_Time = 0;
    std::vector<DrawItem> m_Draws;
    std::unordered_map<std::string,u32> m_TextureSamplers;

    RenderStats m_Stats;

    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_PickBuffers;
    std::array<bool, RHI::kFramesInFlight> m_PickPending{};
    bool m_PickRequested = false, m_HasPickResult = false;
    u32 m_PickX = 0, m_PickY = 0, m_PickResult = 0;
};

} // namespace Stardust
