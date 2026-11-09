#pragma once
#include "RHI/RHI.h"
#include <cstring>
// CPU resource storage for importer/terrain regression tests; no rendering or window is simulated.
namespace Stardust::Tests {
class MemoryBuffer:public RHI::Buffer {public:std::vector<u8> bytes;MemoryBuffer(const RHI::BufferDesc& desc,const void* data){m_Desc=desc;if(data||desc.memory!=RHI::MemoryUsage::GPUOnly){bytes.resize(desc.size);if(data)std::memcpy(bytes.data(),data,bytes.size());}}u32 Index()const override{return 0;}void* Mapped()const override{return bytes.empty()?nullptr:(void*)bytes.data();}};
class MemoryTexture:public RHI::Texture {public:std::vector<u8> pixels;MemoryTexture(const RHI::TextureDesc& desc){m_Desc=desc;}u32 SRV()const override{return 0;}u32 UAV(u32=0)const override{return 0;}};
class MemoryPipeline:public RHI::Pipeline {};
class MemoryDevice:public RHI::Device {
public:RHI::Backend GetBackend()const override{return RHI::Backend::Vulkan;}const char* GetAdapterName()const override{return "CPU resource test";}
 Ref<RHI::Buffer> CreateBuffer(const RHI::BufferDesc& d,const void* p=nullptr)override{return MakeRef<MemoryBuffer>(d,p);}Ref<RHI::Texture> CreateTexture(const RHI::TextureDesc& d)override{return MakeRef<MemoryTexture>(d);}
 void UploadTexture(RHI::Texture* texture,const void* pixels,u64 size)override{if(size<=1024*1024){auto* target=static_cast<MemoryTexture*>(texture);target->pixels.assign((const u8*)pixels,(const u8*)pixels+size);}}void UploadBuffer(RHI::Buffer* buffer,const void* data,u64 size,u64 offset=0)override{auto* b=static_cast<MemoryBuffer*>(buffer);if(b->bytes.size()<offset+size)b->bytes.resize(offset+size);std::memcpy(b->bytes.data()+offset,data,size);}
 Ref<RHI::Pipeline> CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc&)override{return MakeRef<MemoryPipeline>();}Ref<RHI::Pipeline> CreateComputePipeline(const RHI::ComputePipelineDesc&)override{return MakeRef<MemoryPipeline>();}
 bool BeginFrame()override{return false;}RHI::CommandList& Cmd()override{throw std::logic_error("GPU command requested by a CPU-only regression test");}RHI::Texture* BackBuffer()override{return nullptr;}void EndFrame()override{}void WaitIdle()override{}void SetVSync(bool)override{}u64 FrameNumber()const override{return 0;}u32 FrameIndex()const override{return 0;}
 void BeginGpuTimer(const char*)override{}void EndGpuTimer()override{}const std::vector<RHI::GpuTiming>& GpuTimings()const override{static std::vector<RHI::GpuTiming> timings;return timings;}
 void ImGuiInit()override{}void ImGuiNewFrame()override{}void ImGuiRender(RHI::Texture*)override{}void ImGuiShutdown()override{}void* ImGuiTexture(RHI::Texture*)override{return nullptr;}
};
}
