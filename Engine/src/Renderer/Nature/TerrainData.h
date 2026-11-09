#pragma once
#include "RHI/RHI.h"
#include <glm/glm.hpp>
#include <array>
#include <filesystem>

namespace Stardust {

// One paintable surface layer (like a Unity TerrainLayer). Without a texture a procedural look is used.
struct TerrainLayer {
    std::string name;
    std::string albedoMap;   // asset path, optional
    glm::vec3 tint{ 1.0f };
    float tiling = 4.0f;     // metres per texture repeat
    float roughness = 0.9f;
    std::string assetPath;
};

static constexpr u32 kTerrainLayers = 16;

struct TerrainSettings {
    // Procedural generator parameters (used by "Generate")
    float size = 2048.0f;          // metres, square, centred on the entity
    float mountainHeight = 220.0f;
    float hillHeight = 14.0f;
    float meadowRadius = 260.0f;
    u32 seed = 7;
    u32 resolution = 1024;         // heightmap samples per side

    bool proceduralAppearance=false;
    bool grass = false;
    float grassDensity = 1.0f;
    float grassHeight = 0.55f;
    float grassDistance = 55.0f;
    float flowerAmount = 0.03f;
    glm::vec3 grassColor{ 0.13f, 0.24f, 0.04f };
    glm::vec3 dryGrassColor{ 0.38f, 0.37f, 0.12f };

    bool trees = false;
    float treeDensity = 1.0f;
    u32 maxTrees = 6000;
    u32 treeSeed = 3;

    std::vector<TerrainLayer> layers = {{"Layer 0", "", { .5f,.5f,.5f },4,.9f}};

};

struct TreeInstance {
    glm::vec3 position; // terrain-local
    float rotation;
    float scale;
    u32 type;
};

struct TerrainBrushStamp {int shape=0;float rotation=0;std::string texture;};

enum class TerrainBrush : u8 { None, Raise, Smooth, Flatten, SetHeight, Paint, Trees };

// Heights + splat weights + trees of one terrain. Editable on the CPU, mirrored to the GPU incrementally.
// Instances are shared per terrain entity (by UUID), so the scene view, game view and physics see the same data.
class TerrainData {
public:
    TerrainData(RHI::Device& device, const TerrainSettings& settings);

    // Registry keyed by the terrain entity UUID. Loads dataPath if it exists, otherwise generates procedurally.
    static Ref<TerrainData> Acquire(RHI::Device& device, u64 key, const TerrainSettings& settings, const std::string& dataPath,bool generate=false,std::array<u32,4> layers={0,1,2,3});
    static Ref<TerrainData> Find(u64 key);
    static void ClearCache();
    static void ResolveLayers(TerrainSettings& settings);

    void Generate(const TerrainSettings& settings,std::array<u32,4> layers={0,1,2,3});  // procedural heights + slope-based splat + trees
    void MakeFlat(float height, u32 resolution);
    void ScatterTrees();
    void ClearTrees() { m_Trees.clear(); m_TreesVersion++; }
    bool Save(const std::filesystem::path& path) const;
    bool Load(const std::filesystem::path& path);

    // Editing (local = terrain-local metres, centre at 0,0). Returns true if anything changed.
    bool ApplyBrush(TerrainBrush brush, glm::vec2 local, float radius, float strength, float hardness,
                    float targetHeight, u32 layer, bool invert, float dt,const TerrainBrushStamp& stamp={});
    bool StitchEdge(TerrainData& other,int side,float heightOffset=0);
    static float BrushWeight(glm::vec2 local,const TerrainBrushStamp& stamp);
    void RemoveLayer(u32 index);
    void EndStroke(); // re-projects trees onto the edited surface

    float HeightAt(float x, float z) const;
    glm::vec3 NormalAt(float x, float z) const;
    glm::vec4 SplatAt(float x, float z) const;
    float LayerWeightAt(float x,float z,u32 layer) const;
    float MinHeight() const { return m_MinH; }
    float MaxHeight() const { return m_MaxH; }
    u32 Resolution() const { return m_Res; }
    const std::vector<float>& Heights() const { return m_Heights; }
    u64 HeightsVersion() const { return m_HeightsVersion; } // bumps on every edit (physics rebuilds on change)

    RHI::Texture* Heightmap() const { return m_Heightmap.get(); }
    RHI::Texture* Splatmap() const { return m_Splatmap.get(); }
    const TerrainSettings& Settings() const { return m_Settings; }
    void SetSettings(const TerrainSettings& s) { u32 res = m_Settings.resolution; m_Settings = s; m_Settings.resolution = res; }
    const std::vector<TreeInstance>& Trees() const { return m_Trees; }
    u64 TreesVersion() const { return m_TreesVersion; }

    // Uploads the edited region to the GPU inside the frame's command list
    void FlushGpu(RHI::CommandList& cmd, u32 frameIndex);

private:
    void CreateGpuResources();
    void SplatFromSlope();
    void UpdateBounds();
    void MarkDirty(int x0, int z0, int x1, int z1);
    float Step() const { return m_Settings.size / (m_Res - 1); }

    RHI::Device& m_Device;
    TerrainSettings m_Settings;
    u32 m_Res = 0;
    std::vector<float> m_Heights;
    std::vector<u8> m_Splat;  // RGBA8 weights per sample: grass, dirt, rock, sand
    float m_MinH = 0, m_MaxH = 0;
    Ref<RHI::Texture> m_Heightmap, m_Splatmap;
    std::array<Ref<RHI::Buffer>, RHI::kFramesInFlight> m_Staging;
    std::vector<TreeInstance> m_Trees;
    u64 m_HeightsVersion = 1, m_TreesVersion = 1;

    int m_DirtyX0 = 0, m_DirtyZ0 = 0, m_DirtyX1 = -1, m_DirtyZ1 = -1; // inclusive, empty when x1 < x0
};

} // namespace Stardust
