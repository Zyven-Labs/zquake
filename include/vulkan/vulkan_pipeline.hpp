#pragma once
#include <vulkan/vulkan.hpp>

namespace zq::vk {

// Graphics pipeline for world rendering
// Vertex layout: pos(3) uv(2) normal(3) lightmapuv(3) = 11 floats = 44 bytes
class VulkanPipeline {
public:
    VulkanPipeline();
    ~VulkanPipeline();

    bool Create(VkDevice device, VkRenderPass render_pass,
                const void* vertex_spv, size_t vertex_spv_size,
                const void* fragment_spv, size_t fragment_spv_size);
    void Destroy();
    bool IsCreated() const;

    VkPipeline GetPipeline() const { return pipeline_; }
    VkPipelineLayout GetLayout() const { return layout_; }
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return desc_set_layout_; }

private:
    VkShaderModule CreateShaderModule(VkDevice device,
                                      const void* spv_data, size_t spv_size);

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_set_layout_ = VK_NULL_HANDLE;
    VkShaderModule vertex_module_ = VK_NULL_HANDLE;
    VkShaderModule fragment_module_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    bool created_ = false;
};

}