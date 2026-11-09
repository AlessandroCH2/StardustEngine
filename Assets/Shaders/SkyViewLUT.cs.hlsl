#include "Atmosphere.hlsli"

struct Push { uint frameBuffer; uint output; uint2 size; };
PUSH_CONSTANTS(Push, g_Push);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    FrameData f = LoadFrame(g_Push.frameBuffer);
    float2 uv = (id.xy + 0.5) / float2(g_Push.size);
    float3 rd = SkyViewDir(uv);
    float3 ro = float3(0, kGroundRadius + CameraHeightKm(f), 0);
    float3 sunDir = normalize(f.sunDir);
    uint tLut = f.atmosphereInfo.x;

    float tGround = RaySphere(ro, rd, kGroundRadius);
    float tTop = RaySphere(ro, rd, kTopRadius);
    float tMax = tGround > 0.0 ? tGround : tTop;
    tMax = min(tMax, 600.0);

    float cosTheta = dot(rd, sunDir);
    float phaseR = PhaseRayleigh(cosTheta);
    float phaseM = PhaseMie(cosTheta, kMieG);

    const int steps = 32;
    float3 L = 0.0, T = 1.0;
    float tPrev = 0.0;
    for (int i = 0; i < steps; ++i) {
        float t = tMax * pow((i + 0.5) / steps, 2.0); // denser samples close to the camera
        float dt = t - tPrev;
        tPrev = t;
        float3 p = ro + rd * t;
        float r = length(p);
        float h = r - kGroundRadius;
        Medium m = SampleMedium(h);
        float sunMu = dot(p / r, sunDir);
        float3 Tsun = SampleTransmittance(tLut, h, sunMu);
        // single scattering + a cheap isotropic multiple-scattering term
        float3 S = Tsun * (m.scatterR * phaseR + m.scatterM * phaseM)
                 + (m.scatterR + m.scatterM) * 0.06 * SampleTransmittance(tLut, h, max(sunMu, 0.1));
        float3 sampleT = exp(-m.extinction * dt);
        float3 integ = (S - S * sampleT) / max(m.extinction, 1e-6); // energy-conserving integration
        L += T * integ;
        T *= sampleT;
    }
    // Light bounced by the ground below the horizon
    if (tGround > 0.0) {
        float3 p = ro + rd * tGround;
        float sunMu = dot(normalize(p), sunDir);
        L += T * SampleTransmittance(tLut, 0.0, sunMu) * saturate(sunMu) * kGroundAlbedo / PI;
    }
    GetRWTex2D(g_Push.output)[id.xy] = float4(L, 1.0);
}
