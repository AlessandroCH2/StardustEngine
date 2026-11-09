#ifndef SHADOWS_HLSLI
#define SHADOWS_HLSLI
#include "Common.hlsli"

// Cascaded shadow map lookup with normal-offset bias, 3x3 bilinear PCF (= smooth 4x4 footprint)
// and a soft cross-fade between consecutive cascades.
float SampleCascade(FrameData f, uint frameBuffer, uint c, float3 worldPos, float3 N, float NoL) {
    float texel = f.cascadeTexelSize[c];
    // offset more at grazing angles where acne appears
    float3 p = worldPos + N * texel * (1.0 + 2.0 * (1.0 - NoL));
    float4 sp = mul(LoadCascadeMatrix(frameBuffer, c), float4(p, 1.0));
    float2 uv = float2(sp.x * 0.5 + 0.5, 0.5 - sp.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;

    Texture2DArray sm = GetTex2DArray(f.shadowInfo.x);
    SamplerComparisonState cmp = GetCmpSampler(SAMPLER_SHADOW);
    float2 ts = 1.0 / float(f.shadowInfo.w);
    float sum = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            sum += sm.SampleCmpLevelZero(cmp, float3(uv + float2(x, y) * ts, c), sp.z);
    return sum / 9.0;
}

float SampleShadow(FrameData f, uint frameBuffer, float3 worldPos, float3 N, float NoL) {
    if (f.shadowInfo.z == 0) return 1.0;
    float viewDepth = -mul(f.view, float4(worldPos, 1.0)).z;
    uint count = f.shadowInfo.y;
    uint c = 0;
    [loop] for (; c < count; ++c)
        if (viewDepth < f.cascadeSplits[c]) break;
    if (c >= count) return 1.0;

    float shadow = SampleCascade(f, frameBuffer, c, worldPos, N, NoL);

    // Fade into the next cascade over the last 10% of this one, and out to "lit" at the shadow distance
    float start = c == 0 ? 0.0 : f.cascadeSplits[c - 1];
    float end = f.cascadeSplits[c];
    float t = saturate((viewDepth - lerp(start, end, 0.9)) / (end - lerp(start, end, 0.9)));
    if (t > 0.0) {
        float next = (c + 1 < count) ? SampleCascade(f, frameBuffer, c + 1, worldPos, N, NoL) : 1.0;
        shadow = lerp(shadow, next, t);
    }
    return lerp(1.0,shadow,saturate(f.pad0));
}

// Single-tap (hardware bilinear PCF) lookup for dense geometry such as grass
float SampleShadowFast(FrameData f, uint frameBuffer, float3 worldPos) {
    if (f.shadowInfo.z == 0) return 1.0;
    float viewDepth = -mul(f.view, float4(worldPos, 1.0)).z;
    uint c = 0;
    [loop] for (; c < f.shadowInfo.y; ++c)
        if (viewDepth < f.cascadeSplits[c]) break;
    if (c >= f.shadowInfo.y) return 1.0;
    float4 sp = mul(LoadCascadeMatrix(frameBuffer, c), float4(worldPos, 1.0));
    float2 uv = float2(sp.x * 0.5 + 0.5, 0.5 - sp.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    // small constant bias: grass blades are thin and self-shadowing looks like noise
    return lerp(1.0,GetTex2DArray(f.shadowInfo.x).SampleCmpLevelZero(GetCmpSampler(SAMPLER_SHADOW), float3(uv, c), sp.z + 0.0005),saturate(f.pad0));
}

#endif
