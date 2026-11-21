// Depth-aware blur of the half resolution ambient occlusion (removes the sampling noise, keeps edges)
#include "Common.hlsli"

struct Push {
    uint frame, src, depth, dst;
    uint2 size; int2 direction;
};
PUSH_CONSTANTS(Push, g_Push);

float LinearDepth(FrameData f, Texture2D depthTex, float2 uv, uint2 full) {
    float d = depthTex.Load(int3(int2(uv * full), 0)).r;
    float4 p = mul(f.invViewProj, float4(uv.x * 2 - 1, 1 - uv.y * 2, max(d, 1e-7), 1));
    return -mul(f.view, float4(p.xyz / p.w, 1)).z;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    FrameData f = LoadFrame(g_Push.frame);
    Texture2D src = GetTex2D(g_Push.src), depthTex = GetTex2D(g_Push.depth);
    uint w, h; depthTex.GetDimensions(w, h);
    float2 uv = (id.xy + 0.5) / g_Push.size;
    float center = LinearDepth(f, depthTex, uv, uint2(w, h));
    float sum = 0, weight = 0;
    for (int i = -4; i <= 4; ++i) {
        int2 at = clamp(int2(id.xy) + g_Push.direction * i, int2(0, 0), int2(g_Push.size) - 1);
        float2 suv = (at + 0.5) / g_Push.size;
        float dz = abs(LinearDepth(f, depthTex, suv, uint2(w, h)) - center) / max(center, 0.1);
        float wgt = exp(-i * i / 8.0) * exp(-dz * 60.0);
        sum += src.Load(int3(at, 0)).r * wgt; weight += wgt;
    }
    GetRWTex2D(g_Push.dst)[id.xy] = sum / max(weight, 1e-4);
}
