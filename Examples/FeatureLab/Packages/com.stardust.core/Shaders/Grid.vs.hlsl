#include "Common.hlsli"

struct Push { uint frameBuffer; float extent; };
PUSH_CONSTANTS(Push, g_Push);

struct VSOut {
    float4 position : SV_Position;
    float3 worldPos : POSITION;
};

// Large quad on the y = 0 plane, centered under the camera
VSOut main(uint vid : SV_VertexID) {
    static const float2 corners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };
    FrameData f = LoadFrame(g_Push.frameBuffer);
    if(g_Push.extent<0){VSOut o;o.position=float4(corners[vid],0,1);float4 a=mul(f.invViewProj,float4(corners[vid],1,1)),b=mul(f.invViewProj,float4(corners[vid],.5,1));float3 origin=a.xyz/a.w,direction=b.xyz/b.w-origin;o.worldPos=origin+direction*(-origin.z/direction.z);return o;}
    float2 c = corners[vid] * g_Push.extent + f.cameraPos.xz;
    VSOut o;
    o.worldPos = float3(c.x, 0.0, c.y);
    o.position = mul(f.viewProj, float4(o.worldPos, 1.0));
    o.position.z *= 1.0005; // tiny pull towards the camera (reverse-Z) so a ground plane at y = 0 does not z-fight
    return o;
}
