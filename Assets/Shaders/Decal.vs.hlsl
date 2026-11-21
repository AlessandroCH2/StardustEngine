// Decal box: the unit cube of the decal, drawn from the inside (front faces culled) so it also works
// with the camera inside the box. The pixel shader projects the texture onto the depth buffer.
#include "Decal.hlsli"

DecalVSOut main(uint vid : SV_VertexID) {
    static const uint indices[36] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5 };
    uint c = indices[vid];
    float3 corner = float3((c & 1) ? 0.5 : -0.5, (c & 2) ? 0.5 : -0.5, (c & 4) ? 0.5 : -0.5);
    FrameData f = LoadFrame(g_Push.frame);
    DecalData d = LoadDecal(g_Push.decals, g_Push.index);
    DecalVSOut o;
    o.position = mul(f.viewProj, mul(d.model, float4(corner, 1)));
    return o;
}
