#include "Terrain.hlsli"

float4 main(uint vid : SV_VertexID, uint iid : SV_InstanceID) : SV_Position {
    float2 local;
    float3 world = TerrainVertex(vid, iid, local);
    return mul(LoadCascadeMatrix(g_Push.frameBuffer, g_Push.cascade), float4(world, 1.0));
}
