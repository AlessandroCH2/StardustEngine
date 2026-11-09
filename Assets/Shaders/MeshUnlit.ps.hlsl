// Stardust/Unlit: colour * texture + emissive, no lighting (UI-like objects, skyboxes, effects)
#include "MeshCommon.hlsli"

MeshPSOut main(MeshVSOut i) {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    float4 albedo = SampleAlbedo(d, i.uv);
    if (d.params.w > 0.0 && albedo.a < d.params.w) discard;
    MeshPSOut o;
    o.color = float4(albedo.rgb + d.emissive.rgb, 1.0);
    o.entityId = d.ids.z;
    return o;
}
