// Stardust/Toon: banded diffuse, hard specular highlight and rim light (cel shading)
#include "MeshCommon.hlsli"
#include "Atmosphere.hlsli"
#include "Shadows.hlsli"
#include "PBR.hlsli"

MeshPSOut main(MeshVSOut i, bool frontFace : SV_IsFrontFace) {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    float4 albedo = SampleAlbedo(d, i.uv);
    if (d.params.w > 0.0 && albedo.a < d.params.w) discard;
    FrameData f = LoadFrame(g_Push.frameBuffer);
    AmbientData amb = LoadAmbient(f);
    float3 N = SampleNormal(d, i, frontFace);
    float3 V = normalize(f.cameraPos - i.worldPos);
    float3 L = normalize(f.sunDir);

    float steps = max(abs(d.emissive.w), 1.0);
    float NoL = saturate(dot(N, L));
    float shadow = d.emissive.w<0?1.0:NoL > 0.0 ? SampleShadowFast(f, g_Push.frameBuffer, i.worldPos) : 0.0;
    float band = floor(NoL * shadow * steps + 0.5) / steps;
    float3 H = normalize(V + L);
    float spec = step(0.97 - d.params.x * 0.2, dot(N, H)) * (1.0 - d.params.x) * shadow;
    float rim = smoothstep(0.55, 0.75, 1.0 - saturate(dot(N, V))) * asfloat(d.ids.w);

    float3 sun = SunRadiance(f, amb);
    float3 color = albedo.rgb * (sun * band / PI + amb.skyUp * f.ambientIntensity) + sun * spec * 0.1 + rim * amb.horizon;
    color += EvaluateLocalLights(f, i.worldPos, N, V, albedo.rgb, 1.0, 0.0, true);
    color += d.emissive.rgb;
    color = ApplyAerialPerspective(f, color, i.worldPos);

    MeshPSOut o;
    o.color = float4(color, 1.0);
    o.entityId = d.ids.z;
    return o;
}
