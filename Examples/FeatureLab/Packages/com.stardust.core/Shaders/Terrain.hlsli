#ifndef TERRAIN_HLSLI
#define TERRAIN_HLSLI
#include "Common.hlsli"

// Shared by Terrain.vs / Terrain.ps / TerrainShadow.vs - must match Renderer/Nature/TerrainRenderer.cpp
struct TerrainPush {
    uint frameBuffer;
    uint instanceBuffer;   // float4 per patch: terrain-local minX, minZ, size, lod
    uint instanceBase;
    uint heightmap;
    float3 origin;         // world position of the terrain centre
    float size;
    uint patchRes;         // quads per patch side
    uint entityId;
    uint cascade;
    float skirtDepth;
    float4 grassColor;
    float4 dryColor;
    uint splatmap;
    uint layerBuffer;    // 4 x TerrainLayerGPU (32 bytes)
    uint2 pad;
};
PUSH_CONSTANTS(TerrainPush, g_Push);

float2 HeightmapUV(float2 local, uint heightmap) {
    uint w, h;
    GetTex2D(heightmap).GetDimensions(w, h);
    float res = float(w);
    return (local / g_Push.size + 0.5) * (res - 1.0) / res + 0.5 / res;
}

float2 SplatUV(float2 local,uint group){float2 uv=HeightmapUV(local,g_Push.heightmap);return float2((uv.x+group)/4.0,uv.y);}

float SampleHeight(float2 local) {
    return GetTex2D(g_Push.heightmap).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), HeightmapUV(local, g_Push.heightmap), 0).r;
}

// Patch vertices are generated from the vertex index: a (N+1)^2 grid followed by a skirt ring
float3 TerrainVertex(uint vid, uint instance, out float2 localXZ) {
    float4 inst = GetBuffer(g_Push.instanceBuffer).Load<float4>((g_Push.instanceBase + instance) * 16);
    uint N = g_Push.patchRes;
    uint gridCount = (N + 1) * (N + 1);
    float2 g;
    bool skirt = vid >= gridCount;
    if (!skirt) {
        g = float2(vid % (N + 1), vid / (N + 1));
    } else {
        uint s = vid - gridCount;
        uint side = s / (N + 1), k = s % (N + 1);
        g = side == 0 ? float2(k, 0) : side == 1 ? float2(N, k) : side == 2 ? float2(k, N) : float2(0, k);
    }
    localXZ = inst.xy + g / float(N) * inst.z;
    localXZ = clamp(localXZ, -g_Push.size * 0.5, g_Push.size * 0.5);
    float h = SampleHeight(localXZ);
    if (skirt) h -= g_Push.skirtDepth * (inst.z / 64.0);
    return g_Push.origin + float3(localXZ.x, h, localXZ.y);
}

#endif
