// FXAA (simplified 3.11 quality preset): edge detection on luma stored in alpha, then a search along the edge.
#include "Common.hlsli"

struct Push { uint source; };
PUSH_CONSTANTS(Push, g_Push);

static const float kEdgeThresholdMin = 0.0312;
static const float kEdgeThreshold = 0.125;
static const float kSubpix = 0.75;
static const int kSearchSteps = 10;
static const float kStepSizes[10] = { 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0 };

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    Texture2D t = GetTex2D(g_Push.source);
    SamplerState s = GetSampler(SAMPLER_LINEAR_CLAMP);
    uint w, h;
    t.GetDimensions(w, h);
    float2 rcp = 1.0 / float2(w, h);

    float4 center = t.SampleLevel(s, uv, 0);
    float lM = center.a;
    float lN = t.SampleLevel(s, uv + float2(0, -rcp.y), 0).a;
    float lS = t.SampleLevel(s, uv + float2(0, rcp.y), 0).a;
    float lE = t.SampleLevel(s, uv + float2(rcp.x, 0), 0).a;
    float lW = t.SampleLevel(s, uv + float2(-rcp.x, 0), 0).a;
    float maxL = max(lM, max(max(lN, lS), max(lE, lW)));
    float minL = min(lM, min(min(lN, lS), min(lE, lW)));
    float range = maxL - minL;
    if (range < max(kEdgeThresholdMin, maxL * kEdgeThreshold)) return float4(center.rgb, 1.0);

    float lNW = t.SampleLevel(s, uv + float2(-rcp.x, -rcp.y), 0).a;
    float lNE = t.SampleLevel(s, uv + float2(rcp.x, -rcp.y), 0).a;
    float lSW = t.SampleLevel(s, uv + float2(-rcp.x, rcp.y), 0).a;
    float lSE = t.SampleLevel(s, uv + float2(rcp.x, rcp.y), 0).a;

    // sub-pixel aliasing amount
    float avg = (2.0 * (lN + lS + lE + lW) + lNW + lNE + lSW + lSE) / 12.0;
    float subpix = saturate(abs(avg - lM) / range);
    subpix = smoothstep(0.0, 1.0, subpix);
    subpix = subpix * subpix * kSubpix;

    float edgeH = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
    float edgeV = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
    bool horizontal = edgeH >= edgeV;

    float l1 = horizontal ? lN : lW;
    float l2 = horizontal ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    bool steep1 = g1 >= g2;
    float gradScaled = 0.25 * max(g1, g2);
    float stepLen = horizontal ? rcp.y : rcp.x;
    float lAvg;
    if (steep1) { stepLen = -stepLen; lAvg = 0.5 * (l1 + lM); }
    else lAvg = 0.5 * (l2 + lM);

    float2 cuv = uv;
    if (horizontal) cuv.y += stepLen * 0.5; else cuv.x += stepLen * 0.5;
    float2 off = horizontal ? float2(rcp.x, 0) : float2(0, rcp.y);

    float2 uv1 = cuv - off, uv2 = cuv + off;
    float e1 = t.SampleLevel(s, uv1, 0).a - lAvg;
    float e2 = t.SampleLevel(s, uv2, 0).a - lAvg;
    bool done1 = abs(e1) >= gradScaled, done2 = abs(e2) >= gradScaled;
    [loop] for (int i = 1; i < kSearchSteps && !(done1 && done2); ++i) {
        if (!done1) { uv1 -= off * kStepSizes[i]; e1 = t.SampleLevel(s, uv1, 0).a - lAvg; done1 = abs(e1) >= gradScaled; }
        if (!done2) { uv2 += off * kStepSizes[i]; e2 = t.SampleLevel(s, uv2, 0).a - lAvg; done2 = abs(e2) >= gradScaled; }
    }
    float d1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;
    float d2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;
    bool dir1 = d1 < d2;
    float dmin = min(d1, d2);
    float edgeLen = d1 + d2;
    float pixelOffset = -dmin / edgeLen + 0.5;
    bool lCenterSmaller = lM < lAvg;
    bool correct = ((dir1 ? e1 : e2) < 0.0) != lCenterSmaller;
    float finalOffset = correct ? pixelOffset : 0.0;
    finalOffset = max(finalOffset, subpix);

    float2 fuv = uv;
    if (horizontal) fuv.y += finalOffset * stepLen; else fuv.x += finalOffset * stepLen;
    return float4(t.SampleLevel(s, fuv, 0).rgb, 1.0);
}
