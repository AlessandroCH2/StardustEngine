#include "MeshCommon.hlsli"
#include "PBR.hlsli"
#include "Shadows.hlsli"
MeshPSOut main(MeshVSOut i) {
    DrawData d=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex);
    FrameData f=LoadFrame(g_Push.frameBuffer);AmbientData ambient=LoadAmbient(f);
    float2 p=i.worldPos.xz*max(.01,d.tilingOffset.x);
    float t=f.time*d.tilingOffset.y;
    float2 ripple=float2(cos(p.x*1.7+t)+sin(p.y*.8+t*.7),sin(p.y*1.2-t)+cos(p.x*.6+t*.8));
    float3 N=normalize(float3(ripple.x*d.params.z,1,ripple.y*d.params.z));
    float3 V=normalize(f.cameraPos-i.worldPos),L=normalize(f.sunDir);
    float fresnel=.02+.98*pow(1-saturate(dot(N,V)),5);
    float3 reflected=SampleSky(f,reflect(-V,N));
    float shadow=SampleShadow(f,g_Push.frameBuffer,i.worldPos,N,saturate(dot(N,L)));
    float3 color=lerp(d.color.rgb,reflected,fresnel*.8);
    color+=EvaluateBRDF(N,V,L,d.color.rgb,max(.03,d.params.x),0)*SunRadiance(f,ambient)*shadow;
    color+=EvaluateAmbient(f,ambient,N,V,d.color.rgb,d.params.x,0,1);
    MeshPSOut o;o.color=float4(ApplyAerialPerspective(f,color,i.worldPos),saturate(d.color.a+fresnel*.3));o.entityId=d.ids.z;return o;
}
