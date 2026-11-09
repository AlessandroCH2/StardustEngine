#include "Tree.hlsli"

struct VSOut {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float2 extra    : TEXCOORD1;  // x = height ratio, y = per-tree hash
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    TreeVertexOut t = TransformTreeVertex(f, vid, iid);
    VSOut o;
    o.worldPos = t.world;
    o.position = mul(f.viewProj, float4(t.world, 1.0));
    o.normal = t.normal;
    o.uv = t.uv;
    o.extra = float2(t.heightRatio, t.hash);
    return o;
}
