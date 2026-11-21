// Reflection probe capture: copies one rendered view (HDR) into a face of the probe's cube map (one mip).
// Texels are clamped to 64: the sun disc would otherwise turn into sparkles in the filtered mips.
// Each cube texel is turned into its direction and projected into the captured view, so the
// capture camera only needs to look roughly along the face (its field of view is a little wider than 90°).
#include "Common.hlsli"

struct Push {
    uint src, dst, face, size;
    float4x4 viewProj; // capture camera at the origin (rotation only)
};
PUSH_CONSTANTS(Push, g_Push);

float3 CubeDirection(uint face, float2 uv) {
    float a = uv.x * 2 - 1, b = uv.y * 2 - 1;
    switch (face) {
    case 0: return float3(1, -b, -a);
    case 1: return float3(-1, -b, a);
    case 2: return float3(a, 1, b);
    case 3: return float3(a, -1, -b);
    case 4: return float3(a, -b, 1);
    default: return float3(-a, -b, -1);
    }
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    // Smaller mips average several points of the texel's area
    uint taps = g_Push.size >= 64 ? 1 : 4;
    float3 c = 0;
    for (uint ty = 0; ty < taps; ++ty)
        for (uint tx = 0; tx < taps; ++tx) {
            float2 cell = id.xy + (float2(tx, ty) + 0.5) / taps;
            float3 dir = normalize(CubeDirection(g_Push.face, cell / g_Push.size));
            float4 clip = mul(g_Push.viewProj, float4(dir, 1));
            float2 ndc = clip.xy / max(clip.w, 1e-5);
            float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
            c += min(GetTex2D(g_Push.src).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), uv, 0).rgb, 64.0);
        }
    c /= taps * taps;
    GetRWTex2DArray(g_Push.dst)[uint3(id.xy, g_Push.face)] = float4(max(c, 0), 1);
}
