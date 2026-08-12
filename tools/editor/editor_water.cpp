#include "editor_water.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "core/logger.hpp"
#include <cstring>
#include <cmath>

namespace wowee {
namespace editor {

static constexpr float TILE_SIZE = 533.33333f;
static constexpr float CHUNK_SIZE = TILE_SIZE / 16.0f;

EditorWater::EditorWater() = default;
EditorWater::~EditorWater() { shutdown(); }

bool EditorWater::initialize(rendering::VkContext* ctx, VkRenderPass renderPass,
                              VkDescriptorSetLayout perFrameLayout) {
    vkCtx_ = ctx;
    renderPass_ = renderPass;
    perFrameLayout_ = perFrameLayout;
    return createPipeline();
}

void EditorWater::shutdown() {
    if (!vkCtx_) return;
    VkDevice dev = vkCtx_->getDevice();

    if (vertexBuffer_) {
        vmaDestroyBuffer(vkCtx_->getAllocator(), vertexBuffer_, vertexAlloc_);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (pipeline_) { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    vkCtx_ = nullptr;
}

void EditorWater::clear() {
    if (vertexBuffer_) {
        vmaDestroyBuffer(vkCtx_->getAllocator(), vertexBuffer_, vertexAlloc_);
        vertexBuffer_ = VK_NULL_HANDLE;
        vertexCount_ = 0;
    }
}

void EditorWater::update(const pipeline::ADTTerrain& terrain, int tileX, int tileY) {
    clear();

    std::vector<WaterVertex> verts;

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            int idx = cy * 16 + cx;
            const auto& water = terrain.waterData[idx];
            if (!water.hasWater()) continue;

            float tileNW_X = (32.0f - static_cast<float>(tileY)) * TILE_SIZE;
            float tileNW_Y = (32.0f - static_cast<float>(tileX)) * TILE_SIZE;
            float x0 = tileNW_X - static_cast<float>(cy) * CHUNK_SIZE;
            float y0 = tileNW_Y - static_cast<float>(cx) * CHUNK_SIZE;
            float x1 = x0 - CHUNK_SIZE;
            float y1 = y0 - CHUNK_SIZE;

            float h = water.layers[0].maxHeight;
            // NaN water height would produce NaN vertex positions and
            // Vulkan would drop the whole water mesh. WOT load already
            // scrubs but defending here is cheap insurance.
            if (!std::isfinite(h)) continue;

            // Water color by type
            float r = 0.1f, g = 0.3f, b = 0.7f, a = 0.45f;
            uint16_t lt = water.layers[0].liquidType;
            if (lt == 2) { r = 0.8f; g = 0.2f; b = 0.05f; a = 0.7f; } // magma
            if (lt == 3) { r = 0.2f; g = 0.6f; b = 0.1f; a = 0.6f; }  // slime

            // Two triangles per chunk
            WaterVertex v;
            v.color[0] = r; v.color[1] = g; v.color[2] = b; v.color[3] = a;

            v.pos[0] = x0; v.pos[1] = y0; v.pos[2] = h; verts.push_back(v);
            v.pos[0] = x1; v.pos[1] = y0; v.pos[2] = h; verts.push_back(v);
            v.pos[0] = x1; v.pos[1] = y1; v.pos[2] = h; verts.push_back(v);

            v.pos[0] = x0; v.pos[1] = y0; v.pos[2] = h; verts.push_back(v);
            v.pos[0] = x1; v.pos[1] = y1; v.pos[2] = h; verts.push_back(v);
            v.pos[0] = x0; v.pos[1] = y1; v.pos[2] = h; verts.push_back(v);
        }
    }

    if (verts.empty()) return;
    vertexCount_ = static_cast<uint32_t>(verts.size());

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = verts.size() * sizeof(WaterVertex);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapInfo{};
    if (vmaCreateBuffer(vkCtx_->getAllocator(), &bufInfo, &allocInfo,
            &vertexBuffer_, &vertexAlloc_, &mapInfo) != VK_SUCCESS) {
        LOG_ERROR("Failed to create water vertex buffer");
        return;
    }
    std::memcpy(mapInfo.pMappedData, verts.data(), verts.size() * sizeof(WaterVertex));
}

void EditorWater::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (!vertexBuffer_ || vertexCount_ == 0 || !pipeline_) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &perFrameSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdDraw(cmd, vertexCount_, 1, 0, 0);
}

bool EditorWater::createPipeline() {
    VkDevice dev = vkCtx_->getDevice();

    // Pipeline layout: set 0 = per-frame UBO (reuse terrain's layout)
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
        LOG_WARNING("Water shaders not found - water rendering disabled");
        return true;
    }

    VkPipelineShaderStageCreateInfo stages[2]{shaders.vertStage, shaders.fragStage};

    // Vertex input: pos(3f) + color(4f) = 28 bytes
    VkVertexInputBindingDescription binding{};
    binding.stride = sizeof(WaterVertex);
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
    vps.viewportCount = 1;
    vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode = VK_CULL_MODE_NONE;
    rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = vkCtx_->getMsaaSamples();

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE; // Transparent - don't write depth
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
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
        LOG_ERROR("Failed to create water pipeline");
        pipeline_ = VK_NULL_HANDLE;
    }

    return true;
}

} // namespace editor
} // namespace wowee
