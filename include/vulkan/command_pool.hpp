#pragma once
#include <vulkan/vulkan.hpp>

namespace zq::vk {

class CommandPool {
public:
    CommandPool();
    ~CommandPool();
    bool Create(VkDevice device, uint32_t queue_family_index);
    void SetQueue(VkQueue queue) { queue_ = queue; }
    void Destroy();
    VkCommandPool GetPool() const;
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer cmd);
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

}
