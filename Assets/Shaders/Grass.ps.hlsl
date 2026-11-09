#include "PBR.hlsli"
#include "Shadows.hlsli"

struct Push {
    uint frameBuffer;
    uint instanceBuffer;
    uint entityId;
    float radius;
    float4 grassColor;
    float4 dryColor;
};
PUSH_CONSTANTS(Push, g_Push);

struct PSIn {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float3 color    : COLOR0;
    float  height   : TEXCOORD0;
};

struct PSOut {
    float4 color    : SV_Target0;
    uint   entityId : SV_Target1;
};

PSOut main(PSIn i, bool frontFace : SV_IsFrontFace) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    AmbientData amb = LoadAmbient(f);
    float3 N = normalize(i.normal) * (frontFace ? 1.0 : -1.0);
    float3 V = normalize(f.cameraPos - i.worldPos);
    float3 L = normalize(f.sunDir);
    float3 albedo = i.color;

    float shadow = SampleShadowFast(f, g_Push.frameBuffer, i.worldPos);
    float3 sun = SunRadiance(f, amb) * shadow;
    float ao = lerp(0.3, 1.0, pow(i.height, 0.7));

    // Wrapped diffuse + light transmitted through the thin blade + a soft sheen
    float wrap = saturate((dot(N, L) + 0.35) / 1.35);
    float trans = pow(saturate(dot(-V, L)), 4.0) * 0.8 + 0.2 * saturate(-dot(N, L));
    float3 H = normalize(V + L);
    float spec = pow(saturate(dot(N, H)), 24.0) * 0.12 * i.height;
    float3 color = sun * (albedo / PI * wrap + albedo * trans * 0.35 + spec);
    color += SceneAmbient(f,amb,N) * albedo * ao * f.ambientIntensity;
    color += EvaluateLocalLights(f, i.worldPos, N, V, albedo, 1.0, 0.0, true) * ao;
    color = ApplyAerialPerspective(f, color, i.worldPos);

    PSOut o;
    o.color = float4(color, 1.0);
    o.entityId = g_Push.entityId;
    return o;
}
