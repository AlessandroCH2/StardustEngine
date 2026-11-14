#include "MeshCommon.hlsli"

float4 main(uint vid : SV_VertexID,uint instance : SV_InstanceID) : SV_Position {
    uint drawIndex=g_Push.materialCount==0xffffffffu?GetBuffer(g_Push.materialBuffer).Load((g_Push.materialOffset+instance)*4):g_Push.drawIndex;
    DrawData d = LoadDraw(g_Push.drawBuffer,drawIndex);
    Vertex v = LoadVertex(g_Push.vertexBuffer, vid);
    float4 world = mul(d.model, float4(v.position, 1.0));
    return mul(LoadCascadeMatrix(g_Push.frameBuffer, g_Push.cascade), world);
}
