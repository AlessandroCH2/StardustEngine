// Volumetric cumulus clouds, ray marched at reduced resolution.
// Output: rgb = light scattered towards the camera, a = transmittance of the cloud layer.
#include "Atmosphere.hlsli"

struct Push {
    uint frameBuffer;
    uint noiseTexture;
    uint output;
    uint pad;
    uint2 size;
};
PUSH_CONSTANTS(Push, g_Push);

static const float kPlanetRadius = 6360000.0;
static const float kCloudBottom = 1500.0;
static const float kCloudTop = 3300.0;
static const float kMaxDistance = 40000.0;

float Remap(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }

float HG(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

float CloudDensity(FrameData f, Texture3D noise, SamplerState s, float3 p, float heightFrac, bool cheap) {
    float2 wind = f.windParams.xy * (f.time * (8.0 + f.windParams.z * 6.0));
    float3 q = p + float3(wind.x, 0, wind.y);

    // Weather map: where cumulus can form. Coverage moves the threshold.
    float weather = saturate((Fbm(q.xz * 0.0009, 4) - 0.5) * 2.2 + 0.5);
    float c = f.fogParams.z;
    // Dome: the cloud footprint shrinks with height -> rounded tops, flat base
    float dome = weather - (1.0 - c) - 0.06 - heightFrac * 0.12 - heightFrac * heightFrac * 0.25;
    float bottom = saturate(heightFrac / 0.08);
    // 3D billows break up the outline
    float base = saturate((noise.SampleLevel(s, q * 0.0005, 0).r - 0.45) * 2.2);
    float shape = saturate(dome * 3.0 + (base - 0.55) * 0.8) * bottom;
    if (shape <= 0.0 || cheap) return shape;

    float3 detailP = q * 0.0016 + float3(0, -f.time * 0.01, 0);
    float3 d = noise.SampleLevel(s, detailP, 0).gba;
    float detail = dot(d, float3(0.625, 0.25, 0.125));
    float erosion = lerp(detail, 1.0 - detail, saturate(heightFrac * 4.0)) * 0.45;
    return saturate(Remap(shape, erosion, 1.0, 0.0, 1.0));
}

// Planet curvature as a parabola around the camera: height above the curved ground.
// (A true sphere test with R = 6360 km loses all precision in 32-bit floats.)
float CurvedHeight(float3 p, float3 cam) {
    float2 d = p.xz - cam.xz;
    return p.y + dot(d, d) / (2.0 * kPlanetRadius);
}

// Distance along the ray to the curved altitude H, for a camera below it (numerically stable root)
float AltitudeT(float camY, float rdy, float H) {
    float a = (1.0 - rdy * rdy) / (2.0 * kPlanetRadius);
    float c = camY - H;
    float disc = max(rdy * rdy - 4.0 * a * c, 0.0);
    return (-2.0 * c) / (rdy + sqrt(disc));
}

float2 LayerIntersect(float3 ro, float3 rd) {
    if (ro.y >= kCloudBottom) return float2(-1, -1); // above the layer: not supported yet
    return float2(AltitudeT(ro.y, rd.y, kCloudBottom), AltitudeT(ro.y, rd.y, kCloudTop));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_Push.size)) return;
    FrameData f = LoadFrame(g_Push.frameBuffer);
    RWTexture2D<float4> output = GetRWTex2D(g_Push.output);

    float2 uv = (id.xy + 0.5) / float2(g_Push.size);
    float2 ndc = uv * float2(2, -2) + float2(-1, 1);
    float4 world = mul(f.invViewProj, float4(ndc, 0.5, 1.0));
    float3 rd = normalize(world.xyz / world.w - f.cameraPos);
    float3 ro = f.cameraPos;

    if (rd.y < 0.0 || f.fogParams.z <= 0.0) { output[id.xy] = float4(0, 0, 0, 1); return; }

    float2 seg = LayerIntersect(ro, rd);
    float tStart = max(seg.x, 0.0);
    float tEnd = min(min(seg.y, kMaxDistance), tStart + 9000.0); // bounded march: steps stay short at grazing angles
    if (seg.y < 0.0 || tEnd <= tStart) { output[id.xy] = float4(0, 0, 0, 1); return; }

    Texture3D noise = GetTex3D(g_Push.noiseTexture);
    SamplerState s = GetSampler(SAMPLER_LINEAR_WRAP);
    AmbientData amb = LoadAmbient(f);
    float3 sunDir = normalize(f.sunDir);
    float3 sunL = SunRadiance(f, amb) * 1.2;
    float cosTheta = dot(rd, sunDir);
    float sigmaScale = 0.025 * max(f.fogParams.w, 0.01);

    const int steps = 48;
    float dt = (tEnd - tStart) / steps;
    float t = tStart + dt * IGN(float2(id.xy));
    float3 L = 0.0;
    float T = 1.0;
    float tWeighted = 0.0, wSum = 0.0;
    for (int i = 0; i < steps && T > 0.02; ++i, t += dt) {
        float3 p = ro + rd * t;
        float height = CurvedHeight(p, ro);
        float hf = saturate((height - kCloudBottom) / (kCloudTop - kCloudBottom));
        float dens = CloudDensity(f, noise, s, p, hf, false);
        if (dens <= 0.001) continue;

        // light march towards the sun
        float lightOptical = 0.0;
        float ls = 90.0;
        float3 lp = p;
        for (int j = 0; j < 5; ++j) {
            lp += sunDir * ls;
            float lh = CurvedHeight(lp, ro);
            float lhf = saturate((lh - kCloudBottom) / (kCloudTop - kCloudBottom));
            lightOptical += CloudDensity(f, noise, s, lp, lhf, j > 2) * ls;
            ls *= 1.7;
        }
        float sigma = dens * sigmaScale;
        // Multiple scattering approximation (Wrenninge): octaves with weaker extinction and flatter phase
        float lightTerm = 0.0, aw = 1.0, bw = 1.0, cw = 1.0;
        [unroll] for (int o = 0; o < 3; ++o) {
            float ph = lerp(HG(cosTheta * cw, 0.75), HG(cosTheta * cw, -0.25), 0.35);
            lightTerm += aw * exp(-lightOptical * sigmaScale * bw) * ph;
            aw *= 0.55; bw *= 0.35; cw *= 0.5;
        }
        float powder = 1.0 - exp(-dens * sigmaScale * 400.0);
        // Sky + sun-lit environment light; brighter towards the cloud tops
        float3 ambient = lerp(amb.horizon, amb.skyUp, hf) * (1.2 + hf) + sunL * 0.035 * (0.4 + hf);
        float3 S = sigma * (sunL * lightTerm * lerp(0.7, 1.0, powder) * 1.6 + ambient);

        float sampleT = exp(-sigma * dt);
        L += T * (S - S * sampleT) / max(sigma, 1e-6);
        tWeighted += t * T * (1.0 - sampleT);
        wSum += T * (1.0 - sampleT);
        T *= sampleT;
    }

    // Fade distant clouds into the sky haze
    float dist = wSum > 0.0 ? tWeighted / wSum : tStart;
    float fade = exp(-dist / 22000.0);
    L *= fade;
    T = lerp(1.0, T, fade);
    output[id.xy] = float4(L, T);
}
