#pragma once
// Stardust Render Hardware Interface.
// Backend-agnostic API modelled on modern explicit APIs (Vulkan 1.3 / D3D12):
//  - fully bindless: every texture/buffer gets a global descriptor index that shaders read from push constants
//  - vertex pulling: no vertex input layouts, meshes are read from storage buffers
//  - automatic texture state tracking, explicit memory barriers for buffers
#include "Core/Base.h"
#include <glm/glm.hpp>
#include <span>

namespace Stardust { class Window; }

namespace Stardust::RHI {

enum class Backend { Vulkan, D3D12 };

enum class Format : u8 {
    Unknown,
    R8_UNORM, RG8_UNORM, RGBA8_UNORM, RGBA8_SRGB, BGRA8_UNORM, BGRA8_SRGB,
    R16F, RG16F, RGBA16F, R32F, RG32F, RGBA32F, R32U, RG32U,
    R11G11B10F, RGB10A2,
    D32F, D24S8,
};
bool IsDepthFormat(Format f);
u32 FormatBytesPerPixel(Format f);

enum class TextureUsage : u32 {
    None = 0, Sampled = SD_BIT(0), Storage = SD_BIT(1), RenderTarget = SD_BIT(2),
    DepthStencil = SD_BIT(3), CopyDst = SD_BIT(4), CopySrc = SD_BIT(5),
};
SD_ENUM_FLAGS(TextureUsage)

enum class TextureDim : u8 { Tex2D, Tex2DArray, Tex3D, Cube };

struct TextureDesc {
    u32 width = 1, height = 1, depth = 1;
    u32 mipLevels = 1, arrayLayers = 1;
    Format format = Format::RGBA8_UNORM;
    TextureDim dim = TextureDim::Tex2D;
    TextureUsage usage = TextureUsage::Sampled;
    std::string name;
};

enum class BufferUsage : u32 {
    None = 0, Vertex = SD_BIT(0), Index = SD_BIT(1), Storage = SD_BIT(2), Indirect = SD_BIT(3),
    CopySrc = SD_BIT(4), CopyDst = SD_BIT(5),
};
SD_ENUM_FLAGS(BufferUsage)

enum class MemoryUsage : u8 { GPUOnly, CPUToGPU, GPUToCPU };

struct BufferDesc {
    u64 size = 0;
    BufferUsage usage = BufferUsage::Storage;
    MemoryUsage memory = MemoryUsage::GPUOnly;
    std::string name;
};

enum class ResourceState : u8 {
    Undefined, RenderTarget, DepthWrite, DepthRead, ShaderRead, UnorderedAccess, CopySrc, CopyDst, Present,
};

// Fixed sampler table, same indices on every backend (see Bindless.hlsli)
enum class SamplerIndex : u32 { LinearClamp = 0, LinearWrap, PointClamp, PointWrap, AnisoWrap, ShadowCompare, Count };

static constexpr u32 kInvalidIndex = ~0u;
static constexpr u32 kFramesInFlight = 2;
static constexpr u32 kMaxPushConstantSize = 128;

class Texture {
public:
    virtual ~Texture() = default;
    const TextureDesc& Desc() const { return m_Desc; }
    u32 Width() const { return m_Desc.width; }
    u32 Height() const { return m_Desc.height; }
    // Bindless indices. SRV = whole resource. UAV = one mip level.
    virtual u32 SRV() const = 0;
    virtual u32 UAV(u32 mip = 0) const = 0;
    ResourceState state = ResourceState::Undefined; // tracked by the command list
protected:
    TextureDesc m_Desc;
};

class Buffer {
public:
    virtual ~Buffer() = default;
    const BufferDesc& Desc() const { return m_Desc; }
    u64 Size() const { return m_Desc.size; }
    virtual u32 Index() const = 0;      // bindless (ByteAddressBuffer) index, kInvalidIndex if not Storage
    virtual void* Mapped() const = 0;   // non-null for CPUToGPU / GPUToCPU
    template<typename T> T* As() const { return (T*)Mapped(); }
protected:
    BufferDesc m_Desc;
};

enum class CullMode : u8 { None, Back, Front };
enum class CompareOp : u8 { Never, Less, LessEqual, Greater, GreaterEqual, Equal, Always };
enum class BlendMode : u8 { None, Alpha, Additive, Premultiplied };
enum class Topology : u8 { TriangleList, TriangleStrip, LineList, PointList };

struct GraphicsPipelineDesc {
    std::string vs, ps;                 // shader names without extension, e.g. "Mesh.vs"
    std::vector<Format> colorFormats;
    Format depthFormat = Format::Unknown;
    CullMode cull = CullMode::Back;
    bool frontCCW = true;
    bool depthTest = true, depthWrite = true;
    CompareOp depthCompare = CompareOp::GreaterEqual; // reverse-Z everywhere
    BlendMode blend = BlendMode::None;
    Topology topology = Topology::TriangleList;
    bool wireframe = false;
    bool depthClamp = false;
    float depthBiasConstant = 0, depthBiasSlope = 0;
    bool alphaToCoverage = false;
    std::string name;
};

struct ComputePipelineDesc {
    std::string cs;
    std::string name;
};

class Pipeline {
public:
    virtual ~Pipeline() = default;
    bool IsCompute() const { return m_Compute; }
protected:
    bool m_Compute = false;
};

enum class LoadOp : u8 { Load, Clear, DontCare };

struct ColorAttachment {
    Texture* texture = nullptr;
    LoadOp load = LoadOp::Clear;
    glm::vec4 clear{ 0, 0, 0, 1 };
    u32 layer = 0, mip = 0;
};

struct DepthAttachment {
    Texture* texture = nullptr;
    LoadOp load = LoadOp::Clear;
    float clear = 0.0f; // reverse-Z: far = 0
    u32 layer = 0;
    bool readOnly = false;
};

struct RenderingDesc {
    std::vector<ColorAttachment> colors;
    DepthAttachment depth;
    u32 width = 0, height = 0; // 0 = from the first attachment
};

class CommandList {
public:
    virtual ~CommandList() = default;

    virtual void Barrier(Texture* tex, ResourceState newState) = 0;
    virtual void MemoryBarrier() = 0; // full UAV/compute -> anything barrier for buffers

    virtual void BeginRendering(const RenderingDesc& desc) = 0;
    virtual void EndRendering() = 0;
    virtual void SetViewport(float x, float y, float w, float h) = 0;
    virtual void SetScissor(i32 x, i32 y, u32 w, u32 h) = 0;

    virtual void SetPipeline(Pipeline* pipeline) = 0;
    virtual void PushConstants(const void* data, u32 size) = 0;
    template<typename T> void Push(const T& data) {
        static_assert(sizeof(T) <= kMaxPushConstantSize, "push constants too large");
        PushConstants(&data, sizeof(T));
    }
    virtual void SetIndexBuffer(Buffer* buffer, bool u16Indices = false, u64 offset = 0) = 0;

    virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) = 0;
    virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) = 0;
    virtual void DrawIndexedIndirect(Buffer* args, u64 offset, u32 drawCount, u32 stride = 20) = 0;
    virtual void DrawIndirect(Buffer* args, u64 offset, u32 drawCount, u32 stride = 16) = 0;
    virtual void Dispatch(u32 x, u32 y = 1, u32 z = 1) = 0;
    virtual void DispatchIndirect(Buffer* args, u64 offset) = 0;

    virtual void CopyBuffer(Buffer* src, u64 srcOffset, Buffer* dst, u64 dstOffset, u64 size) = 0;
    virtual void FillBuffer(Buffer* dst, u64 offset, u64 size, u32 value) = 0;
    virtual void CopyTexture(Texture* src, Texture* dst) = 0;
    // Copies a region of mip 0 / layer 0 into a buffer (tightly packed rows), e.g. for editor picking readback
    virtual void CopyTextureToBuffer(Texture* src, Buffer* dst, u32 x, u32 y, u32 w, u32 h, u64 dstOffset = 0) = 0;
    // Copies tightly packed rows from a buffer into a region of mip 0 / layer 0 (leaves the texture in ShaderRead)
    virtual void CopyBufferToTexture(Buffer* src, u64 srcOffset, Texture* dst, u32 x, u32 y, u32 w, u32 h) = 0;

    virtual void BeginMarker(const char* name) = 0;
    virtual void EndMarker() = 0;
};

struct DeviceDesc {
    Backend backend = Backend::Vulkan;
    bool validation = false;
    bool vsync = true;
};

struct GpuTiming { std::string name; float ms; };

class Device {
public:
    static Scope<Device> Create(const DeviceDesc& desc, Window& window);
    virtual ~Device() = default;

    virtual Backend GetBackend() const = 0;
    virtual const char* GetAdapterName() const = 0;

    virtual Ref<Buffer> CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
    virtual Ref<Texture> CreateTexture(const TextureDesc& desc) = 0;
    // Uploads mip 0 / all layers from tightly packed data (blocks until done; use at load time).
    virtual void UploadTexture(Texture* tex, const void* data, u64 size) = 0;
    virtual void UploadBuffer(Buffer* dst, const void* data, u64 size, u64 dstOffset = 0) = 0;
    virtual Ref<Pipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual Ref<Pipeline> CreateComputePipeline(const ComputePipelineDesc& desc) = 0;

    // Frame loop. BeginFrame returns false when the swapchain is not presentable (e.g. minimized).
    virtual bool BeginFrame() = 0;
    virtual CommandList& Cmd() = 0;
    virtual Texture* BackBuffer() = 0;
    virtual void EndFrame() = 0; // submits and presents
    virtual void WaitIdle() = 0;
    virtual void SetVSync(bool vsync) = 0;
    virtual u64 FrameNumber() const = 0;
    virtual u32 FrameIndex() const = 0; // 0..kFramesInFlight-1

    // GPU profiling: scoped timestamp regions, results available a few frames later.
    virtual void BeginGpuTimer(const char* name) = 0;
    virtual void EndGpuTimer() = 0;
    virtual const std::vector<GpuTiming>& GpuTimings() const = 0;

    // Dear ImGui integration (backend specific renderer)
    virtual void ImGuiInit() = 0;
    virtual void ImGuiNewFrame() = 0;
    virtual void ImGuiRender(Texture* target) = 0;
    virtual void ImGuiShutdown() = 0;
    virtual void ImGuiRebuildFonts() {} // Called outside a frame, after the GPU is idle.
    virtual void* ImGuiTexture(Texture* tex) = 0; // ImTextureID for a sampled texture in ShaderRead state
};

struct ScopedGpuTimer {
    Device& d;
    ScopedGpuTimer(Device& dev, const char* name) : d(dev) { d.BeginGpuTimer(name); }
    ~ScopedGpuTimer() { d.EndGpuTimer(); }
};

} // namespace Stardust::RHI
