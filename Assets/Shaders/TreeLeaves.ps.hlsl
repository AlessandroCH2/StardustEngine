#include "Tree.hlsli"
#include "PBR.hlsli"
#include "Shadows.hlsli"

struct PSIn {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float2 extra    : TEXCOORD1;
};

struct PSOut {
    float4 color    : SV_Target0;
    uint   entityId : SV_Target1;
};

PSOut main(PSIn i) {
    if (LeafAlpha(i.uv) < 0.5) discard;
    FrameData f = LoadFrame(g_Push.frameBuffer);
    AmbientData amb = LoadAmbient(f);

    float4 tex = GetTex2D(g_Push.leafTexture).Sample(GetSampler(SAMPLER_LINEAR_WRAP), i.uv);
    // per-tree tint variation
    float3 tint = lerp(float3(0.85, 1.0, 0.8), float3(1.1, 1.0, 0.75), i.extra.y);
    float3 albedo = tex.rgb * tint;

    // Normals point away from the crown centre (set at generation) -> soft, volumetric crown lighting
    float3 N = normalize(i.normal);
    float3 V = normalize(f.cameraPos - i.worldPos);
    float3 L = normalize(f.sunDir);
    float shadow = SampleShadowFast(f, g_Push.frameBuffer, i.worldPos);
    float3 sun = SunRadiance(f, amb) * shadow;

    float wrap = saturate((dot(N, L) + 0.5) / 1.5);
    float trans = pow(saturate(dot(-V, L)), 3.0) * 0.6;
    float3 H = normalize(V + L);
    float spec = pow(saturate(dot(N, H)), 32.0) * 0.08;
    float ao = lerp(0.55, 1.0, saturate(N.y * 0.5 + 0.5)) * lerp(0.75, 1.0, i.extra.x);

    float3 color = sun * (albedo / PI * wrap + albedo * trans + spec);
    // sky light reaches the inside of the crown both directly and scattered through the leaves
    color += (SceneAmbient(f,amb,N) + amb.skyUp * 0.35) * albedo * ao * f.ambientIntensity * 1.4;
    color = ApplyAerialPerspective(f, color, i.worldPos);

    PSOut o;
    o.color = float4(color, 1.0);
    o.entityId = g_Push.entityId;
    return o;
}
