#include "MeshCommon.hlsli"
// Particle quads are built in world space on the CPU (Engine/src/Scene/ParticleSystem.cpp).
// The vertex tangent carries the particle colour (rgb may exceed 1 for HDR glow).
MeshVSOut main(uint vid : SV_VertexID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    Vertex v = LoadVertex(g_Push.vertexBuffer, vid);
    float4 world = mul(d.model, float4(v.position, 1.0));
    MeshVSOut o;
    o.position = mul(f.viewProj, world);
    o.worldPos = world.xyz;
    o.normal = v.normal;
    o.tangent = v.tangent;
    o.uv = v.uv;
    return o;
}
