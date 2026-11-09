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
    FrameData f = LoadFrame(g_Push.frameBuffer);
    AmbientData amb = LoadAmbient(f);
    float3 N = normalize(i.normal);
    float3 V = normalize(f.cameraPos - i.worldPos);
    float3 L = normalize(f.sunDir);

    // Procedural bark: vertical furrows
    float furrow = ValueNoise(float2(i.uv.x * 14.0, i.uv.y * 1.2)) * 0.6 + ValueNoise(float2(i.uv.x * 40.0, i.uv.y * 4.0)) * 0.4;
    float3 albedo = lerp(float3(0.09, 0.075, 0.06), float3(0.26, 0.23, 0.19), furrow);
    float ao = lerp(0.5, 1.0, saturate(i.extra.x * 3.0));

    float NoL = saturate(dot(N, L));
    float shadow = NoL > 0.0 ? SampleShadow(f, g_Push.frameBuffer, i.worldPos, N, NoL) : 0.0;
    float3 color = EvaluateBRDF(N, V, L, albedo, 0.9, 0.0) * SunRadiance(f, amb) * shadow;
    color += EvaluateAmbient(f, amb, N, V, albedo, 0.9, 0.0, ao);
    color = ApplyAerialPerspective(f, color, i.worldPos);

    PSOut o;
    o.color = float4(color, 1.0);
    o.entityId = g_Push.entityId;
    return o;
}
