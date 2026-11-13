#include "MeshCommon.hlsli"
// World-space canvases. Vertex normals carry the Mask UV (xy) and enable flag (z), so they are passed through untransformed.
MeshVSOut main(uint vid : SV_VertexID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    Vertex v = LoadVertex(g_Push.vertexBuffer, vid);
    float4 world = mul(d.model, float4(v.position, 1.0));
    MeshVSOut o;
    o.position = mul(f.viewProj, world);
    o.worldPos = world.xyz;
    o.normal = v.normal;
    o.tangent = float4(1, 0, 0, 1);
    o.uv = v.uv;
    return o;
}
