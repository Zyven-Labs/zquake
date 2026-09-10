#pragma once
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace zq::render {

class RtTriangle;
struct RtLight;
struct RtTileInfo;
struct BvhNode;

// GPU compute ray tracer. Builds a triangle + BVH scene into storage buffers,
// dispatches a compute kernel that casts one primary ray per pixel into an
// offscreen storage image, then blits that image into a swapchain image.
//
// The scene math (BVH build + traversal, ray-triangle) is implemented on the
// CPU in ray_scene.cpp with matching layouts; the compute shader mirrors it.
class RayTracer {
public:
    RayTracer();
    ~RayTracer();
    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    bool Initialize(VkPhysicalDevice pd, VkDevice dev, uint32_t queueFamily,
                    VkQueue queue, VkFormat swapchainFormat);
    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    // Rebuilds the scene: uploads triangles + BVH nodes, the texture atlas
    // image and per-tile info, and (re)creates the output storage image at the
    // given size.
    bool BuildScene(const std::vector<RtTriangle>& triangles,
                    const std::vector<uint8_t>& atlasRgba,
                    uint32_t atlasWidth, uint32_t atlasHeight,
                    const std::vector<RtTileInfo>& tileInfos,
                    uint32_t outputWidth, uint32_t outputHeight);

    // Installs the scene point lights (typically from `light` entities). The
    // compute shader evaluates diffuse lighting from these with shadow rays.
    void SetLights(const std::vector<RtLight>& lights);
    std::uint32_t LightCount() const { return light_count_; }
    std::uint32_t EntityCount() const { return etri_count_; }

    // Rebuilds ONLY the dynamic MDL entity + submodel BVH + triangle buffer (the
    // static world BVH is not touched). The BVH is built on the CPU (BuildBvh)
    // on the game thread; this just uploads the reordered triangles + nodes and
    // updates the entity descriptor bindings (7,8).
    void UpdateEntities(std::vector<RtTriangle> tris, const std::vector<BvhNode>& nodes);

    // Uploads the small first-person viewmodel triangle set + BVH (bindings
    // 9,10) used by the on-top weapon overlay. When `tris` is empty the gun is
    // hidden. The BVH must already match the reordered triangle list (i.e.
    // BuildBvh must have run on `tris` and returned `nodes`).
    void UpdateGun(std::vector<RtTriangle> tris, const std::vector<BvhNode>& nodes);

    // Copies the GPU-built entity triangle buffer (Morton-reordered) and BVH
    // nodes back to the CPU (used by the CPU-vs-GPU parity test).
    bool ReadbackEntity(std::vector<RtTriangle>& trisOut, std::vector<BvhNode>& nodesOut) const;

    // Records a compute dispatch + fullscreen blit into `cmd`, drawing to the
    // given swapchain image view. Call between VulkanAPI::BeginFrame(false)
    // and VulkanAPI::EndFrame(). projection/view are OpenGL-style column-major
    // matrices (the same ones fed to VulkanAPI::SetCamera).
    void Dispatch(VkCommandBuffer cmd, const float* projection, const float* view,
                  VkImageView swapchainView, uint32_t width, uint32_t height);

    // Offscreen render to a CPU-accessible RGBA8 buffer (for tests). Runs its
    // own one-shot command buffer and waits for completion.
    bool TraceToCPU(const float* projection, const float* view,
                    uint32_t width, uint32_t height,
                    std::vector<uint8_t>& rgbaOut);

    // Copies the current storage image (what the blit samples) to CPU. Used to
    // verify the actual presented frame.
    bool CaptureStorage(std::vector<uint8_t>& rgbaOut);

    // Selects a 0..1 full-screen red tint applied in the compute shader for
    // damage feedback. Read by UpdateCameraUBO on the render thread.
    void SetPainFlash(float p) { pain_ = p; }

    // Selects the 0..1 health fraction drawn as the screen-space HUD bar in
    // the blit pass (fragment push constant).
    void SetHealthFraction(float f) { health_ = f < 0.0f ? -1.0f : (f > 1.0f ? 1.0f : f); }

private:
    void UpdateCameraUBO(const float* projection, const float* view);
    bool CreatePipelines();
    bool CreateDescriptors();
    bool CreateStorageImage(uint32_t w, uint32_t h);
    void TransitionToGeneral(VkCommandBuffer cmd, VkImage image, VkImageLayout from);
    VkShaderModule CreateShaderModule(const char* path) const;

    VkPhysicalDevice pd_ = VK_NULL_HANDLE;
    VkDevice dev_ = VK_NULL_HANDLE;
    uint32_t qf_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_R8G8B8A8_UNORM;
    bool initialized_ = false;

    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;

    // Scene buffers (rebuilt each BuildScene).
    VkBuffer tri_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory tri_mem_ = VK_NULL_HANDLE;
    std::uint32_t tri_count_ = 0;
    VkDeviceSize tri_cap_ = 0;
    VkBuffer node_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory node_mem_ = VK_NULL_HANDLE;
    std::uint32_t node_count_ = 0;
    VkDeviceSize node_cap_ = 0;

    VkBuffer light_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory light_mem_ = VK_NULL_HANDLE;
    std::uint32_t light_count_ = 0;
    float pain_ = 0.0f;
    float health_ = 1.0f;

    VkBuffer tile_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory tile_mem_ = VK_NULL_HANDLE;
    std::uint32_t tile_count_ = 0;

    // Dynamic MDL entity BVH + triangle buffer (separate from the static world).
    VkBuffer etri_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory etri_mem_ = VK_NULL_HANDLE;
    std::uint32_t etri_count_ = 0;
    VkDeviceSize etri_cap_ = 0;
    VkBuffer enode_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory enode_mem_ = VK_NULL_HANDLE;
    std::uint32_t enode_count_ = 0;
    VkDeviceSize enode_cap_ = 0;

    // First-person viewmodel (gun) triangle set + BVH: tiny dedicated buffers
    // for the on-top weapon overlay.
    VkBuffer gtri_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory gtri_mem_ = VK_NULL_HANDLE;
    std::uint32_t gtri_count_ = 0;
    VkDeviceSize gtri_cap_ = 0;
    VkBuffer gnode_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory gnode_mem_ = VK_NULL_HANDLE;
    std::uint32_t gnode_count_ = 0;
    VkDeviceSize gnode_cap_ = 0;

    VkBuffer cam_ubo_ = VK_NULL_HANDLE;
    VkDeviceMemory cam_mem_ = VK_NULL_HANDLE;

    VkImage atlas_image_ = VK_NULL_HANDLE;
    VkDeviceMemory atlas_mem_ = VK_NULL_HANDLE;
    VkImageView atlas_view_ = VK_NULL_HANDLE;
    VkSampler atlas_sampler_ = VK_NULL_HANDLE;
    std::uint32_t atlas_w_ = 0, atlas_h_ = 0;

    VkImage storage_image_ = VK_NULL_HANDLE;
    VkDeviceMemory storage_mem_ = VK_NULL_HANDLE;
    VkImageView storage_view_ = VK_NULL_HANDLE;
    VkSampler storage_sampler_ = VK_NULL_HANDLE;
    std::uint32_t storage_w_ = 0, storage_h_ = 0;

    VkRenderPass blit_pass_ = VK_NULL_HANDLE;
    std::unordered_map<VkImageView, VkFramebuffer> blit_fbs_;

    VkDescriptorSetLayout compute_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout blit_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkDescriptorSet blit_set_ = VK_NULL_HANDLE;
    VkPipelineLayout compute_layout_ = VK_NULL_HANDLE;
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout blit_layout_ = VK_NULL_HANDLE;
    VkPipeline blit_pipeline_ = VK_NULL_HANDLE;
};

} // namespace zq::render