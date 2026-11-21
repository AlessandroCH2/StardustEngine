// Screen-space ambient occlusion at half resolution, from the depth buffer only.
// Positions are rebuilt from depth, the normal from the neighbouring depths; samples in a
// hemisphere of `radius` metres darken the point when the depth buffer is in front of them.
#include "Common.hlsli"

struct Push {
    uint frame, depth, dst, samples;
    uint2 size; float radius; float bias;
};
PUSH_CONSTANTS(Push, g_Push);

float3 WorldAt(FrameData f, float2 uv, float d) {
    float4 p = mul(f.invViewProj, float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1));
    return p.xyz / p.w;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    FrameData f = LoadFrame(g_Push.frame);
    Texture2D depthTex = GetTex2D(g_Push.depth);
    uint w, h; depthTex.GetDimensions(w, h);
    float2 texel = 1.0 / float2(w, h);
    float2 uv = (id.xy + 0.5) / g_Push.size;
    int2 px = int2(uv * float2(w, h));
    float d = depthTex.Load(int3(px, 0)).r;
    if (d <= 0.0) { GetRWTex2D(g_Push.dst)[id.xy] = 1; return; } // sky
    float3 P = WorldAt(f, uv, d);
    // Normal: the smaller of the two differences on each axis avoids normals across edges
    float dl = depthTex.Load(int3(px - int2(1, 0), 0)).r, dr = depthTex.Load(int3(px + int2(1, 0), 0)).r;
    float du = depthTex.Load(int3(px - int2(0, 1), 0)).r, dd = depthTex.Load(int3(px + int2(0, 1), 0)).r;
    float3 Pl = WorldAt(f, uv - float2(texel.x, 0), dl), Pr = WorldAt(f, uv + float2(texel.x, 0), dr);
    float3 Pu = WorldAt(f, uv - float2(0, texel.y), du), Pd = WorldAt(f, uv + float2(0, texel.y), dd);
    float3 dx = abs(dl - d) < abs(dr - d) ? P - Pl : Pr - P;
    float3 dy = abs(du - d) < abs(dd - d) ? P - Pu : Pd - P;
    float3 N = normalize(cross(dy, dx));
    float3 toCam = f.cameraPos - P;
    if (dot(N, toCam) < 0) N = -N;
    float viewDepth = -mul(f.view, float4(P, 1)).z;
    float radius = g_Push.radius;
    // Interleaved gradient noise turns the sample pattern per pixel (removed by the blur)
    float noise = frac(52.9829189 * frac(dot(float2(id.xy) + f.effectsInfo.z * 5.588238, float2(0.06711056, 0.00583715))));
    float3 up = abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N)), B = cross(N, T);
    float occlusion = 0;
    uint count = max(g_Push.samples, 4u);
    for (uint i = 0; i < count; ++i) {
        float a = (i + noise) * 2.39996323; // golden angle
        float r = sqrt((i + 0.5) / count);
        float z = sqrt(max(0.0, 1 - r * r));
        float scale = lerp(0.15, 1.0, frac(i * 0.618034 + noise));
        float3 s = P + (T * (cos(a) * r) + B * (sin(a) * r) + N * z) * radius * scale + N * g_Push.bias;
        float4 clip = mul(f.viewProj, float4(s, 1));
        if (clip.w <= 0) continue;
        float2 suv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
        if (any(suv < 0) || any(suv > 1)) continue;
        float sd = depthTex.Load(int3(int2(suv * float2(w, h)), 0)).r;
        float sampleDepth = -mul(f.view, float4(s, 1)).z;
        float sceneDepth = -mul(f.view, float4(WorldAt(f, suv, sd), 1)).z;
        float range = smoothstep(0.0, 1.0, radius / max(abs(viewDepth - sceneDepth), 1e-3));
        occlusion += (sceneDepth < sampleDepth - 0.02 ? 1.0 : 0.0) * range;
    }
    float ao = 1 - occlusion / count;
    // Far away, the effect fades (it would only flicker)
    ao = lerp(ao, 1, saturate((viewDepth - 150) / 100));
    GetRWTex2D(g_Push.dst)[id.xy] = ao;
}
