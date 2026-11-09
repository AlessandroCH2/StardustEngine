#include "Common.hlsli"

struct Push {
    uint frameBuffer;
    uint instanceBuffer;
    uint entityId;
    float radius;
    float4 grassColor;
    float4 dryColor;
};
PUSH_CONSTANTS(Push, g_Push);

struct VSOut {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float3 color    : COLOR0;
    float  height   : TEXCOORD0;   // 0 at the root, 1 at the tip
};

// Blade: 4 segments -> 9 vertices (pairs per level + a single tip vertex)
VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    RWByteAddressBuffer inst = GetBuffer(g_Push.instanceBuffer);
    float4 a = inst.Load<float4>(iid * 32);
    float4 b = inst.Load<float4>(iid * 32 + 16);
    float3 root = a.xyz;
    float rot = a.w;
    float height = b.x, width = b.y, bend = b.w;
    uint type = (uint)b.z;
    float rnd = frac(b.z) * 2.0;

    uint level = min(vid / 2, 4u);
    float t = level / 4.0;
    float sideSign = vid == 8 ? 0.0 : ((vid & 1) ? 1.0 : -1.0);

    float3 facing = float3(cos(rot), 0, sin(rot));
    float3 side = float3(-facing.z, 0, facing.x);

    // Wind: travelling gusts + per-blade flutter
    float2 windDir = normalize(f.windParams.xy + 1e-4);
    float strength = f.windParams.z;
    float gust = ValueNoise(root.xz * 0.06 - windDir * f.time * (1.2 + strength));
    float flutter = sin(f.time * (3.0 + rnd * 2.0) + rnd * 20.0 + root.x * 0.5) * 0.08;
    float windBend = strength * (0.25 + gust * 1.1) + flutter * strength;
    float3 bendDir = normalize(facing * bend + float3(windDir.x, 0, windDir.y) * windBend + 1e-4);
    float totalBend = saturate(bend + windBend);

    // Parabolic bending, roughly length-preserving
    float vertical = height * t * (1.0 - 0.35 * totalBend * t);
    float3 offset = bendDir * totalBend * t * t * height * 0.9;

    float w = width * pow(1.0 - t, 0.7);
    if (type == 1 && level >= 3) w = width * (level == 3 ? 1.8 : 0.0); // flower head
    float3 pos = root + float3(0, vertical, 0) + offset + side * sideSign * w * 0.5;

    // Normal of the curved blade, tilted sideways for a rounded look
    float3 tangent = normalize(float3(0, height * (1.0 - 0.7 * totalBend * t), 0) + bendDir * 2.0 * totalBend * t * height * 0.9);
    float3 n = normalize(cross(side, tangent));
    n = normalize(n + side * sideSign * 0.5);

    // Colour: matches the terrain dryness, darker at the root, lighter towards the tip
    float dry = GroundDryness(root.xz);
    float3 baseCol = lerp(g_Push.grassColor.rgb, g_Push.dryColor.rgb, saturate(dry + (rnd - 0.5) * 0.35));
    baseCol *= 0.8 + 0.4 * rnd;
    float3 col = baseCol * lerp(0.55, 1.15, t);
    if (type == 1 && level >= 3) col = lerp(float3(0.95, 0.62, 0.08), float3(1.0, 0.85, 0.3), rnd * 0.5);

    VSOut o;
    o.worldPos = pos;
    o.position = mul(f.viewProj, float4(pos, 1.0));
    o.normal = n;
    o.color = col;
    o.height = t;
    return o;
}
