#ifndef DECAL_HLSLI
#define DECAL_HLSLI
#include "Common.hlsli"

// Must match Renderer/SceneRenderer.h :: DecalGPU (208 bytes)
struct DecalData {
    float4x4 model;       // unit cube -> world
    float4x4 invModel;    // world -> unit cube
    float4 color;
    float4 tilingOffset;
    uint4 ids;            // x albedo SRV, y normal SRV (~0 none), z sampler
    float4 params;        // x opacity, y normal strength, z angle fade, w smoothness
    float4 extra;         // x emission
};

struct Push { uint frame, decals, index, depth; };
PUSH_CONSTANTS(Push, g_Push);

DecalData LoadDecal(uint buffer, uint index) {
    RWByteAddressBuffer b = GetBuffer(buffer);
    uint o = index * 208;
    DecalData d;
    d.model = LoadMatrix(b, o);
    d.invModel = LoadMatrix(b, o + 64);
    d.color = b.Load<float4>(o + 128);
    d.tilingOffset = b.Load<float4>(o + 144);
    d.ids = b.Load<uint4>(o + 160);
    d.params = b.Load<float4>(o + 176);
    d.extra = b.Load<float4>(o + 192);
    return d;
}

struct DecalVSOut { float4 position : SV_Position; };
#endif
