#include "Tree.hlsli"

struct VSOut {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    TreeVertexOut t = TransformTreeVertex(f, vid, iid);
    VSOut o;
    o.position = mul(LoadCascadeMatrix(g_Push.frameBuffer, g_Push.cascade), float4(t.world, 1.0));
    o.uv = t.uv;
    return o;
}
