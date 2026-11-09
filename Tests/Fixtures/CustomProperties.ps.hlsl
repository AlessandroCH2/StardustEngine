#include "MeshCommon.hlsli"
MeshPSOut main(MeshVSOut i){DrawData d=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex);MeshPSOut o;o.color=MaterialProperty(0,float4(1,0,0,1))*MaterialFloat(1,1);o.entityId=d.ids.z;return o;}
