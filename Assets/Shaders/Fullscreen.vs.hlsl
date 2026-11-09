// Fullscreen triangle. Depth is 0 (reverse-Z far plane) so it can be drawn behind geometry with depth test.
struct VSOut {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0.0, 1.0);
    return o;
}
