// Reflection probe filtering: each mip of the filtered cube map is the captured cube convolved with a
// GGX lobe of growing roughness (mip 0 = mirror, last mip = rough, also used as local ambient light).
#include "Common.hlsli"

struct Push {
    uint src, dst, size, samples;
    float roughness; float srcSize; float2 pad;
};
PUSH_CONSTANTS(Push, g_Push);

float3 CubeDirection(uint face, float2 uv) {
    float a = uv.x * 2 - 1, b = uv.y * 2 - 1;
    switch (face) {
    case 0: return float3(1, -b, -a);
    case 1: return float3(-1, -b, a);
    case 2: return float3(a, 1, b);
    case 3: return float3(a, -1, -b);
    case 4: return float3(a, -b, 1);
    default: return float3(-a, -b, -1);
    }
}

float2 Hammersley(uint i, uint n) {
    uint bits = reversebits(i);
    return float2((float)i / n, bits * 2.3283064365386963e-10);
}

float3 ImportanceSampleGGX(float2 xi, float3 N, float a) {
    float phi = 2 * PI * xi.x;
    float cosTheta = sqrt((1 - xi.y) / (1 + (a * a - 1) * xi.y));
    float sinTheta = sqrt(1 - cosTheta * cosTheta);
    float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N)), B = cross(N, T);
    return normalize(T * H.x + B * H.y + N * H.z);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    float3 N = normalize(CubeDirection(id.z, (id.xy + 0.5) / g_Push.size));
    TextureCube src = GetTexCube(g_Push.src);
    SamplerState s = GetSampler(SAMPLER_LINEAR_CLAMP);
    float3 color = 0; float weight = 0;
    float a = max(g_Push.roughness * g_Push.roughness, 0.002);
    // Each sample reads a lower mip of the source when the lobe is wide (filtered importance sampling)
    float texelSolid = 4 * PI / (6 * g_Push.srcSize * g_Push.srcSize);
    for (uint i = 0; i < g_Push.samples; ++i) {
        float3 H = ImportanceSampleGGX(Hammersley(i, g_Push.samples), N, a);
        float3 L = 2 * dot(N, H) * H - N;
        float NoL = dot(N, L);
        if (NoL <= 0) continue;
        float NoH = saturate(dot(N, H));
        float d = (NoH * NoH * (a * a - 1) + 1);
        float D = a * a / (PI * d * d);
        float pdf = D / 4 + 1e-4;
        float sampleSolid = 1.0 / (g_Push.samples * pdf);
        float lod = 0.5 * log2(max(sampleSolid / texelSolid, 1.0));
        color += src.SampleLevel(s, L, lod).rgb * NoL;
        weight += NoL;
    }
    GetRWTex2DArray(g_Push.dst)[uint3(id.xy, id.z)] = float4(color / max(weight, 1e-4), 1);
}
