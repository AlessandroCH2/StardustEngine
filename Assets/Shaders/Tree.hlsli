#ifndef TREE_HLSLI
#define TREE_HLSLI
#include "Common.hlsli"

// Must match Renderer/Nature/TreeRenderer.cpp :: TreePush
struct TreePush {
    uint frameBuffer;
    uint vertexBuffer;
    uint instanceBuffer;  // 32 bytes per tree: float4(world pos, rotation), float4(scale, hash, 0, 0)
    uint instanceBase;
    uint leafTexture;
    uint entityId;
    uint cascade;
    uint isLeaf;
    float treeHeight;
    float3 pad;
};
PUSH_CONSTANTS(TreePush, g_Push);

struct TreeVertexOut {
    float3 world;
    float3 normal;
    float2 uv;
    float heightRatio;
    float hash;
};

// Vertex.tangent carries wind data: x = height ratio, y = flexibility, z = random phase
TreeVertexOut TransformTreeVertex(FrameData f, uint vid, uint iid) {
    Vertex v = LoadVertex(g_Push.vertexBuffer, vid);
    RWByteAddressBuffer ib = GetBuffer(g_Push.instanceBuffer);
    uint o = (g_Push.instanceBase + iid) * 32;
    float4 i0 = ib.Load<float4>(o);
    float4 i1 = ib.Load<float4>(o + 16);
    float s = i1.x, hash = i1.y;
    float c = cos(i0.w), sn = sin(i0.w);

    float3 p = v.position * s;
    float3 rp = float3(p.x * c + p.z * sn, p.y, -p.x * sn + p.z * c);
    float3 n = float3(v.normal.x * c + v.normal.z * sn, v.normal.y, -v.normal.x * sn + v.normal.z * c);

    // Whole-tree sway (quadratic with height) + leaf flutter
    float2 windDir = normalize(f.windParams.xy + 1e-4);
    float strength = f.windParams.z;
    float h = v.tangent.x;
    float sway = strength * (0.6 + 0.4 * sin(f.time * 0.8 + hash * 6.2831)) * h * h * g_Push.treeHeight * s * 0.035;
    rp += float3(windDir.x, 0, windDir.y) * sway;
    float flutter = sin(f.time * (4.0 + v.tangent.z * 3.0) + v.tangent.z * 31.0 + hash * 17.0) * 0.05 * v.tangent.y * (0.3 + strength);
    rp += n * flutter;

    TreeVertexOut r;
    r.world = i0.xyz + rp;
    r.normal = n;
    r.uv = v.uv;
    r.heightRatio = h;
    r.hash = hash;
    return r;
}

float LeafAlpha(float2 uv) {
    Texture2D t = GetTex2D(g_Push.leafTexture);
    SamplerState s = GetSampler(SAMPLER_LINEAR_WRAP);
    float a = t.Sample(s, uv).a;
    // keep coverage in distant mips (otherwise foliage thins out with distance)
    a *= 1.0 + max(0.0, t.CalculateLevelOfDetail(s, uv)) * 0.3;
    return a;
}

#endif
