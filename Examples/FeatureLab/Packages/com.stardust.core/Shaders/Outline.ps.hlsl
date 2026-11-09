// Editor selection outline: orange where the selected entity's silhouette borders other pixels
#include "Bindless.hlsli"

struct Push { uint ids; uint selected; };
PUSH_CONSTANTS(Push, g_Push);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    Texture2D<uint4> ids = GetTex2DUint(g_Push.ids);
    int2 p = int2(pos.xy);
    uint w, h;
    ids.GetDimensions(w, h);
    bool inside = ids.Load(int3(p, 0)).x == g_Push.selected;
    int edge = 0;
    [unroll] for (int y = -2; y <= 2; ++y)
        [unroll] for (int x = -2; x <= 2; ++x) {
            int2 q = clamp(p + int2(x, y), int2(0, 0), int2(w - 1, h - 1));
            edge += (ids.Load(int3(q, 0)).x == g_Push.selected) != inside ? 1 : 0;
        }
    if (edge == 0) return 0.0;
    float a = inside ? 0.35 : 1.0; // crisp outside edge, softer inner rim
    return float4(1.0, 0.55, 0.1, a * saturate(edge / 6.0 + 0.4));
}
