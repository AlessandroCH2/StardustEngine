#include "Renderer/SceneRenderer.h"
#include "Renderer/Quality.h"
#include "Assets/TextureImport.h"
#include "Renderer/Nature/SkyRenderer.h"
#include "Renderer/Nature/TerrainRenderer.h"
#include "Renderer/Nature/GrassRenderer.h"
#include "Renderer/Nature/TreeRenderer.h"
#include "Core/Log.h"
#include "Assets/AssetManager.h"
#include "Assets/Pak.h"
#include "Core/FileSystem.h"
#include <bit>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

namespace Stardust {

using namespace RHI;

static constexpr Format kHDRFormat = Format::RGBA16F;
static constexpr Format kDepthFormat = Format::D32F;
static constexpr Format kIdFormat = Format::R32U;
static constexpr Format kOutputFormat = Format::RGBA8_UNORM;

static constexpr u32 kMaxDraws = 16384;

struct MeshPush {
    u32 frameBuffer, drawBuffer, drawIndex, vertexBuffer, cascade;
    u32 materialBuffer=0,materialOffset=0,materialCount=0;
};

struct SceneRenderer::TerrainBatch {Ref<TerrainData> data;glm::vec3 origin{0};u32 entity=0;Scope<TerrainRenderer> terrain;Scope<GrassRenderer> grass;Scope<TreeRenderer> trees;TerrainBatch(RHI::Device& device){terrain=MakeScope<TerrainRenderer>(device,kHDRFormat,kIdFormat,kDepthFormat);grass=MakeScope<GrassRenderer>(device,kHDRFormat,kIdFormat,kDepthFormat);trees=MakeScope<TreeRenderer>(device,kHDRFormat,kIdFormat,kDepthFormat);}};
SceneRenderer::SceneRenderer(Device& device) : m_Device(device) {
    // One pipeline per material shader, single- and double-sided
    const char* shaderPS[(int)ShaderType::Count] = { "Mesh.ps", "MeshUnlit.ps", "MeshToon.ps", "MeshTransparent.ps", "MeshUI.ps", "Water.ps" };
    for (int s = 0; s < (int)ShaderType::Count; ++s)
        for (int ds = 0; ds < 2; ++ds) {
            GraphicsPipelineDesc mesh;
            mesh.name = std::string(ShaderName((ShaderType)s)) + (ds ? " (double sided)" : "");
            mesh.vs = (ShaderType)s==ShaderType::Water?"Water.vs":(ShaderType)s==ShaderType::UI?"UIWorld.vs":"Mesh.vs"; mesh.ps = shaderPS[s];
            mesh.colorFormats = { kHDRFormat, kIdFormat };
            mesh.depthFormat = kDepthFormat;
            mesh.cull = ds ? CullMode::None : CullMode::Back;
            if (((ShaderType)s == ShaderType::Transparent || (ShaderType)s == ShaderType::UI || (ShaderType)s==ShaderType::Water)) { mesh.blend = BlendMode::Alpha; mesh.depthWrite = false; }
            if((ShaderType)s==ShaderType::Water)mesh.blend=BlendMode::None;
            m_MeshPipelines[s][ds] = device.CreateGraphicsPipeline(mesh);
        }

    GraphicsPipelineDesc ui;
    ui.name="UI Overlay"; ui.vs="UI.vs"; ui.ps="UI.ps"; ui.colorFormats={kOutputFormat};
    ui.depthTest=false; ui.depthWrite=false; ui.cull=CullMode::None; ui.blend=BlendMode::Alpha;
    m_UIOverlayPipeline=device.CreateGraphicsPipeline(ui);

    for (int additive = 0; additive < 2; ++additive) {
        GraphicsPipelineDesc particles;
        particles.name = additive ? "Particles (additive)" : "Particles";
        particles.vs = "Particle.vs"; particles.ps = "Particle.ps";
        particles.colorFormats = { kHDRFormat, kIdFormat };
        particles.depthFormat = kDepthFormat;
        particles.depthWrite = false;
        particles.cull = CullMode::None;
        particles.blend = additive ? BlendMode::Additive : BlendMode::Alpha;
        m_ParticlePipelines[additive] = device.CreateGraphicsPipeline(particles);
    }

    GraphicsPipelineDesc sky;
    sky.name = "Sky";
    sky.vs = "Fullscreen.vs"; sky.ps = "Sky.ps";
    sky.colorFormats = { kHDRFormat, kIdFormat };
    sky.depthFormat = kDepthFormat;
    sky.depthWrite = false;
    sky.cull = CullMode::None;
    m_SkyPipeline = device.CreateGraphicsPipeline(sky);

    GraphicsPipelineDesc grid;
    grid.name = "EditorGrid";
    grid.vs = "Grid.vs"; grid.ps = "Grid.ps";
    grid.colorFormats = { kOutputFormat };
    grid.depthFormat = kDepthFormat;
    grid.depthWrite = false;
    grid.cull = CullMode::None;
    grid.blend = BlendMode::Alpha;
    m_GridPipeline = device.CreateGraphicsPipeline(grid);grid.name="EditorGrid2D";grid.depthTest=false;m_Grid2DPipeline=device.CreateGraphicsPipeline(grid);

    GraphicsPipelineDesc outline;
    outline.name = "SelectionOutline";
    outline.vs = "Fullscreen.vs"; outline.ps = "Outline.ps";
    outline.colorFormats = { kOutputFormat };
    outline.cull = CullMode::None;
    outline.blend = BlendMode::Alpha;
    m_OutlinePipeline = device.CreateGraphicsPipeline(outline);

    GraphicsPipelineDesc shadow;
    shadow.name = "ShadowDepth";
    shadow.vs = "Shadow.vs";
    shadow.depthFormat = kDepthFormat;
    shadow.cull = CullMode::None;          // thin geometry (planes, leaves) must cast from both sides
    shadow.depthClamp = true;              // "pancaking": casters in front of the cascade still write depth
    shadow.depthBiasConstant = -1.5f;      // reverse-Z: negative bias pushes depth away from the light
    shadow.depthBiasSlope = -2.0f;
    m_ShadowPipeline = device.CreateGraphicsPipeline(shadow);

    CreateShadowMap(GraphicsQuality::Current().shadowMapSize);

    for (u32 i = 0; i < kFramesInFlight; ++i) {
        m_FrameBuffers[i] = device.CreateBuffer({ sizeof(FrameConstants), BufferUsage::Storage, MemoryUsage::CPUToGPU, "FrameConstants" });
        m_PickBuffers[i] = device.CreateBuffer({ 16, BufferUsage::CopyDst, MemoryUsage::GPUToCPU, "PickReadback" });
        m_MaterialBuffers[i]=device.CreateBuffer({65536*sizeof(glm::vec4),BufferUsage::Storage,MemoryUsage::CPUToGPU,"MaterialProperties"});
        m_DrawBuffers[i] = device.CreateBuffer({ kMaxDraws * sizeof(DrawDataGPU), BufferUsage::Storage, MemoryUsage::CPUToGPU, "DrawData" });
        m_LightBuffers[i] = device.CreateBuffer({ kMaxLocalLights * sizeof(LocalLight), BufferUsage::Storage, MemoryUsage::CPUToGPU, "LocalLights" });
    }

    m_Sky = MakeScope<SkyRenderer>(device);

    m_Post = MakeScope<PostProcess>(device, kOutputFormat);

    Resize(1280, 720);
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::SyncQuality() {
    if (GraphicsQuality::Current().shadowMapSize != m_ShadowSize) CreateShadowMap(GraphicsQuality::Current().shadowMapSize);
}

void SceneRenderer::CreateShadowMap(u32 size) {
    if (m_ShadowMap) m_Device.WaitIdle(); // quality changed: the old map may still be in use
    m_ShadowSize = size;
    TextureDesc sm;
    sm.name = "ShadowCascades";
    sm.width = sm.height = size;
    sm.arrayLayers = kShadowCascades;
    sm.dim = TextureDim::Tex2DArray;
    sm.format = kDepthFormat;
    sm.usage = TextureUsage::DepthStencil | TextureUsage::Sampled | TextureUsage::CopySrc;
    m_ShadowMap = m_Device.CreateTexture(sm);
}

void SceneRenderer::Resize(u32 width, u32 height) {
    width = std::max(width, 1u); height = std::max(height, 1u);
    if (width == m_Width && height == m_Height) return;
    m_Width = width; m_Height = height;

    TextureDesc d;
    d.width = width; d.height = height;
    d.name = "OpaqueColor";d.format=kHDRFormat;d.usage=TextureUsage::CopyDst|TextureUsage::Sampled;m_OpaqueColor=m_Device.CreateTexture(d);
    d.name="OpaqueDepth";d.format=kDepthFormat;m_OpaqueDepth=m_Device.CreateTexture(d);
    d.name = "SceneHDR"; d.format = kHDRFormat; d.usage = TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopySrc;
    m_HDR = m_Device.CreateTexture(d);
    d.name = "SceneDepth"; d.format = kDepthFormat; d.usage = TextureUsage::DepthStencil | TextureUsage::Sampled | TextureUsage::CopySrc;
    m_Depth = m_Device.CreateTexture(d);
    d.name = "EntityId"; d.format = kIdFormat; d.usage = TextureUsage::RenderTarget | TextureUsage::CopySrc | TextureUsage::Sampled;
    m_EntityId = m_Device.CreateTexture(d);
    d.name = "SceneOutput"; d.format = kOutputFormat; d.usage = TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopySrc;
    m_Output = m_Device.CreateTexture(d);
    m_Sky->Resize(width, height);
    m_Post->Resize(width, height);
}


void SceneRenderer::BeginScene(const RenderCamera& camera, const EnvironmentSettings& env, float time) {
    m_TextureSamplers.clear();
    m_Camera = camera;
    m_Env = env;
    m_Time = time;
    m_Draws.clear();
    m_DrawData.clear();m_MaterialValues.clear();
    m_Lights.clear();
    m_ActiveTerrains.clear();
}

void SceneRenderer::Submit(const Ref<Mesh>& sourceMesh, const glm::mat4& transform, const Material& m, u32 entityId, bool castShadows,u32 firstIndex,u32 indexCount,bool receiveShadows,int sortingOrder) {
    Ref<Mesh> mesh=m.shader==ShaderType::Water&&sourceMesh&&sourceMesh->Name()=="Plane"?MeshLibrary::WaterGrid():sourceMesh;
    if(mesh!=sourceMesh&&mesh){firstIndex=0;indexCount=mesh->IndexCount();}
    if (!mesh || m_DrawData.size() >= kMaxDraws) return;
    DrawDataGPU d;
    d.model = transform;
    d.color = m.color;
    d.params = { m.roughness, m.metallic, m.normalStrength, m.alphaCutoff };
    d.emissive = glm::vec4(m.emissive * m.emissiveIntensity, receiveShadows?std::max(1.f,m.toonSteps):-std::max(1.f,m.toonSteps));
    d.tilingOffset = { m.tiling, m.offset };
    d.ids = { m.runtimeAlbedo ? m.runtimeAlbedo->SRV() : AssetManager::TextureSRV(m.albedoMap, true, DefaultTexture::White),
              m.runtimeMask ? m.runtimeMask->SRV() : AssetManager::TextureSRV(m.normalMap, false, DefaultTexture::FlatNormal),
              entityId, std::bit_cast<u32>(m.rimStrength) };
    auto sampler=[&](const std::string& path){auto found=m_TextureSamplers.find(path);if(found!=m_TextureSamplers.end())return found->second;auto value=(u32)TextureImportSettings::Load(path).Sampler();m_TextureSamplers[path]=value;return value;};
    d.ids.w=(d.ids.w&~63u)|(sampler(m.albedoMap)&7u)|((sampler(m.normalMap)&7u)<<3);
    m_DrawData.push_back(d);

    const AABB& b = mesh->Bounds();
    glm::vec3 c = glm::vec3(transform * glm::vec4(b.Center(), 1.0f));
    float s = std::max({ glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])), glm::length(glm::vec3(transform[2])) });
    bool transparent = m.shader == ShaderType::Transparent || m.shader == ShaderType::UI || m.shader==ShaderType::Water;
    m_Draws.push_back({ mesh, m.shader, m.doubleSided, castShadows && !transparent && m.shader != ShaderType::Unlit,
                        0.0f, c, glm::length(b.Extents()) * s });
    if(!m.customPixelShader.empty()||m.shader==ShaderType::Water){auto values=m.PropertyValues();if(m_MaterialValues.size()+values.size()<=65536){m_Draws.back().propertyOffset=(u32)m_MaterialValues.size();m_Draws.back().propertyCount=(u32)values.size();m_MaterialValues.insert(m_MaterialValues.end(),values.begin(),values.end());}}
    m_Draws.back().sortingOrder=sortingOrder;m_Draws.back().firstIndex=firstIndex;m_Draws.back().indexCount=indexCount?indexCount:mesh->IndexCount();
    if (!m.customPixelShader.empty()) {
        auto path = AssetManager::ToAbsolute(m.customPixelShader);
        std::error_code ec;
        auto stamp = std::filesystem::last_write_time(path, ec);
        if (!ec||Pak::Contains(path)) {
            std::string key = FileSystem::Utf8(path) + std::to_string(m.doubleSided) + std::to_string(transparent);
            key += std::to_string(ec?Pak::Generation():stamp.time_since_epoch().count());
            auto& pipeline = m_CustomPipelines[key];
            if (!pipeline) {
                GraphicsPipelineDesc custom;
                custom.name = m.customPixelShader;
                custom.vs = "Mesh.vs"; custom.ps = "asset:" + m.customPixelShader;
                custom.colorFormats = { kHDRFormat, kIdFormat }; custom.depthFormat = kDepthFormat;
                custom.cull = m.doubleSided ? CullMode::None : CullMode::Back;
                if (transparent) { custom.blend = BlendMode::Alpha; custom.depthWrite = false; }
                pipeline = m_Device.CreateGraphicsPipeline(custom);
            }
            m_Draws.back().customPipeline = pipeline;
        }
    }
}

void SceneRenderer::SubmitUI(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material, u32 entityId, bool overlay) {
    size_t previous=m_Draws.size();
    Submit(mesh,transform,material,entityId,false);
    if(m_Draws.size()==previous) return;
    auto& item=m_Draws.back(); item.uiOverlay=overlay;
    if(overlay) {
        item.customPipeline=m_UIOverlayPipeline;
        if(!material.customPixelShader.empty()) {
            auto path=AssetManager::ToAbsolute(material.customPixelShader);
            std::error_code ec; auto stamp=std::filesystem::last_write_time(path,ec);
            if(!ec||Pak::Contains(path)) {
                auto key="ui-overlay:"+FileSystem::Utf8(path)+std::to_string(ec?Pak::Generation():stamp.time_since_epoch().count());
                auto& pipeline=m_CustomPipelines[key];
                if(!pipeline) {
                    GraphicsPipelineDesc d; d.name=key; d.vs="UI.vs"; d.ps="asset:"+material.customPixelShader;
                    d.colorFormats={kOutputFormat}; d.depthTest=false; d.depthWrite=false; d.cull=CullMode::None; d.blend=BlendMode::Alpha;
                    pipeline=m_Device.CreateGraphicsPipeline(d);
                }
                item.customPipeline=pipeline;
            }
        }
    }
}

void SceneRenderer::SubmitParticles(const Ref<Mesh>& mesh, const Material& material, u32 entityId, u32 indexCount, bool additive, int sortingOrder) {
    Material m = material;
    const bool custom = !m.customPixelShader.empty();
    if (!custom) { m.shader = ShaderType::Transparent; }
    size_t previous = m_Draws.size();
    Submit(mesh, glm::mat4(1), m, entityId, false, 0, indexCount, false, sortingOrder);
    if (m_Draws.size() == previous) return;
    if (!custom) {
        m_Draws.back().customPipeline = m_ParticlePipelines[additive ? 1 : 0];
        m_DrawData.back().params.z = additive ? 1.0f : 0.0f;
    }
}

// Draw items and their GPU data share the same index
void SceneRenderer::DrawMeshes(CommandList& cmd, Buffer* frameBuf, bool transparent) {
    std::vector<u32> order;
    for (u32 i = 0; i < (u32)m_Draws.size(); ++i)
        if (!m_Draws[i].uiOverlay && ((m_Draws[i].shader == ShaderType::Transparent || m_Draws[i].shader == ShaderType::UI || m_Draws[i].shader==ShaderType::Water) == transparent)) order.push_back(i);
    if (transparent) {
        for (u32 i : order) m_Draws[i].sortDepth = glm::length(m_Draws[i].boundsCenter - m_Camera.position);
        std::stable_sort(order.begin(), order.end(), [&](u32 a, u32 b) { bool au=m_Draws[a].shader==ShaderType::UI,bu=m_Draws[b].shader==ShaderType::UI;
            if(m_Draws[a].sortingOrder!=m_Draws[b].sortingOrder)return m_Draws[a].sortingOrder<m_Draws[b].sortingOrder;
            if(au!=bu) return !au;
            if(au) return a<b;
            return m_Draws[a].sortDepth > m_Draws[b].sortDepth; });
    } else {
        std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
            return std::tie(m_Draws[a].shader, m_Draws[a].doubleSided) < std::tie(m_Draws[b].shader, m_Draws[b].doubleSided);
        });
    }
    Pipeline* bound = nullptr;
    Buffer* drawBuf = m_DrawBuffers[m_Device.FrameIndex()].get();
    for (u32 i : order) {
        const DrawItem& d = m_Draws[i];
        Pipeline* p = d.customPipeline ? d.customPipeline.get() : MeshPipeline(d.shader, d.doubleSided);
        if (p != bound) { cmd.SetPipeline(p); bound = p; }
        MeshPush push{ frameBuf->Index(), drawBuf->Index(), i, d.mesh->VertexBuffer()->Index(), 0,m_MaterialBuffers[m_Device.FrameIndex()]->Index(),d.propertyOffset,d.propertyCount };
        cmd.Push(push);
        cmd.SetIndexBuffer(d.mesh->IndexBuffer());
        cmd.DrawIndexed(d.indexCount,1,d.firstIndex);
        m_Stats.drawCalls++;
        m_Stats.triangles += d.indexCount / 3;
    }
}

void SceneRenderer::SetTerrain(const Ref<TerrainData>& terrain, const glm::vec3& origin, u32 entityId) {
    if(!terrain)return;auto& batch=m_TerrainBatches[entityId];if(!batch)batch=MakeScope<TerrainBatch>(m_Device);batch->data=terrain;batch->origin=origin;batch->entity=entityId;m_ActiveTerrains.push_back(batch.get());
}

void SceneRenderer::Render(CommandList& cmd) {
    AssetManager::FlushSkinning(cmd);
    m_Sky->Resize(m_Width, m_Height); // picks up a changed cloud quality
    ScopedGpuTimer frameTimer(m_Device, "Scene");
    m_Stats = {};

    // This frame slot's fence has been waited on, so a pick copied kFramesInFlight frames ago is ready.
    u32 fi = m_Device.FrameIndex();
    if (m_PickPending[fi]) {
        m_PickResult = *m_PickBuffers[fi]->As<u32>();
        m_HasPickResult = true;
        m_PickPending[fi] = false;
    }

    u32 skyPropertyOffset=0,skyPropertyCount=0;
    if(m_Env.skyMode==2)if(auto sky=AssetManager::GetMaterial(m_Env.skyMaterial);sky&&!sky->customPixelShader.empty()){auto values=sky->PropertyValues();if(m_MaterialValues.size()+values.size()<=65536){skyPropertyOffset=(u32)m_MaterialValues.size();skyPropertyCount=(u32)values.size();m_MaterialValues.insert(m_MaterialValues.end(),values.begin(),values.end());}}
    // ---------------------------------------------------------------- per-frame constants
    FrameConstants fc{};
    fc.view = m_Camera.view;
    fc.proj = m_Camera.projection;
    fc.viewProj = fc.proj * fc.view;
    fc.invViewProj = glm::inverse(fc.viewProj);
    fc.cameraPos = m_Camera.position;
    fc.time = m_Time;
    fc.sunDir = glm::normalize(m_Env.sunDirection);
    fc.sunIntensity = m_Env.sunIntensity;
    fc.sunColor = m_Env.sunColor;
    fc.exposure = m_Env.exposure;
    fc.ambientIntensity = m_Env.ambientIntensity;
    fc.pad0=std::clamp(m_Env.shadowStrength,0.f,1.f);fc.pad1=std::max(0.f,m_Env.minimumAmbient);fc.skyZenith=m_Env.ambientSky;fc.skyHorizon=m_Env.ambientHorizon;fc.groundColor=m_Env.ambientGround;
    fc.viewport = { (float)m_Width, (float)m_Height, 1.0f / m_Width, 1.0f / m_Height };
    // A camera that sees few objects should not pay for the whole pipeline: no shadow pass when nothing casts one
    const bool anyCaster = !m_ActiveTerrains.empty() || std::any_of(m_Draws.begin(), m_Draws.end(), [](const DrawItem& d) { return d.castShadows; });
    bool shadows = m_Env.castShadows && m_Camera.shadows && anyCaster && fc.sunDir.y > -0.05f;
    if (shadows) ComputeCascades(fc, (float)m_Width / (float)m_Height);
    fc.shadowInfo = { m_ShadowMap->SRV(), kShadowCascades, shadows ? 1u : 0u, m_ShadowSize };
    const bool skybox = !m_Camera.solidColor;
    bool clouds = skybox && m_Env.cloudCoverage > 0.001f;
    fc.atmosphereInfo = { m_Sky->TransmittanceSRV(), m_Sky->SkyViewSRV(), m_Sky->AmbientBuffer(), clouds ? m_Sky->CloudsSRV() : 0xFFFFFFFFu };
    fc.fogParams = { m_Env.fogDensity, m_Env.fogHeightFalloff, m_Env.cloudCoverage, m_Env.cloudDensity };
    glm::vec2 wd = glm::length(m_Env.windDirection) > 0 ? glm::normalize(m_Env.windDirection) : glm::vec2(1, 0);
    fc.windParams = { wd.x, wd.y, m_Env.windStrength, 0.0f };
    Buffer* frameBuf = m_FrameBuffers[fi].get();
    std::memcpy(frameBuf->Mapped(), &fc, sizeof(fc));
    if(!m_MaterialValues.empty())std::memcpy(m_MaterialBuffers[fi]->Mapped(),m_MaterialValues.data(),m_MaterialValues.size()*sizeof(glm::vec4));
    if (!m_DrawData.empty()) std::memcpy(m_DrawBuffers[fi]->Mapped(), m_DrawData.data(), m_DrawData.size() * sizeof(DrawDataGPU));
    if (!m_Lights.empty()) std::memcpy(m_LightBuffers[fi]->Mapped(), m_Lights.data(), m_Lights.size() * sizeof(LocalLight));
    reinterpret_cast<FrameConstants*>(frameBuf->Mapped())->lightInfo = { m_LightBuffers[fi]->Index(), (u32)m_Lights.size(), m_OpaqueColor->SRV(), m_OpaqueDepth->SRV() };

    Frustum frustum = Frustum::FromViewProj(fc.viewProj);
    for(auto* batch:m_ActiveTerrains){batch->terrain->Prepare(*batch->data,batch->origin,batch->entity,m_Camera.position,frustum,m_Env.shadowDistance);batch->trees->Prepare(*batch->data,batch->origin,batch->entity,m_Camera.position,frustum,fc,shadows);m_Stats.terrainPatches+=batch->terrain->PatchCount();m_Stats.visibleTrees+=batch->trees->VisibleTrees();m_Stats.triangles+=batch->terrain->Triangles()+batch->trees->Triangles();}
    for(auto* batch:m_ActiveTerrains)batch->data->FlushGpu(cmd,fi);
    m_Sky->Update(cmd,frameBuf,clouds);
    for(auto* batch:m_ActiveTerrains)batch->grass->Generate(cmd,frameBuf,*batch->data,batch->origin,m_Camera.position);

    // ---------------------------------------------------------------- shadows
    if (shadows) RenderShadows(cmd, frameBuf);
    cmd.Barrier(m_ShadowMap.get(), ResourceState::DepthRead);

    // ---------------------------------------------------------------- opaque + sky
    {
        ScopedGpuTimer t(m_Device, "Opaque");
        RenderingDesc rd;
        // Solid Color: the background is given in sRGB, the HDR target is linear
        glm::vec4 clear(0, 0, 0, 1);
        if (!skybox) clear = glm::vec4(glm::pow(glm::max(glm::vec3(m_Camera.background), glm::vec3(0)), glm::vec3(2.2f)) / std::max(0.001f, m_Env.exposure), m_Camera.background.a);
        rd.colors.push_back({ m_HDR.get(), LoadOp::Clear, { clear.r, clear.g, clear.b, clear.a } });
        rd.colors.push_back({ m_EntityId.get(), LoadOp::Clear, { 0, 0, 0, 0 } });
        rd.depth = { m_Depth.get(), LoadOp::Clear, 0.0f };
        cmd.BeginRendering(rd);

        DrawMeshes(cmd, frameBuf, false);
        for(auto* batch:m_ActiveTerrains){batch->terrain->Draw(cmd,frameBuf);batch->trees->Draw(cmd,frameBuf);batch->grass->Draw(cmd,frameBuf,*batch->data,batch->entity);m_Stats.drawCalls+=2+TreeRenderer::kTypes*TreeRenderer::kLods*2;}

        struct SkyPush {u32 frame,mode;glm::vec2 pad{};glm::vec4 color;glm::uvec4 facesA{~0u},facesB{~0u};glm::vec4 parameters{0};glm::uvec4 materialProperties{0};};
        SkyPush skyPush;skyPush.materialProperties={m_MaterialBuffers[fi]->Index(),skyPropertyOffset,skyPropertyCount,0};skyPush.frame=frameBuf->Index();skyPush.mode=m_Env.skyMode;skyPush.color=m_Env.skyColor;skyPush.parameters={glm::radians(m_Env.skyRotation),m_Env.skyExposure,0,0};Ref<RHI::Pipeline> customSky;
        if(m_Env.skyMode==3)for(int side=0;side<6;++side){auto texture=AssetManager::GetTexture(m_Env.skyFaces[side],true);auto id=texture?texture->SRV():~0u;if(side<4)skyPush.facesA[side]=id;else skyPush.facesB[side-4]=id;}
        if(m_Env.skyMode==2)if(auto material=AssetManager::GetMaterial(m_Env.skyMaterial)){skyPush.color=material->color;auto tex=AssetManager::GetTexture(material->albedoMap,true);skyPush.facesB.z=tex?tex->SRV():~0u;if(!material->customPixelShader.empty()){std::error_code ec;auto stamp=std::filesystem::last_write_time(AssetManager::ToAbsolute(material->customPixelShader),ec);if(!ec||Pak::Contains(AssetManager::ToAbsolute(material->customPixelShader))){auto key="sky:"+material->customPixelShader+std::to_string(ec?Pak::Generation():stamp.time_since_epoch().count());auto& pipe=m_CustomPipelines[key];if(!pipe){GraphicsPipelineDesc d;d.name=key;d.vs="Fullscreen.vs";d.ps="asset:"+material->customPixelShader;d.colorFormats={kHDRFormat,kIdFormat};d.depthFormat=kDepthFormat;d.depthWrite=false;d.cull=CullMode::None;pipe=m_Device.CreateGraphicsPipeline(d);}customSky=pipe;}}}
        if(skybox){cmd.SetPipeline(customSky?customSky.get():m_SkyPipeline.get());cmd.Push(skyPush);
        cmd.Draw(3);}
        cmd.EndRendering();
        bool hasWater=std::any_of(m_Draws.begin(),m_Draws.end(),[](auto& draw){return draw.shader==ShaderType::Water;});
        if(hasWater){cmd.CopyTexture(m_HDR.get(),m_OpaqueColor.get());cmd.CopyTexture(m_Depth.get(),m_OpaqueDepth.get());cmd.Barrier(m_OpaqueColor.get(),ResourceState::ShaderRead);cmd.Barrier(m_OpaqueDepth.get(),ResourceState::ShaderRead);}
        rd.colors[0].load=LoadOp::Load;rd.colors[1].load=LoadOp::Load;rd.depth.load=LoadOp::Load;
        cmd.BeginRendering(rd);
        DrawMeshes(cmd, frameBuf, true); // transparent, back to front, after the sky
        cmd.EndRendering();
    }

    if (m_PickRequested && m_PickX < m_Width && m_PickY < m_Height) {
        cmd.CopyTextureToBuffer(m_EntityId.get(), m_PickBuffers[fi].get(), m_PickX, m_PickY, 1, 1);
        m_PickPending[fi] = true;
    }
    m_PickRequested = false;

    // ---------------------------------------------------------------- post
    PostSettings ps;
    ps.exposure = m_Env.exposure;
    ps.bloomIntensity = m_Env.bloomIntensity;
    ps.contrast = m_Env.contrast;
    ps.saturation = m_Env.saturation;
    ps.vignette = m_Env.vignette;
    ps.fxaa = m_Env.fxaa;
    if (!m_Camera.postProcessing) { ps.bloomIntensity = 0; ps.vignette = 0; ps.fxaa = false; }
    m_Post->Execute(cmd, m_HDR.get(), m_Output.get(), ps);

    if (selectedEntity) {
        RenderingDesc rd;
        rd.colors.push_back({ m_Output.get(), LoadOp::Load });
        cmd.Barrier(m_EntityId.get(), ResourceState::ShaderRead);
        cmd.BeginRendering(rd);
        cmd.SetPipeline(m_OutlinePipeline.get());
        struct { u32 ids, selected; } push{ m_EntityId->SRV(), selectedEntity };
        cmd.Push(push);
        cmd.Draw(3);
        cmd.EndRendering();
    }

    if (drawGrid) {
        ScopedGpuTimer t(m_Device, "Grid");
        RenderingDesc rd;
        rd.colors.push_back({ m_Output.get(), LoadOp::Load });
        rd.depth = { m_Depth.get(), LoadOp::Load, 0.0f, 0, true };
        cmd.BeginRendering(rd);
        cmd.SetPipeline(grid2D?m_Grid2DPipeline.get():m_GridPipeline.get());
        struct { u32 frame; float extent; } push{ frameBuf->Index(), grid2D?-1.0f:400.0f };
        cmd.Push(push);
        cmd.Draw(6);
        cmd.EndRendering();
    }
    bool hasUI=std::any_of(m_Draws.begin(),m_Draws.end(),[](const auto& d){return d.uiOverlay;});
    if(hasUI) {
        RenderingDesc rd; rd.colors.push_back({m_Output.get(),LoadOp::Load}); cmd.BeginRendering(rd);
        auto* draws=m_DrawBuffers[fi].get();
        for(u32 i=0;i<m_Draws.size();++i) if(m_Draws[i].uiOverlay) {
            auto& d=m_Draws[i]; cmd.SetPipeline(d.customPipeline.get());
            cmd.Push(MeshPush{frameBuf->Index(),draws->Index(),i,d.mesh->VertexBuffer()->Index(),0,m_MaterialBuffers[fi]->Index(),d.propertyOffset,d.propertyCount});
            cmd.SetIndexBuffer(d.mesh->IndexBuffer()); cmd.DrawIndexed(d.indexCount,1,d.firstIndex); ++m_Stats.drawCalls;
        }
        cmd.EndRendering();
    }
    cmd.Barrier(m_Output.get(), ResourceState::ShaderRead);
}

// Cascade splits: blend of logarithmic and uniform distribution ("practical split scheme").
// Each cascade is fitted with a bounding sphere so its size never changes while the camera rotates,
// and its origin is snapped to shadow texels so edges do not shimmer while the camera moves.
void SceneRenderer::ComputeCascades(FrameConstants& fc, float aspect) {
    const float nearP = m_Camera.nearPlane;
    const float farP = std::max(m_Env.shadowDistance, nearP + 1.0f);
    const float lambda = 0.8f;
    float splits[kShadowCascades];
    for (u32 i = 0; i < kShadowCascades; ++i) {
        float p = (i + 1) / (float)kShadowCascades;
        float logS = nearP * std::pow(farP / nearP, p);
        float uniS = nearP + (farP - nearP) * p;
        splits[i] = lambda * logS + (1.0f - lambda) * uniS;
    }

    glm::mat4 invView = glm::inverse(m_Camera.view);
    float tanHalf = std::tan(m_Camera.fovY * 0.5f);
    glm::vec3 sunDir = glm::normalize(m_Env.sunDirection);
    glm::vec3 up = std::abs(sunDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

    float prev = nearP;
    for (u32 c = 0; c < kShadowCascades; ++c) {
        float zn = prev, zf = splits[c];
        prev = zf;
        glm::vec3 corners[8];
        int k = 0;
        for (float z : { zn, zf }) {
            float h = m_Camera.orthographic?m_Camera.orthographicSize:z * tanHalf, w = h * aspect;
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sx = -1; sx <= 1; sx += 2)
                    corners[k++] = glm::vec3(invView * glm::vec4(sx * w, sy * h, -z, 1.0f));
        }
        glm::vec3 center(0.0f);
        for (auto& p : corners) center += p;
        center /= 8.0f;
        float radius = 0.0f;
        for (auto& p : corners) radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const float casterRange = 250.0f; // how far towards the sun casters are captured
        glm::mat4 lightView = glm::lookAt(center, center - sunDir, up);
        glm::mat4 lightProj = OrthoReverseZ(-radius, radius, -radius, radius, -(radius + casterRange), radius);

        glm::mat4 vp = lightProj * lightView;
        glm::vec4 origin = vp * glm::vec4(0, 0, 0, 1);
        origin *= m_ShadowSize * 0.5f;
        glm::vec2 rounded = glm::round(glm::vec2(origin));
        glm::vec2 offset = (rounded - glm::vec2(origin)) * (2.0f / m_ShadowSize);
        lightProj[3][0] += offset.x;
        lightProj[3][1] += offset.y;

        fc.cascadeViewProj[c] = lightProj * lightView;
        fc.cascadeSplits[c] = zf;
        fc.cascadeTexelSize[c] = 2.0f * radius / m_ShadowSize;
        m_CascadeSpheres[c] = glm::vec4(center, radius + casterRange);
    }
}

void SceneRenderer::RenderShadows(CommandList& cmd, Buffer* frameBuf) {
    ScopedGpuTimer t(m_Device, "Shadows");
    for (u32 c = 0; c < kShadowCascades; ++c) {
        RenderingDesc rd;
        rd.depth = { m_ShadowMap.get(), LoadOp::Clear, 0.0f, c };
        cmd.BeginRendering(rd);
        cmd.SetPipeline(m_ShadowPipeline.get());
        glm::vec3 sphereCenter = glm::vec3(m_CascadeSpheres[c]);
        float sphereRadius = m_CascadeSpheres[c].w;
        Buffer* drawBuf = m_DrawBuffers[m_Device.FrameIndex()].get();
        for (u32 i = 0; i < (u32)m_Draws.size(); ++i) {
            const DrawItem& d = m_Draws[i];
            if (!d.castShadows) continue;
            // Coarse culling: bounding sphere of the mesh vs. bounding sphere of the cascade volume
            if (glm::length(d.boundsCenter - sphereCenter) > sphereRadius + d.boundsRadius) continue;
            MeshPush p{ frameBuf->Index(), drawBuf->Index(), i, d.mesh->VertexBuffer()->Index(), c };
            cmd.Push(p);
            cmd.SetIndexBuffer(d.mesh->IndexBuffer());
            cmd.DrawIndexed(d.indexCount,1,d.firstIndex);
            m_Stats.drawCalls++;
        }
        for(auto* batch:m_ActiveTerrains){batch->terrain->DrawShadow(cmd,frameBuf,c);batch->trees->DrawShadow(cmd,frameBuf,c);}
        cmd.EndRendering();
    }
}

} // namespace Stardust

