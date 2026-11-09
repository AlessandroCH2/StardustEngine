#pragma once
#include "RHI/RHI.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <unordered_map>
#include <array>
#include <deque>

namespace Stardust::RHI::Vk {

#define VK_CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { ::Stardust::RHI::Vk::Fatal(_r, #x, __FILE__, __LINE__); } } while (0)
[[noreturn]] void Fatal(VkResult r, const char* expr, const char* file, int line);

VkFormat ToVk(Format f);

class VulkanDevice;

// Bindless binding slots inside descriptor set 0 (must match Assets/Shaders/Bindless.hlsli)
enum Binding : u32 {
    kBindTex2D = 0, kBindTex2DArray = 1, kBindTex3D = 2, kBindTexCube = 3,
    kBindRWTex2D = 4, kBindRWTex3D = 5, kBindRWTex2DArray = 6,
    kBindBuffers = 7, kBindSamplers = 8, kBindCount = 9,
};

// Simple free-list allocator for descriptor indices
class IndexAllocator {
public:
    explicit IndexAllocator(u32 capacity = 0) : m_Capacity(capacity) {}
    u32 Allocate();
    void Free(u32 i) { if (i != kInvalidIndex) m_Free.push_back(i); }
private:
    u32 m_Capacity, m_Next = 0;
    std::vector<u32> m_Free;
};

class VulkanTexture final : public Texture {
public:
    VulkanTexture(VulkanDevice& dev, const TextureDesc& desc);                      // owns the image
    VulkanTexture(VulkanDevice& dev, const TextureDesc& desc, VkImage swapImage);   // wraps a swapchain image
    ~VulkanTexture() override;
    u32 SRV() const override { return m_SRV; }
    u32 UAV(u32 mip) const override { return mip < m_UAVs.size() ? m_UAVs[mip] : kInvalidIndex; }

    VkImageView AttachmentView(u32 layer, u32 mip);
    VkImageAspectFlags Aspect() const { return m_Aspect; }

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView srvView = VK_NULL_HANDLE;
    VkDescriptorSet imguiSet = VK_NULL_HANDLE;

private:
    void CreateViews();
    VulkanDevice& m_Dev;
    bool m_OwnsImage = true;
    VkImageAspectFlags m_Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    u32 m_SRV = kInvalidIndex;
    std::vector<u32> m_UAVs;
    std::vector<VkImageView> m_UAVViews;
    std::unordered_map<u32, VkImageView> m_AttachmentViews; // key = layer << 16 | mip
};

class VulkanBuffer final : public Buffer {
public:
    VulkanBuffer(VulkanDevice& dev, const BufferDesc& desc);
    ~VulkanBuffer() override;
    u32 Index() const override { return m_Index; }
    void* Mapped() const override { return m_Mapped; }
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
private:
    VulkanDevice& m_Dev;
    void* m_Mapped = nullptr;
    u32 m_Index = kInvalidIndex;
};

class VulkanPipeline final : public Pipeline {
public:
    VulkanPipeline(VulkanDevice& dev, VkPipeline p, bool compute) : m_Dev(dev), pipeline(p) { m_Compute = compute; }
    ~VulkanPipeline() override;
    VulkanDevice& m_Dev;
    VkPipeline pipeline;
};

class VulkanCommandList final : public CommandList {
public:
    explicit VulkanCommandList(VulkanDevice& dev) : m_Dev(dev) {}
    void Begin(VkCommandBuffer cb) { m_Cmd = cb; m_Pipeline = nullptr; }

    void Barrier(Texture* tex, ResourceState newState) override;
    void MemoryBarrier() override;
    void BeginRendering(const RenderingDesc& desc) override;
    void EndRendering() override;
    void SetViewport(float x, float y, float w, float h) override;
    void SetScissor(i32 x, i32 y, u32 w, u32 h) override;
    void SetPipeline(Pipeline* pipeline) override;
    void PushConstants(const void* data, u32 size) override;
    void SetIndexBuffer(Buffer* buffer, bool u16Indices, u64 offset) override;
    void Draw(u32 vc, u32 ic, u32 fv, u32 fi) override;
    void DrawIndexed(u32 idx, u32 ic, u32 fi, i32 vo, u32 finst) override;
    void DrawIndexedIndirect(Buffer* args, u64 offset, u32 drawCount, u32 stride) override;
    void DrawIndirect(Buffer* args, u64 offset, u32 drawCount, u32 stride) override;
    void Dispatch(u32 x, u32 y, u32 z) override;
    void DispatchIndirect(Buffer* args, u64 offset) override;
    void CopyBuffer(Buffer* src, u64 so, Buffer* dst, u64 dof, u64 size) override;
    void FillBuffer(Buffer* dst, u64 offset, u64 size, u32 value) override;
    void CopyTexture(Texture* src, Texture* dst) override;
    void CopyTextureToBuffer(Texture* src, Buffer* dst, u32 x, u32 y, u32 w, u32 h, u64 dstOffset) override;
    void CopyBufferToTexture(Buffer* src, u64 srcOffset, Texture* dst, u32 x, u32 y, u32 w, u32 h) override;
    void BeginMarker(const char* name) override;
    void EndMarker() override;

    VkCommandBuffer Raw() const { return m_Cmd; }

private:
    VulkanDevice& m_Dev;
    VkCommandBuffer m_Cmd = VK_NULL_HANDLE;
    VulkanPipeline* m_Pipeline = nullptr;
};

class VulkanDevice final : public Device {
public:
    VulkanDevice(const DeviceDesc& desc, Window& window);
    ~VulkanDevice() override;

    Backend GetBackend() const override { return Backend::Vulkan; }
    const char* GetAdapterName() const override { return m_Props.properties.deviceName; }

    Ref<Buffer> CreateBuffer(const BufferDesc& desc, const void* initialData) override;
    Ref<Texture> CreateTexture(const TextureDesc& desc) override;
    void UploadTexture(Texture* tex, const void* data, u64 size) override;
    void UploadBuffer(Buffer* dst, const void* data, u64 size, u64 dstOffset) override;
    Ref<Pipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    Ref<Pipeline> CreateComputePipeline(const ComputePipelineDesc& desc) override;

    bool BeginFrame() override;
    CommandList& Cmd() override { return m_CmdList; }
    Texture* BackBuffer() override { return m_SwapTextures[m_ImageIndex].get(); }
    void EndFrame() override;
    void WaitIdle() override { vkDeviceWaitIdle(m_Device); }
    void SetVSync(bool v) override { if (v != m_VSync) { m_VSync = v; m_SwapchainDirty = true; } }
    u64 FrameNumber() const override { return m_FrameNumber; }
    u32 FrameIndex() const override { return m_FrameIndex; }

    void BeginGpuTimer(const char* name) override;
    void EndGpuTimer() override;
    const std::vector<GpuTiming>& GpuTimings() const override { return m_Timings; }

    void ImGuiInit() override;
    void ImGuiNewFrame() override;
    void ImGuiRender(Texture* target) override;
    void ImGuiShutdown() override;
    void ImGuiRebuildFonts() override;
    void* ImGuiTexture(Texture* tex) override;

    // internal
    void Defer(std::function<void()> fn);
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& fn);
    VkDevice Raw() const { return m_Device; }
    VmaAllocator Allocator() const { return m_Allocator; }
    VkPipelineLayout Layout() const { return m_PipelineLayout; }
    VkDescriptorSet BindlessSet() const { return m_BindlessSet; }
    void WriteImageDescriptor(u32 binding, u32 index, VkImageView view, VkDescriptorType type, VkImageLayout layout);
    void WriteBufferDescriptor(u32 index, VkBuffer buffer, u64 size);
    IndexAllocator& SrvAllocator() { return m_SrvAlloc; }
    IndexAllocator& UavAllocator() { return m_UavAlloc; }
    IndexAllocator& BufferAllocator() { return m_BufAlloc; }
    VkSampler Sampler(SamplerIndex s) const { return m_Samplers[(u32)s]; }
    void SetDebugName(VkObjectType type, u64 handle, const std::string& name);
    bool ImGuiReady() const { return m_ImGuiReady; }
    PFN_vkCmdBeginDebugUtilsLabelEXT pfnBeginLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT pfnEndLabel = nullptr;

private:
    void CreateInstance(bool validation);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateBindless();
    void CreateSamplers();
    void CreateSwapchain();
    void DestroySwapchain();
    void CreateFrames();
    VkShaderModule LoadShader(const std::string& name);

    Window& m_Window;
    bool m_VSync = true, m_SwapchainDirty = false;
    bool m_HasAnisotropy = false, m_HasDepthClamp = false, m_HasWireframe = false;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties2 m_Props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    u32 m_QueueFamily = 0;
    VmaAllocator m_Allocator = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT m_pfnSetName = nullptr;

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_SwapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<Scope<VulkanTexture>> m_SwapTextures;
    std::vector<VkSemaphore> m_RenderFinished; // one per swapchain image
    u32 m_ImageIndex = 0;

    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkQueryPool queries = VK_NULL_HANDLE;
        u32 queryCount = 0;
        std::vector<std::pair<std::string, std::pair<u32, u32>>> timerRanges;
    };
    std::array<Frame, kFramesInFlight> m_Frames;
    u32 m_FrameIndex = 0;
    u64 m_FrameNumber = 0;
    bool m_InFrame = false;
    // (frame number at deferral, destroy fn): runs once that frame is known complete on the GPU
    std::deque<std::pair<u64, std::function<void()>>> m_DeletionQueue;
    void FlushDeletions(bool all);
    VulkanCommandList m_CmdList{ *this };
    std::vector<u32> m_TimerStack;
    std::vector<GpuTiming> m_Timings;

    VkCommandPool m_ImmPool = VK_NULL_HANDLE;
    VkFence m_ImmFence = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_BindlessLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_BindlessPool = VK_NULL_HANDLE;
    VkDescriptorSet m_BindlessSet = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    IndexAllocator m_SrvAlloc, m_UavAlloc, m_BufAlloc;
    std::array<VkSampler, (u32)SamplerIndex::Count> m_Samplers{};

    VkDescriptorPool m_ImGuiPool = VK_NULL_HANDLE;
    bool m_ImGuiReady = false;
};

} // namespace Stardust::RHI::Vk
