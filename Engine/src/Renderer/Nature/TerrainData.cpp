#include "Renderer/Nature/TerrainData.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include <sstream>
#include "Core/Noise.h"
#include "Core/Log.h"
#include <chrono>
#include <execution>
#include <numeric>
#include <mutex>
#include <fstream>
#include <unordered_map>
#include <cstring>
#include <stb_image.h>
#include <nlohmann/json.hpp>

namespace Stardust {
void TerrainData::ResolveLayers(TerrainSettings& settings){
    using json=nlohmann::json;
    static std::unordered_map<std::string,std::pair<std::filesystem::file_time_type,json>> cache;
    for(auto& layer:settings.layers)if(!layer.assetPath.empty())try{
        auto path=AssetManager::ToAbsolute(layer.assetPath);std::error_code ec;auto stamp=std::filesystem::last_write_time(path,ec);if(ec)continue;
        auto& entry=cache[FileSystem::Utf8(path)];if(entry.first!=stamp||entry.second.is_null()){entry.first=stamp;entry.second=json::parse(FileSystem::ReadText(path),nullptr,false);}auto& j=entry.second;if(!j.is_object())continue;
        layer.name=j.value("name",layer.name);layer.albedoMap=j.value("albedoMap",layer.albedoMap);layer.tiling=j.value("tiling",layer.tiling);layer.roughness=j.value("roughness",layer.roughness);if(j.contains("tint")&&j["tint"].is_array()&&j["tint"].size()==3)layer.tint={j["tint"][0].get<float>(),j["tint"][1].get<float>(),j["tint"][2].get<float>()};
    }catch(const std::exception& e){SD_WARN("Terrain layer: {}",e.what());}
}

TerrainData::TerrainData(RHI::Device& device, const TerrainSettings& settings) : m_Device(device), m_Settings(settings) {
    m_Res = std::clamp(settings.resolution, 64u, 4096u);
    m_Settings.resolution = m_Res;
}

void TerrainData::CreateGpuResources() {
    RHI::TextureDesc d;
    d.width = d.height = m_Res;
    d.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::CopyDst;
    d.name = "TerrainHeightmap"; d.format = RHI::Format::R32F;
    m_Heightmap = m_Device.CreateTexture(d);
    d.width=m_Res*(kTerrainLayers/4);
    d.name = "TerrainSplatmap"; d.format = RHI::Format::RGBA8_UNORM;
    m_Splatmap = m_Device.CreateTexture(d);
    u64 bytes = (u64)m_Res * m_Res * (4+kTerrainLayers) + 256;
    for (auto& b : m_Staging)
        b = m_Device.CreateBuffer({ bytes, RHI::BufferUsage::CopySrc, RHI::MemoryUsage::CPUToGPU, "TerrainStaging" });
    MarkDirty(0, 0, m_Res - 1, m_Res - 1);
}

// ------------------------------------------------------------------------------------------------ generation

void TerrainData::Generate(const TerrainSettings& settings,std::array<u32,4> layers) {
    auto start = std::chrono::steady_clock::now();
    m_Settings = settings;
    u32 res = std::clamp(settings.resolution, 64u, 4096u);
    bool resChanged = res != m_Res || !m_Heightmap;
    m_Res = res;
    m_Settings.resolution = res;
    const TerrainSettings& s = m_Settings;
    Perlin2D noise(s.seed);
    Perlin2D warpNoise(s.seed * 7919u + 13u);
    m_Heights.assign((size_t)m_Res * m_Res, 0.0f);

    auto height = [&](float x, float z) {
        float d = std::sqrt(x * x + z * z);
        // Domain warp breaks up the grid-like look of plain fBm
        float wx = (warpNoise.Fbm(x * 0.002f, z * 0.002f, 3) - 0.5f) * 160.0f;
        float wz = (warpNoise.Fbm(x * 0.002f + 41.0f, z * 0.002f + 17.0f, 3) - 0.5f) * 160.0f;
        float qx = x + wx, qz = z + wz;
        float hills = (noise.Fbm(qx * 0.006f, qz * 0.006f, 5) - 0.5f) * 2.0f * s.hillHeight;
        hills *= 0.45f + 0.55f * Smoothstep(0.0f, s.meadowRadius, d);
        float broad = (noise.Fbm(qx * 0.0011f + 100.0f, qz * 0.0011f, 4) - 0.5f) * 2.0f * s.mountainHeight * 0.25f
                    * Smoothstep(s.meadowRadius * 0.6f, s.meadowRadius * 2.0f, d);
        float mountains = noise.Ridged(qx * 0.0016f + 300.0f, qz * 0.0016f, 6) * s.mountainHeight
                        * Smoothstep(s.meadowRadius * 1.6f, s.size * 0.42f, d);
        return hills + broad + mountains;
    };

    std::vector<u32> rows(m_Res);
    std::iota(rows.begin(), rows.end(), 0u);
    float step = Step();
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](u32 z) {
        for (u32 x = 0; x < m_Res; ++x)
            m_Heights[(size_t)z * m_Res + x] = height(-s.size * 0.5f + x * step, -s.size * 0.5f + z * step);
    });
    float centre = height(0.0f, 0.0f); // the meadow centre sits at the entity's height
    for (float& h : m_Heights) h -= centre;

    UpdateBounds();
    SplatFromSlope();
    for(size_t i=0;i<m_Splat.size();i+=kTerrainLayers){std::array<unsigned,kTerrainLayers> mapped{};for(int k=0;k<4;++k)mapped[std::min((u32)m_Settings.layers.size()-1,layers[k])]+=m_Splat[i+k];for(int k=0;k<kTerrainLayers;++k)m_Splat[i+k]=(u8)std::min(255u,mapped[k]);}
    ScatterTrees();
    if (resChanged) CreateGpuResources(); else MarkDirty(0, 0, m_Res - 1, m_Res - 1);
    m_HeightsVersion++;
    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    SD_INFO("Terrain generated: {}x{} samples, {} trees, {:.0f} ms", m_Res, m_Res, m_Trees.size(), ms);
}

void TerrainData::MakeFlat(float h, u32 resolution) {
    u32 res = std::clamp(resolution, 64u, 4096u);
    bool resChanged = res != m_Res || !m_Heightmap;
    m_Res = res;
    m_Settings.resolution = res;
    m_Heights.assign((size_t)m_Res * m_Res, h);
    m_Splat.assign((size_t)m_Res * m_Res * kTerrainLayers, 0);
    for (size_t i = 0; i < (size_t)m_Res * m_Res; ++i) m_Splat[i * kTerrainLayers] = 255; // all grass
    m_Trees.clear();
    m_TreesVersion++;
    UpdateBounds();
    if (resChanged) CreateGpuResources(); else MarkDirty(0, 0, m_Res - 1, m_Res - 1);
    m_HeightsVersion++;
    SD_INFO("Flat terrain created: {}x{} samples at height {}", m_Res, m_Res, h);
}

void TerrainData::SplatFromSlope() {
    m_Splat.assign((size_t)m_Res * m_Res * kTerrainLayers, 0);
    float step = Step();
    for (u32 z = 0; z < m_Res; ++z)
        for (u32 x = 0; x < m_Res; ++x) {
            float lx = -m_Settings.size * 0.5f + x * step, lz = -m_Settings.size * 0.5f + z * step;
            float slope = 1.0f - NormalAt(lx, lz).y;
            float n = Hash01(x * 73856093u ^ z * 19349663u) * 0.06f - 0.03f;
            float wDirt = Smoothstep(0.16f, 0.3f, slope + n);
            float wRock = Smoothstep(0.34f, 0.5f, slope + n * 0.5f);
            float g = (1 - wDirt) * (1 - wRock), d = wDirt * (1 - wRock), r = wRock;
            u8* o = &m_Splat[((size_t)z * m_Res + x) * kTerrainLayers];
            o[0] = (u8)(g * 255.0f + 0.5f); o[1] = (u8)(d * 255.0f + 0.5f); o[2] = (u8)(r * 255.0f + 0.5f); o[3] = 0;
        }
}

void TerrainData::UpdateBounds() {
    m_MinH = 1e9f; m_MaxH = -1e9f;
    for (float h : m_Heights) { m_MinH = std::min(m_MinH, h); m_MaxH = std::max(m_MaxH, h); }
}

void TerrainData::ScatterTrees() {
    m_Trees.clear();
    m_TreesVersion++;
    const TerrainSettings& s = m_Settings;
    if (s.treeDensity <= 0) return;
    Perlin2D forestNoise(s.treeSeed * 104729u + 5u);
    const float spacing = 5.5f;
    const float half = s.size * 0.47f;
    std::mt19937 rng(s.treeSeed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    for (float z = -half; z < half; z += spacing)
        for (float x = -half; x < half; x += spacing) {
            float px = x + (U(rng) - 0.5f) * spacing * 0.9f;
            float pz = z + (U(rng) - 0.5f) * spacing * 0.9f;
            float d = std::sqrt(px * px + pz * pz);
            if (d < 30.0f) continue; // keep the spawn area clear
            float forest = Smoothstep(0.46f, 0.58f, forestNoise.Fbm(px * 0.0045f, pz * 0.0045f, 4));
            float ring = Smoothstep(s.meadowRadius * 0.55f, s.meadowRadius * 1.1f, d);
            float prob = (forest * ring * 0.85f + 0.0015f * (1.0f - ring) + 0.002f) * s.treeDensity;
            if (U(rng) > prob) continue;
            glm::vec3 n = NormalAt(px, pz);
            float h = HeightAt(px, pz);
            if (n.y < 0.84f || h > s.mountainHeight * 0.45f) continue;
            float r = U(rng);
            m_Trees.push_back({ glm::vec3(px, h, pz), U(rng) * 6.2831853f, 0.8f + U(rng) * 0.45f, r < 0.45f ? 0u : r < 0.85f ? 1u : 2u });
        }
    if (m_Trees.size() > s.maxTrees) {
        std::shuffle(m_Trees.begin(), m_Trees.end(), rng);
        m_Trees.resize(s.maxTrees);
    }
}

// ------------------------------------------------------------------------------------------------ queries

float TerrainData::HeightAt(float x, float z) const {
    if (m_Heights.empty()) return 0.0f;
    float fx = std::clamp((x / m_Settings.size + 0.5f) * (m_Res - 1), 0.0f, (float)(m_Res - 1));
    float fz = std::clamp((z / m_Settings.size + 0.5f) * (m_Res - 1), 0.0f, (float)(m_Res - 1));
    u32 x0 = (u32)fx, z0 = (u32)fz;
    u32 x1 = std::min(x0 + 1, m_Res - 1), z1 = std::min(z0 + 1, m_Res - 1);
    float tx = fx - x0, tz = fz - z0;
    auto H = [&](u32 xx, u32 zz) { return m_Heights[(size_t)zz * m_Res + xx]; };
    float a = H(x0, z0) + (H(x1, z0) - H(x0, z0)) * tx;
    float b = H(x0, z1) + (H(x1, z1) - H(x0, z1)) * tx;
    return a + (b - a) * tz;
}

glm::vec3 TerrainData::NormalAt(float x, float z) const {
    float e = Step();
    float hl = HeightAt(x - e, z), hr = HeightAt(x + e, z);
    float hd = HeightAt(x, z - e), hu = HeightAt(x, z + e);
    return glm::normalize(glm::vec3(hl - hr, 2.0f * e, hd - hu));
}

glm::vec4 TerrainData::SplatAt(float x, float z) const {
    if (m_Splat.empty()) return { 1, 0, 0, 0 };
    u32 ix = (u32)std::clamp((x / m_Settings.size + 0.5f) * (m_Res - 1) + 0.5f, 0.0f, (float)(m_Res - 1));
    u32 iz = (u32)std::clamp((z / m_Settings.size + 0.5f) * (m_Res - 1) + 0.5f, 0.0f, (float)(m_Res - 1));
    const u8* p = &m_Splat[((size_t)iz * m_Res + ix) * kTerrainLayers];
    return glm::vec4(p[0], p[1], p[2], p[3]) / 255.0f;
}

float TerrainData::LayerWeightAt(float x,float z,u32 layer)const{if(m_Splat.empty()||layer>=kTerrainLayers)return 0;u32 ix=(u32)std::clamp((x/m_Settings.size+.5f)*(m_Res-1)+.5f,0.f,(float)(m_Res-1)),iz=(u32)std::clamp((z/m_Settings.size+.5f)*(m_Res-1)+.5f,0.f,(float)(m_Res-1));return m_Splat[((size_t)iz*m_Res+ix)*kTerrainLayers+layer]/255.f;}
// ------------------------------------------------------------------------------------------------ editing

void TerrainData::MarkDirty(int x0, int z0, int x1, int z1) {
    if (m_DirtyX1 < m_DirtyX0) { m_DirtyX0 = x0; m_DirtyZ0 = z0; m_DirtyX1 = x1; m_DirtyZ1 = z1; return; }
    m_DirtyX0 = std::min(m_DirtyX0, x0); m_DirtyZ0 = std::min(m_DirtyZ0, z0);
    m_DirtyX1 = std::max(m_DirtyX1, x1); m_DirtyZ1 = std::max(m_DirtyZ1, z1);
}

bool TerrainData::ApplyBrush(TerrainBrush brush, glm::vec2 local, float radius, float strength, float hardness,
                             float targetHeight, u32 layer, bool invert, float dt,const TerrainBrushStamp& stamp) {
    layer=std::min(layer,(u32)std::max<size_t>(1,m_Settings.layers.size())-1);
    if (brush == TerrainBrush::None || m_Heights.empty()) return false;
    const float half = m_Settings.size * 0.5f;

    if (brush == TerrainBrush::Trees) {
        if (invert) {
            size_t before = m_Trees.size();
            std::erase_if(m_Trees, [&](const TreeInstance& t) { return glm::length(glm::vec2(t.position.x, t.position.z) - local) < radius; });
            if (m_Trees.size() != before) { m_TreesVersion++; return true; }
            return false;
        }
        static std::mt19937 rng(12345);
        std::uniform_real_distribution<float> U(0.0f, 1.0f);
        float expected = strength * dt * radius * radius * 0.02f;
        int count = (int)expected + (U(rng) < expected - (int)expected ? 1 : 0);
        bool added = false;
        for (int i = 0; i < count; ++i) {
            float a = U(rng) * 6.2831853f, r = std::sqrt(U(rng)) * radius;
            glm::vec2 p = local + glm::vec2(std::cos(a), std::sin(a)) * r;
            if (std::abs(p.x) > half || std::abs(p.y) > half) continue;
            bool tooClose = false;
            for (auto& t : m_Trees)
                if (glm::length(glm::vec2(t.position.x, t.position.z) - p) < 4.0f) { tooClose = true; break; }
            if (tooClose) continue;
            float s = U(rng);
            m_Trees.push_back({ glm::vec3(p.x, HeightAt(p.x, p.y), p.y), U(rng) * 6.2831853f, 0.8f + U(rng) * 0.45f, s < 0.45f ? 0u : s < 0.85f ? 1u : 2u });
            added = true;
        }
        if (added) m_TreesVersion++;
        return added;
    }

    float step = Step();
    int cx = (int)std::round((local.x + half) / step), cz = (int)std::round((local.y + half) / step);
    int r = (int)std::ceil(radius / step) + 1;
    int x0 = std::max(0, cx - r), x1 = std::min((int)m_Res - 1, cx + r);
    int z0 = std::max(0, cz - r), z1 = std::min((int)m_Res - 1, cz + r);
    if (x1 < x0 || z1 < z0) return false;

    // Smoothing reads from a snapshot of the region (with a 1-texel border)
    std::vector<float> snap;
    int sx0 = std::max(0, x0 - 1), sz0 = std::max(0, z0 - 1), sx1 = std::min((int)m_Res - 1, x1 + 1), sz1 = std::min((int)m_Res - 1, z1 + 1);
    int sw = sx1 - sx0 + 1;
    if (brush == TerrainBrush::Smooth) {
        snap.resize((size_t)sw * (sz1 - sz0 + 1));
        for (int z = sz0; z <= sz1; ++z)
            memcpy(&snap[(size_t)(z - sz0) * sw], &m_Heights[(size_t)z * m_Res + sx0], sw * sizeof(float));
    }
    auto S = [&](int x, int z) { x = std::clamp(x, sx0, sx1); z = std::clamp(z, sz0, sz1); return snap[(size_t)(z - sz0) * sw + (x - sx0)]; };

    hardness = std::clamp(hardness, 0.0f, 0.99f);
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            glm::vec2 p(-half + x * step, -half + z * step);
            float d = glm::length(p - local) / radius;
            if (d >= 1.0f) continue;
            float w = (1.0f - Smoothstep(hardness, 1.0f, d)) * BrushWeight((p-local)/radius,stamp) * strength * dt;
            size_t i = (size_t)z * m_Res + x;
            float& h = m_Heights[i];
            switch (brush) {
            case TerrainBrush::Raise: h += (invert ? -1.0f : 1.0f) * w * 25.0f; break;
            case TerrainBrush::Smooth: {
                float avg = 0;
                for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) avg += S(x + dx, z + dz);
                h = h + (avg / 9.0f - h) * std::min(1.0f, w * 10.0f);
                break;
            }
            case TerrainBrush::Flatten:
            case TerrainBrush::SetHeight: h = h + (targetHeight - h) * std::min(1.0f, w * 10.0f); break;
            case TerrainBrush::Paint: {
                u8* sp = &m_Splat[i * kTerrainLayers];
                float wts[kTerrainLayers];for(int k=0;k<kTerrainLayers;++k)wts[k]=sp[k]/255.f;
                float amount = std::min(1.0f, w * 6.0f);
                if(invert)wts[layer]*=1-amount;else{for(u32 k=0;k<kTerrainLayers;++k)if(k!=layer)wts[k]*=1-amount;wts[layer]+=(1-wts[layer])*amount;}
                float sum=0;for(float weight:wts)sum+=weight;
                if (sum < 1e-4f) { wts[0] = 1; sum = 1; }
                for (int k = 0; k < kTerrainLayers; ++k) sp[k] = (u8)std::clamp(wts[k] / sum * 255.0f + 0.5f, 0.0f, 255.0f);
                break;
            }
            default: break;
            }
            if (brush != TerrainBrush::Paint) { m_MinH = std::min(m_MinH, h); m_MaxH = std::max(m_MaxH, h); }
            changed = true;
        }
    if (changed) {
        MarkDirty(x0, z0, x1, z1);
        if (brush != TerrainBrush::Paint) m_HeightsVersion++;
    }
    return changed;
}

float TerrainData::BrushWeight(glm::vec2 p,const TerrainBrushStamp& stamp){float angle=glm::radians(stamp.rotation);p=glm::vec2(std::cos(angle)*p.x-std::sin(angle)*p.y,std::sin(angle)*p.x+std::cos(angle)*p.y);float d=glm::length(p);if(d>1)return 0;if(stamp.shape==1)return 1;if(stamp.shape==2)return std::pow(1-d,2);if(stamp.shape==3)return .25f+.75f*Hash01((u32)((p.x+1)*400)*73856093u^(u32)((p.y+1)*400)*19349663u);if(stamp.shape==4){float edge=.45f+.35f*std::cos(std::atan2(p.y,p.x)*5);return d<edge?1.f:0.f;}if(stamp.shape==5&&!stamp.texture.empty()){struct Mask{int w=0,h=0;std::vector<u8> pixels;};static std::unordered_map<std::string,Mask> cache;auto& mask=cache[stamp.texture];if(mask.pixels.empty()){int channels;auto bytes=FileSystem::ReadBinary(AssetManager::ToAbsolute(stamp.texture));auto* pixels=stbi_load_from_memory(bytes.data(),(int)bytes.size(),&mask.w,&mask.h,&channels,1);if(pixels){mask.pixels.assign(pixels,pixels+mask.w*mask.h);stbi_image_free(pixels);}}if(!mask.pixels.empty()){int x=std::clamp((int)((p.x*.5f+.5f)*(mask.w-1)),0,mask.w-1),y=std::clamp((int)((p.y*.5f+.5f)*(mask.h-1)),0,mask.h-1);return mask.pixels[y*mask.w+x]/255.f;}}return std::exp(-d*d*3);}
bool TerrainData::StitchEdge(TerrainData& other,int side,float heightOffset){if(m_Res!=other.m_Res||std::abs(m_Settings.size-other.m_Settings.size)>.01f||side<0||side>3)return false;for(u32 i=0;i<m_Res;++i){size_t a=side==0?(size_t)i*m_Res:side==1?(size_t)i*m_Res+m_Res-1:side==2?i:(size_t)(m_Res-1)*m_Res+i;size_t b=side==0?(size_t)i*m_Res+m_Res-1:side==1?(size_t)i*m_Res:side==2?(size_t)(m_Res-1)*m_Res+i:i;float average=(m_Heights[a]+other.m_Heights[b]+heightOffset)*.5f;m_Heights[a]=average;other.m_Heights[b]=average-heightOffset;}++m_HeightsVersion;++other.m_HeightsVersion;UpdateBounds();other.UpdateBounds();MarkDirty(0,0,m_Res-1,m_Res-1);other.MarkDirty(0,0,m_Res-1,m_Res-1);EndStroke();other.EndStroke();return true;}
void TerrainData::RemoveLayer(u32 index){if(index>=kTerrainLayers)return;for(size_t p=0;p<m_Splat.size();p+=kTerrainLayers){for(u32 k=index;k+1<kTerrainLayers;++k)m_Splat[p+k]=m_Splat[p+k+1];m_Splat[p+kTerrainLayers-1]=0;int total=0;for(u32 k=0;k<kTerrainLayers;++k)total+=m_Splat[p+k];if(total==0)m_Splat[p]=255;else for(u32 k=0;k<kTerrainLayers;++k)m_Splat[p+k]=(u8)std::clamp((int)std::round(m_Splat[p+k]*255.f/total),0,255);}MarkDirty(0,0,m_Res-1,m_Res-1);}
void TerrainData::EndStroke() {
    for (auto& t : m_Trees) t.position.y = HeightAt(t.position.x, t.position.z);
    m_TreesVersion++;
}

// ------------------------------------------------------------------------------------------------ GPU

void TerrainData::FlushGpu(RHI::CommandList& cmd, u32 frameIndex) {
    if (!m_Heightmap) CreateGpuResources();
    if (m_DirtyX1 < m_DirtyX0) return;
    u32 w = m_DirtyX1 - m_DirtyX0 + 1, h = m_DirtyZ1 - m_DirtyZ0 + 1;
    RHI::Buffer* staging = m_Staging[frameIndex].get();
    u8* dst = (u8*)staging->Mapped();
    u64 heightBytes = (u64)w * h * 4;
    u64 splatOffset = (heightBytes + 255) & ~255ull;
    for (u32 z = 0; z < h; ++z) {
        size_t src = (size_t)(m_DirtyZ0 + z) * m_Res + m_DirtyX0;
        memcpy(dst + (size_t)z * w * 4, &m_Heights[src], w * 4);
        for(u32 group=0;group<kTerrainLayers/4;++group)for(u32 x=0;x<w;++x)memcpy(dst+splatOffset+(u64)group*w*h*4+((size_t)z*w+x)*4,&m_Splat[(src+x)*kTerrainLayers+group*4],4);
    }
    cmd.CopyBufferToTexture(staging, 0, m_Heightmap.get(), m_DirtyX0, m_DirtyZ0, w, h);
    for(u32 group=0;group<kTerrainLayers/4;++group)cmd.CopyBufferToTexture(staging,splatOffset+(u64)group*w*h*4,m_Splatmap.get(),m_DirtyX0+group*m_Res,m_DirtyZ0,w,h);
    m_DirtyX0 = 0; m_DirtyZ0 = 0; m_DirtyX1 = -1; m_DirtyZ1 = -1;
}

// ------------------------------------------------------------------------------------------------ files

static constexpr u32 kTerrainMagic = 0x32544453; // "SDT1"

bool TerrainData::Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) { SD_ERROR("Cannot write terrain: {}", FileSystem::Utf8(path)); return false; }
    u32 count = (u32)m_Trees.size();
    f.write((const char*)&kTerrainMagic, 4);
    f.write((const char*)&m_Res, 4);u32 channels=kTerrainLayers;f.write((const char*)&channels,4);
    f.write((const char*)m_Heights.data(), m_Heights.size() * 4);
    f.write((const char*)m_Splat.data(), m_Splat.size());
    f.write((const char*)&count, 4);
    f.write((const char*)m_Trees.data(), count * sizeof(TreeInstance));
    return true;
}

bool TerrainData::Load(const std::filesystem::path& path) {
    auto bytes=FileSystem::ReadBinary(path);std::istringstream f(std::string(bytes.begin(),bytes.end()),std::ios::binary);
    u32 magic = 0, res = 0, count = 0;
    if (!f || !f.read((char*)&magic, 4) || (magic != kTerrainMagic && magic!=0x31544453) || !f.read((char*)&res, 4) || res < 2 || res > 8192) {
        SD_ERROR("Invalid terrain file: {}", FileSystem::Utf8(path));
        return false;
    }
    u32 channels=4;if(magic==kTerrainMagic)f.read((char*)&channels,4);if(!channels||channels>kTerrainLayers)return false;
    std::vector<float> heights((size_t)res * res);
    std::vector<u8> packed((size_t)res*res*channels);std::vector<u8> splat((size_t)res * res * kTerrainLayers,0);
    f.read((char*)heights.data(), heights.size() * 4);
    f.read((char*)packed.data(), packed.size());for(size_t i=0;i<(size_t)res*res;++i)memcpy(&splat[i*kTerrainLayers],&packed[i*channels],channels);
    f.read((char*)&count, 4);
    std::vector<TreeInstance> trees(f ? count : 0);
    if (count) f.read((char*)trees.data(), count * sizeof(TreeInstance));
    if (!f) { SD_ERROR("Truncated terrain file: {}", FileSystem::Utf8(path)); return false; }
    bool resChanged = res != m_Res || !m_Heightmap;
    m_Res = res;
    m_Settings.resolution = res;
    m_Heights = std::move(heights);
    m_Splat = std::move(splat);
    m_Trees = std::move(trees);
    m_TreesVersion++;
    m_HeightsVersion++;
    UpdateBounds();
    if (resChanged) CreateGpuResources(); else MarkDirty(0, 0, m_Res - 1, m_Res - 1);
    SD_INFO("Terrain loaded: {} ({}x{}, {} trees)", FileSystem::Utf8(path), res, res, m_Trees.size());
    return true;
}

// ------------------------------------------------------------------------------------------------ registry

static std::mutex s_CacheMutex;
static std::unordered_map<u64, Ref<TerrainData>> s_Cache;

Ref<TerrainData> TerrainData::Acquire(RHI::Device& device, u64 key, const TerrainSettings& settings, const std::string& dataPath,bool generate,std::array<u32,4> layers) {
    std::lock_guard lock(s_CacheMutex);
    if (auto it = s_Cache.find(key); it != s_Cache.end()) {
        it->second->SetSettings(settings);
        return it->second;
    }
    auto t = MakeRef<TerrainData>(device, settings);
    std::filesystem::path abs = dataPath.empty() ? std::filesystem::path() : AssetManager::ToAbsolute(dataPath);
    if (dataPath.empty() || !FileSystem::Exists(abs) || !t->Load(abs)) {if(generate)t->Generate(settings,layers);else t->MakeFlat(0,settings.resolution);}
    s_Cache[key] = t;
    return t;
}

Ref<TerrainData> TerrainData::Find(u64 key) {
    std::lock_guard lock(s_CacheMutex);
    auto it = s_Cache.find(key);
    return it == s_Cache.end() ? nullptr : it->second;
}

void TerrainData::ClearCache() {
    std::lock_guard lock(s_CacheMutex);
    s_Cache.clear();
}

} // namespace Stardust
