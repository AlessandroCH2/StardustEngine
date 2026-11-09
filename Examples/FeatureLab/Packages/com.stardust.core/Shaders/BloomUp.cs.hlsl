// 3x3 tent upsample of the lower mip added onto the current downsampled level
#include "Common.hlsli"

struct Push {
    uint low;
    uint current;
    uint dst;
    float radius;
    uint2 dstSize;
};
PUSH_CONSTANTS(Push, g_Push);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.dstSize)) return;
    Texture2D lowT = GetTex2D(g_Push.low);
    SamplerState s = GetSampler(SAMPLER_LINEAR_CLAMP);
    uint w, h;
    lowT.GetDimensions(w, h);
    float2 o = g_Push.radius / float2(w, h);
    float2 uv = (id.xy + 0.5) / float2(g_Push.dstSize);

    float3 sum = lowT.SampleLevel(s, uv, 0).rgb * 4.0;
    sum += (lowT.SampleLevel(s, uv + float2(-o.x, 0), 0).rgb + lowT.SampleLevel(s, uv + float2(o.x, 0), 0).rgb +
            lowT.SampleLevel(s, uv + float2(0, -o.y), 0).rgb + lowT.SampleLevel(s, uv + float2(0, o.y), 0).rgb) * 2.0;
    sum += lowT.SampleLevel(s, uv + float2(-o.x, -o.y), 0).rgb + lowT.SampleLevel(s, uv + float2(o.x, -o.y), 0).rgb +
           lowT.SampleLevel(s, uv + float2(-o.x, o.y), 0).rgb + lowT.SampleLevel(s, uv + float2(o.x, o.y), 0).rgb;
    float3 current = GetTex2D(g_Push.current).SampleLevel(s, uv, 0).rgb;
    GetRWTex2D(g_Push.dst)[id.xy] = float4(current + sum / 16.0, 1.0);
}
