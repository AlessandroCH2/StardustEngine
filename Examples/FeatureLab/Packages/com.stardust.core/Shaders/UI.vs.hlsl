#include "MeshCommon.hlsli"
MeshVSOut main(uint vid : SV_VertexID) {
    Vertex v=LoadVertex(g_Push.vertexBuffer,vid);
    DrawData d=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex);
    MeshVSOut o; o.position=mul(d.model,float4(v.position,1));
    o.worldPos=v.position; o.normal=v.normal; o.tangent=float4(1,0,0,1); o.uv=v.uv; return o;
}
