#include "RHI/Vulkan/VulkanDevice.h"
#include "Core/Log.h"
#include <imgui_impl_vulkan.h>

namespace Stardust::RHI::Vk {

VkFormat ToVk(Format f) {
    switch (f) {
    case Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
    case Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
    case Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R16F: return VK_FORMAT_R16_SFLOAT;
    case Format::RG16F: return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32F: return VK_FORMAT_R32_SFLOAT;
    case Format::RG32F: return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R32U: return VK_FORMAT_R32_UINT;
    case Format::RG32U: return VK_FORMAT_R32G32_UINT;
    case Format::R11G11B10F: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::RGB10A2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::D32F: return VK_FORMAT_D32_SFLOAT;
    case Format::D24S8: return VK_FORMAT_D32_SFLOAT_S8_UINT; // AMD has no D24S8
    default: return VK_FORMAT_UNDEFINED;
    }
}

struct StateInfo { VkImageLayout layout; VkPipelineStageFlags2 stage; VkAccessFlags2 access; };

static StateInfo GetStateInfo(ResourceState s) {
    switch (s) {
    case ResourceState::RenderTarget:
        return { VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT };
    case ResourceState::DepthWrite:
        return { VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
    case ResourceState::DepthRead:
        return { VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };
    case ResourceState::ShaderRead:
        return { VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };
    case ResourceState::UnorderedAccess:
        return { VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };
    case ResourceState::CopySrc:
        return { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT };
    case ResourceState::CopyDst:
        return { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT };
    case ResourceState::Present:
        return { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE };
    default:
        return { VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE };
    }
}

void ImageBarrier(VkCommandBuffer cb, VulkanTexture* t, ResourceState from, ResourceState to) {
    StateInfo a = GetStateInfo(from), b = GetStateInfo(to);
    VkImageMemoryBarrier2 ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    ib.srcStageMask = a.stage;
    ib.srcAccessMask = a.access;
    ib.dstStageMask = to == ResourceState::Present ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT : b.stage;
    ib.dstAccessMask = b.access;
    ib.oldLayout = a.layout;
    ib.newLayout = b.layout;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = t->image;
    VkImageAspectFlags aspect = t->Aspect();
    if (t->Desc().format == Format::D24S8) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    ib.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
    VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &ib;
    vkCmdPipelineBarrier2(cb, &di);
}

// ------------------------------------------------------------------------------------------------ texture

static VkImageViewType ViewType(TextureDim d) {
    switch (d) {
    case TextureDim::Tex2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDim::Tex3D: return VK_IMAGE_VIEW_TYPE_3D;
    case TextureDim::Cube: return VK_IMAGE_VIEW_TYPE_CUBE;
    default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}

VulkanTexture::VulkanTexture(VulkanDevice& dev, const TextureDesc& desc) : m_Dev(dev) {
    m_Desc = desc;
    if (IsDepthFormat(desc.format)) m_Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = desc.dim == TextureDim::Tex3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ci.format = ToVk(desc.format);
    ci.extent = { desc.width, desc.height, desc.dim == TextureDim::Tex3D ? desc.depth : 1 };
    ci.mipLevels = desc.mipLevels;
    ci.arrayLayers = desc.dim == TextureDim::Cube ? desc.arrayLayers * 6 : desc.arrayLayers;
    if (desc.dim == TextureDim::Cube) ci.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    const bool srgb = desc.format == Format::RGBA8_SRGB || desc.format == Format::BGRA8_SRGB;
    if (srgb && Any(desc.usage & TextureUsage::Sampled)) ci.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (Any(desc.usage & TextureUsage::Sampled)) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (Any(desc.usage & TextureUsage::Storage)) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (Any(desc.usage & TextureUsage::RenderTarget)) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (Any(desc.usage & TextureUsage::DepthStencil)) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ci.usage = usage;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    if (Any(desc.usage & (TextureUsage::RenderTarget | TextureUsage::DepthStencil | TextureUsage::Storage)))
        ai.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    VK_CHECK(vmaCreateImage(dev.Allocator(), &ci, &ai, &image, &allocation, nullptr));
    dev.SetDebugName(VK_OBJECT_TYPE_IMAGE, (u64)image, desc.name);
    CreateViews();
}

VulkanTexture::VulkanTexture(VulkanDevice& dev, const TextureDesc& desc, VkImage swapImage) : m_Dev(dev) {
    m_Desc = desc;
    m_OwnsImage = false;
    image = swapImage;
}

void VulkanTexture::CreateViews() {
    VkDevice d = m_Dev.Raw();
    u32 layers = m_Desc.dim == TextureDim::Cube ? m_Desc.arrayLayers * 6 : m_Desc.arrayLayers;
    if (Any(m_Desc.usage & TextureUsage::Sampled)) {
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = image;
        vi.viewType = ViewType(m_Desc.dim);
        vi.format = ToVk(m_Desc.format);
        vi.subresourceRange = { m_Aspect, 0, m_Desc.mipLevels, 0, layers };
        VK_CHECK(vkCreateImageView(d, &vi, nullptr, &srvView));
        if (m_Desc.format == Format::RGBA8_SRGB || m_Desc.format == Format::BGRA8_SRGB) {
            VkImageViewCreateInfo ui = vi;
            ui.format = m_Desc.format == Format::RGBA8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
            VK_CHECK(vkCreateImageView(d, &ui, nullptr, &imguiView));
        }
        m_SRV = m_Dev.SrvAllocator().Allocate();
        u32 binding = m_Desc.dim == TextureDim::Tex2DArray ? kBindTex2DArray
                    : m_Desc.dim == TextureDim::Tex3D ? kBindTex3D
                    : m_Desc.dim == TextureDim::Cube ? kBindTexCube : kBindTex2D;
        m_Dev.WriteImageDescriptor(binding, m_SRV, srvView, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    }
    if (Any(m_Desc.usage & TextureUsage::Storage)) {
        for (u32 mip = 0; mip < m_Desc.mipLevels; ++mip) {
            VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image = image;
            vi.viewType = m_Desc.dim == TextureDim::Cube ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : ViewType(m_Desc.dim);
            vi.format = ToVk(m_Desc.format);
            vi.subresourceRange = { m_Aspect, mip, 1, 0, layers };
            VkImageView v;
            VK_CHECK(vkCreateImageView(d, &vi, nullptr, &v));
            u32 idx = m_Dev.UavAllocator().Allocate();
            u32 binding = m_Desc.dim == TextureDim::Tex3D ? kBindRWTex3D
                        : (m_Desc.dim == TextureDim::Tex2D ? kBindRWTex2D : kBindRWTex2DArray);
            m_Dev.WriteImageDescriptor(binding, idx, v, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
            m_UAVViews.push_back(v);
            m_UAVs.push_back(idx);
        }
    }
}

VkImageView VulkanTexture::AttachmentView(u32 layer, u32 mip) {
    u32 key = (layer << 16) | mip;
    auto it = m_AttachmentViews.find(key);
    if (it != m_AttachmentViews.end()) return it->second;
    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ToVk(m_Desc.format);
    vi.subresourceRange = { m_Aspect, mip, 1, layer, 1 };
    VkImageView v;
    VK_CHECK(vkCreateImageView(m_Dev.Raw(), &vi, nullptr, &v));
    m_AttachmentViews[key] = v;
    return v;
}

VulkanTexture::~VulkanTexture() {
    VulkanDevice* dev = &m_Dev;
    VkDevice d = m_Dev.Raw();
    std::vector<VkImageView> views = m_UAVViews;
    for (auto& [k, v] : m_AttachmentViews) views.push_back(v);
    if (srvView) views.push_back(srvView);
    if (imguiView) views.push_back(imguiView);
    VkImage img = m_OwnsImage ? image : VK_NULL_HANDLE;
    VmaAllocation alloc = allocation;
    u32 srv = m_SRV;
    std::vector<u32> uavs = m_UAVs;
    VkDescriptorSet set = imguiSet;
    m_Dev.Defer([=] {
        if (set && dev->ImGuiReady()) ImGui_ImplVulkan_RemoveTexture(set);
        for (VkImageView v : views) vkDestroyImageView(d, v, nullptr);
        if (img) vmaDestroyImage(dev->Allocator(), img, alloc);
        dev->SrvAllocator().Free(srv);
        for (u32 u : uavs) dev->UavAllocator().Free(u);
    });
}

// ------------------------------------------------------------------------------------------------ buffer

VulkanBuffer::VulkanBuffer(VulkanDevice& dev, const BufferDesc& desc) : m_Dev(dev) {
    m_Desc = desc;
    if (m_Desc.size == 0) m_Desc.size = 16;
    VkBufferCreateInfo ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    ci.size = m_Desc.size;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (Any(desc.usage & BufferUsage::Vertex)) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (Any(desc.usage & BufferUsage::Index)) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (Any(desc.usage & BufferUsage::Storage)) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (Any(desc.usage & BufferUsage::Indirect)) usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    ci.usage = usage;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.memory == MemoryUsage::CPUToGPU)
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    else if (desc.memory == MemoryUsage::GPUToCPU)
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    VK_CHECK(vmaCreateBuffer(dev.Allocator(), &ci, &ai, &buffer, &allocation, &info));
    m_Mapped = info.pMappedData;
    dev.SetDebugName(VK_OBJECT_TYPE_BUFFER, (u64)buffer, desc.name);

    if (Any(desc.usage & BufferUsage::Storage)) {
        m_Index = dev.BufferAllocator().Allocate();
        dev.WriteBufferDescriptor(m_Index, buffer, m_Desc.size);
    }
}

VulkanBuffer::~VulkanBuffer() {
    VulkanDevice* dev = &m_Dev;
    VkBuffer b = buffer;
    VmaAllocation a = allocation;
    u32 idx = m_Index;
    m_Dev.Defer([=] {
        vmaDestroyBuffer(dev->Allocator(), b, a);
        dev->BufferAllocator().Free(idx);
    });
}

// ------------------------------------------------------------------------------------------------ command list

void VulkanCommandList::Barrier(Texture* tex, ResourceState newState) {
    if (tex->state == newState && newState != ResourceState::UnorderedAccess) return;
    ImageBarrier(m_Cmd, (VulkanTexture*)tex, tex->state, newState);
    tex->state = newState;
}

void VulkanCommandList::MemoryBarrier() {
    VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(m_Cmd, &di);
}

static VkAttachmentLoadOp ToVk(LoadOp op) {
    return op == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD : op == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

void VulkanCommandList::BeginRendering(const RenderingDesc& desc) {
    VkRenderingAttachmentInfo colors[8]{};
    u32 w = desc.width, h = desc.height;
    for (size_t i = 0; i < desc.colors.size(); ++i) {
        auto& c = desc.colors[i];
        auto* t = (VulkanTexture*)c.texture;
        Barrier(t, ResourceState::RenderTarget);
        colors[i] = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        colors[i].imageView = t->AttachmentView(c.layer, c.mip);
        colors[i].imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        colors[i].loadOp = ToVk(c.load);
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colors[i].clearValue.color = { { c.clear.r, c.clear.g, c.clear.b, c.clear.a } };
        if (w == 0) { w = std::max(1u, t->Width() >> c.mip); h = std::max(1u, t->Height() >> c.mip); }
    }
    VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    if (desc.depth.texture) {
        auto* t = (VulkanTexture*)desc.depth.texture;
        Barrier(t, desc.depth.readOnly ? ResourceState::DepthRead : ResourceState::DepthWrite);
        depth.imageView = t->AttachmentView(desc.depth.layer, 0);
        depth.imageLayout = desc.depth.readOnly ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        depth.loadOp = ToVk(desc.depth.load);
        depth.storeOp = desc.depth.readOnly ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = { desc.depth.clear, 0 };
        if (w == 0) { w = t->Width(); h = t->Height(); }
    }
    VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea = { { 0, 0 }, { w, h } };
    ri.layerCount = 1;
    ri.colorAttachmentCount = (u32)desc.colors.size();
    ri.pColorAttachments = colors;
    ri.pDepthAttachment = desc.depth.texture ? &depth : nullptr;
    vkCmdBeginRendering(m_Cmd, &ri);
    SetViewport(0, 0, (float)w, (float)h);
    SetScissor(0, 0, w, h);
}

void VulkanCommandList::EndRendering() { vkCmdEndRendering(m_Cmd); }

void VulkanCommandList::SetViewport(float x, float y, float w, float h) {
    // Negative height: Y-up clip space like D3D, so both backends share projection matrices and shaders.
    VkViewport vp{ x, y + h, w, -h, 0.0f, 1.0f };
    vkCmdSetViewport(m_Cmd, 0, 1, &vp);
}

void VulkanCommandList::SetScissor(i32 x, i32 y, u32 w, u32 h) {
    VkRect2D r{ { x, y }, { w, h } };
    vkCmdSetScissor(m_Cmd, 0, 1, &r);
}

void VulkanCommandList::SetPipeline(Pipeline* pipeline) {
    auto* p = (VulkanPipeline*)pipeline;
    VkPipelineBindPoint bp = p->IsCompute() ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindPipeline(m_Cmd, bp, p->pipeline);
    VkDescriptorSet set = m_Dev.BindlessSet();
    vkCmdBindDescriptorSets(m_Cmd, bp, m_Dev.Layout(), 0, 1, &set, 0, nullptr);
    m_Pipeline = p;
}

void VulkanCommandList::PushConstants(const void* data, u32 size) {
    vkCmdPushConstants(m_Cmd, m_Dev.Layout(), VK_SHADER_STAGE_ALL, 0, size, data);
}

void VulkanCommandList::SetIndexBuffer(Buffer* buffer, bool u16Indices, u64 offset) {
    vkCmdBindIndexBuffer(m_Cmd, ((VulkanBuffer*)buffer)->buffer, offset, u16Indices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::Draw(u32 vc, u32 ic, u32 fv, u32 fi) { vkCmdDraw(m_Cmd, vc, ic, fv, fi); }
void VulkanCommandList::DrawIndexed(u32 idx, u32 ic, u32 fi, i32 vo, u32 finst) { vkCmdDrawIndexed(m_Cmd, idx, ic, fi, vo, finst); }
void VulkanCommandList::DrawIndexedIndirect(Buffer* args, u64 offset, u32 drawCount, u32 stride) {
    vkCmdDrawIndexedIndirect(m_Cmd, ((VulkanBuffer*)args)->buffer, offset, drawCount, stride);
}
void VulkanCommandList::DrawIndirect(Buffer* args, u64 offset, u32 drawCount, u32 stride) {
    vkCmdDrawIndirect(m_Cmd, ((VulkanBuffer*)args)->buffer, offset, drawCount, stride);
}
void VulkanCommandList::Dispatch(u32 x, u32 y, u32 z) { vkCmdDispatch(m_Cmd, x, y, z); }
void VulkanCommandList::DispatchIndirect(Buffer* args, u64 offset) { vkCmdDispatchIndirect(m_Cmd, ((VulkanBuffer*)args)->buffer, offset); }

void VulkanCommandList::CopyBuffer(Buffer* src, u64 so, Buffer* dst, u64 dof, u64 size) {
    VkBufferCopy c{ so, dof, size };
    vkCmdCopyBuffer(m_Cmd, ((VulkanBuffer*)src)->buffer, ((VulkanBuffer*)dst)->buffer, 1, &c);
}

void VulkanCommandList::FillBuffer(Buffer* dst, u64 offset, u64 size, u32 value) {
    vkCmdFillBuffer(m_Cmd, ((VulkanBuffer*)dst)->buffer, offset, size, value);
}

void VulkanCommandList::CopyTexture(Texture* src, Texture* dst) {
    Barrier(src, ResourceState::CopySrc);
    Barrier(dst, ResourceState::CopyDst);
    auto* s = (VulkanTexture*)src;
    auto* d = (VulkanTexture*)dst;
    VkImageCopy c{};
    c.srcSubresource = { s->Aspect(), 0, 0, 1 };
    c.dstSubresource = { d->Aspect(), 0, 0, 1 };
    c.extent = { std::min(src->Width(), dst->Width()), std::min(src->Height(), dst->Height()), 1 };
    vkCmdCopyImage(m_Cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
}

void VulkanCommandList::CopyTextureToBuffer(Texture* src, Buffer* dst, u32 x, u32 y, u32 w, u32 h, u64 dstOffset) {
    Barrier(src, ResourceState::CopySrc);
    auto* s = (VulkanTexture*)src;
    VkBufferImageCopy c{};
    c.bufferOffset = dstOffset;
    c.imageSubresource = { s->Aspect(), 0, 0, 1 };
    c.imageOffset = { (i32)x, (i32)y, 0 };
    c.imageExtent = { w, h, 1 };
    vkCmdCopyImageToBuffer(m_Cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ((VulkanBuffer*)dst)->buffer, 1, &c);
}

void VulkanCommandList::CopyBufferToTexture(Buffer* src, u64 srcOffset, Texture* dst, u32 x, u32 y, u32 w, u32 h) {
    Barrier(dst, ResourceState::CopyDst);
    auto* d = (VulkanTexture*)dst;
    VkBufferImageCopy c{};
    c.bufferOffset = srcOffset;
    c.imageSubresource = { d->Aspect(), 0, 0, 1 };
    c.imageOffset = { (i32)x, (i32)y, 0 };
    c.imageExtent = { w, h, 1 };
    vkCmdCopyBufferToImage(m_Cmd, ((VulkanBuffer*)src)->buffer, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    Barrier(dst, ResourceState::ShaderRead);
}

void VulkanCommandList::BeginMarker(const char* name) {
    if (!m_Dev.pfnBeginLabel) return;
    VkDebugUtilsLabelEXT l{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
    l.pLabelName = name;
    m_Dev.pfnBeginLabel(m_Cmd, &l);
}

void VulkanCommandList::EndMarker() {
    if (m_Dev.pfnEndLabel) m_Dev.pfnEndLabel(m_Cmd);
}

} // namespace Stardust::RHI::Vk
