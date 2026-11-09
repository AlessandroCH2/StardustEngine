#ifndef MESH_COMMON_HLSLI
#define MESH_COMMON_HLSLI
#include "Common.hlsli"

// Per-draw data written by SceneRenderer into a storage buffer (144 bytes, must match DrawDataGPU in C++)
struct DrawData {
    float4x4 model;
    float4 color;
    float4 params;        // x roughness, y metallic, z normal strength, w alpha cutoff
    float4 emissive;      // rgb emissive (already * intensity), w toon steps
    float4 tilingOffset;  // xy tiling, zw offset
    uint4 sampling;
    uint4  ids;           // x albedo SRV, y normal SRV, z entity id, w rim strength (float bits)
};

DrawData LoadDraw(uint buffer, uint index) {
    RWByteAddressBuffer b = GetBuffer(buffer);
    uint o = index * 144;
    DrawData d;
    d.model = LoadMatrix(b, o);
    d.color = b.Load<float4>(o + 64);
    d.params = b.Load<float4>(o + 80);
    d.emissive = b.Load<float4>(o + 96);
    d.tilingOffset = b.Load<float4>(o + 112);
    d.ids = b.Load<uint4>(o + 128);
    d.sampling=uint4(d.ids.w&7,(d.ids.w>>3)&7,0,0);d.ids.w&=~63u;
    return d;
}

struct MeshPush {
    uint frameBuffer;
    uint drawBuffer;
    uint drawIndex;
    uint vertexBuffer;
    uint cascade;
    uint materialBuffer;
    uint materialOffset;
    uint materialCount;
};
PUSH_CONSTANTS(MeshPush, g_Push);
// Slots follow the order in <shader>.spv.properties.json. Missing slots use fallback.
float4 MaterialProperty(uint slot,float4 fallback){if(slot>=g_Push.materialCount)return fallback;return GetBuffer(g_Push.materialBuffer).Load<float4>((g_Push.materialOffset+slot)*16);}
float MaterialFloat(uint slot,float fallback){return MaterialProperty(slot,float4(fallback,0,0,0)).x;}
float4 MaterialTexture(uint slot,float2 uv){float4 p=MaterialProperty(slot,0);return GetTex2D((uint)p.x).Sample(GetSampler((uint)p.y),uv);}


struct MeshVSOut {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 uv       : TEXCOORD0;
};

struct MeshPSOut {
    float4 color    : SV_Target0;
    uint   entityId : SV_Target1;
};

float2 MaterialUV(DrawData d, float2 uv) { return uv * d.tilingOffset.xy + d.tilingOffset.zw; }

float4 SampleAlbedo(DrawData d, float2 uv) {
    return d.color * GetTex2D(d.ids.x).Sample(GetSampler(d.sampling.x), MaterialUV(d, uv));
}

// Tangent-space normal mapping
float3 SampleNormal(DrawData d, MeshVSOut i, bool frontFace) {
    float3 N = normalize(i.normal) * (frontFace ? 1.0 : -1.0);
    float3 T = normalize(i.tangent.xyz - N * dot(N, i.tangent.xyz));
    float3 B = cross(N, T) * i.tangent.w;
    float3 tn = GetTex2D(d.ids.y).Sample(GetSampler(d.sampling.y), MaterialUV(d, i.uv)).xyz * 2.0 - 1.0;
    tn.xy *= d.params.z;
    return normalize(T * tn.x + B * tn.y + N * max(tn.z, 1e-3));
}

#endif
