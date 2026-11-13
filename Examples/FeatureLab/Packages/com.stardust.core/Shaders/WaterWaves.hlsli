#ifndef WATER_WAVES
#define WATER_WAVES
float2 WaterWind(){float2 wind=MaterialProperty(3,float4(1,.3,0,0)).xy;return dot(wind,wind)>.0001?normalize(wind):float2(1,0);}
// A directional spectrum, not a scrolling sine texture. Incommensurate wavelengths
// and dispersion give independent periods; no modulo-time loop or fixed world direction.
void WaterSurface(float2 position,float time,float footprint,out float height,out float2 slope,out float2 displacement){
 float amplitude=max(0,MaterialFloat(4,.16)),scale=max(.1,MaterialFloat(5,1)),spread=saturate(MaterialFloat(13,.75));float2 wind=WaterWind();height=0;slope=0;displacement=0;
 const float lengths[8]={22,13.17,8.63,5.29,3.13,1.87,1.09,.637};
 const float angles[8]={0,.83,-.67,1.31,-1.15,.31,-.38,1.87};
 const float weights[8]={.57,.32,.22,.14,.085,.05,.029,.017};
 [unroll]for(int wave=0;wave<8;++wave){float angle=angles[wave]*spread;float2 direction=float2(wind.x*cos(angle)-wind.y*sin(angle),wind.x*sin(angle)+wind.y*cos(angle));float k=6.2831853/(lengths[wave]*scale);float phase=dot(direction,position)*k-sqrt(9.81*k)*time+wave*7.137;float a=amplitude*weights[wave];height+=a*sin(phase);slope+=direction*(a*k*cos(phase))*(1-smoothstep(.4,1.6,k*footprint));if(wave<4)displacement+=direction*a*cos(phase)*MaterialFloat(6,.4);}
}
#endif
