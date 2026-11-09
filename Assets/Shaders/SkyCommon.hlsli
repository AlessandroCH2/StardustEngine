#include "Atmosphere.hlsli"
struct SkyPush {uint frameBuffer;uint mode;float2 pad;float4 color;uint4 facesA;uint4 facesB;float4 parameters;uint4 materialProperties;};
PUSH_CONSTANTS(SkyPush,g_Push);
float4 MaterialProperty(uint slot,float4 fallback){if(slot>=g_Push.materialProperties.z)return fallback;return GetBuffer(g_Push.materialProperties.x).Load<float4>((g_Push.materialProperties.y+slot)*16);}
float MaterialFloat(uint slot,float fallback){return MaterialProperty(slot,float4(fallback,0,0,0)).x;}
float4 MaterialTexture(uint slot,float2 uv){float4 p=MaterialProperty(slot,0);return GetTex2D((uint)p.x).Sample(GetSampler((uint)p.y),uv);}

struct PSOut {float4 color:SV_Target0;uint entityId:SV_Target1;};
float3 SkyDirection(float2 uv){FrameData f=LoadFrame(g_Push.frameBuffer);float2 ndc=uv*float2(2,-2)+float2(-1,1);float4 world=mul(f.invViewProj,float4(ndc,.5,1));float3 d=normalize(world.xyz/world.w-f.cameraPos);if(abs(f.proj[3][3])>.5)d=-normalize(float3(f.view[2][0],f.view[2][1],f.view[2][2]));float c=cos(g_Push.parameters.x),s=sin(g_Push.parameters.x);return float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);}
