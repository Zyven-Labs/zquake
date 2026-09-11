#include "render/raytracer.hpp"
#include "render/ray_scene.hpp"
#include "core/logging/logger.hpp"

#include <cstring>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <SDL.h>

namespace zq::render {

namespace {

constexpr int kLocalSize = 8;

std::vector<uint8_t> LoadSPVFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Camera UBO, std140 layout (192 bytes). Mirrors the CamUBO block in the
// compute shader. `padEnd` keeps muzzleFlash/muzzleColor aligned to vec4.
struct CamUBO {
    float invViewProj[16];
    float camPos[4];
    float lightDir[4];
    float lightColor[4];
    float ambient[4];
    float atlasSize[2];
    std::uint32_t triCount;
    std::uint32_t numLights;
    std::uint32_t etriCount;
    std::uint32_t numShadowLights;
    std::uint32_t gunTriCount;
    std::uint32_t padEnd;
    float muzzleFlash[4]; // xyz = viewmodel strobe pos, w = intensity (0 = off)
    float muzzleColor[4]; // rgb = tint, w = radius
};

// Point light as laid out for the std430 LightBuf (32 bytes, matches PLight).
struct LightGPU {
    float pos[4];    // xyz = origin, w = intensity
    float color[4];  // rgb = tint, w = radius (0 disables)
};



// 4x4 matrix inverse (column-major). Returns true on success.
bool Mat4Inverse(const float* m, float* out) {
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]  + m[8]*m[7]*m[13]  + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]  - m[8]*m[6]*m[13]  - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
    return true;
}

VkShaderModule CreateModule(VkDevice dev, const std::vector<uint8_t>& spv) {
    if (spv.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(spv.data());
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

} // namespace

RayTracer::RayTracer() = default;
RayTracer::~RayTracer() { Shutdown(); }

VkShaderModule RayTracer::CreateShaderModule(const char* path) const {
    return CreateModule(dev_, LoadSPVFile(path));
}

bool RayTracer::Initialize(VkPhysicalDevice pd, VkDevice dev, uint32_t qf,
                           VkQueue queue, VkFormat swapchainFormat) {
    pd_ = pd;
    dev_ = dev;
    qf_ = qf;
    queue_ = queue;
    swapchain_format_ = swapchainFormat;

    VkCommandPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qf_;
    if (vkCreateCommandPool(dev_, &pci, nullptr, &cmd_pool_) != VK_SUCCESS) return false;

    if (!CreatePipelines()) return false;
    if (!CreateDescriptors()) return false;

    initialized_ = true;
    return true;
}

void RayTracer::Shutdown() {
    if (dev_ == VK_NULL_HANDLE) return;
    if (cmd_pool_) vkDestroyCommandPool(dev_, cmd_pool_, nullptr);
    cmd_pool_ = VK_NULL_HANDLE;

    if (tri_buf_) vkDestroyBuffer(dev_, tri_buf_, nullptr);
    if (tri_mem_) vkFreeMemory(dev_, tri_mem_, nullptr);
    if (node_buf_) vkDestroyBuffer(dev_, node_buf_, nullptr);
    if (node_mem_) vkFreeMemory(dev_, node_mem_, nullptr);
    if (cam_ubo_) vkDestroyBuffer(dev_, cam_ubo_, nullptr);
    if (cam_mem_) vkFreeMemory(dev_, cam_mem_, nullptr);
    tri_buf_ = node_buf_ = cam_ubo_ = VK_NULL_HANDLE;
    tri_mem_ = node_mem_ = cam_mem_ = VK_NULL_HANDLE;

    if (atlas_view_) vkDestroyImageView(dev_, atlas_view_, nullptr);
    if (atlas_sampler_) vkDestroySampler(dev_, atlas_sampler_, nullptr);
    if (atlas_image_) vkDestroyImage(dev_, atlas_image_, nullptr);
    if (atlas_mem_) vkFreeMemory(dev_, atlas_mem_, nullptr);
    atlas_view_ = VK_NULL_HANDLE; atlas_sampler_ = VK_NULL_HANDLE;
    atlas_image_ = VK_NULL_HANDLE; atlas_mem_ = VK_NULL_HANDLE;

    if (storage_view_) vkDestroyImageView(dev_, storage_view_, nullptr);
    if (storage_sampler_) vkDestroySampler(dev_, storage_sampler_, nullptr);
    if (storage_image_) vkDestroyImage(dev_, storage_image_, nullptr);
    if (storage_mem_) vkFreeMemory(dev_, storage_mem_, nullptr);
    storage_view_ = VK_NULL_HANDLE; storage_sampler_ = VK_NULL_HANDLE;
    storage_image_ = VK_NULL_HANDLE; storage_mem_ = VK_NULL_HANDLE;

    for (auto& kv : blit_fbs_)
        if (kv.second) vkDestroyFramebuffer(dev_, kv.second, nullptr);
    blit_fbs_.clear();
    if (blit_pass_) vkDestroyRenderPass(dev_, blit_pass_, nullptr);
    blit_pass_ = VK_NULL_HANDLE;

    if (compute_pipeline_) vkDestroyPipeline(dev_, compute_pipeline_, nullptr);
    if (compute_layout_) vkDestroyPipelineLayout(dev_, compute_layout_, nullptr);
    if (blit_pipeline_) vkDestroyPipeline(dev_, blit_pipeline_, nullptr);
    if (blit_layout_) vkDestroyPipelineLayout(dev_, blit_layout_, nullptr);
    if (compute_set_layout_) vkDestroyDescriptorSetLayout(dev_, compute_set_layout_, nullptr);
    if (blit_set_layout_) vkDestroyDescriptorSetLayout(dev_, blit_set_layout_, nullptr);
    if (pool_) vkDestroyDescriptorPool(dev_, pool_, nullptr);
    compute_pipeline_ = blit_pipeline_ = VK_NULL_HANDLE;
    compute_layout_ = blit_layout_ = VK_NULL_HANDLE;
    compute_set_layout_ = blit_set_layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;

    initialized_ = false;
}

bool RayTracer::CreatePipelines() {
    // ---- Compute pipeline (ray trace) ----
    VkDescriptorSetLayoutBinding compute_bindings[11] = {};
    compute_bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[2] = { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[5] = { 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[6] = { 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[7] = { 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[8] = { 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[9] = { 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    compute_bindings[10] = { 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

    VkDescriptorSetLayoutCreateInfo compute_lci = {};
    compute_lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    compute_lci.bindingCount = 11;
    compute_lci.pBindings = compute_bindings;
    if (vkCreateDescriptorSetLayout(dev_, &compute_lci, nullptr, &compute_set_layout_) != VK_SUCCESS)
        return false;

    VkShaderModule compute_mod = CreateShaderModule("shaders/spv/raytrace_compute.spv");
    if (!compute_mod) { log::Error("raytracer: failed to load raytrace_compute.spv"); return false; }

    VkPipelineShaderStageCreateInfo compute_stage = {};
    compute_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute_stage.module = compute_mod;
    compute_stage.pName = "main";

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &compute_set_layout_;
    if (vkCreatePipelineLayout(dev_, &plci, nullptr, &compute_layout_) != VK_SUCCESS) return false;

    VkComputePipelineCreateInfo cpci = {};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = compute_stage;
    cpci.layout = compute_layout_;
    if (vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cpci, nullptr, &compute_pipeline_) != VK_SUCCESS)
        return false;
    vkDestroyShaderModule(dev_, compute_mod, nullptr);

    // ---- Blit pipeline (fullscreen triangle sampling the storage image) ----
    VkDescriptorSetLayoutBinding blit_binding = {};
    blit_binding.binding = 0;
    blit_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    blit_binding.descriptorCount = 1;
    blit_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo blit_lci = {};
    blit_lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    blit_lci.bindingCount = 1;
    blit_lci.pBindings = &blit_binding;
    if (vkCreateDescriptorSetLayout(dev_, &blit_lci, nullptr, &blit_set_layout_) != VK_SUCCESS)
        return false;

    VkShaderModule blit_vs = CreateShaderModule("shaders/spv/blit_vertex.spv");
    VkShaderModule blit_fs = CreateShaderModule("shaders/spv/blit_fragment.spv");
    if (!blit_vs || !blit_fs) { log::Error("raytracer: failed to load blit shaders"); return false; }

    VkPipelineShaderStageCreateInfo blit_stages[2] = {};
    blit_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    blit_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    blit_stages[0].module = blit_vs;
    blit_stages[0].pName = "main";
    blit_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    blit_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    blit_stages[1].module = blit_fs;
    blit_stages[1].pName = "main";

    VkPipelineLayoutCreateInfo blit_plci = {};
    blit_plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    blit_plci.setLayoutCount = 1;
    blit_plci.pSetLayouts = &blit_set_layout_;
    // Screen-space HUD state (health fraction + blit size) for the blit stage.
    VkPushConstantRange blit_pc = {};
    blit_pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    blit_pc.offset = 0;
    blit_pc.size = 12;
    blit_plci.pushConstantRangeCount = 1;
    blit_plci.pPushConstantRanges = &blit_pc;
    if (vkCreatePipelineLayout(dev_, &blit_plci, nullptr, &blit_layout_) != VK_SUCCESS) return false;

    // Blit render pass (color only, no depth).
    VkAttachmentDescription color_att = {};
    color_att.format = swapchain_format_;
    color_att.samples = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Leave the swapchain in COLOR_ATTACHMENT_OPTIMAL so a follow-up HUD
    // overlay pass (LOAD) can draw the crosshair on top before presentation.
    color_att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci = {};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color_att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    if (vkCreateRenderPass(dev_, &rpci, nullptr, &blit_pass_) != VK_SUCCESS) return false;

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynci = {};
    dynci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynci.dynamicStateCount = 2;
    dynci.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gpci = {};
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2;
    gpci.pStages = blit_stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.pViewportState = &vp;
    gpci.pDynamicState = &dynci;
    gpci.layout = blit_layout_;
    gpci.renderPass = blit_pass_;
    gpci.subpass = 0;
    if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gpci, nullptr, &blit_pipeline_) != VK_SUCCESS)
        return false;
    vkDestroyShaderModule(dev_, blit_vs, nullptr);
    vkDestroyShaderModule(dev_, blit_fs, nullptr);
    return true;
}

bool RayTracer::CreateDescriptors() {
    VkDescriptorPoolSize sizes[4] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sizes[0].descriptorCount = 8;
    sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; sizes[1].descriptorCount = 1;
    sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[2].descriptorCount = 2;
    sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; sizes[3].descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci = {};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 2;
    dpci.poolSizeCount = 4;
    dpci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(dev_, &dpci, nullptr, &pool_) != VK_SUCCESS) return false;

    VkDescriptorSetLayout layouts[2] = { compute_set_layout_, blit_set_layout_ };
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 2;
    ai.pSetLayouts = layouts;
    VkDescriptorSet sets[2];
    if (vkAllocateDescriptorSets(dev_, &ai, sets) != VK_SUCCESS) return false;
    compute_set_ = sets[0];
    blit_set_ = sets[1];
    return true;
}

bool RayTracer::BuildScene(const std::vector<RtTriangle>& triangles,
                           const std::vector<uint8_t>& atlasRgba,
                           uint32_t atlasWidth, uint32_t atlasHeight,
                           const std::vector<RtTileInfo>& tileInfos,
                           uint32_t outputWidth, uint32_t outputHeight) {
    if (dev_ == VK_NULL_HANDLE || !initialized_) return false;

    // Reorder triangles + build BVH (reorders `triangles` in place) so the
    // uploaded triangle buffer matches leaf indices.
    std::vector<RtTriangle> tris = triangles;
    std::vector<BvhNode> nodes = BuildBvh(tris);
    tri_count_ = (std::uint32_t)tris.size();
    node_count_ = (std::uint32_t)nodes.size();

    // Reuse the tri/node GPU buffers when possible: only (re)allocate when the
    // existing buffer is too small. Rebuilding the scene every frame would
    // otherwise pay a vkAllocateMemory every frame.
    auto ensureBuf = [&](VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& cap,
                         VkDeviceSize bytes) -> bool {
        if (buf && cap >= bytes) return true;
        if (buf) { vkDestroyBuffer(dev_, buf, nullptr); vkFreeMemory(dev_, mem, nullptr); }
        buf = VK_NULL_HANDLE; mem = VK_NULL_HANDLE;
        VkBufferCreateInfo bc = {};
        bc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bc.size = bytes; bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev_, &bc, nullptr, &buf) != VK_SUCCESS) return false;
        VkMemoryRequirements reqs; vkGetBufferMemoryRequirements(dev_, buf, &reqs);
        VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = reqs.size;
        VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(pd_, &props);
        uint32_t mt = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++)
            if ((reqs.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mt = i; break; }
        if (mt == UINT32_MAX) return false;
        ai.memoryTypeIndex = mt;
        if (vkAllocateMemory(dev_, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        vkBindBufferMemory(dev_, buf, mem, 0);
        cap = bytes;
        return true;
    };
    {
        VkDeviceSize triBytes = std::max<std::uint32_t>(tri_count_, 1) * sizeof(RtTriangle);
        VkDeviceSize nodeBytes = std::max<std::uint32_t>(node_count_, 1) * sizeof(BvhNode);
        if (!ensureBuf(tri_buf_, tri_mem_, tri_cap_, triBytes)) return false;
        if (!ensureBuf(node_buf_, node_mem_, node_cap_, nodeBytes)) return false;

        void* p; if (vkMapMemory(dev_, tri_mem_, 0, triBytes, 0, &p) == VK_SUCCESS) {
            if (tri_count_ > 0) std::memcpy(p, tris.data(), (VkDeviceSize)tri_count_ * sizeof(RtTriangle));
            else std::memset(p, 0, triBytes);
            vkUnmapMemory(dev_, tri_mem_);
        }
        void* np; if (vkMapMemory(dev_, node_mem_, 0, nodeBytes, 0, &np) == VK_SUCCESS) {
            if (node_count_ > 0) std::memcpy(np, nodes.data(), (VkDeviceSize)node_count_ * sizeof(BvhNode));
            else std::memset(np, 0, nodeBytes);
            vkUnmapMemory(dev_, node_mem_);
        }
    }

    // Camera UBO (allocated once, updated each Dispatch).
    if (!cam_ubo_) {
        VkBufferCreateInfo cc = {};
        cc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        cc.size = sizeof(CamUBO);
        cc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        cc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev_, &cc, nullptr, &cam_ubo_) != VK_SUCCESS) return false;
        VkMemoryRequirements reqs; vkGetBufferMemoryRequirements(dev_, cam_ubo_, &reqs);
        VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = reqs.size;
        VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(pd_, &props);
        uint32_t mt = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++)
            if ((reqs.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mt = i; break; }
        if (mt == UINT32_MAX) return false;
        ai.memoryTypeIndex = mt;
        if (vkAllocateMemory(dev_, &ai, nullptr, &cam_mem_) != VK_SUCCESS) return false;
        vkBindBufferMemory(dev_, cam_ubo_, cam_mem_, 0);
    }

    if (!atlas_image_) {
    // Texture atlas image.
    if (atlas_view_) vkDestroyImageView(dev_, atlas_view_, nullptr);
    if (atlas_sampler_) vkDestroySampler(dev_, atlas_sampler_, nullptr);
    if (atlas_image_) vkDestroyImage(dev_, atlas_image_, nullptr);
    if (atlas_mem_) vkFreeMemory(dev_, atlas_mem_, nullptr);
    atlas_view_ = VK_NULL_HANDLE; atlas_sampler_ = VK_NULL_HANDLE;
    atlas_image_ = VK_NULL_HANDLE; atlas_mem_ = VK_NULL_HANDLE;
    atlas_w_ = atlasWidth; atlas_h_ = atlasHeight;

    // Upload per-tile info (origin + native size) into a storage buffer.
    tile_count_ = (std::uint32_t)tileInfos.size();
    {
        // RtTileInfo is 4 floats = 16 bytes; matches the vec4 tileInfo[] layout.
        std::vector<float> fbuf;
        fbuf.reserve(tileInfos.size() * 4);
        for (auto& ti : tileInfos) {
            fbuf.push_back(ti.u); fbuf.push_back(ti.v);
            fbuf.push_back(ti.w); fbuf.push_back(ti.h);
        }
        if (fbuf.empty()) fbuf.resize(4, 0.0f);
        if (tile_buf_) { vkDestroyBuffer(dev_, tile_buf_, nullptr); vkFreeMemory(dev_, tile_mem_, nullptr); }
        tile_buf_ = VK_NULL_HANDLE; tile_mem_ = VK_NULL_HANDLE;
        VkBufferCreateInfo tb = {};
        tb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        tb.size = fbuf.size() * 4; tb.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; tb.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev_, &tb, nullptr, &tile_buf_);
        VkMemoryRequirements treq; vkGetBufferMemoryRequirements(dev_, tile_buf_, &treq);
        VkMemoryAllocateInfo tai = {}; tai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; tai.allocationSize = treq.size;
        VkPhysicalDeviceMemoryProperties tprops; vkGetPhysicalDeviceMemoryProperties(pd_, &tprops);
        uint32_t tmt = UINT32_MAX;
        for (uint32_t i = 0; i < tprops.memoryTypeCount; i++)
            if ((treq.memoryTypeBits & (1u << i)) &&
                (tprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (tprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { tmt = i; break; }
        tai.memoryTypeIndex = tmt;
        vkAllocateMemory(dev_, &tai, nullptr, &tile_mem_);
        vkBindBufferMemory(dev_, tile_buf_, tile_mem_, 0);
        void* tp; if (vkMapMemory(dev_, tile_mem_, 0, fbuf.size()*4, 0, &tp) == VK_SUCCESS) {
            std::memcpy(tp, fbuf.data(), fbuf.size()*4);
            vkUnmapMemory(dev_, tile_mem_);
        }
    }

    if (atlasWidth > 0 && atlasHeight > 0 && !atlasRgba.empty()) {
        VkImageCreateInfo ici = {};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = { atlasWidth, atlasHeight, 1 };
        ici.mipLevels = 1; ici.arrayLayers = 1;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(dev_, &ici, nullptr, &atlas_image_) != VK_SUCCESS) return false;
        VkMemoryRequirements reqs; vkGetImageMemoryRequirements(dev_, atlas_image_, &reqs);
        VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = reqs.size;
        VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(pd_, &props);
        uint32_t mt = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++)
            if ((reqs.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { mt = i; break; }
        if (mt == UINT32_MAX) return false;
        ai.memoryTypeIndex = mt;
        if (vkAllocateMemory(dev_, &ai, nullptr, &atlas_mem_) != VK_SUCCESS) return false;
        vkBindImageMemory(dev_, atlas_image_, atlas_mem_, 0);

        // Upload via staging buffer + transition to SHADER_READ_ONLY.
        VkDeviceSize bytes = (VkDeviceSize)atlasRgba.size();
        VkBuffer staging; VkDeviceMemory stagingMem;
        VkBufferCreateInfo sbc = {};
        sbc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbc.size = bytes; sbc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; sbc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev_, &sbc, nullptr, &staging);
        VkMemoryRequirements sreqs; vkGetBufferMemoryRequirements(dev_, staging, &sreqs);
        VkMemoryAllocateInfo sai = {}; sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        sai.allocationSize = sreqs.size;
        uint32_t smt = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++)
            if ((sreqs.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { smt = i; break; }
        sai.memoryTypeIndex = smt;
        vkAllocateMemory(dev_, &sai, nullptr, &stagingMem);
        vkBindBufferMemory(dev_, staging, stagingMem, 0);
        void* sp; vkMapMemory(dev_, stagingMem, 0, bytes, 0, &sp);
        std::memcpy(sp, atlasRgba.data(), bytes);
        vkUnmapMemory(dev_, stagingMem);

        VkCommandBuffer cb;
        VkCommandBufferAllocateInfo cbai = {};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = cmd_pool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
        vkAllocateCommandBuffers(dev_, &cbai, &cb);
        VkCommandBufferBeginInfo bbi = {};
        bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bbi);

        VkImageMemoryBarrier toDst = {};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = atlas_image_;
        toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy region = {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { atlasWidth, atlasHeight, 1 };
        vkCmdCopyBufferToImage(cb, staging, atlas_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);

        vkEndCommandBuffer(cb);
        VkSubmitInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue_);
        vkFreeCommandBuffers(dev_, cmd_pool_, 1, &cb);
        vkDestroyBuffer(dev_, staging, nullptr);
        vkFreeMemory(dev_, stagingMem, nullptr);

        VkImageViewCreateInfo iv = {};
        iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = atlas_image_;
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = VK_FORMAT_R8G8B8A8_UNORM;
        iv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(dev_, &iv, nullptr, &atlas_view_);

        VkSamplerCreateInfo sci = {};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        // Nearest so Quake's textures stay crisp/pixelated (bilinear blurs them).
        sci.magFilter = VK_FILTER_NEAREST;
        sci.minFilter = VK_FILTER_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(dev_, &sci, nullptr, &atlas_sampler_);
    }

    }

    // Output storage image (only (re)create if the size changed).
    if (!storage_image_ || storage_w_ != outputWidth || storage_h_ != outputHeight) {
        if (!CreateStorageImage(outputWidth, outputHeight)) return false;
    }

    // Update compute descriptors (tri, node, cam, atlas, storage).
    VkDescriptorBufferInfo tri_info = { tri_buf_, 0, tri_buf_ ? sizeof(RtTriangle)*tri_count_ : VK_WHOLE_SIZE };
    VkDescriptorBufferInfo node_info = { node_buf_, 0, node_buf_ ? sizeof(BvhNode)*node_count_ : VK_WHOLE_SIZE };
    VkDescriptorBufferInfo cam_info = { cam_ubo_, 0, sizeof(CamUBO) };
    VkDescriptorImageInfo atlas_info = { atlas_sampler_, atlas_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo stg_info = { VK_NULL_HANDLE, storage_view_, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorBufferInfo tile_info = { tile_buf_, 0, VK_WHOLE_SIZE };
    // Entity buffers: an empty (1-element) buffer so descriptors stay valid even
    // before UpdateEntities is called.
    if (!etri_buf_) {
        VkBufferCreateInfo eb = {};
        eb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        eb.size = sizeof(RtTriangle); eb.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; eb.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev_, &eb, nullptr, &etri_buf_);
        VkMemoryRequirements er; vkGetBufferMemoryRequirements(dev_, etri_buf_, &er);
        VkMemoryAllocateInfo eai = {}; eai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; eai.allocationSize = er.size;
        VkPhysicalDeviceMemoryProperties ep; vkGetPhysicalDeviceMemoryProperties(pd_, &ep);
        for (uint32_t i = 0; i < ep.memoryTypeCount; i++)
            if ((er.memoryTypeBits & (1u<<i)) && (ep.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (ep.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { eai.memoryTypeIndex=i; break; }
        vkAllocateMemory(dev_, &eai, nullptr, &etri_mem_);
        vkBindBufferMemory(dev_, etri_buf_, etri_mem_, 0);
        void* e0; if (vkMapMemory(dev_, etri_mem_,0,sizeof(RtTriangle),0,&e0)==VK_SUCCESS){ memset(e0,0,sizeof(RtTriangle)); vkUnmapMemory(dev_,etri_mem_); }
        // enode
        eb.size = sizeof(BvhNode);
        vkCreateBuffer(dev_, &eb, nullptr, &enode_buf_);
        VkMemoryRequirements nr; vkGetBufferMemoryRequirements(dev_, enode_buf_, &nr);
        VkMemoryAllocateInfo nai = {}; nai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; nai.allocationSize = nr.size;
        for (uint32_t i = 0; i < ep.memoryTypeCount; i++)
            if ((nr.memoryTypeBits & (1u<<i)) && (ep.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (ep.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { nai.memoryTypeIndex=i; break; }
        vkAllocateMemory(dev_, &nai, nullptr, &enode_mem_);
        vkBindBufferMemory(dev_, enode_buf_, enode_mem_, 0);
        void* n0; if (vkMapMemory(dev_, enode_mem_,0,sizeof(BvhNode),0,&n0)==VK_SUCCESS){ memset(n0,0,sizeof(BvhNode)); vkUnmapMemory(dev_,enode_mem_); }
        etri_count_ = enode_count_ = 0;
        etri_cap_ = sizeof(RtTriangle); enode_cap_ = sizeof(BvhNode);
    }
    VkDescriptorBufferInfo etri_info = { etri_buf_, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo enode_info = { enode_buf_, 0, VK_WHOLE_SIZE };
    // Viewmodel (gun): an empty (1-element) buffer so descriptors stay valid
    // even before UpdateGun is called.
    if (!gtri_buf_) {
        VkBufferCreateInfo gb = {};
        gb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        gb.size = sizeof(RtTriangle); gb.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; gb.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev_, &gb, nullptr, &gtri_buf_);
        VkMemoryRequirements gr; vkGetBufferMemoryRequirements(dev_, gtri_buf_, &gr);
        VkMemoryAllocateInfo gai = {}; gai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; gai.allocationSize = gr.size;
        VkPhysicalDeviceMemoryProperties gp; vkGetPhysicalDeviceMemoryProperties(pd_, &gp);
        for (uint32_t i = 0; i < gp.memoryTypeCount; i++)
            if ((gr.memoryTypeBits & (1u<<i)) && (gp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (gp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { gai.memoryTypeIndex=i; break; }
        vkAllocateMemory(dev_, &gai, nullptr, &gtri_mem_);
        vkBindBufferMemory(dev_, gtri_buf_, gtri_mem_, 0);
        gb.size = sizeof(BvhNode);
        vkCreateBuffer(dev_, &gb, nullptr, &gnode_buf_);
        VkMemoryRequirements gnr; vkGetBufferMemoryRequirements(dev_, gnode_buf_, &gnr);
        VkMemoryAllocateInfo gnai = {}; gnai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; gnai.allocationSize = gnr.size;
        for (uint32_t i = 0; i < gp.memoryTypeCount; i++)
            if ((gnr.memoryTypeBits & (1u<<i)) && (gp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (gp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { gnai.memoryTypeIndex=i; break; }
        vkAllocateMemory(dev_, &gnai, nullptr, &gnode_mem_);
        vkBindBufferMemory(dev_, gnode_buf_, gnode_mem_, 0);
        gtri_count_ = gnode_count_ = 0;
        gtri_cap_ = sizeof(RtTriangle); gnode_cap_ = sizeof(BvhNode);
    }
    VkDescriptorBufferInfo gtri_info = { gtri_buf_, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo gnode_info = { gnode_buf_, 0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet writes[10] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = compute_set_; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].pBufferInfo = &tri_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = compute_set_; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &node_info;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = compute_set_; writes[2].dstBinding = 2; writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[2].pBufferInfo = &cam_info;
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = compute_set_; writes[3].dstBinding = 3; writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[3].pImageInfo = &atlas_info;
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = compute_set_; writes[4].dstBinding = 4; writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[4].pImageInfo = &stg_info;
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = compute_set_; writes[5].dstBinding = 6; writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[5].pBufferInfo = &tile_info;
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = compute_set_; writes[6].dstBinding = 7; writes[6].descriptorCount = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[6].pBufferInfo = &etri_info;
    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = compute_set_; writes[7].dstBinding = 8; writes[7].descriptorCount = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[7].pBufferInfo = &enode_info;
    writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[8].dstSet = compute_set_; writes[8].dstBinding = 9; writes[8].descriptorCount = 1;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[8].pBufferInfo = &gtri_info;
    writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[9].dstSet = compute_set_; writes[9].dstBinding = 10; writes[9].descriptorCount = 1;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[9].pBufferInfo = &gnode_info;
    vkUpdateDescriptorSets(dev_, 10, writes, 0, nullptr);
    // Binding 5 (lights) is owned by SetLights; if lights were already installed
    // we must re-point it at the (possibly recreated) buffer.
    if (light_buf_) {
        VkDescriptorBufferInfo light_info = { light_buf_, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet wl = {};
        wl.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wl.dstSet = compute_set_; wl.dstBinding = 5; wl.descriptorCount = 1;
        wl.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; wl.pBufferInfo = &light_info;
        vkUpdateDescriptorSets(dev_, 1, &wl, 0, nullptr);
    }

    // Blit descriptor: sample the storage image.
    VkDescriptorImageInfo blit_info = { storage_sampler_, storage_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet bw = {};
    bw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    bw.dstSet = blit_set_; bw.dstBinding = 0; bw.descriptorCount = 1;
    bw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; bw.pImageInfo = &blit_info;
    vkUpdateDescriptorSets(dev_, 1, &bw, 0, nullptr);

    return true;
}

bool RayTracer::CreateStorageImage(uint32_t w, uint32_t h) {
    if (storage_view_) vkDestroyImageView(dev_, storage_view_, nullptr);
    if (storage_sampler_) vkDestroySampler(dev_, storage_sampler_, nullptr);
    if (storage_image_) vkDestroyImage(dev_, storage_image_, nullptr);
    if (storage_mem_) vkFreeMemory(dev_, storage_mem_, nullptr);
    storage_view_ = VK_NULL_HANDLE; storage_sampler_ = VK_NULL_HANDLE;
    storage_image_ = VK_NULL_HANDLE; storage_mem_ = VK_NULL_HANDLE;
    storage_w_ = w; storage_h_ = h;
    if (w == 0 || h == 0) return true;

    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent = { w, h, 1 };
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(dev_, &ici, nullptr, &storage_image_) != VK_SUCCESS) return false;
    VkMemoryRequirements reqs; vkGetImageMemoryRequirements(dev_, storage_image_, &reqs);
    VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = reqs.size;
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(pd_, &props);
    uint32_t mt = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((reqs.memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { mt = i; break; }
    if (mt == UINT32_MAX) return false;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev_, &ai, nullptr, &storage_mem_) != VK_SUCCESS) return false;
    vkBindImageMemory(dev_, storage_image_, storage_mem_, 0);

    VkImageViewCreateInfo iv = {};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = storage_image_;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = VK_FORMAT_R8G8B8A8_UNORM;
    iv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(dev_, &iv, nullptr, &storage_view_) != VK_SUCCESS) return false;

    VkSamplerCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Nearest: the RT output is rendered at a lower internal resolution and
    // upscaled here; nearest keeps it crisp rather than smeared by bilinear.
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(dev_, &sci, nullptr, &storage_sampler_);

    // Transition to GENERAL for storage writes (persistently).
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo cbai = {};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev_, &cbai, &cb);
    VkCommandBufferBeginInfo bbi = {};
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bbi);
    VkImageMemoryBarrier b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = storage_image_;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(dev_, cmd_pool_, 1, &cb);
    return true;
}

void RayTracer::UpdateCameraUBO(const float* projection, const float* view) {
    if (!cam_ubo_) return;
    float projY[16];
    std::memcpy(projY, projection, sizeof(projY));
    projY[5] = -projY[5]; // match SetCamera's Y-flip
    float pv[16];
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += projY[k*4+row] * view[col*4+k];
            pv[col*4+row] = s;
        }
    float inv[16];
    if (!Mat4Inverse(pv, inv)) {
        std::memset(inv, 0, sizeof(inv)); inv[0] = inv[5] = inv[10] = inv[15] = 1.0f;
    }

    CamUBO ubo = {};
    std::memcpy(ubo.invViewProj, inv, sizeof(ubo.invViewProj));
    // Recover eye position from the view matrix (LookAt sets m[12..14]).
    float tx = view[12], ty = view[13], tz = view[14];
    ubo.camPos[0] = -(view[0]*tx + view[1]*ty + view[2]*tz);
    ubo.camPos[1] = -(view[4]*tx + view[5]*ty + view[6]*tz);
    ubo.camPos[2] = -(view[8]*tx + view[9]*ty + view[10]*tz);
    ubo.camPos[3] = pain_; // pain flash (replaces the std140 vec3 padding)

    float lx=0.5f, ly=0.5f, lz=1.0f, ll=std::sqrt(lx*lx+ly*ly+lz*lz);
    ubo.lightDir[0] = lx/ll; ubo.lightDir[1] = ly/ll; ubo.lightDir[2] = lz/ll;
    ubo.lightColor[0] = 1.0f; ubo.lightColor[1] = 1.0f; ubo.lightColor[2] = 1.0f;
    // Low ambient + a strong directional key light (which casts shadows) so
    // surfaces facing the light are clearly brighter than those in shadow.
    ubo.ambient[0] = 0.01f; ubo.ambient[1] = 0.01f; ubo.ambient[2] = 0.01f;
    ubo.atlasSize[0] = (float)atlas_w_; ubo.atlasSize[1] = (float)atlas_h_;
    ubo.triCount = tri_count_;
    ubo.numLights = light_count_;
    ubo.etriCount = etri_count_;
    ubo.numShadowLights = std::min(light_count_, (std::uint32_t)8);
    ubo.gunTriCount = gtri_count_;
    std::memcpy(ubo.muzzleFlash, muzzle_flash_, sizeof(ubo.muzzleFlash));
    std::memcpy(ubo.muzzleColor, muzzle_color_, sizeof(ubo.muzzleColor));

    void* p; if (vkMapMemory(dev_, cam_mem_, 0, sizeof(CamUBO), 0, &p) == VK_SUCCESS) {
        std::memcpy(p, &ubo, sizeof(CamUBO));
        vkUnmapMemory(dev_, cam_mem_);
    }
}

void RayTracer::SetLights(const std::vector<RtLight>& lights) {
    if (!initialized_ || !compute_set_) return;
    uint32_t n = (uint32_t)std::min<size_t>(lights.size(), 32);
    if (n == 0) { light_count_ = 0; return; }

    // Allocate a fixed-capacity buffer once; reuse by re-uploading contents.
    if (!light_buf_) {
        VkBufferCreateInfo lc = {};
        lc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        lc.size = 32 * sizeof(LightGPU);
        lc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; lc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev_, &lc, nullptr, &light_buf_) != VK_SUCCESS) { light_count_ = 0; return; }
        VkMemoryRequirements lreqs; vkGetBufferMemoryRequirements(dev_, light_buf_, &lreqs);
        VkMemoryAllocateInfo lai = {}; lai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; lai.allocationSize = lreqs.size;
        VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(pd_, &props);
        uint32_t mt = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++)
            if ((lreqs.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mt = i; break; }
        if (mt == UINT32_MAX) { light_count_ = 0; return; }
        lai.memoryTypeIndex = mt;
        if (vkAllocateMemory(dev_, &lai, nullptr, &light_mem_) != VK_SUCCESS) { light_count_ = 0; return; }
        vkBindBufferMemory(dev_, light_buf_, light_mem_, 0);
        VkDescriptorBufferInfo li = { light_buf_, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet w = {};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = compute_set_; w.dstBinding = 5; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &li;
        vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
    }

    std::vector<LightGPU> g(n);
    for (uint32_t i = 0; i < n; i++) {
        const auto& L = lights[i];
        g[i].pos[0] = L.pos[0]; g[i].pos[1] = L.pos[1]; g[i].pos[2] = L.pos[2];
        g[i].pos[3] = L.intensity;
        g[i].color[0] = L.color[0]; g[i].color[1] = L.color[1]; g[i].color[2] = L.color[2];
        g[i].color[3] = L.radius;
    }
    void* lp;
    if (vkMapMemory(dev_, light_mem_, 0, n * sizeof(LightGPU), 0, &lp) == VK_SUCCESS) {
        std::memcpy(lp, g.data(), n * sizeof(LightGPU));
        vkUnmapMemory(dev_, light_mem_);
    }
    light_count_ = n;
}

void RayTracer::SetMuzzleFlash(float intensity, const float pos[3],
                               const float color[3], float radius) {
    muzzle_flash_[0] = pos[0]; muzzle_flash_[1] = pos[1]; muzzle_flash_[2] = pos[2];
    muzzle_flash_[3] = intensity;
    muzzle_color_[0] = color[0]; muzzle_color_[1] = color[1]; muzzle_color_[2] = color[2];
    muzzle_color_[3] = radius;
}

void RayTracer::Dispatch(VkCommandBuffer cmd, const float* projection, const float* view,
                         VkImageView swapchainView, uint32_t blitWidth, uint32_t blitHeight) {
    if (!initialized_ || !compute_pipeline_) return;

    UpdateCameraUBO(projection, view);

    // ---- Record compute dispatch over the internal storage image ----
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout_, 0, 1, &compute_set_, 0, nullptr);
    vkCmdDispatch(cmd, (uint32_t)std::ceil(storage_w_ / (float)kLocalSize),
                  (uint32_t)std::ceil(storage_h_ / (float)kLocalSize), 1);

    // ---- storage image: COMPUTE_WRITE -> FRAGMENT_READ (same GENERAL layout) ----
    VkImageMemoryBarrier toRead = {};
    toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image = storage_image_;
    toRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toRead);

    // ---- Blit storage image -> swapchain ----
    auto it = blit_fbs_.find(swapchainView);
    VkFramebuffer fb;
    if (it != blit_fbs_.end()) {
        fb = it->second;
    } else {
        VkImageView att = swapchainView;
        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = blit_pass_;
        fci.attachmentCount = 1;
        fci.pAttachments = &att;
        fci.width = blitWidth; fci.height = blitHeight; fci.layers = 1;
        vkCreateFramebuffer(dev_, &fci, nullptr, &fb);
        blit_fbs_[swapchainView] = fb;
    }

    VkClearValue clear = {};
    clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = blit_pass_;
    rp.framebuffer = fb;
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = { blitWidth, blitHeight };
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = { 0, 0, (float)blitWidth, (float)blitHeight, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc = { {0,0}, {blitWidth, blitHeight} };
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blit_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blit_layout_, 0, 1, &blit_set_, 0, nullptr);
    float pc[3] = { health_, (float)blitWidth, (float)blitHeight };
    vkCmdPushConstants(cmd, blit_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // ---- storage image: FRAGMENT_READ -> COMPUTE_WRITE (next frame) ----
    VkImageMemoryBarrier toWrite = {};
    toWrite.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toWrite.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toWrite.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toWrite.image = storage_image_;
    toWrite.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toWrite.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toWrite);
}

void RayTracer::UpdateEntities(std::vector<RtTriangle> tris, const std::vector<BvhNode>& nodes) {
    if (!initialized_ || !compute_set_) return;
    uint32_t N = (std::uint32_t)tris.size();
    etri_count_ = N;
    enode_count_ = (std::uint32_t)nodes.size();
    if (N == 0) { enode_count_ = 0; return; }

    // Grow the entity buffers to fit.
    VkDeviceSize trisBytes = (VkDeviceSize)N * sizeof(RtTriangle);
    VkDeviceSize nodeBytes = (VkDeviceSize)nodes.size() * sizeof(BvhNode);
    auto growBuf = [&](VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& cap,
                       VkDeviceSize bytes) {
        if (buf && cap >= bytes) return;
        if (buf) { vkDestroyBuffer(dev_, buf, nullptr); vkFreeMemory(dev_, mem, nullptr); }
        buf = VK_NULL_HANDLE; mem = VK_NULL_HANDLE;
        VkBufferCreateInfo bc = {};
        bc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bc.size = bytes; bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev_, &bc, nullptr, &buf);
        VkMemoryRequirements r; vkGetBufferMemoryRequirements(dev_, buf, &r);
        VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = r.size;
        VkPhysicalDeviceMemoryProperties p; vkGetPhysicalDeviceMemoryProperties(pd_, &p);
        for (uint32_t i = 0; i < p.memoryTypeCount; i++)
            if ((r.memoryTypeBits & (1u<<i)) && (p.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (p.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { ai.memoryTypeIndex=i; break; }
        vkAllocateMemory(dev_, &ai, nullptr, &mem);
        vkBindBufferMemory(dev_, buf, mem, 0);
        cap = bytes;
    };
    growBuf(etri_buf_, etri_mem_, etri_cap_, trisBytes);
    growBuf(enode_buf_, enode_mem_, enode_cap_, nodeBytes);

    void* tp;
    if (vkMapMemory(dev_, etri_mem_, 0, trisBytes, 0, &tp) == VK_SUCCESS) {
        std::memcpy(tp, tris.data(), trisBytes);
        vkUnmapMemory(dev_, etri_mem_);
    }
    void* np;
    if (vkMapMemory(dev_, enode_mem_, 0, nodeBytes, 0, &np) == VK_SUCCESS) {
        std::memcpy(np, nodes.data(), nodeBytes);
        vkUnmapMemory(dev_, enode_mem_);
    }

    // Update the ray-trace entity descriptors (7,8) to the (possibly resized) buffers.
    VkDescriptorBufferInfo ei = { etri_buf_, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo ni = { enode_buf_, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w[2] = {};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = compute_set_; w[0].dstBinding = 7; w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[0].pBufferInfo = &ei;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = compute_set_; w[1].dstBinding = 8; w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &ni;
    vkUpdateDescriptorSets(dev_, 2, w, 0, nullptr);
}

void RayTracer::UpdateGun(std::vector<RtTriangle> tris, const std::vector<BvhNode>& nodes) {
    if (!initialized_ || !compute_set_) return;
    uint32_t N = (std::uint32_t)tris.size();
    gtri_count_ = N;
    gnode_count_ = (std::uint32_t)nodes.size();
    if (N == 0) { gnode_count_ = 0; return; }

    VkDeviceSize trisBytes = (VkDeviceSize)N * sizeof(RtTriangle);
    VkDeviceSize nodeBytes = (VkDeviceSize)nodes.size() * sizeof(BvhNode);
    auto growBuf = [&](VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& cap,
                       VkDeviceSize bytes) {
        if (buf && cap >= bytes) return;
        if (buf) { vkDestroyBuffer(dev_, buf, nullptr); vkFreeMemory(dev_, mem, nullptr); }
        buf = VK_NULL_HANDLE; mem = VK_NULL_HANDLE;
        VkBufferCreateInfo bc = {};
        bc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bc.size = bytes; bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev_, &bc, nullptr, &buf);
        VkMemoryRequirements r; vkGetBufferMemoryRequirements(dev_, buf, &r);
        VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = r.size;
        VkPhysicalDeviceMemoryProperties p; vkGetPhysicalDeviceMemoryProperties(pd_, &p);
        for (uint32_t i = 0; i < p.memoryTypeCount; i++)
            if ((r.memoryTypeBits & (1u<<i)) && (p.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (p.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { ai.memoryTypeIndex=i; break; }
        vkAllocateMemory(dev_, &ai, nullptr, &mem);
        vkBindBufferMemory(dev_, buf, mem, 0);
        cap = bytes;
    };
    growBuf(gtri_buf_, gtri_mem_, gtri_cap_, trisBytes);
    growBuf(gnode_buf_, gnode_mem_, gnode_cap_, nodeBytes);

    void* tp;
    if (vkMapMemory(dev_, gtri_mem_, 0, trisBytes, 0, &tp) == VK_SUCCESS) {
        std::memcpy(tp, tris.data(), trisBytes);
        vkUnmapMemory(dev_, gtri_mem_);
    }
    void* np;
    if (vkMapMemory(dev_, gnode_mem_, 0, nodeBytes, 0, &np) == VK_SUCCESS) {
        std::memcpy(np, nodes.data(), nodeBytes);
        vkUnmapMemory(dev_, gnode_mem_);
    }

    VkDescriptorBufferInfo ei = { gtri_buf_, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo ni = { gnode_buf_, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w[2] = {};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = compute_set_; w[0].dstBinding = 9; w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[0].pBufferInfo = &ei;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = compute_set_; w[1].dstBinding = 10; w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &ni;
    vkUpdateDescriptorSets(dev_, 2, w, 0, nullptr);
}


bool RayTracer::ReadbackEntity(std::vector<RtTriangle>& trisOut, std::vector<BvhNode>& nodesOut) const {
    if (!etri_buf_ || !enode_buf_ || etri_count_ == 0) return false;
    trisOut.resize(etri_count_);
    nodesOut.resize(enode_count_);
    void* tp; if (vkMapMemory(dev_, etri_mem_, 0, (VkDeviceSize)etri_count_*sizeof(RtTriangle), 0, &tp) != VK_SUCCESS) return false;
    std::memcpy(trisOut.data(), tp, (VkDeviceSize)etri_count_*sizeof(RtTriangle));
    vkUnmapMemory(dev_, etri_mem_);
    void* np; if (vkMapMemory(dev_, enode_mem_, 0, (VkDeviceSize)enode_count_*sizeof(BvhNode), 0, &np) != VK_SUCCESS) return false;
    std::memcpy(nodesOut.data(), np, (VkDeviceSize)enode_count_*sizeof(BvhNode));
    vkUnmapMemory(dev_, enode_mem_);
    return true;
}

bool RayTracer::TraceToCPU(const float* projection, const float* view,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& rgbaOut) {
    if (!initialized_) return false;

    UpdateCameraUBO(projection, view);

    // Allocate a temporary storage image + staging buffer.
    VkImage tmp; VkDeviceMemory tmpMem; VkImageView tmpView;
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent = { width, height, 1 };
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(dev_, &ici, nullptr, &tmp) != VK_SUCCESS) return false;
    VkMemoryRequirements reqs; vkGetImageMemoryRequirements(dev_, tmp, &reqs);
    VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = reqs.size;
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(pd_, &props);
    uint32_t mt = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((reqs.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { mt = i; break; }
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev_, &ai, nullptr, &tmpMem) != VK_SUCCESS) return false;
    vkBindImageMemory(dev_, tmp, tmpMem, 0);
    VkImageViewCreateInfo iv = {};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = tmp; iv.viewType = VK_IMAGE_VIEW_TYPE_2D; iv.format = VK_FORMAT_R8G8B8A8_UNORM;
    iv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(dev_, &iv, nullptr, &tmpView);

    VkDeviceSize bytes = (VkDeviceSize)width * height * 4;
    VkBuffer staging; VkDeviceMemory stagingMem;
    VkBufferCreateInfo sbc = {};
    sbc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbc.size = bytes; sbc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; sbc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(dev_, &sbc, nullptr, &staging);
    VkMemoryRequirements sreqs; vkGetBufferMemoryRequirements(dev_, staging, &sreqs);
    VkMemoryAllocateInfo sai = {}; sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; sai.allocationSize = sreqs.size;
    uint32_t smt = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((sreqs.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { smt = i; break; }
    sai.memoryTypeIndex = smt;
    vkAllocateMemory(dev_, &sai, nullptr, &stagingMem);
    vkBindBufferMemory(dev_, staging, stagingMem, 0);

    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo cbai = {};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev_, &cbai, &cb);
    VkCommandBufferBeginInfo bbi = {};
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bbi);

    // Transition tmp to GENERAL, then dispatch with tmp as the storage image.
    VkImageMemoryBarrier toGen = {};
    toGen.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGen.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toGen.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGen.image = tmp;
    toGen.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toGen.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGen);

    // Temporarily point compute binding 4 at tmpView.
    VkDescriptorImageInfo stg = { VK_NULL_HANDLE, tmpView, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet w = {};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = compute_set_; w.dstBinding = 4; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo = &stg;
    vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout_, 0, 1, &compute_set_, 0, nullptr);
    vkCmdDispatch(cb, (uint32_t)std::ceil(width / (float)kLocalSize),
                  (uint32_t)std::ceil(height / (float)kLocalSize), 1);

    // tmp: GENERAL -> TRANSFER_SRC.
    VkImageMemoryBarrier toSrc = toGen;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyImageToBuffer(cb, tmp, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

    vkEndCommandBuffer(cb);

    VkFence fence;
    VkFenceCreateInfo fci = {}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(dev_, &fci, nullptr, &fence);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    VkResult sr = vkQueueSubmit(queue_, 1, &si, fence);
    VkResult wr = sr == VK_SUCCESS ? vkWaitForFences(dev_, 1, &fence, VK_TRUE, UINT64_MAX) : VK_SUCCESS;
    bool ok = (sr == VK_SUCCESS && wr == VK_SUCCESS);
    if (!ok) {
        std::fprintf(stderr, "[raytracer] TraceToCPU submit=%d wait=%d\n", (int)sr, (int)wr);
    }

    rgbaOut.resize(bytes);
    if (ok) {
        void* p; vkMapMemory(dev_, stagingMem, 0, bytes, 0, &p);
        std::memcpy(rgbaOut.data(), p, bytes);
        vkUnmapMemory(dev_, stagingMem);
    }

    // Restore compute binding 4 to the real storage image.
    if (storage_view_) {
        VkDescriptorImageInfo back = { VK_NULL_HANDLE, storage_view_, VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet w2 = {};
        w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = compute_set_; w2.dstBinding = 4; w2.descriptorCount = 1;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w2.pImageInfo = &back;
        vkUpdateDescriptorSets(dev_, 1, &w2, 0, nullptr);
    }

    vkDestroyFence(dev_, fence, nullptr);
    vkFreeCommandBuffers(dev_, cmd_pool_, 1, &cb);
    vkDestroyImageView(dev_, tmpView, nullptr);
    vkDestroyImage(dev_, tmp, nullptr);
    vkFreeMemory(dev_, tmpMem, nullptr);
    vkDestroyBuffer(dev_, staging, nullptr);
    vkFreeMemory(dev_, stagingMem, nullptr);
    return ok;
}

bool RayTracer::CaptureStorage(std::vector<uint8_t>& rgbaOut) {
    if (!initialized_ || !storage_image_) return false;
    uint32_t w = storage_w_, h = storage_h_;
    if (w == 0 || h == 0) return false;
    VkDeviceSize bytes = (VkDeviceSize)w * h * 4;

    VkBuffer staging; VkDeviceMemory stagingMem;
    VkBufferCreateInfo sbc = {};
    sbc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbc.size = bytes; sbc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; sbc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(dev_, &sbc, nullptr, &staging);
    VkMemoryRequirements sreqs; vkGetBufferMemoryRequirements(dev_, staging, &sreqs);
    VkMemoryAllocateInfo sai = {}; sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; sai.allocationSize = sreqs.size;
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(pd_, &props);
    uint32_t smt = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((sreqs.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { smt = i; break; }
    sai.memoryTypeIndex = smt;
    vkAllocateMemory(dev_, &sai, nullptr, &stagingMem);
    vkBindBufferMemory(dev_, staging, stagingMem, 0);

    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo cbai = {};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev_, &cbai, &cb);
    VkCommandBufferBeginInfo bbi = {};
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bbi);
    VkImageMemoryBarrier toSrc = {};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = storage_image_;
    toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toSrc.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { w, h, 1 };
    vkCmdCopyImageToBuffer(cb, storage_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);
    VkImageMemoryBarrier back = toSrc;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &back);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    bool ok = vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS &&
              vkQueueWaitIdle(queue_) == VK_SUCCESS;
    rgbaOut.resize(bytes);
    if (ok) {
        void* p; vkMapMemory(dev_, stagingMem, 0, bytes, 0, &p);
        std::memcpy(rgbaOut.data(), p, bytes);
        vkUnmapMemory(dev_, stagingMem);
    }
    vkFreeCommandBuffers(dev_, cmd_pool_, 1, &cb);
    vkDestroyBuffer(dev_, staging, nullptr);
    vkFreeMemory(dev_, stagingMem, nullptr);
    return ok;
}

} // namespace zq::render