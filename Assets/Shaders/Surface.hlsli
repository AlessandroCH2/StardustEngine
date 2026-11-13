// Surface shaders: what .shader files and Shader Graphs compile against (the editor wraps their code with a main()).
// A surface function fills SurfaceOutputStandard; ShadeStandard lights it exactly like
// Stardust/Standard (sun with shadows, ambient, point and spot lights, aerial perspective). Unlit shaders return a colour.
#ifndef SURFACE_HLSLI
#define SURFACE_HLSLI
#include "MeshCommon.hlsli"
#include "PBR.hlsli"
#include "Shadows.hlsli"

// Short type names found in many existing shaders
#define fixed float
#define fixed2 float2
#define fixed3 float3
#define fixed4 float4
#define half float
#define half2 float2
#define half3 float3
#define half4 float4

struct SurfaceInput {
    float3 worldPos;
    float3 worldNormal;     // geometric normal, facing the viewer on back faces
    float3 worldTangent;
    float3 worldBitangent;
    float3 viewDir;         // from the surface towards the camera, normalized
    float2 uv;
    float4 screenPos;       // xy 0..1 across the view (y down), z depth, w 1
    float  time;            // seconds
    bool   frontFace;
};

struct SurfaceOutputStandard {
    float3 Albedo;
    float3 Normal;          // tangent space (0,0,1 = unchanged)
    float3 Emission;
    float  Metallic;
    float  Smoothness;
    float  Occlusion;
    float  Alpha;
};
typedef SurfaceOutputStandard SurfaceOutput;

static float4 g_SurfaceTime;
// _Time: (t/20, t, t*2, t*3)
#define _Time g_SurfaceTime
#define _SinTime float4(sin(g_SurfaceTime.y / 8), sin(g_SurfaceTime.y / 4), sin(g_SurfaceTime.y / 2), sin(g_SurfaceTime.y))
#define _CosTime float4(cos(g_SurfaceTime.y / 8), cos(g_SurfaceTime.y / 4), cos(g_SurfaceTime.y / 2), cos(g_SurfaceTime.y))
#define _WorldSpaceCameraPos (LoadFrame(g_Push.frameBuffer).cameraPos)

// Textures are material slots: tex2D(_MainTex, uv)
float4 tex2D(uint slot, float2 uv) { return MaterialTexture(slot, uv); }
float4 tex2Dlod(uint slot, float4 uvLod) { float4 p = MaterialProperty(slot, 0); return GetTex2D((uint)p.x).SampleLevel(GetSampler((uint)p.y), uvLod.xy, uvLod.w); }
float4 SampleTexture(uint slot, float2 uv) { return MaterialTexture(slot, uv); }
#define TRANSFORM_TEX(uv, name) ((uv) * name##_ST.xy + name##_ST.zw)
float3 UnpackNormal(float4 packed) { float3 n = packed.xyz * 2.0 - 1.0; return normalize(float3(n.xy, max(n.z, 1e-3))); }
float3 UnpackScaleNormal(float4 packed, float scale) { float3 n = packed.xyz * 2.0 - 1.0; n.xy *= scale; return normalize(float3(n.xy, max(n.z, 1e-3))); }
float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

SurfaceInput MakeSurfaceInput(MeshVSOut i, bool frontFace) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    SurfaceInput s;
    s.worldPos = i.worldPos;
    s.worldNormal = normalize(i.normal) * (frontFace ? 1.0 : -1.0);
    s.worldTangent = normalize(i.tangent.xyz - s.worldNormal * dot(s.worldNormal, i.tangent.xyz));
    s.worldBitangent = cross(s.worldNormal, s.worldTangent) * (i.tangent.w < 0 ? -1.0 : 1.0);
    s.viewDir = normalize(f.cameraPos - i.worldPos);
    s.uv = i.uv;
    s.screenPos = float4(i.position.xy * f.viewport.zw, i.position.z, 1);
    s.time = f.time;
    s.frontFace = frontFace;
    g_SurfaceTime = float4(f.time / 20.0, f.time, f.time * 2.0, f.time * 3.0);
    return s;
}

SurfaceOutputStandard DefaultSurfaceOutput() {
    SurfaceOutputStandard o;
    o.Albedo = 1; o.Normal = float3(0, 0, 1); o.Emission = 0;
    o.Metallic = 0; o.Smoothness = 0.5; o.Occlusion = 1; o.Alpha = 1;
    return o;
}

float3 TangentToWorld(SurfaceInput s, float3 n) {
    return normalize(s.worldTangent * n.x + s.worldBitangent * n.y + s.worldNormal * max(n.z, 1e-3));
}

float4 ShadeStandard(SurfaceInput s, SurfaceOutputStandard o) {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    FrameData f = LoadFrame(g_Push.frameBuffer);
    AmbientData amb = LoadAmbient(f);
    float3 N = TangentToWorld(s, o.Normal);
    float3 V = s.viewDir;
    float3 L = normalize(f.sunDir);
    float roughness = saturate(1.0 - o.Smoothness), metallic = saturate(o.Metallic);
    float3 albedo = max(o.Albedo, 0);
    float NoL = saturate(dot(N, L));
    float shadow = d.emissive.w < 0 ? 1.0 : NoL > 0.0 ? SampleShadow(f, g_Push.frameBuffer, s.worldPos, N, NoL) : 0.0;
    float3 color = EvaluateBRDF(N, V, L, albedo, roughness, metallic) * SunRadiance(f, amb) * shadow;
    color += EvaluateAmbient(f, amb, N, V, albedo, roughness, metallic, saturate(o.Occlusion));
    color += EvaluateLocalLights(f, s.worldPos, N, V, albedo, roughness, metallic, false);
    color += o.Emission;
    color = ApplyAerialPerspective(f, color, s.worldPos);
    return float4(color, saturate(o.Alpha));
}

float4 ShadeUnlit(SurfaceInput s, float4 color) {
    return float4(max(color.rgb, 0), saturate(color.a));
}

// Procedural helpers used by Shader Graph nodes (and free to use in .shader code)
float SG_Hash(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float SG_ValueNoise(float2 uv) {
    float2 i = floor(uv), f = frac(uv), u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(SG_Hash(i), SG_Hash(i + float2(1, 0)), u.x), lerp(SG_Hash(i + float2(0, 1)), SG_Hash(i + float2(1, 1)), u.x), u.y);
}
float SG_SimpleNoise(float2 uv, float scale) {
    float t = 0;
    t += SG_ValueNoise(uv * scale) * 0.5;
    t += SG_ValueNoise(uv * scale * 2.0) * 0.25;
    t += SG_ValueNoise(uv * scale * 4.0) * 0.125;
    return t / 0.875;
}
float2 SG_GradientDir(float2 p) { float a = SG_Hash(p) * 6.2831853; return float2(cos(a), sin(a)); }
float SG_GradientNoise(float2 uv, float scale) {
    float2 p = uv * scale, i = floor(p), f = frac(p), u = f * f * (3.0 - 2.0 * f);
    float a = dot(SG_GradientDir(i), f), b = dot(SG_GradientDir(i + float2(1, 0)), f - float2(1, 0));
    float c = dot(SG_GradientDir(i + float2(0, 1)), f - float2(0, 1)), e = dot(SG_GradientDir(i + float2(1, 1)), f - float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, e, u.x), u.y) + 0.5;
}
float SG_Voronoi(float2 uv, float density, float offset) {
    float2 p = uv * density, i = floor(p), f = frac(p);
    float best = 8.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float2 cell = float2(x, y);
            float2 h = float2(SG_Hash(i + cell), SG_Hash(i + cell + 17.0));
            float2 point_ = cell + 0.5 + 0.5 * sin(offset + 6.2831853 * h);
            best = min(best, length(point_ - f));
        }
    return best;
}
float SG_Checkerboard(float2 uv, float2 frequency) { float2 c = floor(uv * frequency); return fmod(abs(c.x + c.y), 2.0); }
float SG_Fresnel(float3 normal, float3 viewDir, float power) { return pow(1.0 - saturate(dot(normalize(normal), normalize(viewDir))), power); }

#endif
