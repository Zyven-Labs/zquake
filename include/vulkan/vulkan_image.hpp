#pragma once
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <cstddef>

namespace zq::vk {

class VulkanImage {
public:
    VulkanImage();
    ~VulkanImage();

    // Creates a texture image from RGBA8 pixel data.
    // Uploads via a staging buffer (host visible) then transitions to
    // SHADER_READ_ONLY_OPTIMAL. Also creates an image view and sampler.
    bool Create(VkPhysicalDevice physical_device, VkDevice device,
                VkCommandPool pool, VkQueue queue,
                uint32_t width, uint32_t height, const void* rgba8_pixels);

    // Create an empty image (e.g. solid lightmap) filled with a single color
    bool CreateSolid(VkPhysicalDevice physical_device, VkDevice device,
                     VkCommandPool pool, VkQueue queue,
                     uint32_t width, uint32_t height,
                     const float rgba[4]);

    void Destroy();

    VkImage GetImage() const { return image_; }
    VkImageView GetImageView() const { return image_view_; }
    VkSampler GetSampler() const { return sampler_; }
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }

private:
    void TransitionLayout(VkCommandPool pool, VkQueue queue,
                          VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

}