#include "SkyCommon.hlsli"

PSOut main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    float2 ndc = uv * float2(2, -2) + float2(-1, 1);
    float4 world = mul(f.invViewProj, float4(ndc, 0.5, 1.0));
    float3 dir = SkyDirection(uv);

    if(g_Push.mode!=0){dir=SkyDirection(uv);float3 color=g_Push.color.rgb;if(g_Push.mode==2){uint tex=g_Push.facesB.z;if(tex!=0xFFFFFFFF){float2 panorama=float2(atan2(dir.x,dir.z)/(2*3.14159265)+.5,acos(clamp(dir.y,-1,1))/3.14159265);color*=GetTex2D(tex).SampleLevel(GetSampler(SAMPLER_LINEAR_WRAP),panorama,0).rgb;}}
    if(g_Push.mode==3){float3 a=abs(dir);uint side;float2 st;if(a.x>=a.y&&a.x>=a.z){side=dir.x>0?0:1;st=float2(dir.x>0?-dir.z:dir.z,-dir.y)/a.x;}else if(a.y>=a.z){side=dir.y>0?2:3;st=float2(dir.x,dir.y>0?dir.z:-dir.z)/a.y;}else{side=dir.z>0?4:5;st=float2(dir.z>0?dir.x:-dir.x,-dir.y)/a.z;}uint tex=side<4?g_Push.facesA[side]:g_Push.facesB[side-4];if(tex!=0xFFFFFFFF)color=GetTex2D(tex).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP),st*.5+.5,0).rgb;}
    PSOut result;result.color=float4(color*exp2(g_Push.parameters.y),1);result.entityId=0;return result;}

    float3 c = SampleSky(f, dir);

    // Sun disk, dimmed and reddened by the atmosphere, with limb darkening
    float3 sunDir = normalize(f.sunDir);
    float cosAngle = dot(dir, sunDir);
    const float cosSunRadius = 0.99996; // ~0.5 degrees
    if (cosAngle > cosSunRadius && dir.y > -0.01) {
        float3 T = SampleTransmittance(f.atmosphereInfo.x, CameraHeightKm(f), dir.y);
        float r = saturate((1.0 - cosAngle) / (1.0 - cosSunRadius));
        float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - r * r));
        c += f.sunColor * f.sunIntensity * T * 400.0 * limb;
    }

    // Volumetric clouds (rgb = in-scattered light, a = transmittance), rendered at reduced resolution
    if (f.atmosphereInfo.w != 0xFFFFFFFF) {
        float4 clouds = GetTex2D(f.atmosphereInfo.w).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP), uv, 0);
        c = c * clouds.a + clouds.rgb;
    }

    PSOut o;
    o.color = float4(c, 1.0);
    o.entityId = 0;
    return o;
}
