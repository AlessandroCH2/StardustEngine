#ifndef PBR_HLSLI
#define PBR_HLSLI
#include "Atmosphere.hlsli"

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

float3 F_Schlick(float3 f0, float VoH) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Cook-Torrance GGX for one directional light (without light radiance)
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 albedo, float roughness, float metallic) {
    float3 H = normalize(V + L);
    float NoV = abs(dot(N, V)) + 1e-5;
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    float a = max(roughness * roughness, 0.002);

    float3 f0 = lerp(0.04.xxx, albedo, metallic);
    float3 F = F_Schlick(f0, VoH);
    float3 specular = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
    float3 diffuse = (1.0 - metallic) * albedo / PI * (1.0 - F);
    return (diffuse + specular) * NoL;
}

// Point and spot lights (UE4-style inverse-square falloff with a smooth range window)
float3 EvaluateLocalLights(FrameData f, float3 P, float3 N, float3 V, float3 albedo, float roughness, float metallic, bool wrapDiffuse) {
    uint count = f.lightInfo.y;
    if (count == 0) return 0.0;
    RWByteAddressBuffer b = GetBuffer(f.lightInfo.x);
    float3 sum = 0.0;
    [loop] for (uint i = 0; i < count; ++i) {
        uint o = i * 64;
        float4 pr = b.Load<float4>(o);
        float3 Lv = pr.xyz - P;
        float d2 = dot(Lv, Lv);
        if (d2 > pr.w * pr.w) continue;
        float4 ci = b.Load<float4>(o + 16);
        float d = sqrt(d2);
        float3 L = Lv / max(d, 1e-4);
        float window = saturate(1.0 - pow(d / pr.w, 4.0));
        float att = window * window / (d2 + 1.0);
        if (ci.w > 0.5) {
            float4 dc = b.Load<float4>(o + 32);
            float cosInner = b.Load<float4>(o + 48).x;
            float spot = saturate((dot(-L, dc.xyz) - dc.w) / max(cosInner - dc.w, 1e-3));
            att *= spot * spot;
        }
        if (att <= 0.0) continue;
        float3 brdf = wrapDiffuse ? albedo / PI * saturate((dot(N, L) + 0.4) / 1.4) : EvaluateBRDF(N, V, L, albedo, roughness, metallic);
        sum += brdf * ci.rgb * att;
    }
    return sum;
}

// Image-based-ish lighting from the atmosphere: hemisphere irradiance + sky reflection blurred by roughness
float3 SceneAmbient(FrameData f,AmbientData amb,float3 N) {
    float3 baseline=N.y>0 ? lerp(f.skyHorizon,f.skyZenith,saturate(N.y)) : lerp(f.skyHorizon,f.groundColor,saturate(-N.y));
    return max(max(AmbientIrradiance(amb,N),baseline),max(0.0,f.pad1).xxx);
}
float3 EvaluateAmbient(FrameData f, AmbientData amb, float3 N, float3 V, float3 albedo, float roughness, float metallic, float ao) {
    float3 irradiance = SceneAmbient(f,amb,N);
    float3 f0 = lerp(0.04.xxx, albedo, metallic);
    float NoV = saturate(dot(N, V));
    float3 R = reflect(-V, N);
    R.y = max(R.y, 0.02);
    float3 specEnv = lerp(SampleSky(f, normalize(R)), irradiance, roughness * roughness);
    float3 F = f0 + (max((1.0 - roughness).xxx, f0) - f0) * pow(1.0 - NoV, 5.0);
    float3 diffuse = (1.0 - metallic) * albedo * irradiance;
    return (diffuse * (1.0 - F) + specEnv * F) * ao * f.ambientIntensity;
}

#endif
