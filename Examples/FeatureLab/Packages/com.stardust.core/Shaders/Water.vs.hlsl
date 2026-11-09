#include "MeshCommon.hlsli"
#include "WaterWaves.hlsli"
MeshVSOut main(uint vid:SV_VertexID){FrameData f=LoadFrame(g_Push.frameBuffer);DrawData d=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex);Vertex v=LoadVertex(g_Push.vertexBuffer,vid);float4 world=mul(d.model,float4(v.position,1));float h;float2 slope;WaterSurface(world.xz*max(.01,d.tilingOffset.x),f.time*d.tilingOffset.y,d.params.z,h,slope);world.y+=h;MeshVSOut o;o.position=mul(f.viewProj,world);o.worldPos=world.xyz;o.normal=normalize(float3(-slope.x,1,-slope.y));o.tangent=float4(1,0,0,1);o.uv=v.uv;return o;}
