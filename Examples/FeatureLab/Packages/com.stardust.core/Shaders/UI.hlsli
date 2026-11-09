#include "MeshCommon.hlsli"
float4 UIColor(MeshVSOut i) {
    DrawData d=LoadDraw(g_Push.drawBuffer,g_Push.drawIndex);
    float4 sample=GetTex2D(d.ids.x).Sample(GetSampler(SAMPLER_LINEAR_CLAMP),MaterialUV(d,i.uv));
    if(d.params.z>0.5) {
        float edge=d.params.w;
        float width=max(fwidth(sample.a),0.002);
        float face=smoothstep(edge-width,edge+width,sample.a);
        float outline=smoothstep(edge-d.params.x-width,edge-d.params.x+width,sample.a);
        return float4(lerp(d.emissive.rgb,d.color.rgb,face),max(face,outline)*d.color.a);
    }
    return sample*d.color;
}
