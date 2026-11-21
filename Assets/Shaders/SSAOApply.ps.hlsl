// Multiplies the opaque image by the ambient occlusion (multiply blending), before transparent objects
#include "Common.hlsli"

struct Push { uint ao, depth; float intensity, directLight; };
PUSH_CONSTANTS(Push, g_Push);

struct VSOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };

float4 main(VSOut i) : SV_Target0 {
    Texture2D depthTex = GetTex2D(g_Push.depth);
    if (depthTex.Load(int3(int2(i.position.xy), 0)).r <= 0.0) return 1;
    float ao = GetTex2D(g_Push.ao).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), i.uv, 0).r;
    // 0.5 = the occlusion as computed, 1 = squared (stronger corners)
    return float4(pow(saturate(ao), g_Push.intensity * 2.0).xxx, 1);
}
