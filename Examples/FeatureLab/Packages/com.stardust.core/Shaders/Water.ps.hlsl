#include "MeshCommon.hlsli"
#include "PBR.hlsli"
#include "Shadows.hlsli"
#include "WaterWaves.hlsli"
float3 WaterWorld(FrameData f,float2 uv,float depth){float4 p=mul(f.invViewProj,float4(uv.x*2-1,1-uv.y*2,depth,1));return p.xyz/p.w;}
MeshPSOut main(MeshVSOut i){
 DrawData d=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex);FrameData f=LoadFrame(g_Push.frameBuffer);AmbientData ambient=LoadAmbient(f);
 float time=f.time*MaterialFloat(2,1),h;float2 slope,displacement;float footprint=max(length(ddx(i.worldPos.xz)),length(ddy(i.worldPos.xz)));WaterSurface(i.worldPos.xz,time,footprint,h,slope,displacement);
 float2 wind=WaterWind(),crossWind=float2(-wind.y,wind.x);float2 small=float2(Fbm(i.worldPos.xz*2+wind*time*.37,3),Fbm(i.worldPos.zx*2-crossWind*time*.23,3))-.5;
 float detail=MaterialFloat(7,.45)*saturate(.12/max(.001,footprint))*saturate(25/max(1,length(f.cameraPos-i.worldPos)));
 float3 N=normalize(float3(-slope.x-small.x*detail,1,-slope.y-small.y*detail));float3 V=normalize(f.cameraPos-i.worldPos),L=normalize(f.sunDir);
 float2 uv=i.position.xy*f.viewport.zw;float sceneDepth=GetTex2D(f.lightInfo.w).SampleLevel(GetSampler(SAMPLER_POINT_CLAMP),uv,0).r;
 float thickness=sceneDepth>0?max(0,dot(WaterWorld(f,uv,sceneDepth)-i.worldPos,-V)):40;
 float2 refractedUV=clamp(uv+N.xz*MaterialFloat(12,.018)*saturate(thickness*.5),.001,.999);float refractedDepth=GetTex2D(f.lightInfo.w).SampleLevel(GetSampler(SAMPLER_POINT_CLAMP),refractedUV,0).r;
 if(refractedDepth>0&&dot(WaterWorld(f,refractedUV,refractedDepth)-i.worldPos,-V)<0)refractedUV=uv;
 float3 background=GetTex2D(f.lightInfo.z).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP),refractedUV,0).rgb;
 float3 absorption=exp(-max(0,MaterialProperty(10,float4(.42,.12,.065,0)).rgb)*thickness);float3 shallow=MaterialProperty(0,float4(.055,.32,.28,1)).rgb,deep=MaterialProperty(1,float4(.018,.12,.18,1)).rgb;float lightShadow=SampleShadow(f,g_Push.frameBuffer,i.worldPos,N,saturate(dot(N,L)));float3 scattering=lerp(shallow,deep,1-exp(-thickness*.15))*(SceneAmbient(f,ambient,N)+SunRadiance(f,ambient)*(.06*saturate(L.y)*lightShadow));
 float3 transmission=background*absorption+scattering*(1-absorption);
 float3 R=reflect(-V,N),reflection=SampleSky(f,R);
 // Screen-space reflection with sky fallback outside the camera frustum.
 [loop]for(int step=1;step<=24;++step){float travel=step*.35+step*step*.025;float3 rayPoint=i.worldPos+N*.08+R*travel;float4 clip=mul(f.viewProj,float4(rayPoint,1));if(clip.w<=0)break;float2 sampleUV=float2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5);if(any(sampleUV<.002)||any(sampleUV>.998))break;float z=GetTex2D(f.lightInfo.w).SampleLevel(GetSampler(SAMPLER_POINT_CLAMP),sampleUV,0).r;if(z<=0)continue;float3 surface=WaterWorld(f,sampleUV,z);float difference=length(rayPoint-f.cameraPos)-length(surface-f.cameraPos);if(difference>0&&difference<.35+travel*.02){float edge=saturate(min(min(sampleUV.x,sampleUV.y),min(1-sampleUV.x,1-sampleUV.y))*12);reflection=lerp(reflection,GetTex2D(f.lightInfo.z).SampleLevel(GetSampler(SAMPLER_LINEAR_CLAMP),sampleUV,0).rgb,edge);break;}}
 float fresnel=.02+.98*pow(1-saturate(dot(N,V)),5);float shadow=SampleShadow(f,g_Push.frameBuffer,i.worldPos,N,saturate(dot(N,L)));
 float3 color=lerp(transmission,reflection,fresnel);color+=EvaluateBRDF(N,V,L,float3(.02,.02,.02),sqrt(max(.025,MaterialFloat(11,.09))*max(.025,MaterialFloat(11,.09))+min(.2,max(dot(ddx(N),ddx(N)),dot(ddy(N),ddy(N)))*.5)),0)*SunRadiance(f,ambient)*shadow;
 float foamNoise=Fbm(i.worldPos.xz*3+wind*time*.31,3);float shore=1-smoothstep(.015,max(.02,MaterialFloat(9,.7)),thickness);float crest=smoothstep(.14,.35,length(slope))*saturate(h/max(.01,MaterialFloat(4,.16)));float foam=saturate((shore*smoothstep(.24,.68,foamNoise)+crest*.18)*MaterialFloat(8,.6));color=lerp(color,float3(.8,.87,.85)*(SceneAmbient(f,ambient,N)+.25),foam);
 MeshPSOut o;o.color=float4(ApplyAerialPerspective(f,color,i.worldPos),1);o.entityId=d.ids.z;return o;
}

