// Deforms a skinned mesh into its per-instance vertex buffer, so every later pass reads ordinary vertices.
#include "Common.hlsli"

struct Push {
    uint source;     // rest pose vertices (48 bytes each)
    uint influences; // 2 uints with four 16-bit joints + 4 floats of weights (24 bytes each)
    uint palette;    // float4x4 per joint
    uint output;     // deformed vertices
    uint count;
};
PUSH_CONSTANTS(Push, g_Push);

float3x4 Joint(RWByteAddressBuffer palette, uint joint) {
    float4x4 m = LoadMatrix(palette, joint * 64);
    return (float3x4)m;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= g_Push.count) return;
    RWByteAddressBuffer source = GetBuffer(g_Push.source);
    RWByteAddressBuffer influences = GetBuffer(g_Push.influences);
    RWByteAddressBuffer palette = GetBuffer(g_Push.palette);
    RWByteAddressBuffer output = GetBuffer(g_Push.output);

    uint from = i * 48, to = i * 48, influence = i * 24;
    float3 position = source.Load<float3>(from);
    float3 normal = source.Load<float3>(from + 12);
    float2 uv = source.Load<float2>(from + 24);
    float4 tangent = source.Load<float4>(from + 32);

    uint2 packed = influences.Load<uint2>(influence);
    float4 weights = influences.Load<float4>(influence + 8);
    uint joints[4] = { packed.x & 0xFFFF, packed.x >> 16, packed.y & 0xFFFF, packed.y >> 16 };

    float3x4 skin = (float3x4)0;
    for (uint k = 0; k < 4; ++k)
        if (weights[k] > 0) skin += Joint(palette, joints[k]) * weights[k];

    output.Store<float3>(to, mul(skin, float4(position, 1)));
    output.Store<float3>(to + 12, normalize(mul(skin, float4(normal, 0))));
    output.Store<float2>(to + 24, uv);
    output.Store<float4>(to + 32, float4(normalize(mul(skin, float4(tangent.xyz, 0))), tangent.w));
}
