#include "RHI/Vulkan/VulkanDevice.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Core/FileSystem.h"
#include "Assets/AssetManager.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>

#pragma warning(push, 0)
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#pragma warning(pop)

namespace Stardust::RHI::Vk {

static constexpr u32 kMaxSampledImages = 8192;
static constexpr u32 kMaxStorageImages = 4096;
static constexpr u32 kMaxBuffers = 16384;
static constexpr u32 kMaxQueries = 256;

[[noreturn]] void Fatal(VkResult r, const char* expr, const char* file, int line) {
    std::string msg = std::format("Vulkan error {} in {} ({}:{})", (int)r, expr, file, line);
    SD_ERROR("{}", msg);
    throw std::runtime_error(msg);
}

u32 IndexAllocator::Allocate() {
    if (!m_Free.empty()) { u32 i = m_Free.back(); m_Free.pop_back(); return i; }
    SD_VERIFY(m_Next < m_Capacity, "Bindless descriptor heap exhausted");
    return m_Next++;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) SD_ERROR("[Vulkan] {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) SD_WARN("[Vulkan] {}", data->pMessage);
    return VK_FALSE;
}

// ------------------------------------------------------------------------------------------------ setup

VulkanDevice::VulkanDevice(const DeviceDesc& desc, Window& window)
    : m_Window(window), m_VSync(desc.vsync),
      m_SrvAlloc(kMaxSampledImages), m_UavAlloc(kMaxStorageImages), m_BufAlloc(kMaxBuffers) {
    CreateInstance(desc.validation);
    VK_CHECK(glfwCreateWindowSurface(m_Instance, window.Native(), nullptr, &m_Surface));
    PickPhysicalDevice();
    CreateLogicalDevice();

    VmaAllocatorCreateInfo ai{};
    ai.vulkanApiVersion = VK_API_VERSION_1_3;
    ai.instance = m_Instance;
    ai.physicalDevice = m_PhysicalDevice;
    ai.device = m_Device;
    VK_CHECK(vmaCreateAllocator(&ai, &m_Allocator));

    CreateBindless();
    CreateSamplers();
    CreateFrames();
    CreateSwapchain();
}

VulkanDevice::~VulkanDevice() {
    vkDeviceWaitIdle(m_Device);
    DestroySwapchain();
    FlushDeletions(true);
    for (auto& f : m_Frames) {
        vkDestroyCommandPool(m_Device, f.pool, nullptr);
        vkDestroyFence(m_Device, f.fence, nullptr);
        vkDestroySemaphore(m_Device, f.acquired, nullptr);
        vkDestroyQueryPool(m_Device, f.queries, nullptr);
    }
    vkDestroyCommandPool(m_Device, m_ImmPool, nullptr);
    vkDestroyFence(m_Device, m_ImmFence, nullptr);
    for (VkSampler s : m_Samplers) vkDestroySampler(m_Device, s, nullptr);
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    vkDestroyDescriptorPool(m_Device, m_BindlessPool, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_BindlessLayout, nullptr);
    vmaDestroyAllocator(m_Allocator);
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    if (m_Messenger) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(m_Instance, m_Messenger, nullptr);
    }
    vkDestroyInstance(m_Instance, nullptr);
}

void VulkanDevice::CreateInstance(bool validation) {
    u32 extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());
    auto hasExt = [&](const char* n) {
        return std::any_of(available.begin(), available.end(), [&](auto& e) { return strcmp(e.extensionName, n) == 0; });
    };

    u32 glfwCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwCount);
    bool debugUtils = hasExt(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    if (validation) {
        u32 lc = 0;
        vkEnumerateInstanceLayerProperties(&lc, nullptr);
        std::vector<VkLayerProperties> lp(lc);
        vkEnumerateInstanceLayerProperties(&lc, lp.data());
        bool found = std::any_of(lp.begin(), lp.end(), [](auto& l) { return strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
        if (found) layers.push_back("VK_LAYER_KHRONOS_validation");
        else SD_WARN("Vulkan validation layer not available");
    }

    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "Stardust";
    app.pEngineName = "Stardust Engine";
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (u32)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = (u32)layers.size();
    ci.ppEnabledLayerNames = layers.data();
    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_Instance));

    if (debugUtils) {
        if (!layers.empty()) {
            VkDebugUtilsMessengerCreateInfoEXT mi{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = DebugCallback;
            auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
            if (fn) fn(m_Instance, &mi, nullptr, &m_Messenger);
        }
        pfnBeginLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_Instance, "vkCmdBeginDebugUtilsLabelEXT");
        pfnEndLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_Instance, "vkCmdEndDebugUtilsLabelEXT");
        m_pfnSetName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(m_Instance, "vkSetDebugUtilsObjectNameEXT");
    }
}

namespace {
// Everything the renderer asks of the GPU. The third column says if it can run without it.
#define SD_VK_FEATURES(X) \
    X(f13, dynamicRendering, true) X(f13, synchronization2, true) X(f13, maintenance4, false) X(f13, shaderDemoteToHelperInvocation, true) \
    X(f12, descriptorIndexing, true) X(f12, runtimeDescriptorArray, true) X(f12, descriptorBindingPartiallyBound, true) \
    X(f12, descriptorBindingSampledImageUpdateAfterBind, true) X(f12, descriptorBindingStorageImageUpdateAfterBind, true) \
    X(f12, descriptorBindingStorageBufferUpdateAfterBind, true) X(f12, descriptorBindingUpdateUnusedWhilePending, true) \
    X(f12, shaderSampledImageArrayNonUniformIndexing, true) X(f12, shaderStorageImageArrayNonUniformIndexing, false) \
    X(f12, shaderStorageBufferArrayNonUniformIndexing, true) X(f12, scalarBlockLayout, true) X(f12, timelineSemaphore, true) \
    X(f12, hostQueryReset, true) X(f12, drawIndirectCount, false) X(f12, samplerFilterMinmax, false) X(f12, uniformBufferStandardLayout, false) \
    X(f11, shaderDrawParameters, true) \
    X(f2.features, samplerAnisotropy, false) X(f2.features, multiDrawIndirect, false) X(f2.features, drawIndirectFirstInstance, false) \
    X(f2.features, fillModeNonSolid, false) X(f2.features, depthClamp, false) X(f2.features, depthBiasClamp, false) \
    X(f2.features, independentBlend, false) X(f2.features, imageCubeArray, false) X(f2.features, shaderStorageImageReadWithoutFormat, false) \
    X(f2.features, shaderStorageImageWriteWithoutFormat, true) X(f2.features, fragmentStoresAndAtomics, true) \
    X(f2.features, vertexPipelineStoresAndAtomics, true) X(f2.features, shaderImageGatherExtended, false)

struct DeviceFeatures {
    VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features f12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan11Features f11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceFeatures2 f2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    DeviceFeatures() { f12.pNext = &f13; f11.pNext = &f12; f2.pNext = &f11; }
    DeviceFeatures(const DeviceFeatures&) = delete;
    DeviceFeatures& operator=(const DeviceFeatures&) = delete;
};

std::string MissingFeatures(VkPhysicalDevice device, bool required) {
    DeviceFeatures supported;
    vkGetPhysicalDeviceFeatures2(device, &supported.f2);
    std::string missing;
#define SD_CHECK_FEATURE(group, name, essential) if (essential == required && !supported.group.name) missing += (missing.empty() ? "" : ", ") + std::string(#name);
    SD_VK_FEATURES(SD_CHECK_FEATURE)
#undef SD_CHECK_FEATURE
    return missing;
}
}

void VulkanDevice::PickPhysicalDevice() {
    u32 count = 0;
    vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pd, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) { SD_INFO("GPU {}: skipped, it does not offer Vulkan 1.3", p.deviceName); continue; }
        if (std::string missing = MissingFeatures(pd, true); !missing.empty()) { SD_WARN("GPU {}: skipped, missing {}", p.deviceName, missing); continue; }

        u32 qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, qf.data());
        for (u32 i = 0; i < qc; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, m_Surface, &present);
            bool ok = (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && present;
            if (!ok) continue;
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
            score += (int)(p.limits.maxImageDimension2D / 1024);
            if (score > bestScore) { bestScore = score; m_PhysicalDevice = pd; m_QueueFamily = i; }
            break;
        }
    }
    SD_VERIFY(m_PhysicalDevice, "No Vulkan 1.3 GPU with graphics+compute+present and the required features found");
    vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &m_Props);
    SD_INFO("GPU: {} (driver {:x})", m_Props.properties.deviceName, m_Props.properties.driverVersion);
}

void VulkanDevice::CreateLogicalDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qi.queueFamilyIndex = m_QueueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;

    // Turn on only what this GPU has: asking for anything else makes vkCreateDevice fail
    DeviceFeatures supported, wanted;
    vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supported.f2);
#define SD_ENABLE_FEATURE(group, name, essential) wanted.group.name = supported.group.name;
    SD_VK_FEATURES(SD_ENABLE_FEATURE)
#undef SD_ENABLE_FEATURE
    if (std::string missing = MissingFeatures(m_PhysicalDevice, false); !missing.empty()) SD_WARN("Optional GPU features not available: {}", missing);
    m_HasAnisotropy = supported.f2.features.samplerAnisotropy;
    m_HasDepthClamp = supported.f2.features.depthClamp;
    m_HasWireframe = supported.f2.features.fillModeNonSolid;

    u32 ec = 0;
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &ec, nullptr);
    std::vector<VkExtensionProperties> avail(ec);
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &ec, avail.data());
    std::vector<const char*> exts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    // Dear ImGui's backend fetches the KHR entry points, so expose them explicitly.
    for (auto& e : avail)
        if (strcmp(e.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0) exts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    VkDeviceCreateInfo ci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    ci.pNext = &wanted.f2;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = (u32)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    VK_CHECK(vkCreateDevice(m_PhysicalDevice, &ci, nullptr, &m_Device));
    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
}

void VulkanDevice::CreateBindless() {
    struct B { u32 binding; VkDescriptorType type; u32 count; };
    const B defs[kBindCount] = {
        { kBindTex2D, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSampledImages },
        { kBindTex2DArray, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSampledImages },
        { kBindTex3D, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSampledImages },
        { kBindTexCube, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSampledImages },
        { kBindRWTex2D, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxStorageImages },
        { kBindRWTex3D, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxStorageImages },
        { kBindRWTex2DArray, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxStorageImages },
        { kBindBuffers, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxBuffers },
        { kBindSamplers, VK_DESCRIPTOR_TYPE_SAMPLER, (u32)SamplerIndex::Count },
    };
    VkDescriptorSetLayoutBinding bindings[kBindCount]{};
    VkDescriptorBindingFlags flags[kBindCount]{};
    u32 sampled = 0, storageImg = 0, storageBuf = 0, samplers = 0;
    for (u32 i = 0; i < kBindCount; ++i) {
        bindings[i] = { defs[i].binding, defs[i].type, defs[i].count, VK_SHADER_STAGE_ALL, nullptr };
        flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        if (defs[i].type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) sampled += defs[i].count;
        if (defs[i].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) storageImg += defs[i].count;
        if (defs[i].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) storageBuf += defs[i].count;
        if (defs[i].type == VK_DESCRIPTOR_TYPE_SAMPLER) samplers += defs[i].count;
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo fi{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    fi.bindingCount = kBindCount;
    fi.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    li.pNext = &fi;
    li.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    li.bindingCount = kBindCount;
    li.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &li, nullptr, &m_BindlessLayout));

    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sampled },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImg },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBuf },
        { VK_DESCRIPTOR_TYPE_SAMPLER, samplers },
    };
    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pi.maxSets = 1;
    pi.poolSizeCount = 4;
    pi.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(m_Device, &pi, nullptr, &m_BindlessPool));

    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = m_BindlessPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_BindlessLayout;
    VK_CHECK(vkAllocateDescriptorSets(m_Device, &ai, &m_BindlessSet));

    VkPushConstantRange pc{ VK_SHADER_STAGE_ALL, 0, kMaxPushConstantSize };
    VkPipelineLayoutCreateInfo pl{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_BindlessLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pl, nullptr, &m_PipelineLayout));
}

void VulkanDevice::CreateSamplers() {
    auto make = [&](VkFilter filter, VkSamplerAddressMode addr, float aniso, bool compare) {
        VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        si.magFilter = si.minFilter = filter;
        si.mipmapMode = filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = addr;
        si.maxLod = VK_LOD_CLAMP_NONE;
        si.anisotropyEnable = aniso > 1.0f && m_HasAnisotropy;
        si.maxAnisotropy = std::min(aniso, m_Props.properties.limits.maxSamplerAnisotropy);
        si.compareEnable = compare;
        si.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // reverse-Z shadow maps
        si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        VkSampler s;
        VK_CHECK(vkCreateSampler(m_Device, &si, nullptr, &s));
        return s;
    };
    m_Samplers[(u32)SamplerIndex::LinearClamp] = make(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, false);
    m_Samplers[(u32)SamplerIndex::LinearWrap]  = make(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, false);
    m_Samplers[(u32)SamplerIndex::PointClamp]  = make(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, false);
    m_Samplers[(u32)SamplerIndex::PointWrap]   = make(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, false);
    m_Samplers[(u32)SamplerIndex::AnisoWrap]   = make(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, 16, false);
    m_Samplers[(u32)SamplerIndex::ShadowCompare] = make(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, true);

    std::vector<VkDescriptorImageInfo> infos;
    for (VkSampler s : m_Samplers) infos.push_back({ s, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED });
    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = m_BindlessSet;
    w.dstBinding = kBindSamplers;
    w.descriptorCount = (u32)infos.size();
    w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    w.pImageInfo = infos.data();
    vkUpdateDescriptorSets(m_Device, 1, &w, 0, nullptr);
}

void VulkanDevice::CreateFrames() {
    for (auto& f : m_Frames) {
        VkCommandPoolCreateInfo pi{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pi.queueFamilyIndex = m_QueueFamily;
        VK_CHECK(vkCreateCommandPool(m_Device, &pi, nullptr, &f.pool));
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(m_Device, &ai, &f.cmd));
        VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(m_Device, &fi, nullptr, &f.fence));
        VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(m_Device, &si, nullptr, &f.acquired));
        VkQueryPoolCreateInfo qi{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = kMaxQueries;
        VK_CHECK(vkCreateQueryPool(m_Device, &qi, nullptr, &f.queries));
        vkResetQueryPool(m_Device, f.queries, 0, kMaxQueries);
    }
    VkCommandPoolCreateInfo pi{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pi.queueFamilyIndex = m_QueueFamily;
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VK_CHECK(vkCreateCommandPool(m_Device, &pi, nullptr, &m_ImmPool));
    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(m_Device, &fi, nullptr, &m_ImmFence));
}

void VulkanDevice::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &caps));
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) extent = { m_Window.Width(), m_Window.Height() };
    if (extent.width == 0 || extent.height == 0) return;

    u32 fc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &fc, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &fc, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f;
    m_SwapFormat = chosen.format;

    u32 pc = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &pc, nullptr);
    std::vector<VkPresentModeKHR> modes(pc);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &pc, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_VSync) {
        auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
        mode = has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
             : has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
    }

    u32 imageCount = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface = m_Surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(m_Device, &ci, nullptr, &m_Swapchain));

    u32 n = 0;
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &n, nullptr);
    std::vector<VkImage> images(n);
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &n, images.data());

    TextureDesc td;
    td.width = extent.width; td.height = extent.height;
    td.format = m_SwapFormat == VK_FORMAT_B8G8R8A8_UNORM ? Format::BGRA8_UNORM : Format::RGBA8_UNORM;
    td.usage = TextureUsage::RenderTarget | TextureUsage::CopyDst;
    td.name = "BackBuffer";
    for (VkImage img : images) {
        m_SwapTextures.push_back(MakeScope<VulkanTexture>(*this, td, img));
        VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkSemaphore s;
        VK_CHECK(vkCreateSemaphore(m_Device, &si, nullptr, &s));
        m_RenderFinished.push_back(s);
    }
    m_SwapchainDirty = false;
}

void VulkanDevice::DestroySwapchain() {
    m_SwapTextures.clear();
    FlushDeletions(true); // swap textures defer their view destruction
    for (VkSemaphore s : m_RenderFinished) vkDestroySemaphore(m_Device, s, nullptr);
    m_RenderFinished.clear();
    if (m_Swapchain) vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
    m_Swapchain = VK_NULL_HANDLE;
}

// ------------------------------------------------------------------------------------------------ frame loop

void VulkanDevice::Defer(std::function<void()> fn) {
    m_DeletionQueue.emplace_back(m_FrameNumber, std::move(fn));
}

void VulkanDevice::FlushDeletions(bool all) {
    // Frames up to (m_FrameNumber - kFramesInFlight) are guaranteed complete once the current slot's fence is signaled.
    while (!m_DeletionQueue.empty()) {
        auto& [frame, fn] = m_DeletionQueue.front();
        if (!all && frame + kFramesInFlight > m_FrameNumber) break;
        auto f = std::move(fn);
        m_DeletionQueue.pop_front();
        f();
    }
}

bool VulkanDevice::BeginFrame() {
    if (m_Window.IsMinimized()) return false;
    if (m_SwapchainDirty || !m_Swapchain ||
        m_SwapTextures.empty() || m_SwapTextures[0]->Width() != m_Window.Width() || m_SwapTextures[0]->Height() != m_Window.Height()) {
        vkDeviceWaitIdle(m_Device);
        DestroySwapchain();
        CreateSwapchain();
        if (!m_Swapchain) return false;
        if (m_ImGuiReady) ImGui_ImplVulkan_SetMinImageCount(2);
    }

    Frame& f = m_Frames[m_FrameIndex];
    VK_CHECK(vkWaitForFences(m_Device, 1, &f.fence, VK_TRUE, UINT64_MAX));

    // Resolve this slot's timestamps from kFramesInFlight frames ago
    if (f.queryCount > 0) {
        std::vector<u64> ts(f.queryCount);
        if (vkGetQueryPoolResults(m_Device, f.queries, 0, f.queryCount, ts.size() * sizeof(u64), ts.data(), sizeof(u64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            m_Timings.clear();
            double period = m_Props.properties.limits.timestampPeriod;
            for (auto& [name, range] : f.timerRanges)
                if (range.second != kInvalidIndex)
                    m_Timings.push_back({ name, (float)((ts[range.second] - ts[range.first]) * period * 1e-6) });
        }
    }
    FlushDeletions(false);

    VkResult r = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &m_ImageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { m_SwapchainDirty = true; return false; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) VK_CHECK(r);

    VK_CHECK(vkResetFences(m_Device, 1, &f.fence));
    VK_CHECK(vkResetCommandPool(m_Device, f.pool, 0));
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));
    vkCmdResetQueryPool(f.cmd, f.queries, 0, kMaxQueries);
    f.queryCount = 0;
    f.timerRanges.clear();
    m_TimerStack.clear();

    m_CmdList.Begin(f.cmd);
    m_SwapTextures[m_ImageIndex]->state = ResourceState::Undefined;
    m_InFrame = true;
    return true;
}

void VulkanDevice::EndFrame() {
    Frame& f = m_Frames[m_FrameIndex];
    m_CmdList.Barrier(m_SwapTextures[m_ImageIndex].get(), ResourceState::Present);
    VK_CHECK(vkEndCommandBuffer(f.cmd));

    VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    signal.semaphore = m_RenderFinished[m_ImageIndex];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cbi.commandBuffer = f.cmd;
    VkSubmitInfo2 si{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    si.waitSemaphoreInfoCount = 1;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = &signal;
    VK_CHECK(vkQueueSubmit2(m_Queue, 1, &si, f.fence));

    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_RenderFinished[m_ImageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_Swapchain;
    pi.pImageIndices = &m_ImageIndex;
    VkResult r = vkQueuePresentKHR(m_Queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) m_SwapchainDirty = true;
    else if (r != VK_SUCCESS) VK_CHECK(r);

    m_InFrame = false;
    m_FrameIndex = (m_FrameIndex + 1) % kFramesInFlight;
    ++m_FrameNumber;
}

void VulkanDevice::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& fn) {
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = m_ImmPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &ai, &cb));
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &bi));
    fn(cb);
    VK_CHECK(vkEndCommandBuffer(cb));
    VkCommandBufferSubmitInfo cbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cbi.commandBuffer = cb;
    VkSubmitInfo2 si{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    VK_CHECK(vkQueueSubmit2(m_Queue, 1, &si, m_ImmFence));
    VK_CHECK(vkWaitForFences(m_Device, 1, &m_ImmFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_Device, 1, &m_ImmFence));
    vkFreeCommandBuffers(m_Device, m_ImmPool, 1, &cb);
}

// ------------------------------------------------------------------------------------------------ GPU timers

void VulkanDevice::BeginGpuTimer(const char* name) {
    if (!m_InFrame) return;
    Frame& f = m_Frames[m_FrameIndex];
    m_CmdList.BeginMarker(name);
    if (f.queryCount + 2 > kMaxQueries) { m_TimerStack.push_back(kInvalidIndex); return; }
    u32 q = f.queryCount++;
    vkCmdWriteTimestamp2(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, q);
    m_TimerStack.push_back((u32)f.timerRanges.size());
    f.timerRanges.push_back({ name, { q, kInvalidIndex } });
}

void VulkanDevice::EndGpuTimer() {
    if (!m_InFrame || m_TimerStack.empty()) return;
    Frame& f = m_Frames[m_FrameIndex];
    u32 range = m_TimerStack.back();
    m_TimerStack.pop_back();
    m_CmdList.EndMarker();
    if (range == kInvalidIndex) return;
    u32 q = f.queryCount++;
    vkCmdWriteTimestamp2(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, q);
    f.timerRanges[range].second.second = q;
}

// ------------------------------------------------------------------------------------------------ descriptors

void VulkanDevice::WriteImageDescriptor(u32 binding, u32 index, VkImageView view, VkDescriptorType type, VkImageLayout layout) {
    VkDescriptorImageInfo ii{ VK_NULL_HANDLE, view, layout };
    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = m_BindlessSet;
    w.dstBinding = binding;
    w.dstArrayElement = index;
    w.descriptorCount = 1;
    w.descriptorType = type;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_Device, 1, &w, 0, nullptr);
}

void VulkanDevice::WriteBufferDescriptor(u32 index, VkBuffer buffer, u64 size) {
    VkDescriptorBufferInfo bi{ buffer, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = m_BindlessSet;
    w.dstBinding = kBindBuffers;
    w.dstArrayElement = index;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_Device, 1, &w, 0, nullptr);
}

void VulkanDevice::SetDebugName(VkObjectType type, u64 handle, const std::string& name) {
    if (!m_pfnSetName || name.empty()) return;
    VkDebugUtilsObjectNameInfoEXT ni{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    ni.objectType = type;
    ni.objectHandle = handle;
    ni.pObjectName = name.c_str();
    m_pfnSetName(m_Device, &ni);
}

// ------------------------------------------------------------------------------------------------ pipelines

VkShaderModule VulkanDevice::LoadShader(const std::string& name) {
    auto path = FileSystem::ExecutableDir() / "shaders" / (name + ".spv");
    if (name.starts_with("asset:")) path = AssetManager::ToAbsolute(name.substr(6));
    auto code = FileSystem::ReadBinary(path);
    SD_VERIFY(!code.empty(), "Shader not found: " + path.string());
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = code.size();
    ci.pCode = (const u32*)code.data();
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(m_Device, &ci, nullptr, &m));
    return m;
}

static VkCompareOp ToVk(CompareOp op) {
    switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

Ref<Pipeline> VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& d) {
    VkShaderModule vs = LoadShader(d.vs);
    VkShaderModule ps = d.ps.empty() ? VK_NULL_HANDLE : LoadShader(d.ps);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main" };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "main" };

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = d.topology == Topology::TriangleList ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                : d.topology == Topology::TriangleStrip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                : d.topology == Topology::LineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = d.wireframe && m_HasWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = d.cull == CullMode::None ? VK_CULL_MODE_NONE : d.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
    rs.frontFace = d.frontCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.depthClampEnable = d.depthClamp && m_HasDepthClamp;
    rs.depthBiasEnable = d.depthBiasConstant != 0 || d.depthBiasSlope != 0;
    rs.depthBiasConstantFactor = d.depthBiasConstant;
    rs.depthBiasSlopeFactor = d.depthBiasSlope;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ms.alphaToCoverageEnable = d.alphaToCoverage;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = d.depthTest && d.depthFormat != Format::Unknown;
    ds.depthWriteEnable = d.depthWrite && d.depthFormat != Format::Unknown;
    ds.depthCompareOp = ToVk(d.depthCompare);

    std::vector<VkPipelineColorBlendAttachmentState> blends(d.colorFormats.size());
    for (size_t bi = 0; bi < blends.size(); ++bi) {
        auto& b = blends[bi];
        b.colorWriteMask = 0xF;
        // integer targets (entity ids) cannot blend
        bool integer = d.colorFormats[bi] == Format::R32U || d.colorFormats[bi] == Format::RG32U;
        b.blendEnable = d.blend != BlendMode::None && !integer;
        if (!b.blendEnable) continue;
        b.colorBlendOp = b.alphaBlendOp = VK_BLEND_OP_ADD;
        switch (d.blend) {
        case BlendMode::Alpha:
            b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        case BlendMode::Additive:
            b.srcColorBlendFactor = b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.srcAlphaBlendFactor = b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; break;
        case BlendMode::Premultiplied:
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        default: break;
        }
    }
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = (u32)blends.size();
    cb.pAttachments = blends.data();

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    std::vector<VkFormat> colorFormats;
    for (Format f : d.colorFormats) colorFormats.push_back(Vk::ToVk(f));
    VkPipelineRenderingCreateInfo ri{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    ri.colorAttachmentCount = (u32)colorFormats.size();
    ri.pColorAttachmentFormats = colorFormats.data();
    ri.depthAttachmentFormat = Vk::ToVk(d.depthFormat);
    if (d.depthFormat == Format::D24S8) ri.stencilAttachmentFormat = ri.depthAttachmentFormat;

    VkGraphicsPipelineCreateInfo ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    ci.pNext = &ri;
    ci.stageCount = ps ? 2 : 1;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = m_PipelineLayout;
    VkPipeline p;
    VkResult r = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
    vkDestroyShaderModule(m_Device, vs, nullptr);
    if (ps) vkDestroyShaderModule(m_Device, ps, nullptr);
    VK_CHECK(r);
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, (u64)p, d.name.empty() ? d.vs : d.name);
    return MakeRef<VulkanPipeline>(*this, p, false);
}

Ref<Pipeline> VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& d) {
    VkShaderModule cs = LoadShader(d.cs);
    VkComputePipelineCreateInfo ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    ci.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, cs, "main" };
    ci.layout = m_PipelineLayout;
    VkPipeline p;
    VkResult r = vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
    vkDestroyShaderModule(m_Device, cs, nullptr);
    VK_CHECK(r);
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, (u64)p, d.name.empty() ? d.cs : d.name);
    return MakeRef<VulkanPipeline>(*this, p, true);
}

VulkanPipeline::~VulkanPipeline() {
    VkDevice dev = m_Dev.Raw();
    VkPipeline p = pipeline;
    m_Dev.Defer([dev, p] { vkDestroyPipeline(dev, p, nullptr); });
}

// ------------------------------------------------------------------------------------------------ resources

Ref<Buffer> VulkanDevice::CreateBuffer(const BufferDesc& desc, const void* initialData) {
    auto b = MakeRef<VulkanBuffer>(*this, desc);
    if (initialData) UploadBuffer(b.get(), initialData, desc.size, 0);
    return b;
}

Ref<Texture> VulkanDevice::CreateTexture(const TextureDesc& desc) {
    return MakeRef<VulkanTexture>(*this, desc);
}

void VulkanDevice::UploadBuffer(Buffer* dst, const void* data, u64 size, u64 dstOffset) {
    if (dst->Mapped()) {
        memcpy((u8*)dst->Mapped() + dstOffset, data, size);
        return;
    }
    VulkanBuffer staging(*this, { size, BufferUsage::CopySrc, MemoryUsage::CPUToGPU, "Staging" });
    memcpy(staging.Mapped(), data, size);
    auto* vdst = (VulkanBuffer*)dst;
    ImmediateSubmit([&](VkCommandBuffer cb) {
        VkBufferCopy c{ 0, dstOffset, size };
        vkCmdCopyBuffer(cb, staging.buffer, vdst->buffer, 1, &c);
    });
}

void ImageBarrier(VkCommandBuffer cb, VulkanTexture* t, ResourceState from, ResourceState to); // VulkanResources.cpp

void VulkanDevice::UploadTexture(Texture* tex, const void* data, u64 size) {
    auto* t = (VulkanTexture*)tex;
    const TextureDesc& d = t->Desc();
    VulkanBuffer staging(*this, { size, BufferUsage::CopySrc, MemoryUsage::CPUToGPU, "TexStaging" });
    memcpy(staging.Mapped(), data, size);
    u32 layers = d.dim == TextureDim::Cube ? 6 * d.arrayLayers : d.arrayLayers;

    ImmediateSubmit([&](VkCommandBuffer cb) {
        ImageBarrier(cb, t, t->state, ResourceState::CopyDst);
        VkBufferImageCopy region{};
        region.imageSubresource = { t->Aspect(), 0, 0, layers };
        region.imageExtent = { d.width, d.height, d.depth };
        vkCmdCopyBufferToImage(cb, staging.buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Generate the mip chain with linear blits
        i32 w = (i32)d.width, h = (i32)d.height;
        for (u32 mip = 1; mip < d.mipLevels; ++mip) {
            VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            b.image = t->image;
            b.subresourceRange = { t->Aspect(), mip - 1, 1, 0, layers };
            VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            di.imageMemoryBarrierCount = 1;
            di.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &di);

            VkImageBlit blit{};
            blit.srcSubresource = { t->Aspect(), mip - 1, 0, layers };
            blit.srcOffsets[1] = { w, h, 1 };
            w = std::max(1, w / 2); h = std::max(1, h / 2);
            blit.dstSubresource = { t->Aspect(), mip, 0, layers };
            blit.dstOffsets[1] = { w, h, 1 };
            vkCmdBlitImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            b.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkCmdPipelineBarrier2(cb, &di);
        }
        ImageBarrier(cb, t, ResourceState::CopyDst, ResourceState::ShaderRead);
    });
    t->state = ResourceState::ShaderRead;
}

// ------------------------------------------------------------------------------------------------ ImGui

void VulkanDevice::ImGuiInit() {
    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 };
    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 1024;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VK_CHECK(vkCreateDescriptorPool(m_Device, &pi, nullptr, &m_ImGuiPool));

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = m_Instance;
    ii.PhysicalDevice = m_PhysicalDevice;
    ii.Device = m_Device;
    ii.QueueFamily = m_QueueFamily;
    ii.Queue = m_Queue;
    ii.DescriptorPool = m_ImGuiPool;
    ii.MinImageCount = 2;
    ii.ImageCount = (u32)std::max<size_t>(2, m_SwapTextures.size());
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_SwapFormat;
    ii.CheckVkResultFn = [](VkResult r) { if (r != VK_SUCCESS) SD_ERROR("ImGui Vulkan error {}", (int)r); };
    ImGui_ImplVulkan_Init(&ii);
    m_ImGuiReady = true;
}

void VulkanDevice::ImGuiNewFrame() { ImGui_ImplVulkan_NewFrame(); }
void VulkanDevice::ImGuiRebuildFonts() {
    ImGui_ImplVulkan_DestroyFontsTexture();
    ImGui_ImplVulkan_CreateFontsTexture();
}

void VulkanDevice::ImGuiRender(Texture* target) {
    RenderingDesc rd;
    rd.colors.push_back({ target, LoadOp::Clear, { 0.1f, 0.1f, 0.1f, 1.0f } });
    m_CmdList.BeginMarker("ImGui");
    m_CmdList.BeginRendering(rd);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CmdList.Raw());
    m_CmdList.EndRendering();
    m_CmdList.EndMarker();
}

void VulkanDevice::ImGuiShutdown() {
    if (!m_ImGuiReady) return;
    vkDeviceWaitIdle(m_Device);
    FlushDeletions(true);
    ImGui_ImplVulkan_Shutdown();
    m_ImGuiReady = false;
    vkDestroyDescriptorPool(m_Device, m_ImGuiPool, nullptr); // frees every texture set
    m_ImGuiPool = VK_NULL_HANDLE;
}

void* VulkanDevice::ImGuiTexture(Texture* tex) {
    auto* t = (VulkanTexture*)tex;
    if (!t || !t->srvView) return nullptr;
    if (!t->imguiSet)
        t->imguiSet = ImGui_ImplVulkan_AddTexture(Sampler(SamplerIndex::LinearClamp), t->srvView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    return (void*)t->imguiSet;
}

} // namespace Stardust::RHI::Vk
