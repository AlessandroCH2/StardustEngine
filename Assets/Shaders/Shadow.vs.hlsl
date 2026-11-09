#include "MeshCommon.hlsli"

float4 main(uint vid : SV_VertexID) : SV_Position {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    Vertex v = LoadVertex(g_Push.vertexBuffer, vid);
    float4 world = mul(d.model, float4(v.position, 1.0));
    return mul(LoadCascadeMatrix(g_Push.frameBuffer, g_Push.cascade), world);
}
