// Stardust/Standard: metallic-roughness PBR with albedo + normal maps, shadows and aerial perspective.
// Included by Mesh.ps (opaque) and MeshTransparent.ps (alpha blended, MATERIAL_TRANSPARENT defined).
#include "MeshCommon.hlsli"
#include "PBR.hlsli"
#include "Shadows.hlsli"

MeshPSOut main(MeshVSOut i, bool frontFace : SV_IsFrontFace) {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    float4 albedo = SampleAlbedo(d, i.uv);
#ifndef MATERIAL_TRANSPARENT
    if (d.params.w > 0.0 && albedo.a < d.params.w) discard;
#endif
    FrameData f = LoadFrame(g_Push.frameBuffer);
    AmbientData amb = LoadAmbient(f);
    float3 N = SampleNormal(d, i, frontFace);
    float3 V = normalize(f.cameraPos - i.worldPos);
    float3 L = normalize(f.sunDir);
    float roughness = saturate(d.params.x), metallic = saturate(d.params.y);

    float NoL = saturate(dot(N, L));
    float shadow = d.emissive.w<0?1.0:NoL > 0.0 ? SampleShadow(f, g_Push.frameBuffer, i.worldPos, N, NoL) : 0.0;
    float3 color = EvaluateBRDF(N, V, L, albedo.rgb, roughness, metallic) * SunRadiance(f, amb) * shadow;
    color += EvaluateAmbient(f, amb, N, V, albedo.rgb, roughness, metallic, 1.0);
    color += EvaluateLocalLights(f, i.worldPos, N, V, albedo.rgb, roughness, metallic, false);
    color += d.emissive.rgb;
    color = ApplyAerialPerspective(f, color, i.worldPos);

    MeshPSOut o;
#ifdef MATERIAL_TRANSPARENT
    o.color = float4(color, albedo.a);
#else
    o.color = float4(color, 1.0);
#endif
    o.entityId = d.ids.z;
    return o;
}
