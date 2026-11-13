#include "MeshCommon.hlsli"
float4 UIShade(MeshVSOut i, DrawData d);
float4 UIColor(MeshVSOut i) {
    DrawData d=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex);
    float4 color=UIShade(i,d);
    // normal.z flags a Mask: normal.xy is the UV in the mask image, stored in the normal-map slot
    if(i.normal.z>0.5) color.a*=GetTex2D(d.ids.y).Sample(GetSampler(SAMPLER_LINEAR_CLAMP),i.normal.xy).a;
    return color;
}
float4 UIShade(MeshVSOut i, DrawData d) {
    // Sprites set params.y (metallic) to use their texture's own filtering (point for pixel art); canvases stay linear clamp
    float4 sample=GetTex2D(d.ids.x).Sample(GetSampler(d.params.y>0.5?d.sampling.x:SAMPLER_LINEAR_CLAMP),MaterialUV(d,i.uv));
    if(d.params.z>0.5) {
        float edge=d.params.w;
        float width=max(fwidth(sample.a),0.002);
        float face=smoothstep(edge-width,edge+width,sample.a);
        float outline=smoothstep(edge-d.params.x-width,edge-d.params.x+width,sample.a);
        return float4(lerp(d.emissive.rgb,d.color.rgb,face),max(face,outline)*d.color.a);
    }
    return sample*d.color;
}
