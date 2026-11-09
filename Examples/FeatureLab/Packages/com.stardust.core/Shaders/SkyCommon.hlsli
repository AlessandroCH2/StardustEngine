#include "Atmosphere.hlsli"
struct SkyPush {uint frameBuffer;uint mode;float2 pad;float4 color;uint4 facesA;uint4 facesB;float4 parameters;};
PUSH_CONSTANTS(SkyPush,g_Push);
struct PSOut {float4 color:SV_Target0;uint entityId:SV_Target1;};
float3 SkyDirection(float2 uv){FrameData f=LoadFrame(g_Push.frameBuffer);float2 ndc=uv*float2(2,-2)+float2(-1,1);float4 world=mul(f.invViewProj,float4(ndc,.5,1));float3 d=normalize(world.xyz/world.w-f.cameraPos);if(abs(f.proj[3][3])>.5)d=-normalize(float3(f.view[2][0],f.view[2][1],f.view[2][2]));float c=cos(g_Push.parameters.x),s=sin(g_Push.parameters.x);return float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);}
