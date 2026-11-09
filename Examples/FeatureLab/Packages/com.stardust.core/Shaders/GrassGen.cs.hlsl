// GPU grass placement: one thread per cell of a camera-centred grid. Each surviving blade is appended to the
// instance buffer and counted in the indirect draw arguments (instanceCount at byte offset 4).
#include "Common.hlsli"

struct Push {
    uint frameBuffer;
    uint instanceBuffer;
    uint argsBuffer;
    uint heightmap;
    float3 terrainOrigin;
    float terrainSize;
    float2 gridOrigin;     // world xz of cell (0,0), snapped to the cell size so blades do not swim
    float spacing;
    uint gridDim;
    float radius;
    float bladeHeight;
    float flowerAmount;
    uint capacity;
    uint splatmap;   // RGBA layer weights, R = grass
    uint3 pad;
};
PUSH_CONSTANTS(Push, g_Push);

float SampleTerrainHeight(float2 local, out float slope) {
    Texture2D hm = GetTex2D(g_Push.heightmap);
    uint w, h;
    hm.GetDimensions(w, h);
    float res = float(w);
    float2 uv = (local / g_Push.terrainSize + 0.5) * (res - 1.0) / res + 0.5 / res;
    SamplerState s = GetSampler(SAMPLER_LINEAR_CLAMP);
    float e = 1.0 / res;
    float hc = hm.SampleLevel(s, uv, 0).r;
    float hx = hm.SampleLevel(s, uv + float2(e, 0), 0).r;
    float hz = hm.SampleLevel(s, uv + float2(0, e), 0).r;
    float texel = g_Push.terrainSize / (res - 1.0);
    float3 n = normalize(float3(hc - hx, texel, hc - hz));
    slope = 1.0 - n.y;
    return hc;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.gridDim)) return;
    FrameData f = LoadFrame(g_Push.frameBuffer);

    float2 cell = g_Push.gridOrigin + float2(id.xy) * g_Push.spacing;
    float2 cellKey = floor(cell / g_Push.spacing + 0.5);
    float2 jitter = Hash22(cellKey) - 0.5;
    float2 xz = cell + jitter * g_Push.spacing;

    float2 toCam = xz - f.cameraPos.xz;
    float dist = length(toCam);
    if (dist > g_Push.radius) return;

    // Density falls off with distance; far blades get wider to keep the coverage
    float keep = Hash12(cellKey + 71.3);
    float d01 = dist / g_Push.radius;
    float density = saturate(1.0 - d01 * d01) * (d01 < 0.15 ? 1.0 : 0.85);
    // patchy meadow: some areas sparser
    float clump = Fbm(xz * 0.08, 3);
    density *= lerp(0.55, 1.0, smoothstep(0.25, 0.6, clump));
    if (keep > density) return;

    float2 local = xz - g_Push.terrainOrigin.xz;
    if (any(abs(local) > g_Push.terrainSize * 0.5)) return;
    float slope;
    float y = SampleTerrainHeight(local, slope) + g_Push.terrainOrigin.y;
    if (slope > 0.45) return;
    // Grass only grows where the grass layer is painted
    {
        uint w, h;
        GetTex2D(g_Push.splatmap).GetDimensions(w, h);
        float2 suv = (local / g_Push.terrainSize + 0.5) * (h - 1.0) / h + 0.5 / h;suv.x/=4.0;
        float grassWeight = GetTex2D(g_Push.splatmap).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), suv, 0).r;
        if (Hash12(cellKey + 211.7) > smoothstep(0.25, 0.75, grassWeight)) return;
    }

    float rnd = Hash12(cellKey + 13.7);
    float tall = smoothstep(0.55, 0.85, Fbm(xz * 0.12 + 50.0, 3)); // clumps of tall grass
    float height = g_Push.bladeHeight * (0.45 + 0.75 * rnd) * (1.0 + tall * 1.1) * (1.0 - slope);
    float width = 0.035 * (0.7 + 0.6 * Hash12(cellKey + 3.1)) * (1.0 + d01 * 2.5);
    float3 center = float3(xz.x, y + height * 0.5, xz.y);

    // Frustum culling against the 4 side planes
    float4x4 vp = f.viewProj;
    float4 r0 = float4(vp[0]), r1 = float4(vp[1]), r3 = float4(vp[3]);
    float4 planes[4] = { r3 + r0, r3 - r0, r3 + r1, r3 - r1 };
    [unroll] for (int i = 0; i < 4; ++i) {
        float3 n = planes[i].xyz;
        if (dot(n, center) + planes[i].w < -height * length(n)) return;
    }

    uint type = Hash12(cellKey + 5.9) < g_Push.flowerAmount * (1.0 - tall) ? 1u : 0u;
    if (type == 1) height *= 0.8;

    RWByteAddressBuffer args = GetBuffer(g_Push.argsBuffer);
    uint index;
    args.InterlockedAdd(4, 1, index);
    if (index >= g_Push.capacity) { args.InterlockedAdd(4, 0xFFFFFFFFu); return; } // undo: final count == capacity
    float rotation = Hash12(cellKey + 91.1) * 6.2831853;
    float bend = 0.15 + 0.35 * Hash12(cellKey + 27.3);
    RWByteAddressBuffer inst = GetBuffer(g_Push.instanceBuffer);
    inst.Store<float4>(index * 32, float4(xz.x, y, xz.y, rotation));
    inst.Store<float4>(index * 32 + 16, float4(height, width, float(type) + rnd * 0.5, bend));
}
