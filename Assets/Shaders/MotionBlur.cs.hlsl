// Camera motion blur: each pixel's movement since the last frame (from the depth buffer and the two
// view-projections) blurs it along that direction.
#include "Common.hlsli"

struct Push {
    uint frame, src, depth, dst;
    uint2 size; float intensity; uint samples;
};
PUSH_CONSTANTS(Push, g_Push);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    FrameData f = LoadFrame(g_Push.frame);
    Texture2D src = GetTex2D(g_Push.src);
    float2 texel = 1.0 / g_Push.size;
    float2 uv = (id.xy + 0.5) * texel;
    float d = GetTex2D(g_Push.depth).Load(int3(id.xy, 0)).r;
    float4 clip = mul(f.prevViewProj, mul(f.invViewProj, float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1)));
    float3 center = src.Load(int3(id.xy, 0)).rgb;
    if (clip.w <= 0) { GetRWTex2D(g_Push.dst)[id.xy] = float4(center, 1); return; }
    float2 prevUV = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
    float2 velocity = (uv - prevUV) * g_Push.intensity;
    float len = length(velocity);
    const float maxLen = 0.04;
    if (len > maxLen) velocity *= maxLen / len;
    if (length(velocity * g_Push.size) < 0.5) { GetRWTex2D(g_Push.dst)[id.xy] = float4(center, 1); return; }
    uint count = max(g_Push.samples, 4u);
    float noise = frac(52.9829189 * frac(dot(float2(id.xy), float2(0.06711056, 0.00583715))));
    float3 sum = 0;
    for (uint i = 0; i < count; ++i) {
        float t = (i + noise) / count - 0.5;
        sum += src.SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), uv + velocity * t, 0).rgb;
    }
    GetRWTex2D(g_Push.dst)[id.xy] = float4(sum / count, 1);
}
