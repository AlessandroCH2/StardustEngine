#ifndef GPU_PARTICLES_HLSLI
#define GPU_PARTICLES_HLSLI
#include "Common.hlsli"
// Must match Scene/GpuParticles.cpp :: Header (512 bytes) and GpuParticle (64 bytes after the header)
static const uint kParticleHeader = 512, kParticleStride = 64;

struct ParticleHeader {
    float4x4 world;
    float4 acceleration, velocity, noise, collision;
    uint4 flags;
    float4 misc;
    float4 colorTimes[2];
    float4 colors[8];
    float4 sizeTimes[2], sizes[2];
    float4 stretch, sheet, time;
    float4x4 inverseWorld;
};
ParticleHeader LoadHeader(RWByteAddressBuffer b) {
    ParticleHeader h;
    h.world = LoadMatrix(b, 0);
    h.acceleration = b.Load<float4>(64); h.velocity = b.Load<float4>(80); h.noise = b.Load<float4>(96); h.collision = b.Load<float4>(112);
    h.flags = b.Load<uint4>(128); h.misc = b.Load<float4>(144);
    h.colorTimes[0] = b.Load<float4>(160); h.colorTimes[1] = b.Load<float4>(176);
    [unroll] for (uint i = 0; i < 8; ++i) h.colors[i] = b.Load<float4>(192 + i * 16);
    h.sizeTimes[0] = b.Load<float4>(320); h.sizeTimes[1] = b.Load<float4>(336);
    h.sizes[0] = b.Load<float4>(352); h.sizes[1] = b.Load<float4>(368);
    h.stretch = b.Load<float4>(384); h.sheet = b.Load<float4>(400); h.time = b.Load<float4>(416);
    h.inverseWorld = LoadMatrix(b, 432);
    return h;
}

struct GpuParticle { float3 position; float age; float3 velocity; float lifetime; float4 color; float size, rotation, angularVelocity, seed; };
GpuParticle LoadParticle(RWByteAddressBuffer b, uint i) {
    uint o = kParticleHeader + i * kParticleStride;
    GpuParticle p;
    float4 a = b.Load<float4>(o), v = b.Load<float4>(o + 16), s = b.Load<float4>(o + 48);
    p.position = a.xyz; p.age = a.w; p.velocity = v.xyz; p.lifetime = v.w; p.color = b.Load<float4>(o + 32);
    p.size = s.x; p.rotation = s.y; p.angularVelocity = s.z; p.seed = s.w;
    return p;
}
void StoreParticle(RWByteAddressBuffer b, uint i, GpuParticle p) {
    uint o = kParticleHeader + i * kParticleStride;
    b.Store<float4>(o, float4(p.position, p.age)); b.Store<float4>(o + 16, float4(p.velocity, p.lifetime));
    b.Store<float4>(o + 48, float4(p.size, p.rotation, p.angularVelocity, p.seed));
}

float KeyTime(float4 times[2], uint i) { return times[i / 4][i % 4]; }
float4 EvaluateColor(ParticleHeader h, float t) {
    uint n = h.flags.z;
    if (n == 0) return 1;
    if (t <= KeyTime(h.colorTimes, 0)) return h.colors[0];
    for (uint i = 1; i < n; ++i) {
        float t0 = KeyTime(h.colorTimes, i - 1), t1 = KeyTime(h.colorTimes, i);
        if (t <= t1) return lerp(h.colors[i - 1], h.colors[i], t1 > t0 ? (t - t0) / (t1 - t0) : 1);
    }
    return h.colors[n - 1];
}
float EvaluateSize(ParticleHeader h, float t) {
    uint n = h.flags.w;
    if (n == 0) return 1;
    if (t <= KeyTime(h.sizeTimes, 0)) return h.sizes[0].x;
    for (uint i = 1; i < n; ++i) {
        float t0 = KeyTime(h.sizeTimes, i - 1), t1 = KeyTime(h.sizeTimes, i);
        float v0 = h.sizes[(i - 1) / 4][(i - 1) % 4], v1 = h.sizes[i / 4][i % 4];
        if (t <= t1) return lerp(v0, v1, t1 > t0 ? (t - t0) / (t1 - t0) : 1);
    }
    return h.sizes[(n - 1) / 4][(n - 1) % 4];
}
#endif
