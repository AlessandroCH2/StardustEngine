#include "Terrain.hlsli"
#include "PBR.hlsli"
#include "Shadows.hlsli"

struct PSIn {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float2 local    : TEXCOORD0;
};

struct PSOut {
    float4 color    : SV_Target0;
    uint   entityId : SV_Target1;
};

float3 TerrainNormal(float2 local) {
    uint w, h;
    GetTex2D(g_Push.heightmap).GetDimensions(w, h);
    float e = g_Push.size / (w - 1.0);
    float hl = SampleHeight(local - float2(e, 0)), hr = SampleHeight(local + float2(e, 0));
    float hd = SampleHeight(local - float2(0, e)), hu = SampleHeight(local + float2(0, e));
    return normalize(float3(hl - hr, 2.0 * e, hd - hu));
}

PSOut main(PSIn i) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    AmbientData amb = LoadAmbient(f);
    float3 N = TerrainNormal(i.local);
    float2 xz = i.worldPos.xz;

    // Small-scale bumps so close-up ground is not perfectly smooth
    float2 dn = float2(ValueNoise(xz * 1.7) - ValueNoise(xz * 1.7 + float2(0.37, 0)),
                       ValueNoise(xz * 1.7) - ValueNoise(xz * 1.7 + float2(0, 0.37)));
    if(g_Push.grassColor.a>0.5) N = normalize(N + float3(dn.x, 0, dn.y) * 0.35);

    // Splat-mapped layers (painted in the editor). Each layer uses its texture or a procedural look.
    float weights[16];float sum=0;[unroll]for(uint group=0;group<4;++group){float4 packed=GetTex2D(g_Push.splatmap).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP),SplatUV(i.local,group),0);[unroll]for(uint channel=0;channel<4;++channel){weights[group*4+channel]=packed[channel];sum+=packed[channel];}}
    [unroll]for(uint wi=0;wi<16;++wi)weights[wi]/=max(sum,1e-3);
    float macro = Fbm(xz * 0.03, 3);
    float micro = Fbm(xz * 0.6, 2);
    float dry = GroundDryness(xz);
    float3 procedural[4] = {
        lerp(g_Push.grassColor.rgb, g_Push.dryColor.rgb, dry) * (0.75 + 0.5 * micro),
        float3(0.22, 0.17, 0.11) * (0.8 + 0.4 * micro),
        float3(0.30, 0.29, 0.27) * (0.7 + 0.6 * Fbm(xz * 0.15, 3)),
        float3(0.55, 0.49, 0.36) * (0.85 + 0.3 * micro),
    };
    RWByteAddressBuffer lb = GetBuffer(g_Push.layerBuffer);
    float3 albedo = 0.0;
    float roughness = 0.0;
    [unroll] for (uint k = 0; k < 16; ++k) {
        float w = weights[k];
        if (w <= 0.001) continue;
        float4 tintRough = lb.Load<float4>(k * 32);
        uint tex = lb.Load(k * 32 + 16);
        float tiling = asfloat(lb.Load(k * 32 + 20));
        float3 c = g_Push.grassColor.a>0.5 ? procedural[min(k,3)] : 1.0.xxx;
        if (tex != 0xFFFFFFFF)
            c = GetTex2D(tex).Sample(GetSampler(SAMPLER_ANISO_WRAP), xz / tiling).rgb;
        albedo += c * tintRough.rgb * w;
        roughness += tintRough.a * w;
    }

    float3 V = normalize(f.cameraPos - i.worldPos);
    float3 L = normalize(f.sunDir);
    float NoL = saturate(dot(N, L));
    float shadow = NoL > 0.0 ? SampleShadow(f, g_Push.frameBuffer, i.worldPos, N, NoL) : 0.0;
    float3 color = EvaluateBRDF(N, V, L, albedo, roughness, 0.0) * SunRadiance(f, amb) * shadow;
    color += EvaluateAmbient(f, amb, N, V, albedo, roughness, 0.0, 1.0);
    color += EvaluateLocalLights(f, i.worldPos, N, V, albedo, roughness, 0.0, false);
    color = ApplyAerialPerspective(f, color, i.worldPos);

    PSOut o;
    o.color = float4(color, 1.0);
    o.entityId = g_Push.entityId;
    return o;
}
