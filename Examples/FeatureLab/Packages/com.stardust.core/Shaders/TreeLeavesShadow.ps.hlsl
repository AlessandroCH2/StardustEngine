#include "Tree.hlsli"

void main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    if (LeafAlpha(uv) < 0.5) discard;
}
