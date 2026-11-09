// Bindless resource access shared by every shader.
// Vulkan: one descriptor set with typed arrays (bindings must match RHI/Vulkan/VulkanDevice.h).
// D3D12 (SM 6.6): ResourceDescriptorHeap / SamplerDescriptorHeap (same indices).
#ifndef BINDLESS_HLSLI
#define BINDLESS_HLSLI

#define SAMPLER_LINEAR_CLAMP 0
#define SAMPLER_LINEAR_WRAP  1
#define SAMPLER_POINT_CLAMP  2
#define SAMPLER_POINT_WRAP   3
#define SAMPLER_ANISO_WRAP   4
#define SAMPLER_SHADOW       5

#ifdef STARDUST_VULKAN
[[vk::binding(0, 0)]] Texture2D                g_Tex2D[];
[[vk::binding(0, 0)]] Texture2D<uint4>         g_Tex2DUint[];   // same descriptors, for integer formats
[[vk::binding(1, 0)]] Texture2DArray           g_Tex2DArray[];
[[vk::binding(2, 0)]] Texture3D                g_Tex3D[];
[[vk::binding(3, 0)]] TextureCube              g_TexCube[];
[[vk::binding(4, 0)]] RWTexture2D<float4>      g_RWTex2D[];
[[vk::binding(5, 0)]] RWTexture3D<float4>      g_RWTex3D[];
[[vk::binding(6, 0)]] RWTexture2DArray<float4> g_RWTex2DArray[];
[[vk::binding(7, 0)]] RWByteAddressBuffer      g_Buffers[];
[[vk::binding(8, 0)]] SamplerState             g_Samplers[];
[[vk::binding(8, 0)]] SamplerComparisonState   g_CmpSamplers[];

#define PUSH_CONSTANTS(Type, name) [[vk::push_constant]] ConstantBuffer<Type> name

Texture2D                GetTex2D(uint i)       { return g_Tex2D[NonUniformResourceIndex(i)]; }
Texture2D<uint4>         GetTex2DUint(uint i)   { return g_Tex2DUint[NonUniformResourceIndex(i)]; }
Texture2DArray           GetTex2DArray(uint i)  { return g_Tex2DArray[NonUniformResourceIndex(i)]; }
Texture3D                GetTex3D(uint i)       { return g_Tex3D[NonUniformResourceIndex(i)]; }
TextureCube              GetTexCube(uint i)     { return g_TexCube[NonUniformResourceIndex(i)]; }
RWTexture2D<float4>      GetRWTex2D(uint i)     { return g_RWTex2D[NonUniformResourceIndex(i)]; }
RWTexture3D<float4>      GetRWTex3D(uint i)     { return g_RWTex3D[NonUniformResourceIndex(i)]; }
RWTexture2DArray<float4> GetRWTex2DArray(uint i){ return g_RWTex2DArray[NonUniformResourceIndex(i)]; }
RWByteAddressBuffer      GetBuffer(uint i)      { return g_Buffers[NonUniformResourceIndex(i)]; }
SamplerState             GetSampler(uint i)     { return g_Samplers[i]; }
SamplerComparisonState   GetCmpSampler(uint i)  { return g_CmpSamplers[i]; }
#else
#define PUSH_CONSTANTS(Type, name) ConstantBuffer<Type> name : register(b0)
Texture2D                GetTex2D(uint i)       { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
Texture2D<uint4>         GetTex2DUint(uint i)   { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
Texture2DArray           GetTex2DArray(uint i)  { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
Texture3D                GetTex3D(uint i)       { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
TextureCube              GetTexCube(uint i)     { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
RWTexture2D<float4>      GetRWTex2D(uint i)     { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
RWTexture3D<float4>      GetRWTex3D(uint i)     { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
RWTexture2DArray<float4> GetRWTex2DArray(uint i){ return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
RWByteAddressBuffer      GetBuffer(uint i)      { return ResourceDescriptorHeap[NonUniformResourceIndex(i)]; }
SamplerState             GetSampler(uint i)     { return SamplerDescriptorHeap[i]; }
SamplerComparisonState   GetCmpSampler(uint i)  { return SamplerDescriptorHeap[i]; }
#endif

// Matrices in buffers are stored column-major (glm). Load as 4 columns -> use mul(M, v).
float4x4 LoadMatrix(RWByteAddressBuffer b, uint offset) {
    float4 c0 = b.Load<float4>(offset + 0);
    float4 c1 = b.Load<float4>(offset + 16);
    float4 c2 = b.Load<float4>(offset + 32);
    float4 c3 = b.Load<float4>(offset + 48);
    return transpose(float4x4(c0, c1, c2, c3));
}

#endif
