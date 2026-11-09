#include "Common.hlsli"

struct Push { uint frameBuffer; float extent; };
PUSH_CONSTANTS(Push, g_Push);

// Anti-aliased grid line coverage for a given cell size
float GridLine(float2 p, float cell) {
    float2 coord = p / cell;
    float2 d = fwidth(coord);
    float2 g = abs(frac(coord - 0.5) - 0.5) / d;
    return 1.0 - min(min(g.x, g.y), 1.0);
}

float4 main(float4 pos : SV_Position, float3 worldPos : POSITION) : SV_Target0 {
    FrameData f = LoadFrame(g_Push.frameBuffer);
    if(g_Push.extent<0){float2 p=worldPos.xy;float step=pow(10,floor(log10(max(fwidth(p).x*f.viewport.x/10,.001))));float coverage=GridLine(p,step);float3 c=.5.xxx;float2 dw=max(fwidth(p),.0001.xx);float x=1-saturate(abs(p.y)/(dw.y*1.5)),y=1-saturate(abs(p.x)/(dw.x*1.5));float alpha=coverage*.2;if(x>.01){c=float3(.75,.25,.25);alpha=max(alpha,x*.65);}if(y>.01){c=float3(.3,.65,.3);alpha=max(alpha,y*.65);}return float4(c,alpha);}
    float2 p = worldPos.xz;
    float minor = GridLine(p, 1.0);
    float major = GridLine(p, 10.0);

    float3 color = float3(0.55, 0.55, 0.55);
    float alpha = max(minor * 0.45, major * 0.7);

    // Axis lines: X axis (z = 0) red, Z axis (x = 0) blue
    float2 dw = fwidth(p);
    float xAxis = 1.0 - saturate(abs(p.y) / (dw.y * 1.5));
    float zAxis = 1.0 - saturate(abs(p.x) / (dw.x * 1.5));
    if (xAxis > 0.01) { color = float3(0.85, 0.25, 0.25); alpha = max(alpha, xAxis * 0.9); }
    if (zAxis > 0.01) { color = float3(0.25, 0.45, 0.9); alpha = max(alpha, zAxis * 0.9); }

    // Fade with distance and at grazing angles to avoid moire
    float dist = length(worldPos - f.cameraPos);
    float fade = 1.0 - saturate(dist / (g_Push.extent * 0.8));
    float3 V = normalize(f.cameraPos - worldPos);
    fade *= saturate(abs(V.y) * 4.0);
    return float4(color, alpha * fade);
}
