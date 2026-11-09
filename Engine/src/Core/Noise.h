#pragma once
#include "Core/Base.h"
#include <array>
#include <cmath>
#include <random>
#include <algorithm>

namespace Stardust {

// Seeded 2D gradient noise (improved Perlin) for CPU-side procedural generation
class Perlin2D {
public:
    explicit Perlin2D(u32 seed = 1337) {
        for (int i = 0; i < 256; ++i) m_P[i] = (u8)i;
        std::mt19937 rng(seed);
        std::shuffle(m_P.begin(), m_P.begin() + 256, rng);
        for (int i = 0; i < 256; ++i) m_P[256 + i] = m_P[i];
    }

    // [-1, 1]
    float Noise(float x, float y) const {
        int xi = (int)std::floor(x), yi = (int)std::floor(y);
        float xf = x - xi, yf = y - yi;
        xi &= 255; yi &= 255;
        float u = Fade(xf), v = Fade(yf);
        int aa = m_P[m_P[xi] + yi], ab = m_P[m_P[xi] + yi + 1];
        int ba = m_P[m_P[xi + 1] + yi], bb = m_P[m_P[xi + 1] + yi + 1];
        float x1 = Lerp(Grad(aa, xf, yf), Grad(ba, xf - 1, yf), u);
        float x2 = Lerp(Grad(ab, xf, yf - 1), Grad(bb, xf - 1, yf - 1), u);
        return Lerp(x1, x2, v) * 1.414f;
    }

    // fBm in [0, 1]
    float Fbm(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const {
        float sum = 0, amp = 0.5f, norm = 0;
        for (int i = 0; i < octaves; ++i) {
            sum += Noise(x, y) * amp;
            norm += amp;
            x = x * lacunarity + 31.7f; y = y * lacunarity + 17.3f;
            amp *= gain;
        }
        return sum / norm * 0.5f + 0.5f;
    }

    // Ridged multifractal in [0, 1] (sharp mountain crests)
    float Ridged(float x, float y, int octaves) const {
        float sum = 0, amp = 0.5f, norm = 0, prev = 1.0f;
        for (int i = 0; i < octaves; ++i) {
            float n = 1.0f - std::abs(Noise(x, y));
            n *= n;
            sum += n * amp * prev;
            norm += amp;
            prev = n;
            x = x * 2.1f + 11.3f; y = y * 2.1f + 7.9f;
            amp *= 0.5f;
        }
        return sum / norm;
    }

private:
    static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    static float Grad(int h, float x, float y) {
        switch (h & 7) {
        case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
        case 4: return x; case 5: return -x; case 6: return y; default: return -y;
        }
    }
    std::array<u8, 512> m_P{};
};

inline float Smoothstep(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// Deterministic hash -> [0,1)
inline float Hash01(u32 x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (x & 0xFFFFFF) / 16777216.0f;
}

} // namespace Stardust
