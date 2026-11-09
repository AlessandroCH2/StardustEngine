#include "Atmosphere.hlsli"

struct Push { uint output; uint2 size; };
PUSH_CONSTANTS(Push, g_Push);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    float2 uv = (id.xy + 0.5) / float2(g_Push.size);
    float mu = uv.x * 2.0 - 1.0;
    float h = uv.y * (kTopRadius - kGroundRadius);
    float3 ro = float3(0, kGroundRadius + h, 0);
    float3 rd = float3(sqrt(saturate(1.0 - mu * mu)), mu, 0);

    float3 T = 0.0;
    if (RaySphere(ro, rd, kGroundRadius) < 0.0) {
        float tMax = RaySphere(ro, rd, kTopRadius);
        const int steps = 40;
        float dt = tMax / steps;
        float3 optical = 0.0;
        for (int i = 0; i < steps; ++i) {
            float3 p = ro + rd * (i + 0.5) * dt;
            optical += SampleMedium(length(p) - kGroundRadius).extinction * dt;
        }
        T = exp(-optical);
    }
    GetRWTex2D(g_Push.output)[id.xy] = float4(T, 1.0);
}
