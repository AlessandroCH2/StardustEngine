// Physically based atmosphere (after Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere").
// Distances inside the atmosphere model are in kilometres.
#ifndef ATMOSPHERE_HLSLI
#define ATMOSPHERE_HLSLI
#include "Common.hlsli"

static const float kGroundRadius = 6360.0;
static const float kTopRadius = 6460.0;
static const float3 kRayleighScattering = float3(5.802, 13.558, 33.1) * 1e-3;
static const float kRayleighHeight = 8.0;
static const float kMieScattering = 3.996e-3;
static const float kMieAbsorption = 4.4e-3;
static const float kMieHeight = 1.2;
static const float kMieG = 0.8;
static const float3 kOzoneAbsorption = float3(0.650, 1.881, 0.085) * 1e-3;
static const float3 kGroundAlbedo = float3(0.12, 0.14, 0.08);

struct Medium {
    float3 scatterR;
    float3 scatterM;
    float3 extinction;
};

Medium SampleMedium(float h) {
    float dR = exp(-h / kRayleighHeight);
    float dM = exp(-h / kMieHeight);
    float ozone = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    Medium m;
    m.scatterR = kRayleighScattering * dR;
    m.scatterM = kMieScattering * dM;
    m.extinction = m.scatterR + (kMieScattering + kMieAbsorption) * dM + kOzoneAbsorption * ozone;
    return m;
}

// Distance to a sphere centred at the origin, -1 if missed
float RaySphere(float3 ro, float3 rd, float r) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float d = b * b - c;
    if (d < 0.0) return -1.0;
    float s = sqrt(d);
    float t0 = -b - s, t1 = -b + s;
    if (t0 > 0.0) return t0;
    if (t1 > 0.0) return t1;
    return -1.0;
}

float PhaseRayleigh(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
float PhaseMie(float c, float g) {
    float g2 = g * g;
    return 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + c * c)) / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

// ---- transmittance LUT parameterisation: x = cos(sun zenith), y = altitude
float2 TransmittanceUV(float h, float mu) { return float2(mu * 0.5 + 0.5, saturate(h / (kTopRadius - kGroundRadius))); }

float3 SampleTransmittance(uint lut, float h, float mu) {
    return GetTex2D(lut).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), TransmittanceUV(h, mu), 0).rgb;
}

// ---- sky-view LUT: x = world azimuth, y = elevation with more resolution around the horizon
float2 SkyViewUV(float3 dir) {
    float az = atan2(dir.z, dir.x);
    float el = asin(clamp(dir.y, -1.0, 1.0));
    float v = 0.5 + 0.5 * sign(el) * sqrt(abs(el) / (PI * 0.5));
    return float2(az / (2.0 * PI) + 0.5, v);
}
float3 SkyViewDir(float2 uv) {
    float az = (uv.x - 0.5) * 2.0 * PI;
    float x = uv.y * 2.0 - 1.0;
    float el = sign(x) * x * x * PI * 0.5;
    return float3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
}

float CameraHeightKm(FrameData f) { return max(f.cameraPos.y * 0.001, 0.0) + 0.2; }

// Radiance of the sky in a direction, already scaled by the sun intensity
float3 SampleSky(FrameData f, float3 dir) {
    float3 l = GetTex2D(f.atmosphereInfo.y).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), SkyViewUV(dir), 0).rgb;
    return l * f.sunIntensity * f.sunColor;
}

// Ambient data computed once per frame by SkyAmbient.cs
struct AmbientData {
    float3 skyUp;            // irradiance / PI from the upper hemisphere
    float3 horizon;          // average radiance near the horizon
    float3 sunTransmittance; // atmosphere transmittance towards the sun at the camera
    float3 ground;           // bounce light from the ground
};

AmbientData LoadAmbient(FrameData f) {
    RWByteAddressBuffer b = GetBuffer(f.atmosphereInfo.z);
    AmbientData a;
    a.skyUp = b.Load<float4>(0).rgb;
    a.horizon = b.Load<float4>(16).rgb;
    a.sunTransmittance = b.Load<float4>(32).rgb;
    a.ground = b.Load<float4>(48).rgb;
    return a;
}

// Direct sun radiance reaching the scene (reddens at sunset through the transmittance)
float3 SunRadiance(FrameData f, AmbientData a) { return f.sunColor * f.sunIntensity * a.sunTransmittance; }

// Diffuse sky light for a normal (hemisphere blend between bounce light, horizon and sky)
float3 AmbientIrradiance(AmbientData a, float3 N) {
    float up = N.y * 0.5 + 0.5;
    float3 side = lerp(a.ground, a.horizon, 0.5);
    return up > 0.5 ? lerp(side, a.skyUp, up * 2.0 - 1.0) : lerp(a.ground, side, up * 2.0);
}

// Aerial perspective: exponential height fog lit by the sky in the view direction
float3 ApplyAerialPerspective(FrameData f, float3 color, float3 worldPos) {
    float3 toP = worldPos - f.cameraPos;
    float dist = length(toP);
    float3 rd = toP / max(dist, 1e-3);
    float density = f.fogParams.x;
    float falloff = f.fogParams.y;
    float camTerm = density * exp(-falloff * f.cameraPos.y);
    float k = falloff * rd.y;
    float integral = abs(k) > 1e-5 ? (1.0 - exp(-k * dist)) / k : dist;
    float fog = 1.0 - exp(-camTerm * integral);
    float3 dir = normalize(float3(rd.x, max(rd.y, 0.03), rd.z));
    float3 fogColor = SampleSky(f, dir);
    return lerp(color, fogColor, saturate(fog));
}

#endif
