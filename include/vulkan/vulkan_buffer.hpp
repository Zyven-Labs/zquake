#pragma once
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <cstddef>

namespace zq::vk {

class VulkanBuffer {
public:
    VulkanBuffer();
    ~VulkanBuffer();

    bool Create(VkPhysicalDevice physical_device, VkDevice device,
                size_t size, VkBufferUsageFlags usage,
                VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void Destroy();

    void Update(const void* data, size_t size);
    void* Map();
    void Unmap();

    VkBuffer GetBuffer() const { return buffer_; }
    VkDeviceMemory GetMemory() const { return memory_; }
    size_t GetSize() const { return size_; }

private:
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    size_t size_ = 0;
};

}