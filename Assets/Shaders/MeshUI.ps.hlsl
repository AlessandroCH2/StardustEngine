#include "UI.hlsli"
MeshPSOut main(MeshVSOut i) {
    MeshPSOut o; o.color=UIColor(i); if(o.color.a<0.01) discard;
    o.entityId=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex).ids.z; return o;
}
