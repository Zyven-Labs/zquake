#include "vulkan/vulkan_buffer.hpp"
#include <cstring>

namespace zq::vk {

namespace {
// Find first memory type matching the given properties
uint32_t FindMemoryType(VkPhysicalDevice device, uint32_t type_filter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}
}

VulkanBuffer::VulkanBuffer() {}
VulkanBuffer::~VulkanBuffer() { Destroy(); }

bool VulkanBuffer::Create(VkPhysicalDevice physical_device, VkDevice device,
                          size_t size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties) {
    physical_device_ = physical_device;
    device_ = device;
    size_ = size;

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &buffer_info, nullptr, &buffer_) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, buffer_, &mem_reqs);

    uint32_t mem_type = FindMemoryType(physical_device_, mem_reqs.memoryTypeBits, properties);
    if (mem_type == UINT32_MAX) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;

    if (vkAllocateMemory(device_, &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device_, buffer_, memory_, 0);
    return true;
}

void VulkanBuffer::Destroy() {
    if (buffer_) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    size_ = 0;
}

void VulkanBuffer::Update(const void* data, size_t size) {
    if (!data || !buffer_) return;
    void* mapped = Map();
    if (mapped) {
        memcpy(mapped, data, size < size_ ? size : size_);
        Unmap();
    }
}

void* VulkanBuffer::Map() {
    if (!buffer_ || !memory_) return nullptr;
    void* result = nullptr;
    vkMapMemory(device_, memory_, 0, size_, 0, &result);
    return result;
}

void VulkanBuffer::Unmap() {
    if (device_ && memory_) vkUnmapMemory(device_, memory_);
}

} // namespace zq::vk