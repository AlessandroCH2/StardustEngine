// GPU particles: each particle of the buffer (bound as the vertex buffer) becomes a quad, like the CPU
// particles of Scene/ParticleSystem.cpp :: BuildVertices. Dead particles collapse to nothing.
#include "MeshCommon.hlsli"
#include "GpuParticles.hlsli"

MeshVSOut main(uint vid : SV_VertexID) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    RWByteAddressBuffer b = GetBuffer(g_Push.vertexBuffer);
    const uint index = vid / 4, corner = vid % 4;
    GpuParticle p = LoadParticle(b, index);
    MeshVSOut o = (MeshVSOut)0;
    o.drawIndex = g_Push.drawIndex;
    o.position = float4(2, 2, 2, 1); // outside the view: culled
    if (p.lifetime <= 0 || p.age >= p.lifetime) return o;
    ParticleHeader h = LoadHeader(b);
    const bool local = h.flags.x != 0;
    const float t = saturate(p.age / p.lifetime);
    const float size = p.size * h.time.y * max(0.0, EvaluateSize(h, t));
    float4 color = p.color * EvaluateColor(h, t);
    color = float4(color.rgb * h.misc.z, saturate(color.a));
    if (size <= 0 || color.a <= 0.002) return o;
    const float3 center = local ? mul(h.world, float4(p.position, 1)).xyz : p.position;
    const float3 camRight = float3(f.view[0][0], f.view[0][1], f.view[0][2]);
    const float3 camUp = float3(f.view[1][0], f.view[1][1], f.view[1][2]);
    const float3 camBack = float3(f.view[2][0], f.view[2][1], f.view[2][2]);
    const float half = size * 0.5;
    float3 corners[4];
    const int mode = (int)h.misc.w;
    if (mode == 1) {
        // Stretched along the velocity, the tail behind
        float3 v = (local ? mul(h.world, float4(p.velocity + h.velocity.xyz, 0)).xyz : p.velocity + h.velocity.xyz);
        float speed = length(v);
        float3 axis = speed > 1e-4 ? v / speed : camUp;
        float3 toCamera = normalize(f.cameraPos - center);
        float3 side = cross(axis, toCamera);
        side = length(side) > 1e-5 ? normalize(side) * half : camRight * half;
        float len = size * h.stretch.x + speed * h.stretch.y;
        corners[0] = center - side - axis * len; corners[1] = center + side - axis * len; corners[2] = center + side; corners[3] = center - side;
    } else {
        float3 right = camRight, up = camUp;
        if (mode == 2) { right = float3(1, 0, 0); up = float3(0, 0, -1); }
        else if (mode == 3) { up = float3(0, 1, 0); float3 r = cross(up, f.cameraPos - center); right = length(r) > 1e-5 ? normalize(r) : camRight; }
        float cs = cos(p.rotation), sn = sin(p.rotation);
        float3 r = (right * cs + up * sn) * half, u = (up * cs - right * sn) * half;
        corners[0] = center - r - u; corners[1] = center + r - u; corners[2] = center + r + u; corners[3] = center - r + u;
    }
    // Texture sheet frame
    float2 uvMin = 0, uvMax = 1;
    const int tilesX = h.sheet.x > 0 ? max(1, (int)h.stretch.z) : 1, tilesY = h.sheet.x > 0 ? max(1, (int)h.stretch.w) : 1, frames = tilesX * tilesY;
    if (frames > 1) {
        int frame = h.sheet.w > 0 ? (int)(p.seed * frames) : 0;
        frame += h.sheet.y > 0 ? (int)(p.age * h.sheet.y) : (int)(t * max(0.0, h.sheet.z) * frames);
        frame = ((frame % frames) + frames) % frames;
        float2 cell = float2(1.0 / tilesX, 1.0 / tilesY);
        uvMin = float2(frame % tilesX, frame / tilesX) * cell; uvMax = uvMin + cell;
    }
    const float2 uvs[4] = { float2(uvMin.x, uvMax.y), float2(uvMax.x, uvMax.y), float2(uvMax.x, uvMin.y), float2(uvMin.x, uvMin.y) };
    o.worldPos = corners[corner];
    o.position = mul(f.viewProj, float4(o.worldPos, 1));
    o.normal = camBack;
    o.tangent = color;
    o.uv = uvs[corner];
    return o;
}
