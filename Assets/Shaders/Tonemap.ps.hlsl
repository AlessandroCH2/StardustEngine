#include "Common.hlsli"

struct Push {
    uint hdrTexture;
    uint bloomTexture;
    float exposure;
    float bloomIntensity;
    float contrast;
    float saturation;
    float vignette;
    float pad;
};
PUSH_CONSTANTS(Push, g_Push);

// ACES filmic curve (Stephen Hill's fit of the RRT+ODT)
static const float3x3 ACESInput = {
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
};
static const float3x3 ACESOutput = {
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602
};
float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}
float3 ACES(float3 c) {
    c = mul(ACESInput, c);
    c = RRTAndODTFit(c);
    return saturate(mul(ACESOutput, c));
}

float3 LinearToSRGB(float3 c) {
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

// Output alpha carries luma for FXAA
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    SamplerState s = GetSampler(SAMPLER_LINEAR_CLAMP);
    float3 hdr = GetTex2D(g_Push.hdrTexture).SampleLevel(GetSampler(SAMPLER_POINT_CLAMP), uv, 0).rgb;
    float3 bloom = GetTex2D(g_Push.bloomTexture).SampleLevel(s, uv, 0).rgb;
    float3 c = lerp(hdr, bloom, g_Push.bloomIntensity) * g_Push.exposure;
    c = ACES(c);

    // Light grading in display space
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(luma.xxx, c, g_Push.saturation);
    c = saturate((c - 0.5) * g_Push.contrast + 0.5);
    float2 d = uv - 0.5;
    c *= 1.0 - dot(d, d) * g_Push.vignette;

    float3 srgb = LinearToSRGB(c);
    // tiny dither against banding in the sky gradient
    srgb += (IGN(pos.xy) - 0.5) / 255.0;
    return float4(srgb, dot(srgb, float3(0.299, 0.587, 0.114)));
}
