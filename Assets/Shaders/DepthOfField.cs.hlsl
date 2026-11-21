// Depth of field: what is nearer or farther than the focus distance is blurred over a disc whose size
// (circle of confusion) grows with the distance from the focus. Gather over a golden-angle spiral.
#include "Common.hlsli"

struct Push {
    uint frame, src, depth, dst;
    uint2 size; float focusDistance, focusRange;
    float maxRadius; uint samples; float2 pad;
};
PUSH_CONSTANTS(Push, g_Push);

float Distance(FrameData f, float d, float2 uv) {
    float4 p = mul(f.invViewProj, float4(uv.x * 2 - 1, 1 - uv.y * 2, max(d, 1e-7), 1));
    return length(p.xyz / p.w - f.cameraPos);
}
// Signed: negative in front of the focus, positive behind; in pixels
float CoC(FrameData f, float d, float2 uv) {
    float dist = d <= 0 ? 1e5 : Distance(f, d, uv);
    return clamp((dist - g_Push.focusDistance) / max(g_Push.focusRange, 1e-3), -1, 1) * g_Push.maxRadius;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    FrameData f = LoadFrame(g_Push.frame);
    Texture2D src = GetTex2D(g_Push.src), depthTex = GetTex2D(g_Push.depth);
    float2 texel = 1.0 / g_Push.size;
    float2 uv = (id.xy + 0.5) * texel;
    float centerCoC = CoC(f, depthTex.Load(int3(id.xy, 0)).r, uv);
    float3 center = src.Load(int3(id.xy, 0)).rgb;
    float radius = abs(centerCoC);
    uint count = max(g_Push.samples, 8u);
    float3 sum = center; float weight = 1;
    // Search as far as the largest blur, so out-of-focus foreground also spreads over sharp pixels
    float search = g_Push.maxRadius;
    for (uint i = 0; i < count; ++i) {
        float r = sqrt((i + 0.5) / count) * search;
        float a = i * 2.39996323;
        float2 o = float2(cos(a), sin(a)) * r;
        float2 suv = uv + o * texel;
        int2 at = clamp(int2(suv * g_Push.size), int2(0, 0), int2(g_Push.size) - 1);
        float sCoC = CoC(f, depthTex.Load(int3(at, 0)).r, suv);
        // A sample contributes when its own blur reaches this pixel; background behind a sharp
        // pixel does not bleed onto it
        float reach = sCoC < centerCoC ? abs(sCoC) : min(abs(sCoC), radius);
        float w = saturate(reach - r + 1);
        sum += src.SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), suv, 0).rgb * w;
        weight += w;
    }
    GetRWTex2D(g_Push.dst)[id.xy] = float4(sum / weight, 1);
}
