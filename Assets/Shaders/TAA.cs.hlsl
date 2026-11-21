// Temporal anti-aliasing: the scene is rendered with a different sub-pixel offset every frame and
// blended with the previous result, reprojected through the depth buffer (camera motion). The history
// is kept within the colours around the pixel this frame, which limits ghosting on moving objects.
#include "Common.hlsli"

struct Push {
    uint frame, current, history, depth;
    uint dst; uint2 size; uint reset;
    float blend; float3 pad;
};
PUSH_CONSTANTS(Push, g_Push);

float3 ToYCoCg(float3 c) { return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25))); }
float3 FromYCoCg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
// Compress very bright values so a single bright pixel does not dominate the blend
float3 Tonemap(float3 c) { return c / (1 + max(c.r, max(c.g, c.b))); }
float3 Untonemap(float3 c) { return c / max(1 - max(c.r, max(c.g, c.b)), 1e-4); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    FrameData f = LoadFrame(g_Push.frame);
    Texture2D cur = GetTex2D(g_Push.current);
    SamplerState linearClamp = GetSampler(SAMPLER_LINEAR_CLAMP);
    float2 texel = 1.0 / g_Push.size;
    float2 uv = (id.xy + 0.5) * texel;
    float3 center = Tonemap(cur.Load(int3(id.xy, 0)).rgb);
    if (g_Push.reset != 0) { GetRWTex2D(g_Push.dst)[id.xy] = float4(Untonemap(center), 1); return; }

    // Neighbourhood (3x3) in YCoCg, and the closest depth for the motion of thin edges
    float3 mn = 1e9, mx = -1e9, m1 = 0, m2 = 0;
    float closest = 0; int2 closestAt = int2(id.xy);
    Texture2D depthTex = GetTex2D(g_Push.depth);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            int2 at = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(g_Push.size) - 1);
            float3 c = ToYCoCg(Tonemap(cur.Load(int3(at, 0)).rgb));
            mn = min(mn, c); mx = max(mx, c); m1 += c; m2 += c * c;
            float d = depthTex.Load(int3(at, 0)).r;
            if (d > closest) { closest = d; closestAt = at; } // reverse-Z: bigger is closer
        }
    // Variance clipping tightens the box
    float3 mean = m1 / 9, sigma = sqrt(max(m2 / 9 - mean * mean, 0));
    mn = max(mn, mean - sigma * 1.25); mx = min(mx, mean + sigma * 1.25);

    float2 duv = (closestAt + 0.5) * texel;
    float4 clip = mul(f.prevViewProj, mul(f.invViewProj, float4(duv.x * 2 - 1, 1 - duv.y * 2, closest, 1)));
    float2 prevUV = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5) + (uv - duv);
    // The history holds the image without jitter: remove last frame's offset
    prevUV -= f.jitter.zw * float2(0.5, -0.5);
    float3 result = center;
    if (all(prevUV > 0) && all(prevUV < 1) && clip.w > 0) {
        float3 history = Tonemap(GetTex2D(g_Push.history).SampleLevel(linearClamp, prevUV, 0).rgb);
        float3 h = ToYCoCg(history);
        h = clamp(h, mn, mx);
        float motion = length((prevUV - uv) * g_Push.size);
        float alpha = lerp(g_Push.blend, 0.25, saturate(motion / 24));
        result = lerp(FromYCoCg(h), center, alpha);
    }
    GetRWTex2D(g_Push.dst)[id.xy] = float4(Untonemap(max(result, 0)), 1);
}
