#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string_view>
#include <vector>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct SDL_Window SDL_Window;
#ifdef __cplusplus
}
#endif

namespace zq::vk {

class VulkanPipeline;
class VulkanBuffer;
class VulkanImage;
class CommandPool;

// Core renderer: owns instance, device, swapchain, render pass, framebuffers,
// command buffers, sync objects. Provides a simple DrawMesh() + Present() loop.
class VulkanAPI {
public:
    VulkanAPI();
    ~VulkanAPI();

    // SDL window is required for surface creation.
    bool Initialize(SDL_Window* window);
    void Shutdown();
    bool IsInitialized() const;

    // Returns true if the swapchain was recreated (window resize)
    // If startRenderPass is true (default, raster path) the render pass is
    // begun immediately. Pass false for compute-driven paths (e.g. ray
    // tracing) that record a compute dispatch + their own blit before
    // calling BeginRenderPass() manually.
    bool BeginFrame(bool startRenderPass = true);
    // Begins the raster render pass + viewport/scissor + binds the main
    // graphics pipeline. Used after BeginFrame(false).
    void BeginRenderPass();
    void EndFrame();
    // Blocks until all GPU work on the graphics queue has finished. Used by the
    // ray-traced path, which shares a single storage image across frames.
    void WaitIdle() { if (queue_) vkQueueWaitIdle(queue_); }

    // Ray-tracing integration helpers.
    VkCommandBuffer GetActiveCommandBuffer() const { return command_buffers_[current_frame_]; }
    VkImageView GetSwapchainImageView(uint32_t index) const { return swapchain_image_views_[index]; }
    VkImage GetSwapchainImage(uint32_t index) const { return index < swapchain_images_.size() ? swapchain_images_[index] : VK_NULL_HANDLE; }
    uint32_t GetCurrentImageIndex() const { return image_index_; }
    VkFormat GetSwapchainFormat() const { return swapchain_format_; }
    uint32_t GetSwapchainImageCount() const { return (uint32_t)swapchain_image_views_.size(); }

    // Uploads mesh (position/uv/normal/lightmapuv interleaved) + index buffer.
    struct Mesh {
        VulkanBuffer* vertex_buffer = nullptr;
        VulkanBuffer* index_buffer = nullptr;
        uint32_t index_count = 0;
        VulkanImage* texture = nullptr;
        VulkanImage* lightmap = nullptr;
        float model[16]; // model matrix
        bool has_model = false;
    };

    // Queues a mesh draw for the current frame
    void DrawMesh(const Mesh& mesh);

    // Screen-space HUD overlay: draws a crosshair at the center of the screen.
    // Safe to call once per frame after world/entity draws. Works on both the
    // raster path (inside the active render pass) and the ray path (records a
    // render pass of its own that pulls in the just-blitted image).
    void DrawCrosshair();

    // Textures
    VulkanImage* CreateTexture(uint32_t width, uint32_t height, const void* rgba8_pixels);
    VulkanImage* CreateLightmap(const float rgba[4]);
    void DestroyTexture(VulkanImage* image);

    // Buffers
    VulkanBuffer* CreateVertexBuffer(size_t size);
    VulkanBuffer* CreateIndexBuffer(size_t size);
    void UpdateBuffer(VulkanBuffer* buffer, const void* data, size_t size);
    void DestroyBuffer(VulkanBuffer* buffer);

    // Accessors
    VkInstance GetInstance() const { return instance_; }
    VkPhysicalDevice GetPhysicalDevice() const { return physical_device_; }
    VkDevice GetDevice() const { return device_; }
    VkSurfaceKHR GetSurface() const { return surface_; }
    VkQueue GetGraphicsQueue() const { return queue_; }
    uint32_t GetGraphicsQueueFamily() const { return queue_family_; }
    VkRenderPass GetRenderPass() const { return render_pass_; }
    uint32_t GetSwapchainWidth() const { return swapchain_extent_.width; }
    uint32_t GetSwapchainHeight() const { return swapchain_extent_.height; }

    // Set camera transform. The shader does projection * view * model, so
    // pass projection and view as separate column-major matrices.
    void SetCamera(const float* projection, const float* view);

    // Offscreen render for automated verification / screenshots.
    // Renders the given mesh to a CPU-accessible RGBA8 buffer of size
    // width*height*4. Currently requires width/height == swapchain size.
    bool RenderMeshToImage(uint32_t width, uint32_t height, const Mesh& mesh,
                           std::vector<uint8_t>& out);

    // Same as RenderMeshToImage but renders all meshes into a single image.
    bool RenderMeshesToImage(uint32_t width, uint32_t height,
                             const std::vector<Mesh>& meshes,
                             std::vector<uint8_t>& out);

private:
    bool CreateInstance();
    bool CreateSurface(SDL_Window* window);
    bool CreateDevice();
    bool CreateSwapchain();
    bool CreateRenderPass();
    bool CreateDepthBuffer();
    bool CreateFramebuffers();
    bool CreateSyncObjects();
    bool CreateCommandPool();
    bool CreateDescriptorPool();
    bool CreatePipeline();
    bool CreateOverlay();
    void RecreateSwapchain();
    void SetupDebugMessenger();

    PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugMessenger_ = nullptr;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

    uint32_t FindGraphicsQueueFamily() const;
    uint32_t FindPresentQueueFamily() const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;
    VkFormat swapchain_format_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkColorSpaceKHR swapchain_colorspace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;
    std::vector<VkSemaphore> image_available_;
    std::vector<VkSemaphore> render_finished_;
    std::vector<VkFence> in_flight_fences_;

    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VulkanPipeline* pipeline_ = nullptr;

    // Screen-space HUD overlay (crosshair): its own LOAD render pass + a small
    // full-screen-triangle pipeline + vertex buffer for the three corners.
    VkRenderPass overlay_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout overlay_layout_ = VK_NULL_HANDLE;
    VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;
    VulkanBuffer* overlay_vbuf_ = nullptr;

    // Per-frame-in-flight UBO buffer + descriptor set. Each frame in flight
    // gets its own UBO so a frame still executing on the GPU never reads a
    // half-written camera matrix from a later frame.
    std::vector<VulkanBuffer*> ubo_buffers_;
    std::vector<VkDescriptorSet> descriptor_sets_;

    float projection_[16] = {};
    float view_[16] = {};

    uint32_t current_frame_ = 0;
    uint32_t image_index_ = 0;
    bool swapchain_valid_ = false;
    bool initialized_ = false;
    bool in_frame_ = false;
    bool render_pass_active_ = false;

    // Track created resources for cleanup
    std::vector<VulkanImage*> textures_;
    std::vector<VulkanBuffer*> buffers_;
};

} // namespace zq::vk