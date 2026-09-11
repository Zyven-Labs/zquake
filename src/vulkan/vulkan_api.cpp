#include "vulkan/vulkan_api.hpp"
#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_buffer.hpp"
#include "vulkan/vulkan_image.hpp"
#include "core/logging/logger.hpp"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <vector>

namespace zq::vk {

namespace {
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    (void)type;
    bool error = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    bool warning = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    if (data && data->pMessage) {
        if (error) {
            std::fprintf(stderr, "[VK-ERROR] %s\n", data->pMessage);
        } else if (warning) {
            std::fprintf(stderr, "[VK-WARN]  %s\n", data->pMessage);
        }
    }
    return VK_FALSE;
}

uint32_t FindMemoryTypeIdx(VkPhysicalDevice dev, uint32_t type_filter,
                           VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(dev, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

// Load a SPIR-V file into a byte vector
std::vector<uint8_t> LoadSPV(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(data.data()), size);
    }
    return data;
}

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint8_t>& spv) {
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = reinterpret_cast<const uint32_t*>(spv.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}
}

VulkanAPI::VulkanAPI() {}
VulkanAPI::~VulkanAPI() { Shutdown(); }

bool VulkanAPI::CreateInstance() {
    uint32_t sdl_extension_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &sdl_extension_count, nullptr)) {
        log::Error("vulkan: SDL_Vulkan_GetInstanceExtensions failed");
        return false;
    }
    std::vector<const char*> extensions(sdl_extension_count);
    if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &sdl_extension_count, extensions.data())) {
        log::Error("vulkan: SDL_Vulkan_GetInstanceExtensions (data) failed");
        return false;
    }

    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "zquake";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "zq";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> layers;
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layer_props(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layer_props.data());
    bool has_validation = false;
    for (const auto& p : layer_props) {
        if (strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            has_validation = true;
        }
    }
    if (has_validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    // Debug messenger create info (chained into instance creation)
    VkDebugUtilsMessengerCreateInfoEXT debug_ci = {};
    debug_ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_ci.pfnUserCallback = DebugCallback;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
    create_info.pNext = &debug_ci;

    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
        log::Error("vulkan: failed to create instance");
        return false;
    }
    SetupDebugMessenger();
    return true;
}

void VulkanAPI::SetupDebugMessenger() {
    pfnCreateDebugMessenger_ = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!pfnCreateDebugMessenger_) return;

    VkDebugUtilsMessengerCreateInfoEXT ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugCallback;
    pfnCreateDebugMessenger_(instance_, &ci, nullptr, &debug_messenger_);
}

bool VulkanAPI::CreateSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance_, &surface_)) {
        log::Error("vulkan: SDL_Vulkan_CreateSurface failed");
        return false;
    }
    return true;
}

uint32_t VulkanAPI::FindGraphicsQueueFamily() const {
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count, properties.data());
    for (uint32_t i = 0; i < family_count; i++) {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    return 0;
}

uint32_t VulkanAPI::FindPresentQueueFamily() const {
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count, properties.data());
    for (uint32_t i = 0; i < family_count; i++) {
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &present_support);
        if (present_support) {
            return i;
        }
    }
    return 0;
}

bool VulkanAPI::CreateDevice() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        log::Error("vulkan: no physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
    physical_device_ = devices[0];

    VkPhysicalDeviceProperties device_props;
    vkGetPhysicalDeviceProperties(physical_device_, &device_props);
    log::Info(device_props.deviceName);

    queue_family_ = FindGraphicsQueueFamily();
    FindPresentQueueFamily();

    const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures device_features = {};

    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_create_info;
    create_info.pEnabledFeatures = &device_features;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = device_extensions;

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
        log::Error("vulkan: failed to create logical device");
        return false;
    }

    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    return true;
}

bool VulkanAPI::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());

    if (!formats.empty()) {
        swapchain_format_ = formats[0].format;
        swapchain_colorspace_ = formats[0].colorSpace;
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
                swapchain_format_ = f.format;
                swapchain_colorspace_ = f.colorSpace;
                break;
            }
        }
    }

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, present_modes.data());

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    // Prefer no vsync cap (IMMEDIATE), then MAILBOX, else FIFO.
    for (const auto& pm : present_modes) {
        if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR) { present_mode = pm; break; }
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { present_mode = pm; }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width, 1024u));
        extent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height, 768u));
    }
    swapchain_extent_ = extent;

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = swapchain_format_;
    create_info.imageColorSpace = swapchain_colorspace_;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = swapchain_;

    if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_) != VK_SUCCESS) {
        log::Error("vulkan: failed to create swapchain");
        return false;
    }

    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

    for (auto v : swapchain_image_views_) {
        if (v) vkDestroyImageView(device_, v, nullptr);
    }
    swapchain_image_views_.clear();
    swapchain_image_views_.resize(image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        vkCreateImageView(device_, &view_info, nullptr, &swapchain_image_views_[i]);
    }

    return true;
}

bool VulkanAPI::CreateRenderPass() {
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = swapchain_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = VK_FORMAT_D32_SFLOAT;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref = {};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };
    VkRenderPassCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 2;
    create_info.pAttachments = attachments;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &create_info, nullptr, &render_pass_) != VK_SUCCESS) {
        log::Error("vulkan: failed to create render pass");
        return false;
    }
    return true;
}

bool VulkanAPI::CreateDepthBuffer() {
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = swapchain_extent_.width;
    image_info.extent.height = swapchain_extent_.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_D32_SFLOAT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &image_info, nullptr, &depth_image_) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, depth_image_, &mem_reqs);
    uint32_t mem_type = FindMemoryTypeIdx(physical_device_, mem_reqs.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;
    if (vkAllocateMemory(device_, &alloc_info, nullptr, &depth_memory_) != VK_SUCCESS) {
        vkDestroyImage(device_, depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(device_, depth_image_, depth_memory_, 0);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = depth_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_D32_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    vkCreateImageView(device_, &view_info, nullptr, &depth_view_);
    return true;
}

bool VulkanAPI::CreateFramebuffers() {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    framebuffers_.resize(swapchain_image_views_.size());
    for (size_t i = 0; i < swapchain_image_views_.size(); i++) {
        VkImageView attachments[2] = { swapchain_image_views_[i], depth_view_ };
        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = render_pass_;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.width = swapchain_extent_.width;
        info.height = swapchain_extent_.height;
        info.layers = 1;
        if (vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanAPI::CreateSyncObjects() {
    image_available_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanAPI::CreateCommandPool() {
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_;
    if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS)
        return false;

    command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    if (vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data()) != VK_SUCCESS)
        return false;
    return true;
}

bool VulkanAPI::CreateDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[3] = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
    pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;

    VkDescriptorPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = 3;
    info.pPoolSizes = pool_sizes;
    info.maxSets = MAX_FRAMES_IN_FLIGHT;
    if (vkCreateDescriptorPool(device_, &info, nullptr, &descriptor_pool_) != VK_SUCCESS)
        return false;

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                               pipeline_->GetDescriptorSetLayout());
    descriptor_sets_.resize(MAX_FRAMES_IN_FLIGHT);
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device_, &alloc_info, descriptor_sets_.data()) != VK_SUCCESS)
        return false;

    ubo_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ubo_buffers_[i] = new VulkanBuffer();
        if (!ubo_buffers_[i]->Create(physical_device_, device_, 128,
                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
            return false;
        }
    }
    return true;
}

bool VulkanAPI::Initialize(SDL_Window* window) {
    if (!CreateInstance()) return false;
    if (!CreateSurface(window)) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
        return false;
    }
    if (!CreateDevice()) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        vkDestroyInstance(instance_, nullptr);
        surface_ = VK_NULL_HANDLE;
        instance_ = VK_NULL_HANDLE;
        return false;
    }
    if (!CreateSwapchain()) return false;
    if (!CreateRenderPass()) return false;
    if (!CreateDepthBuffer()) return false;
    if (!CreateFramebuffers()) return false;
    if (!CreateCommandPool()) return false;
    if (!CreateSyncObjects()) return false;

    // Load shaders and create pipeline
    pipeline_ = new VulkanPipeline();
    auto vs = LoadSPV("shaders/spv/forward_vertex.spv");
    auto fs = LoadSPV("shaders/spv/forward_fragment.spv");
    if (vs.empty() || fs.empty()) {
        log::Error("vulkan: failed to load shader SPIR-V");
        delete pipeline_;
        pipeline_ = nullptr;
        return false;
    }
    if (!pipeline_->Create(device_, render_pass_, vs.data(), vs.size(),
                           fs.data(), fs.size())) {
        log::Error("vulkan: failed to create graphics pipeline");
        delete pipeline_;
        pipeline_ = nullptr;
        return false;
    }

    if (!CreateDescriptorPool()) return false;

    CreateOverlay();

    initialized_ = true;
    swapchain_valid_ = true;
    log::Info("vulkan: initialized");
    return true;
}

void VulkanAPI::RecreateSwapchain() {
    vkDeviceWaitIdle(device_);

    if (pipeline_) {
        pipeline_->Destroy();
        delete pipeline_;
        pipeline_ = nullptr;
    }
    if (descriptor_pool_) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    for (auto* ubo : ubo_buffers_) {
        if (ubo) {
            ubo->Destroy();
            delete ubo;
        }
    }
    ubo_buffers_.clear();

    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    for (auto v : swapchain_image_views_) {
        if (v) vkDestroyImageView(device_, v, nullptr);
    }
    swapchain_image_views_.clear();

    if (depth_view_) vkDestroyImageView(device_, depth_view_, nullptr);
    if (depth_image_) vkDestroyImage(device_, depth_image_, nullptr);
    if (depth_memory_) vkFreeMemory(device_, depth_memory_, nullptr);
    depth_view_ = VK_NULL_HANDLE;
    depth_image_ = VK_NULL_HANDLE;
    depth_memory_ = VK_NULL_HANDLE;

    if (render_pass_) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    if (overlay_pipeline_) {
        vkDestroyPipeline(device_, overlay_pipeline_, nullptr);
        overlay_pipeline_ = VK_NULL_HANDLE;
    }
    if (overlay_layout_) {
        vkDestroyPipelineLayout(device_, overlay_layout_, nullptr);
        overlay_layout_ = VK_NULL_HANDLE;
    }
    if (overlay_pass_) {
        vkDestroyRenderPass(device_, overlay_pass_, nullptr);
        overlay_pass_ = VK_NULL_HANDLE;
    }
    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    if (!CreateSwapchain()) return;
    if (!CreateRenderPass()) return;
    if (!CreateDepthBuffer()) return;
    if (!CreateFramebuffers()) return;

    pipeline_ = new VulkanPipeline();
    auto vs = LoadSPV("shaders/spv/forward_vertex.spv");
    auto fs = LoadSPV("shaders/spv/forward_fragment.spv");
    if (!vs.empty() && !fs.empty()) {
        pipeline_->Create(device_, render_pass_, vs.data(), vs.size(), fs.data(), fs.size());
    }
    CreateDescriptorPool();
    CreateOverlay();
    swapchain_valid_ = true;
}

bool VulkanAPI::BeginFrame(bool startRenderPass) {
    if (!initialized_ || !swapchain_valid_) return false;

    vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX,
        image_available_[current_frame_], VK_NULL_HANDLE, &image_index_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return false;
    }

    vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);
    vkResetCommandBuffer(command_buffers_[current_frame_], 0);

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    in_frame_ = true;
    render_pass_active_ = false;
    if (startRenderPass) BeginRenderPass();
    return true;
}

void VulkanAPI::BeginRenderPass() {
    if (!in_frame_) return;
    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkClearValue clear_values[2] = {};
    clear_values[0].color = { { 0.05f, 0.06f, 0.12f, 1.0f } };
    clear_values[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = render_pass_;
    render_pass_info.framebuffer = framebuffers_[image_index_];
    render_pass_info.renderArea.offset = { 0, 0 };
    render_pass_info.renderArea.extent = swapchain_extent_;
    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clear_values;
    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)swapchain_extent_.width;
    viewport.height = (float)swapchain_extent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain_extent_;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (pipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->GetPipeline());
    }

    render_pass_active_ = true;
}

void VulkanAPI::SetCamera(const float* projection, const float* view) {
    memcpy(projection_, projection, sizeof(projection_));
    memcpy(view_, view, sizeof(view_));
    // Vulkan NDC has Y pointing down; OpenGL-style projections are Y-up.
    // Negate the Y scale so the image is not vertically flipped.
    projection_[5] = -projection_[5];
}

void VulkanAPI::DrawMesh(const Mesh& mesh) {
    if (!in_frame_ || !pipeline_ || !mesh.vertex_buffer || !mesh.index_buffer) return;
    if (mesh.index_count == 0) return;

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    // UBO: [projection][view] (column-major). Written once per frame; the GPU
    // reads it at execution time, so per-mesh model matrices go in a push
    // constant (recorded per draw) to avoid every draw seeing the last write.
    float full[32];
    memcpy(full, projection_, 16 * sizeof(float));   // projection slot
    memcpy(full + 16, view_, 16 * sizeof(float));    // view slot

    float model[16];
    memset(model, 0, sizeof(model));
    model[0] = model[5] = model[10] = model[15] = 1.0f;
    if (mesh.has_model) {
        memcpy(model, mesh.model, sizeof(model));
    }

    VulkanBuffer* ubo = ubo_buffers_[current_frame_];
    void* mapped = ubo->Map();
    if (mapped) {
        memcpy(mapped, full, sizeof(full));
        ubo->Unmap();
    }

    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = ubo->GetBuffer();
    buffer_info.offset = 0;
    buffer_info.range = 128;

    VkDescriptorImageInfo tex_info = {};
    tex_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (mesh.texture) {
        tex_info.imageView = mesh.texture->GetImageView();
        tex_info.sampler = mesh.texture->GetSampler();
    }

    VkDescriptorImageInfo lm_info = {};
    lm_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (mesh.lightmap) {
        lm_info.imageView = mesh.lightmap->GetImageView();
        lm_info.sampler = mesh.lightmap->GetSampler();
    }

    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_sets_[current_frame_];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &buffer_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptor_sets_[current_frame_];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &tex_info;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptor_sets_[current_frame_];
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &lm_info;

    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);

    VkBuffer vertex_buffers[] = { mesh.vertex_buffer->GetBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd, mesh.index_buffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_->GetLayout(), 0, 1,
                            &descriptor_sets_[current_frame_], 0, nullptr);

    // Per-mesh model matrix via push constant (recorded per draw).
    vkCmdPushConstants(cmd, pipeline_->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(model), model);

    vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
}

bool VulkanAPI::CreateOverlay() {
    auto vs = LoadSPV("shaders/spv/hud_vertex.spv");
    auto fs = LoadSPV("shaders/spv/hud_fragment.spv");
    if (vs.empty() || fs.empty()) {
        log::Error("vulkan: failed to load HUD shader SPIR-V");
        return false;
    }

    VkShaderModule vs_mod = CreateShaderModule(device_, vs);
    VkShaderModule fs_mod = CreateShaderModule(device_, fs);
    if (!vs_mod || !fs_mod) {
        if (vs_mod) vkDestroyShaderModule(device_, vs_mod, nullptr);
        if (fs_mod) vkDestroyShaderModule(device_, fs_mod, nullptr);
        return false;
    }

    // Overlay render pass: pulls in the already-rendered frame (LOAD) and hands
    // it to the present queue. Depth is referenced for pipeline/render-pass
    // compatibility with the raster pass but never touched (depth disabled).
    if (!overlay_pass_) {
        VkAttachmentDescription color_att = {};
        color_att.format = swapchain_format_;
        color_att.samples = VK_SAMPLE_COUNT_1_BIT;
        color_att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth_att = {};
        depth_att.format = VK_FORMAT_D32_SFLOAT;
        depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_ref = {};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depth_ref = {};
        depth_ref.attachment = 1;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency dep = {};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        // The frame was produced by a previous pass (ray blit); LOAD must see
        // those writes, so the dependency carries color writes -> reads/writes.
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription attachments[2] = { color_att, depth_att };
        VkRenderPassCreateInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.attachmentCount = 2;
        rp_info.pAttachments = attachments;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;
        rp_info.dependencyCount = 1;
        rp_info.pDependencies = &dep;

        if (vkCreateRenderPass(device_, &rp_info, nullptr, &overlay_pass_) != VK_SUCCESS) {
            log::Error("vulkan: failed to create overlay render pass");
            vkDestroyShaderModule(device_, vs_mod, nullptr);
            vkDestroyShaderModule(device_, fs_mod, nullptr);
            return false;
        }
    }

    if (!overlay_layout_) {
        VkPushConstantRange pc = {};
        pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pc.offset = 0;
        pc.size = 8; // uW, uH

        VkPipelineLayoutCreateInfo pl_info = {};
        pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl_info.pushConstantRangeCount = 1;
        pl_info.pPushConstantRanges = &pc;
        if (vkCreatePipelineLayout(device_, &pl_info, nullptr, &overlay_layout_) != VK_SUCCESS) {
            log::Error("vulkan: failed to create overlay pipeline layout");
            vkDestroyRenderPass(device_, overlay_pass_, nullptr);
            overlay_pass_ = VK_NULL_HANDLE;
            vkDestroyShaderModule(device_, vs_mod, nullptr);
            vkDestroyShaderModule(device_, fs_mod, nullptr);
            return false;
        }
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs_mod;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = 16; // vec2 position + vec2 uv
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = 8;

    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend = {};
    blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dynamic;
    info.layout = overlay_layout_;
    info.renderPass = overlay_pass_;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &overlay_pipeline_) != VK_SUCCESS) {
        log::Error("vulkan: failed to create overlay pipeline");
        vkDestroyPipelineLayout(device_, overlay_layout_, nullptr);
        vkDestroyRenderPass(device_, overlay_pass_, nullptr);
        overlay_layout_ = VK_NULL_HANDLE;
        overlay_pass_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, vs_mod, nullptr);
        vkDestroyShaderModule(device_, fs_mod, nullptr);
        return false;
    }

    vkDestroyShaderModule(device_, vs_mod, nullptr);
    vkDestroyShaderModule(device_, fs_mod, nullptr);

    if (!overlay_vbuf_) {
        overlay_vbuf_ = CreateVertexBuffer(3 * 16);
        if (!overlay_vbuf_) return false;
        // Full-screen triangle in NDC (Vulkan NDC, y down): corners cover the
        // whole viewport; UVs match hud_vertex layout even though unused.
        const float kTri[3 * 4] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             3.0f, -1.0f, 2.0f, 0.0f,
            -1.0f,  3.0f, 0.0f, 2.0f,
        };
        UpdateBuffer(overlay_vbuf_, kTri, sizeof(kTri));
    }
    return true;
}

void VulkanAPI::DrawCrosshair() {
    if (!in_frame_ || !overlay_pipeline_ || !overlay_vbuf_) return;

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    const bool separate_pass = !render_pass_active_;
    if (separate_pass) {
        // Ray path: the frame is already in the swapchain (rt.Dispatch blitted
        // it); a LOAD render pass overlays the crosshair on top.
        VkClearValue clear = {};
        clear.depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = overlay_pass_;
        rp.framebuffer = framebuffers_[image_index_];
        rp.renderArea.offset = { 0, 0 };
        rp.renderArea.extent = swapchain_extent_;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    }

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)swapchain_extent_.width;
    viewport.height = (float)swapchain_extent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain_extent_;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
    VkBuffer vb = overlay_vbuf_->GetBuffer();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    float pc[2] = { (float)swapchain_extent_.width, (float)swapchain_extent_.height };
    vkCmdPushConstants(cmd, overlay_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (separate_pass) vkCmdEndRenderPass(cmd);
}

bool VulkanAPI::RenderMeshToImage(uint32_t width, uint32_t height, const Mesh& mesh,
                                  std::vector<uint8_t>& out) {
    std::vector<Mesh> meshes;
    meshes.push_back(mesh);
    return RenderMeshesToImage(width, height, meshes, out);
}

bool VulkanAPI::RenderMeshesToImage(uint32_t width, uint32_t height, const std::vector<Mesh>& meshes,
                                    std::vector<uint8_t>& out) {
    if (!pipeline_) return false;
    // Reuse the swapchain-sized depth buffer.
    if (width != swapchain_extent_.width || height != swapchain_extent_.height) return false;

    // --- Offscreen color image (uses the SAME format as the swapchain so the
    // existing graphics pipeline + render pass are compatible) ---
    VkImage color_image = VK_NULL_HANDLE;
    VkDeviceMemory color_mem = VK_NULL_HANDLE;
    VkImageView color_view = VK_NULL_HANDLE;

    VkImageCreateInfo ii = {};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent.width = width;
    ii.extent.height = height;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.format = swapchain_format_;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device_, &ii, nullptr, &color_image) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device_, color_image, &mr);
    uint32_t mt = FindMemoryTypeIdx(physical_device_, mr.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(device_, &ai, nullptr, &color_mem) != VK_SUCCESS) {
        vkDestroyImage(device_, color_image, nullptr);
        return false;
    }
    vkBindImageMemory(device_, color_image, color_mem, 0);

    VkImageViewCreateInfo iv = {};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = color_image;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = swapchain_format_;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &iv, nullptr, &color_view) != VK_SUCCESS) {
        vkFreeMemory(device_, color_mem, nullptr);
        vkDestroyImage(device_, color_image, nullptr);
        return false;
    }

    VkImageView fb_atts[2] = { color_view, depth_view_ };
    VkFramebufferCreateInfo fbci = {};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = render_pass_; // same render pass the pipeline was built with
    fbci.attachmentCount = 2;
    fbci.pAttachments = fb_atts;
    fbci.width = width;
    fbci.height = height;
    fbci.layers = 1;
    VkFramebuffer offscreen_fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device_, &fbci, nullptr, &offscreen_fb) != VK_SUCCESS) {
        vkDestroyImageView(device_, color_view, nullptr);
        vkFreeMemory(device_, color_mem, nullptr);
        vkDestroyImage(device_, color_image, nullptr);
        return false;
    }

    // --- Staging buffer for readback ---
    size_t pixel_size = (size_t)width * height * 4;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = pixel_size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bci, nullptr, &staging) != VK_SUCCESS) {
        vkDestroyFramebuffer(device_, offscreen_fb, nullptr);
        vkDestroyImageView(device_, color_view, nullptr);
        vkFreeMemory(device_, color_mem, nullptr);
        vkDestroyImage(device_, color_image, nullptr);
        return false;
    }
    vkGetBufferMemoryRequirements(device_, staging, &mr);
    mt = FindMemoryTypeIdx(physical_device_, mr.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(device_, &ai, nullptr, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(device_, staging, nullptr);
        vkDestroyFramebuffer(device_, offscreen_fb, nullptr);
        vkDestroyImageView(device_, color_view, nullptr);
        vkFreeMemory(device_, color_mem, nullptr);
        vkDestroyImage(device_, color_image, nullptr);
        return false;
    }
    vkBindBufferMemory(device_, staging, staging_mem, 0);

    // --- Command buffer ---
    VkCommandBufferAllocateInfo cba = {};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = command_pool_;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &cba, &cmd);

    VkCommandBufferBeginInfo cbbi = {};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);

    VkClearValue clear[2] = {};
    clear[0].color = { { 0.05f, 0.06f, 0.12f, 1.0f } };
    clear[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpbi = {};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = render_pass_;
    rpbi.framebuffer = offscreen_fb;
    rpbi.renderArea.offset = { 0, 0 };
    rpbi.renderArea.extent = { width, height };
    rpbi.clearValueCount = 2;
    rpbi.pClearValues = clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {};
    vp.width = (float)width;
    vp.height = (float)height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc = { {0, 0}, {width, height} };
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->GetPipeline());

    // Record the draw for each mesh (reuse the frame descriptor set).
    for (const auto& mesh : meshes) {
        float full[32];
        memcpy(full, projection_, 16 * sizeof(float));   // projection slot
        memcpy(full + 16, view_, 16 * sizeof(float));    // view slot
        float model[16];
        memset(model, 0, sizeof(model));
        model[0] = model[5] = model[10] = model[15] = 1.0f;
        if (mesh.has_model) memcpy(model, mesh.model, sizeof(model));
        VulkanBuffer* ubo = ubo_buffers_[current_frame_];
        void* m = ubo->Map();
        if (m) { memcpy(m, full, sizeof(full)); ubo->Unmap(); }

        VkDescriptorBufferInfo dbi = {};
        dbi.buffer = ubo->GetBuffer();
        dbi.offset = 0;
        dbi.range = 128;
        VkDescriptorImageInfo tex_info = {};
        tex_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (mesh.texture) { tex_info.imageView = mesh.texture->GetImageView();
                            tex_info.sampler = mesh.texture->GetSampler(); }
        VkDescriptorImageInfo lm_info = {};
        lm_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (mesh.lightmap) { lm_info.imageView = mesh.lightmap->GetImageView();
                             lm_info.sampler = mesh.lightmap->GetSampler(); }

        VkWriteDescriptorSet writes[3] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptor_sets_[current_frame_];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &dbi;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptor_sets_[current_frame_];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &tex_info;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptor_sets_[current_frame_];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &lm_info;
        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);

        if (mesh.vertex_buffer && mesh.index_buffer) {
            VkBuffer vbs[] = { mesh.vertex_buffer->GetBuffer() };
            VkDeviceSize offs[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offs);
            vkCmdBindIndexBuffer(cmd, mesh.index_buffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_->GetLayout(), 0, 1,
                                    &descriptor_sets_[current_frame_], 0, nullptr);
            vkCmdPushConstants(cmd, pipeline_->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(model), model);
            vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);

    // The render pass leaves the image in PRESENT_SRC; transition to TRANSFER_SRC.
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = color_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        vkDestroyFramebuffer(device_, offscreen_fb, nullptr);
        vkDestroyImageView(device_, color_view, nullptr);
        vkFreeMemory(device_, color_mem, nullptr);
        vkDestroyImage(device_, color_image, nullptr);
        return false;
    }
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);

    // Read back
    out.resize(pixel_size);
    void* data = nullptr;
    vkMapMemory(device_, staging_mem, 0, pixel_size, 0, &data);
    if (data) memcpy(out.data(), data, pixel_size);
    vkUnmapMemory(device_, staging_mem);

    // Cleanup
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, staging_mem, nullptr);
    vkDestroyFramebuffer(device_, offscreen_fb, nullptr);
    vkDestroyImageView(device_, color_view, nullptr);
    vkFreeMemory(device_, color_mem, nullptr);
    vkDestroyImage(device_, color_image, nullptr);

    return true;
}

void VulkanAPI::EndFrame() {
    if (!in_frame_) return;

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    if (render_pass_active_) {
        vkCmdEndRenderPass(cmd);
        render_pass_active_ = false;
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore wait_semaphores[] = { image_available_[current_frame_] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = wait_semaphores;
    submit.pWaitDstStageMask = wait_stages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VkSemaphore signal_semaphores[] = { render_finished_[current_frame_] };
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(queue_, 1, &submit, in_flight_fences_[current_frame_]) != VK_SUCCESS) {
        in_frame_ = false;
        return;
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &image_index_;

    VkResult result = vkQueuePresentKHR(queue_, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchain_valid_ = false;
    }

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    in_frame_ = false;
}

void VulkanAPI::Shutdown() {
    if (device_) vkDeviceWaitIdle(device_);

    for (auto* img : textures_) {
        img->Destroy();
        delete img;
    }
    textures_.clear();

    for (auto* buf : buffers_) {
        buf->Destroy();
        delete buf;
    }
    buffers_.clear();

    if (pipeline_) {
        pipeline_->Destroy();
        delete pipeline_;
        pipeline_ = nullptr;
    }
    if (descriptor_pool_) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    for (auto* ubo : ubo_buffers_) {
        if (ubo) {
            ubo->Destroy();
            delete ubo;
        }
    }
    ubo_buffers_.clear();
    if (command_pool_) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
    for (auto s : image_available_) {
        if (s) vkDestroySemaphore(device_, s, nullptr);
    }
    for (auto s : render_finished_) {
        if (s) vkDestroySemaphore(device_, s, nullptr);
    }
    for (auto f : in_flight_fences_) {
        if (f) vkDestroyFence(device_, f, nullptr);
    }
    image_available_.clear();
    render_finished_.clear();
    in_flight_fences_.clear();

    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    for (auto v : swapchain_image_views_) {
        if (v) vkDestroyImageView(device_, v, nullptr);
    }
    swapchain_image_views_.clear();

    if (depth_view_) vkDestroyImageView(device_, depth_view_, nullptr);
    if (depth_image_) vkDestroyImage(device_, depth_image_, nullptr);
    if (depth_memory_) vkFreeMemory(device_, depth_memory_, nullptr);
    depth_view_ = VK_NULL_HANDLE;
    depth_image_ = VK_NULL_HANDLE;
    depth_memory_ = VK_NULL_HANDLE;

    if (render_pass_) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    if (overlay_pipeline_) {
        vkDestroyPipeline(device_, overlay_pipeline_, nullptr);
        overlay_pipeline_ = VK_NULL_HANDLE;
    }
    if (overlay_layout_) {
        vkDestroyPipelineLayout(device_, overlay_layout_, nullptr);
        overlay_layout_ = VK_NULL_HANDLE;
    }
    if (overlay_pass_) {
        vkDestroyRenderPass(device_, overlay_pass_, nullptr);
        overlay_pass_ = VK_NULL_HANDLE;
    }
    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    if (device_) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (debug_messenger_ && pfnCreateDebugMessenger_) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance_, debug_messenger_, nullptr);
        debug_messenger_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    initialized_ = false;
}

bool VulkanAPI::IsInitialized() const { return initialized_; }

VulkanImage* VulkanAPI::CreateTexture(uint32_t width, uint32_t height, const void* rgba8_pixels) {
    VulkanImage* img = new VulkanImage();
    if (!img->Create(physical_device_, device_, command_pool_, queue_, width, height, rgba8_pixels)) {
        delete img;
        return nullptr;
    }
    textures_.push_back(img);
    return img;
}

VulkanImage* VulkanAPI::CreateLightmap(const float rgba[4]) {
    VulkanImage* img = new VulkanImage();
    if (!img->CreateSolid(physical_device_, device_, command_pool_, queue_, 16, 16, rgba)) {
        delete img;
        return nullptr;
    }
    textures_.push_back(img);
    return img;
}

void VulkanAPI::DestroyTexture(VulkanImage* image) {
    if (!image) return;
    auto it = std::find(textures_.begin(), textures_.end(), image);
    if (it != textures_.end()) textures_.erase(it);
    image->Destroy();
    delete image;
}

VulkanBuffer* VulkanAPI::CreateVertexBuffer(size_t size) {
    VulkanBuffer* buf = new VulkanBuffer();
    if (!buf->Create(physical_device_, device_, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        delete buf;
        return nullptr;
    }
    buffers_.push_back(buf);
    return buf;
}

VulkanBuffer* VulkanAPI::CreateIndexBuffer(size_t size) {
    VulkanBuffer* buf = new VulkanBuffer();
    if (!buf->Create(physical_device_, device_, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        delete buf;
        return nullptr;
    }
    buffers_.push_back(buf);
    return buf;
}

void VulkanAPI::UpdateBuffer(VulkanBuffer* buffer, const void* data, size_t size) {
    if (buffer) buffer->Update(data, size);
}

void VulkanAPI::DestroyBuffer(VulkanBuffer* buffer) {
    if (!buffer) return;
    auto it = std::find(buffers_.begin(), buffers_.end(), buffer);
    if (it != buffers_.end()) buffers_.erase(it);
    buffer->Destroy();
    delete buffer;
}

} // namespace zq::vk