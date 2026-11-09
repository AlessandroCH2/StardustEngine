#ifndef COMMON_HLSLI
#define COMMON_HLSLI
#include "Bindless.hlsli"

static const float PI = 3.14159265359;

// Must match Renderer/SceneRenderer.h :: FrameConstants
struct FrameData {
    float4x4 viewProj;
    float4x4 view;
    float4x4 proj;
    float4x4 invViewProj;
    float3 cameraPos;   float time;
    float3 sunDir;      float sunIntensity;   // sunDir points towards the sun
    float3 sunColor;    float exposure;
    float3 skyZenith;   float ambientIntensity;
    float3 skyHorizon;  float pad0;
    float3 groundColor; float pad1;
    float4 viewport;    // w, h, 1/w, 1/h
    float4 cascadeSplits;
    float4 cascadeTexelSize;
    uint4  shadowInfo;  // x = shadow map SRV, y = cascade count, z = enabled, w = resolution
    uint4  atmosphereInfo; // x = transmittance LUT, y = sky-view LUT, z = ambient buffer, w = clouds texture
    float4 fogParams;      // x = fog density (1/m), y = height falloff (1/m), z = cloud coverage, w = cloud density
    float4 windParams;     // xy = direction, z = strength, w = gust frequency
    uint4  lightInfo;      // x = local light buffer, y = count
};

static const uint kFrameCascadeMatrices = 368; // byte offset of cascadeViewProj[4]

float4x4 LoadCascadeMatrix(uint bufferIndex, uint cascade) {
    return LoadMatrix(GetBuffer(bufferIndex), kFrameCascadeMatrices + cascade * 64);
}

FrameData LoadFrame(uint bufferIndex) {
    RWByteAddressBuffer b = GetBuffer(bufferIndex);
    FrameData f;
    f.viewProj    = LoadMatrix(b, 0);
    f.view        = LoadMatrix(b, 64);
    f.proj        = LoadMatrix(b, 128);
    f.invViewProj = LoadMatrix(b, 192);
    float4 v;
    v = b.Load<float4>(256); f.cameraPos = v.xyz;   f.time = v.w;
    v = b.Load<float4>(272); f.sunDir = v.xyz;      f.sunIntensity = v.w;
    v = b.Load<float4>(288); f.sunColor = v.xyz;    f.exposure = v.w;
    v = b.Load<float4>(304); f.skyZenith = v.xyz;   f.ambientIntensity = v.w;
    v = b.Load<float4>(320); f.skyHorizon = v.xyz;  f.pad0 = v.w;
    v = b.Load<float4>(336); f.groundColor = v.xyz; f.pad1 = v.w;
    f.viewport = b.Load<float4>(352);
    f.cascadeSplits = b.Load<float4>(624);
    f.cascadeTexelSize = b.Load<float4>(640);
    f.shadowInfo = b.Load<uint4>(656);
    f.atmosphereInfo = b.Load<uint4>(672);
    f.fogParams = b.Load<float4>(688);
    f.windParams = b.Load<float4>(704);
    f.lightInfo = b.Load<uint4>(720);
    return f;
}

// Mesh vertex layout (48 bytes) - must match Renderer/Mesh.h :: Vertex
struct Vertex {
    float3 position;
    float3 normal;
    float2 uv;
    float4 tangent;
};

Vertex LoadVertex(uint bufferIndex, uint vertexId) {
    RWByteAddressBuffer b = GetBuffer(bufferIndex);
    uint o = vertexId * 48;
    Vertex v;
    v.position = b.Load<float3>(o + 0);
    v.normal   = b.Load<float3>(o + 12);
    v.uv       = b.Load<float2>(o + 24);
    v.tangent  = b.Load<float4>(o + 32);
    return v;
}

// Hash / noise helpers shared by many shaders
float Hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}
float2 Hash22(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}
float ValueNoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash12(i), Hash12(i + float2(1, 0)), u.x), lerp(Hash12(i + float2(0, 1)), Hash12(i + float2(1, 1)), u.x), u.y);
}
float Fbm(float2 p, int octaves) {
    float s = 0, a = 0.5;
    for (int i = 0; i < octaves; ++i) { s += ValueNoise(p) * a; p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}
// Large-scale "dryness" of the meadow, shared by the terrain and the grass so they match
float GroundDryness(float2 xz) {
    float large = Fbm(xz * 0.0025, 3);
    float macro = Fbm(xz * 0.03, 3);
    return saturate(smoothstep(0.45, 0.72, large) * 0.75 + (macro - 0.5) * 0.5);
}

// Interleaved gradient noise (Jimenez) for dithering / ray jitter
float IGN(float2 pixel) { return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715)))); }

#endif
