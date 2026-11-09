// Integrates the sky-view LUT into a few ambient values used by every lit surface this frame.
#include "Atmosphere.hlsli"

struct Push { uint frameBuffer; uint output; };
PUSH_CONSTANTS(Push, g_Push);

groupshared float3 gsUp[64];
groupshared float3 gsHorizon[64];

[numthreads(64, 1, 1)]
void main(uint gi : SV_GroupIndex) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    // Fibonacci hemisphere, cosine weighted
    float3 up = 0.0;
    for (uint k = 0; k < 4; ++k) {
        uint i = gi * 4 + k;
        float u = (i + 0.5) / 256.0;
        float phi = i * 2.39996323;
        float cosT = sqrt(1.0 - u);
        float sinT = sqrt(u);
        float3 d = float3(cos(phi) * sinT, cosT, sin(phi) * sinT);
        up += SampleSky(f, d);
    }
    gsUp[gi] = up / 4.0;
    float az = gi / 64.0 * 2.0 * PI;
    gsHorizon[gi] = SampleSky(f, normalize(float3(cos(az), 0.06, sin(az))));
    GroupMemoryBarrierWithGroupSync();

    if (gi == 0) {
        float3 skyUp = 0.0, horizon = 0.0;
        for (uint i = 0; i < 64; ++i) { skyUp += gsUp[i]; horizon += gsHorizon[i]; }
        skyUp /= 64.0;      // cosine-weighted mean radiance = E / PI
        horizon /= 64.0;
        float3 sunDir = normalize(f.sunDir);
        float3 Tsun = SampleTransmittance(f.atmosphereInfo.x, CameraHeightKm(f), sunDir.y);
        float3 sunE = f.sunColor * f.sunIntensity * Tsun * saturate(sunDir.y);
        float3 ground = (sunE / PI + skyUp) * kGroundAlbedo * 1.5;
        RWByteAddressBuffer o = GetBuffer(g_Push.output);
        o.Store<float4>(0, float4(skyUp, 1));
        o.Store<float4>(16, float4(horizon, 1));
        o.Store<float4>(32, float4(Tsun, 1));
        o.Store<float4>(48, float4(ground, 1));
    }
}
