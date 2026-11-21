// Decal: the surface under each pixel (from the depth buffer) inside the decal box gets the texture,
// lit like a rough dielectric (sun with shadows, ambient, local lights) and alpha blended over it.
#include "Decal.hlsli"
#include "PBR.hlsli"
#include "Shadows.hlsli"

float4 main(DecalVSOut i) : SV_Target0 {
    FrameData f = LoadFrame(g_Push.frame);
    DecalData d = LoadDecal(g_Push.decals, g_Push.index);
    int2 px = int2(i.position.xy);
    float depth = GetTex2D(g_Push.depth).Load(int3(px, 0)).r;
    float2 uv = (px + 0.5) * f.viewport.zw;
    float4 wp = mul(f.invViewProj, float4(uv.x * 2 - 1, 1 - uv.y * 2, max(depth, 1e-7), 1));
    float3 P = wp.xyz / wp.w;
    // Surface normal from the reconstructed positions (before any discard: derivatives need the quad)
    float3 N = normalize(cross(ddy(P), ddx(P)));
    if (dot(N, f.cameraPos - P) < 0) N = -N;
    if (depth <= 0.0) discard;
    float3 local = mul(d.invModel, float4(P, 1)).xyz;
    if (any(abs(local) > 0.5)) discard;

    float3 axisX = normalize(mul(d.model, float4(1, 0, 0, 0)).xyz), axisY = normalize(mul(d.model, float4(0, 1, 0, 0)).xyz), axisZ = normalize(mul(d.model, float4(0, 0, 1, 0)).xyz);
    float facing = dot(N, axisZ); // the decal projects along -Z: surfaces facing +Z receive it
    float angle = d.params.z > 0 ? smoothstep(0.0, d.params.z, facing) : (facing > 0 ? 1.0 : 0.0);
    float depthFade = 1 - smoothstep(0.35, 0.5, abs(local.z));
    float2 tuv = float2(local.x + 0.5, 0.5 - local.y) * d.tilingOffset.xy + d.tilingOffset.zw;
    float4 tex = GetTex2D(d.ids.x).Sample(GetSampler(SAMPLER_LINEAR_WRAP), tuv);
    float alpha = tex.a * d.color.a * d.params.x * angle * depthFade;
    if (alpha < 0.002) discard;
    if (d.ids.y != 0xFFFFFFFFu) {
        float3 n = GetTex2D(d.ids.y).Sample(GetSampler(SAMPLER_LINEAR_WRAP), tuv).xyz * 2 - 1;
        n.xy *= d.params.y;
        float3 T = normalize(axisX - N * dot(axisX, N)), B = cross(N, T) * (dot(cross(N, T), axisY) < 0 ? -1 : 1);
        N = normalize(T * n.x + B * -n.y + N * max(n.z, 1e-3));
    }
    float3 albedo = tex.rgb * d.color.rgb;
    float roughness = saturate(1 - d.params.w);
    AmbientData amb = LoadAmbient(f);
    float3 V = normalize(f.cameraPos - P);
    float3 L = normalize(f.sunDir);
    float NoL = saturate(dot(N, L));
    float shadow = NoL > 0 ? SampleShadow(f, g_Push.frame, P, N, NoL) : 0;
    float3 color = EvaluateBRDF(N, V, L, albedo, roughness, 0) * SunRadiance(f, amb) * shadow;
    color += EvaluateAmbient(f, amb, N, V, albedo, roughness, 0, 1, P);
    color += EvaluateLocalLights(f, P, N, V, albedo, roughness, 0, false);
    color += albedo * d.extra.x;
    color = ApplyAerialPerspective(f, color, P);
    return float4(color, saturate(alpha));
}
