#include "MeshCommon.hlsli"
// Unlit particles: texture x material colour x particle colour.
// params.y > 0.5 samples with the texture's own filtering (point for pixel art); params.z > 0.5 = additive blending.
MeshPSOut main(MeshVSOut i) {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    float4 tex = GetTex2D(d.ids.x).Sample(GetSampler(d.params.y > 0.5 ? d.sampling.x : SAMPLER_LINEAR_CLAMP), i.uv);
    float4 color = tex * d.color * i.tangent;
    if (color.a < 0.004) discard;
    if (d.params.z > 0.5) color.rgb *= color.a; // additive: ONE, ONE blending, alpha fades the contribution
    MeshPSOut o;
    o.color = color;
    o.entityId = d.ids.z;
    return o;
}
