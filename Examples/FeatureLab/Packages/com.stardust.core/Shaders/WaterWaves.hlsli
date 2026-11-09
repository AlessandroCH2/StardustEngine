#ifndef WATER_WAVES
#define WATER_WAVES
// Directional deep-water waves; derivatives are shared by vertex and pixel stages.
void WaterWave(float2 p,float time,float2 direction,float wavelength,float amplitude,inout float height,inout float2 slope){float k=6.2831853/wavelength;float phase=dot(normalize(direction),p)*k-sqrt(9.81*k)*time;height+=amplitude*sin(phase);slope+=normalize(direction)*(amplitude*k*cos(phase));}
void WaterSurface(float2 p,float time,float strength,out float height,out float2 slope){height=0;slope=0;WaterWave(p,time,float2(1,.35),12,1.1*strength,height,slope);WaterWave(p,time,float2(.7,-1),5,.45*strength,height,slope);WaterWave(p,time,float2(-.4,.9),2,.16*strength,height,slope);}
#endif
