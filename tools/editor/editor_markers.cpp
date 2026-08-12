#include "editor_markers.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "core/logger.hpp"
#include <cstring>
#include <cmath>

namespace wowee {
namespace editor {

EditorMarkers::EditorMarkers() = default;
EditorMarkers::~EditorMarkers() { shutdown(); }

bool EditorMarkers::initialize(rendering::VkContext* ctx, VkRenderPass renderPass,
                                VkDescriptorSetLayout perFrameLayout) {
    vkCtx_ = ctx;
    renderPass_ = renderPass;
    perFrameLayout_ = perFrameLayout;
    return createPipeline();
}

void EditorMarkers::shutdown() {
    if (!vkCtx_) return;
    VkDevice dev = vkCtx_->getDevice();
    clear();
    if (pipeline_) { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    vkCtx_ = nullptr;
}

void EditorMarkers::clear() {
    if (vertexBuffer_) {
        vmaDestroyBuffer(vkCtx_->getAllocator(), vertexBuffer_, vertexAlloc_);
        vertexBuffer_ = VK_NULL_HANDLE;
        vertexCount_ = 0;
    }
}

void EditorMarkers::update(const std::vector<PlacedObject>& objects) {
    clear();
    if (objects.empty()) return;

    std::vector<MarkerVertex> verts;

    for (const auto& obj : objects) {
        // Skip markers for objects with non-finite transform - would
        // produce NaN vertex positions and trip Vulkan validation or
        // collapse the entire vertex buffer to garbage.
        if (!std::isfinite(obj.position.x) || !std::isfinite(obj.position.y) ||
            !std::isfinite(obj.position.z) || !std::isfinite(obj.scale)) continue;
        float s = 3.0f * obj.scale;
        float x = obj.position.x;
        float y = obj.position.y;
        float z = obj.position.z;

        // Color: M2 = green, WMO = orange, selected = yellow
        float r, g, b, a = 0.9f;
        if (obj.selected) { r = 1.0f; g = 1.0f; b = 0.2f; }
        else if (obj.type == PlaceableType::M2) { r = 0.2f; g = 0.8f; b = 0.3f; }
        else { r = 0.9f; g = 0.5f; b = 0.1f; }

        // Diamond / octahedron marker
        MarkerVertex top, bot, n, s2, e, w;
        top.pos[0] = x; top.pos[1] = y; top.pos[2] = z + s * 2;
        bot.pos[0] = x; bot.pos[1] = y; bot.pos[2] = z;
        n.pos[0] = x; n.pos[1] = y + s; n.pos[2] = z + s;
        s2.pos[0] = x; s2.pos[1] = y - s; s2.pos[2] = z + s;
        e.pos[0] = x + s; e.pos[1] = y; e.pos[2] = z + s;
        w.pos[0] = x - s; w.pos[1] = y; w.pos[2] = z + s;

        auto setCol = [&](MarkerVertex& v, float br) {
            v.color[0] = r * br; v.color[1] = g * br; v.color[2] = b * br; v.color[3] = a;
        };
        setCol(top, 1.0f); setCol(bot, 0.6f);
        setCol(n, 0.9f); setCol(s2, 0.8f); setCol(e, 0.85f); setCol(w, 0.75f);

        // Top 4 triangles
        verts.push_back(top); verts.push_back(n); verts.push_back(e);
        verts.push_back(top); verts.push_back(e); verts.push_back(s2);
        verts.push_back(top); verts.push_back(s2); verts.push_back(w);
        verts.push_back(top); verts.push_back(w); verts.push_back(n);
        // Bottom 4 triangles
        verts.push_back(bot); verts.push_back(e); verts.push_back(n);
        verts.push_back(bot); verts.push_back(s2); verts.push_back(e);
        verts.push_back(bot); verts.push_back(w); verts.push_back(s2);
        verts.push_back(bot); verts.push_back(n); verts.push_back(w);
    }

    vertexCount_ = static_cast<uint32_t>(verts.size());

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = verts.size() * sizeof(MarkerVertex);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapInfo{};
    if (vmaCreateBuffer(vkCtx_->getAllocator(), &bufInfo, &allocInfo,
            &vertexBuffer_, &vertexAlloc_, &mapInfo) != VK_SUCCESS) {
        LOG_ERROR("Failed to create marker vertex buffer");
        return;
    }
    std::memcpy(mapInfo.pMappedData, verts.data(), verts.size() * sizeof(MarkerVertex));
}

void EditorMarkers::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (!vertexBuffer_ || vertexCount_ == 0 || !pipeline_) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &perFrameSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdDraw(cmd, vertexCount_, 1, 0, 0);
}

bool EditorMarkers::createPipeline() {
    VkDevice dev = vkCtx_->getDevice();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &perFrameLayout_;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
        return false;

    // Not a failure: the editor runs without these and says so.
    auto shaders = rendering::loadShaderPair(dev, "assets/shaders/editor_water.vert.spv",
                                             "assets/shaders/editor_water.frag.spv", "editor_water");
    if (!shaders) {
        LOG_WARNING("Marker shaders not found - markers disabled");
        return true;
    }

    VkPipelineShaderStageCreateInfo stages[2]{shaders.vertStage, shaders.fragStage};

    VkVertexInputBindingDescription binding{};
    binding.stride = sizeof(MarkerVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[1].offset = 12;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode = VK_CULL_MODE_BACK_BIT;
    rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = vkCtx_->getMsaaSamples();

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &blend;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vps;
    pci.pRasterizationState = &rast;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = pipelineLayout_;
    pci.renderPass = renderPass_;

    if (vkCreateGraphicsPipelines(dev, vkCtx_->getPipelineCache(), 1, &pci, nullptr, &pipeline_) != VK_SUCCESS) {
        LOG_ERROR("Failed to create marker pipeline");
        pipeline_ = VK_NULL_HANDLE;
    }

    return true;
}

} // namespace editor
} // namespace wowee
