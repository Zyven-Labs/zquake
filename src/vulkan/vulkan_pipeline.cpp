#include "vulkan/vulkan_pipeline.hpp"

namespace zq::vk {

VulkanPipeline::VulkanPipeline() {}
VulkanPipeline::~VulkanPipeline() { Destroy(); }

VkShaderModule VulkanPipeline::CreateShaderModule(VkDevice device, const void* spv_data, size_t spv_size) {
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv_size;
    info.pCode = static_cast<const uint32_t*>(spv_data);
    VkShaderModule module;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

bool VulkanPipeline::Create(VkDevice device, VkRenderPass render_pass,
                            const void* vertex_spv, size_t vertex_spv_size,
                            const void* fragment_spv, size_t fragment_spv_size) {
    device_ = device;

    vertex_module_ = CreateShaderModule(device, vertex_spv, vertex_spv_size);
    if (!vertex_module_) return false;

    fragment_module_ = CreateShaderModule(device, fragment_spv, fragment_spv_size);
    if (!fragment_module_) {
        vkDestroyShaderModule(device_, vertex_module_, nullptr);
        vertex_module_ = VK_NULL_HANDLE;
        return false;
    }

    // ----- Descriptor set layout -----
    // binding 0: UBO (proj/view/model)
    // binding 1: texture sampler
    // binding 2: lightmap sampler
    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo desc_layout_info = {};
    desc_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    desc_layout_info.bindingCount = 3;
    desc_layout_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &desc_layout_info, nullptr, &desc_set_layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertex_module_, nullptr);
        vkDestroyShaderModule(device_, fragment_module_, nullptr);
        vertex_module_ = VK_NULL_HANDLE;
        fragment_module_ = VK_NULL_HANDLE;
        return false;
    }

    // ----- Pipeline layout -----
    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &desc_set_layout_;
    // push constant range for optional model matrix
    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = 64;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
        vkDestroyShaderModule(device_, vertex_module_, nullptr);
        vkDestroyShaderModule(device_, fragment_module_, nullptr);
        desc_set_layout_ = VK_NULL_HANDLE;
        vertex_module_ = VK_NULL_HANDLE;
        fragment_module_ = VK_NULL_HANDLE;
        return false;
    }

    // ----- Shader stages -----
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_module_;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_module_;
    stages[1].pName = "main";

    // ----- Vertex input -----
    VkVertexInputBindingDescription binding_desc = {};
    binding_desc.binding = 0;
    binding_desc.stride = 44; // 11 floats
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4] = {};
    attrs[0].location = 0; // aPosition
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;

    attrs[1].location = 1; // aUV
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = 12;

    attrs[2].location = 2; // aNormal
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset = 20;

    attrs[3].location = 3; // aLightmapUV
    attrs[3].binding = 0;
    attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[3].offset = 32;

    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = 4;
    vertex_input.pVertexAttributeDescriptions = attrs;

    // ----- Input assembly -----
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // ----- Rasterizer -----
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // ----- Multisampling -----
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ----- Depth stencil -----
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    // ----- Color blending -----
    VkPipelineColorBlendAttachmentState color_attachment = {};
    color_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_attachment.blendEnable = VK_TRUE;
    color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blend = {};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.logicOpEnable = VK_FALSE;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &color_attachment;

    // ----- Viewport / scissor -----
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // ----- Dynamic state -----
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // ----- Final pipeline create -----
    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = layout_;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
        vkDestroyShaderModule(device_, vertex_module_, nullptr);
        vkDestroyShaderModule(device_, fragment_module_, nullptr);
        layout_ = VK_NULL_HANDLE;
        desc_set_layout_ = VK_NULL_HANDLE;
        vertex_module_ = VK_NULL_HANDLE;
        fragment_module_ = VK_NULL_HANDLE;
        return false;
    }

    // Shader modules can be destroyed after pipeline creation
    vkDestroyShaderModule(device_, vertex_module_, nullptr);
    vkDestroyShaderModule(device_, fragment_module_, nullptr);
    vertex_module_ = VK_NULL_HANDLE;
    fragment_module_ = VK_NULL_HANDLE;

    created_ = true;
    return true;
}

void VulkanPipeline::Destroy() {
    if (pipeline_) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    if (desc_set_layout_) {
        vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
        desc_set_layout_ = VK_NULL_HANDLE;
    }
    if (vertex_module_) {
        vkDestroyShaderModule(device_, vertex_module_, nullptr);
        vertex_module_ = VK_NULL_HANDLE;
    }
    if (fragment_module_) {
        vkDestroyShaderModule(device_, fragment_module_, nullptr);
        fragment_module_ = VK_NULL_HANDLE;
    }
    created_ = false;
}

bool VulkanPipeline::IsCreated() const { return created_; }

} // namespace zq::vk