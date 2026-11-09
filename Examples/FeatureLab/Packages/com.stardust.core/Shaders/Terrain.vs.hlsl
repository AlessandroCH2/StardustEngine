#include "Terrain.hlsli"

struct VSOut {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float2 local    : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    VSOut o;
    o.worldPos = TerrainVertex(vid, iid, o.local);
    o.position = mul(f.viewProj, float4(o.worldPos, 1.0));
    return o;
}
