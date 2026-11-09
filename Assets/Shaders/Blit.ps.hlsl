#include "Bindless.hlsli"

struct Push { uint source; };
PUSH_CONSTANTS(Push, g_Push);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return GetTex2D(g_Push.source).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), uv, 0);
}
