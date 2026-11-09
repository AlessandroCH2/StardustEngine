// Generates the tileable 3D cloud noise once at startup.
// R = Perlin-Worley (base shape), G/B/A = Worley fBm at increasing frequencies (erosion detail).
#include "Common.hlsli"

struct Push { uint output; uint size; };
PUSH_CONSTANTS(Push, g_Push);

uint3 PCG3(uint3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}
float3 Hash33(int3 p) { return float3(PCG3(uint3(p))) * (1.0 / 4294967295.0); }

// Tileable Worley noise: 1 at feature points, 0 far away
float Worley(float3 p, int period) {
    float3 cellP = p * period;
    int3 cell = int3(floor(cellP));
    float3 f = frac(cellP);
    float minD = 1.0;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                int3 o = int3(x, y, z);
                int3 c = (cell + o + period) % period;
                float3 fp = float3(o) + Hash33(c) - f;
                minD = min(minD, dot(fp, fp));
            }
    return 1.0 - sqrt(minD);
}

float3 Grad(int3 c, int period) {
    c = (c % period + period) % period;
    return normalize(Hash33(c + 1013) * 2.0 - 1.0);
}

// Tileable gradient (Perlin) noise in [-1, 1]
float Perlin(float3 p, int period) {
    float3 q = p * period;
    int3 i = int3(floor(q));
    float3 f = frac(q);
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float n000 = dot(Grad(i + int3(0, 0, 0), period), f - float3(0, 0, 0));
    float n100 = dot(Grad(i + int3(1, 0, 0), period), f - float3(1, 0, 0));
    float n010 = dot(Grad(i + int3(0, 1, 0), period), f - float3(0, 1, 0));
    float n110 = dot(Grad(i + int3(1, 1, 0), period), f - float3(1, 1, 0));
    float n001 = dot(Grad(i + int3(0, 0, 1), period), f - float3(0, 0, 1));
    float n101 = dot(Grad(i + int3(1, 0, 1), period), f - float3(1, 0, 1));
    float n011 = dot(Grad(i + int3(0, 1, 1), period), f - float3(0, 1, 1));
    float n111 = dot(Grad(i + int3(1, 1, 1), period), f - float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

float WorleyFbm(float3 p, int freq) {
    return Worley(p, freq) * 0.625 + Worley(p, freq * 2) * 0.25 + Worley(p, freq * 4) * 0.125;
}

float Remap(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id >= g_Push.size)) return;
    float3 p = (id + 0.5) / g_Push.size;

    float perlin = 0.0, amp = 1.0, norm = 0.0;
    int freq = 4;
    for (int o = 0; o < 4; ++o) { perlin += Perlin(p, freq) * amp; norm += amp; amp *= 0.5; freq *= 2; }
    perlin = perlin / norm * 0.5 + 0.5;
    float worley = WorleyFbm(p, 4);
    float perlinWorley = saturate(Remap(perlin, worley - 1.0, 1.0, 0.0, 1.0));

    float4 v = float4(perlinWorley, WorleyFbm(p, 4), WorleyFbm(p, 8), WorleyFbm(p, 16));
    GetRWTex3D(g_Push.output)[id] = v;
}
