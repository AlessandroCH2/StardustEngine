#include "MeshCommon.hlsli"

MeshVSOut main(uint vid : SV_VertexID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    Vertex v = LoadVertex(g_Push.vertexBuffer, vid);
    float4 world = mul(d.model, float4(v.position, 1.0));
    // inverse-transpose for correct normals under non-uniform scale
    float3x3 m = (float3x3)d.model;
    float3x3 cof = float3x3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    MeshVSOut o;
    o.position = mul(f.viewProj, world);
    o.worldPos = world.xyz;
    o.normal = normalize(mul(cof, v.normal));
    o.tangent = float4(normalize(mul(m, v.tangent.xyz)), v.tangent.w);
    o.uv = v.uv;
    return o;
}
