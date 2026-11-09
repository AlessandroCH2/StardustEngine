// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: AW").
// The first pass uses a Karis average to suppress fireflies from very bright pixels.
#include "Common.hlsli"

struct Push {
    uint src;
    uint dst;
    uint2 dstSize;
    uint firstPass;
};
PUSH_CONSTANTS(Push, g_Push);

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float KarisWeight(float3 c) { return 1.0 / (1.0 + Luma(c)); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.dstSize)) return;
    Texture2D t = GetTex2D(g_Push.src);
    SamplerState s = GetSampler(SAMPLER_LINEAR_CLAMP);
    uint sw, sh;
    t.GetDimensions(sw, sh);
    float2 texel = 1.0 / float2(sw, sh);
    float2 uv = (id.xy + 0.5) / float2(g_Push.dstSize);

    float3 a = t.SampleLevel(s, uv + texel * float2(-2, -2), 0).rgb;
    float3 b = t.SampleLevel(s, uv + texel * float2( 0, -2), 0).rgb;
    float3 c = t.SampleLevel(s, uv + texel * float2( 2, -2), 0).rgb;
    float3 d = t.SampleLevel(s, uv + texel * float2(-2,  0), 0).rgb;
    float3 e = t.SampleLevel(s, uv, 0).rgb;
    float3 f = t.SampleLevel(s, uv + texel * float2( 2,  0), 0).rgb;
    float3 g = t.SampleLevel(s, uv + texel * float2(-2,  2), 0).rgb;
    float3 h = t.SampleLevel(s, uv + texel * float2( 0,  2), 0).rgb;
    float3 i = t.SampleLevel(s, uv + texel * float2( 2,  2), 0).rgb;
    float3 j = t.SampleLevel(s, uv + texel * float2(-1, -1), 0).rgb;
    float3 k = t.SampleLevel(s, uv + texel * float2( 1, -1), 0).rgb;
    float3 l = t.SampleLevel(s, uv + texel * float2(-1,  1), 0).rgb;
    float3 m = t.SampleLevel(s, uv + texel * float2( 1,  1), 0).rgb;

    float3 result;
    if (g_Push.firstPass != 0) {
        float3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25;
        float3 g2 = (d + e + g + h) * 0.25, g3 = (e + f + h + i) * 0.25, g4 = (j + k + l + m) * 0.25;
        float w0 = KarisWeight(g0), w1 = KarisWeight(g1), w2 = KarisWeight(g2), w3 = KarisWeight(g3), w4 = KarisWeight(g4);
        result = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5)
               / (w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
    } else {
        result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    GetRWTex2D(g_Push.dst)[id.xy] = float4(max(result, 0.0), 1.0);
}
