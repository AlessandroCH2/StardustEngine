#include "RHI/RHI.h"
#include "RHI/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

namespace Stardust::RHI {

bool IsDepthFormat(Format f) { return f == Format::D32F || f == Format::D24S8; }

u32 FormatBytesPerPixel(Format f) {
    switch (f) {
    case Format::R8_UNORM: return 1;
    case Format::RG8_UNORM: case Format::R16F: return 2;
    case Format::RGBA8_UNORM: case Format::RGBA8_SRGB: case Format::BGRA8_UNORM: case Format::BGRA8_SRGB:
    case Format::RG16F: case Format::R32F: case Format::R32U: case Format::R11G11B10F: case Format::RGB10A2:
    case Format::D32F: return 4;
    case Format::RGBA16F: case Format::RG32F: case Format::RG32U: case Format::D24S8: return 8;
    case Format::RGBA32F: return 16;
    default: return 0;
    }
}

Scope<Device> Device::Create(const DeviceDesc& desc, Window& window) {
    switch (desc.backend) {
    case Backend::Vulkan:
        SD_INFO("RHI backend: Vulkan 1.3");
        return MakeScope<Vk::VulkanDevice>(desc, window);
    case Backend::D3D12:
        SD_WARN("D3D12 backend not built yet, falling back to Vulkan");
        return MakeScope<Vk::VulkanDevice>(desc, window);
    }
    return nullptr;
}

} // namespace Stardust::RHI
