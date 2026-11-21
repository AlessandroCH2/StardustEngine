// GPU particles: one step of the simulation (the same rules as Scene/ParticleSystem.cpp :: Step)
#include "GpuParticles.hlsli"

struct Push { uint buffer, capacity; float dt, pad; };
PUSH_CONSTANTS(Push, g_Push);

float Hash(int3 c) {
    uint h = (uint)c.x * 374761393u + (uint)c.y * 668265263u + (uint)c.z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 8388607.5 - 1.0;
}
float ValueNoise(float3 p) {
    float3 f = floor(p), t = p - f, s = t * t * (3.0 - 2.0 * t);
    int3 i = int3(f);
    return lerp(lerp(lerp(Hash(i), Hash(i + int3(1, 0, 0)), s.x), lerp(Hash(i + int3(0, 1, 0)), Hash(i + int3(1, 1, 0)), s.x), s.y),
                lerp(lerp(Hash(i + int3(0, 0, 1)), Hash(i + int3(1, 0, 1)), s.x), lerp(Hash(i + int3(0, 1, 1)), Hash(i + int3(1, 1, 1)), s.x), s.y), s.z);
}
float3 NoiseVector(float3 p) { return float3(ValueNoise(p), ValueNoise(p + float3(31.7, 11.3, 47.1)), ValueNoise(p + float3(-19.1, 53.9, 7.7))); }

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_Push.capacity) return;
    RWByteAddressBuffer b = GetBuffer(g_Push.buffer);
    GpuParticle p = LoadParticle(b, id.x);
    if (p.age >= p.lifetime || p.lifetime <= 0) return;
    ParticleHeader h = LoadHeader(b);
    float remaining = g_Push.dt;
    // Long frames in steps of 50 ms, like the CPU simulation
    [loop] for (int n = 0; n < 8 && remaining > 0; ++n) {
        float dt = min(remaining, 0.05); remaining -= dt;
        p.age += dt;
        if (p.age >= p.lifetime) break;
        p.velocity += h.acceleration.xyz * dt;
        if (h.velocity.w > 0) {
            p.velocity *= max(0.0, 1.0 - h.acceleration.w * dt);
            float speed = length(p.velocity);
            if (speed > h.velocity.w) p.velocity *= h.velocity.w / speed;
        }
        float3 motion = p.velocity + h.velocity.xyz;
        if (h.noise.w > 0) {
            float scroll = h.time.x * h.noise.z;
            motion += NoiseVector(p.position * h.noise.y + float3(scroll, p.seed * 17.0, -scroll)) * h.noise.x;
        }
        p.position += motion * dt;
        p.rotation += (h.misc.y > 0 ? p.angularVelocity : 0) * dt;
        if (h.flags.y != 0) {
            float3 wp = h.flags.x != 0 ? mul(h.world, float4(p.position, 1)).xyz : p.position;
            if (wp.y < h.collision.x) {
                float3 wv = h.flags.x != 0 ? mul(h.world, float4(p.velocity, 0)).xyz : p.velocity;
                wp.y = h.collision.x;
                if (wv.y < 0) wv.y = -wv.y * h.collision.y;
                wv.xz *= 1 - h.collision.z;
                p.age += p.lifetime * h.collision.w;
                if (h.flags.x != 0) {
                    float4x4 inv = LoadMatrix(b, 432);
                    p.position = mul(inv, float4(wp, 1)).xyz;
                    p.velocity = mul(inv, float4(wv, 0)).xyz;
                } else { p.position = wp; p.velocity = wv; }
            }
        }
    }
    StoreParticle(b, id.x, p);
}
