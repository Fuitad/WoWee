#include "transform_gizmo.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "core/logger.hpp"
#include <cstring>
#include <cmath>

namespace wowee {
namespace editor {

TransformGizmo::TransformGizmo() = default;
TransformGizmo::~TransformGizmo() { shutdown(); }

bool TransformGizmo::initialize(rendering::VkContext* ctx, VkRenderPass renderPass,
                                 VkDescriptorSetLayout perFrameLayout) {
    vkCtx_ = ctx;
    renderPass_ = renderPass;
    perFrameLayout_ = perFrameLayout;
    return createPipeline();
}

void TransformGizmo::shutdown() {
    if (!vkCtx_) return;
    if (vertexBuffer_) {
        vmaDestroyBuffer(vkCtx_->getAllocator(), vertexBuffer_, vertexAlloc_);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (pipeline_) { vkDestroyPipeline(vkCtx_->getDevice(), pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(vkCtx_->getDevice(), pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    vkCtx_ = nullptr;
}

void TransformGizmo::setTarget(const glm::vec3& position, float scale) {
    // Hide the gizmo on a NaN target. updateBuffers calls glm::normalize on
    // axis offsets - non-finite targetPos_ would propagate NaN into the
    // gizmo geometry and Vulkan validation would drop the whole batch.
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z) || !std::isfinite(scale) || scale <= 0.0f) {
        visible_ = false;
        return;
    }
    targetPos_ = position;
    targetScale_ = scale;
    visible_ = true;
    updateBuffers();
}

void TransformGizmo::beginDrag(const glm::vec2& screenPos) {
    dragging_ = true;
    dragStart_ = screenPos;
    dragCurrent_ = screenPos;
    moveDelta_ = glm::vec3(0);
    rotateDelta_ = glm::vec3(0);
    scaleDelta_ = 0.0f;
}

void TransformGizmo::updateDrag(const glm::vec2& screenPos, const rendering::Camera& camera,
                                  float screenW, float screenH) {
    if (!dragging_) return;

    glm::vec2 delta = screenPos - dragCurrent_;
    dragCurrent_ = screenPos;

    float sensitivity = 1.0f;

    if (mode_ == TransformMode::Move) {
        glm::vec3 right = camera.getRight();
        glm::vec3 forward = camera.getForward();
        forward.z = 0; forward = glm::normalize(forward);

        if (axis_ == TransformAxis::X || axis_ == TransformAxis::All)
            moveDelta_ += right * delta.x * sensitivity;
        if (axis_ == TransformAxis::Y || axis_ == TransformAxis::All)
            moveDelta_ -= forward * delta.y * sensitivity;
        if (axis_ == TransformAxis::Z)
            moveDelta_.z -= delta.y * sensitivity;

    } else if (mode_ == TransformMode::Rotate) {
        float rotSpeed = 0.5f;
        if (axis_ == TransformAxis::Z || axis_ == TransformAxis::All)
            rotateDelta_.z += delta.x * rotSpeed;
        if (axis_ == TransformAxis::X)
            rotateDelta_.x += delta.y * rotSpeed;
        if (axis_ == TransformAxis::Y)
            rotateDelta_.y += delta.y * rotSpeed;

    } else if (mode_ == TransformMode::Scale) {
        scaleDelta_ += delta.x * 0.01f;
    }

    (void)screenW; (void)screenH;
}

void TransformGizmo::endDrag() {
    dragging_ = false;
}

void TransformGizmo::updateBuffers() {
    if (!vkCtx_ || !visible_) return;

    if (vertexBuffer_) {
        vmaDestroyBuffer(vkCtx_->getAllocator(), vertexBuffer_, vertexAlloc_);
        vertexBuffer_ = VK_NULL_HANDLE;
    }

    std::vector<GizmoVertex> verts;
    float len = 15.0f * targetScale_;
    float tip = 3.0f * targetScale_;
    float w = 0.8f * targetScale_;
    glm::vec3 p = targetPos_;

    auto addLine = [&](glm::vec3 a, glm::vec3 b, float r, float g, float bl, float alpha) {
        // Thick line as thin quad
        glm::vec3 dir = glm::normalize(b - a);
        glm::vec3 up(0,0,1);
        if (std::abs(glm::dot(dir, up)) > 0.99f) up = glm::vec3(1,0,0);
        glm::vec3 side = glm::normalize(glm::cross(dir, up)) * w * 0.5f;

        GizmoVertex v;
        v.color[0] = r; v.color[1] = g; v.color[2] = bl; v.color[3] = alpha;
        v.pos[0] = a.x+side.x; v.pos[1] = a.y+side.y; v.pos[2] = a.z+side.z; verts.push_back(v);
        v.pos[0] = a.x-side.x; v.pos[1] = a.y-side.y; v.pos[2] = a.z-side.z; verts.push_back(v);
        v.pos[0] = b.x+side.x; v.pos[1] = b.y+side.y; v.pos[2] = b.z+side.z; verts.push_back(v);
        v.pos[0] = b.x+side.x; v.pos[1] = b.y+side.y; v.pos[2] = b.z+side.z; verts.push_back(v);
        v.pos[0] = a.x-side.x; v.pos[1] = a.y-side.y; v.pos[2] = a.z-side.z; verts.push_back(v);
        v.pos[0] = b.x-side.x; v.pos[1] = b.y-side.y; v.pos[2] = b.z-side.z; verts.push_back(v);
    };

    auto addArrowhead = [&](glm::vec3 base, glm::vec3 tipPt, float r, float g, float bl) {
        glm::vec3 dir = glm::normalize(tipPt - base);
        glm::vec3 up(0,0,1);
        if (std::abs(glm::dot(dir, up)) > 0.99f) up = glm::vec3(1,0,0);
        glm::vec3 s1 = glm::normalize(glm::cross(dir, up)) * tip * 0.4f;
        glm::vec3 s2 = glm::normalize(glm::cross(dir, s1)) * tip * 0.4f;

        GizmoVertex v;
        v.color[0] = r; v.color[1] = g; v.color[2] = bl; v.color[3] = 1.0f;
        // 4 faces
        auto tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
            v.pos[0]=a.x; v.pos[1]=a.y; v.pos[2]=a.z; verts.push_back(v);
            v.pos[0]=b.x; v.pos[1]=b.y; v.pos[2]=b.z; verts.push_back(v);
            v.pos[0]=c.x; v.pos[1]=c.y; v.pos[2]=c.z; verts.push_back(v);
        };
        tri(tipPt, base+s1, base+s2);
        tri(tipPt, base+s2, base-s1);
        tri(tipPt, base-s1, base-s2);
        tri(tipPt, base-s2, base+s1);
    };

    bool showMove = (mode_ == TransformMode::Move || mode_ == TransformMode::None);
    bool showRot = (mode_ == TransformMode::Rotate);
    bool showScale = (mode_ == TransformMode::Scale);

    float xAlpha = (axis_ == TransformAxis::X || axis_ == TransformAxis::All) ? 1.0f : 0.3f;
    float yAlpha = (axis_ == TransformAxis::Y || axis_ == TransformAxis::All) ? 1.0f : 0.3f;
    float zAlpha = (axis_ == TransformAxis::Z || axis_ == TransformAxis::All) ? 1.0f : 0.3f;

    if (showMove || showRot) {
        // X axis - Red
        addLine(p, p + glm::vec3(len, 0, 0), 1, 0.2f, 0.2f, xAlpha);
        addArrowhead(p + glm::vec3(len, 0, 0), p + glm::vec3(len + tip, 0, 0), 1, 0.2f, 0.2f);
        // Y axis - Green
        addLine(p, p + glm::vec3(0, len, 0), 0.2f, 1, 0.2f, yAlpha);
        addArrowhead(p + glm::vec3(0, len, 0), p + glm::vec3(0, len + tip, 0), 0.2f, 1, 0.2f);
        // Z axis - Blue
        addLine(p, p + glm::vec3(0, 0, len), 0.3f, 0.3f, 1, zAlpha);
        addArrowhead(p + glm::vec3(0, 0, len), p + glm::vec3(0, 0, len + tip), 0.3f, 0.3f, 1);
    }

    if (showScale) {
        // Scale indicator: box at each axis end
        float bs = tip * 0.5f;
        auto addBox = [&](glm::vec3 c, float r, float g, float bl) {
            // Simple cube from 12 triangles
            GizmoVertex v; v.color[0]=r; v.color[1]=g; v.color[2]=bl; v.color[3]=1;
            auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 d) {
                v.pos[0]=a.x;v.pos[1]=a.y;v.pos[2]=a.z;verts.push_back(v);
                v.pos[0]=b.x;v.pos[1]=b.y;v.pos[2]=b.z;verts.push_back(v);
                v.pos[0]=cc.x;v.pos[1]=cc.y;v.pos[2]=cc.z;verts.push_back(v);
                v.pos[0]=cc.x;v.pos[1]=cc.y;v.pos[2]=cc.z;verts.push_back(v);
                v.pos[0]=d.x;v.pos[1]=d.y;v.pos[2]=d.z;verts.push_back(v);
                v.pos[0]=a.x;v.pos[1]=a.y;v.pos[2]=a.z;verts.push_back(v);
            };
            face(c+glm::vec3(-bs,-bs,bs),c+glm::vec3(bs,-bs,bs),c+glm::vec3(bs,bs,bs),c+glm::vec3(-bs,bs,bs));
            face(c+glm::vec3(-bs,-bs,-bs),c+glm::vec3(-bs,bs,-bs),c+glm::vec3(bs,bs,-bs),c+glm::vec3(bs,-bs,-bs));
        };
        addLine(p, p + glm::vec3(len, 0, 0), 1, 0.5f, 0, 1);
        addBox(p + glm::vec3(len, 0, 0), 1, 0.5f, 0);
        addLine(p, p + glm::vec3(0, len, 0), 0.5f, 1, 0, 1);
        addBox(p + glm::vec3(0, len, 0), 0.5f, 1, 0);
        addLine(p, p + glm::vec3(0, 0, len), 0, 0.5f, 1, 1);
        addBox(p + glm::vec3(0, 0, len), 0, 0.5f, 1);
    }

    if (verts.empty()) return;
    vertexCount_ = static_cast<uint32_t>(verts.size());

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = verts.size() * sizeof(GizmoVertex);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo mapInfo{};
    if (vmaCreateBuffer(vkCtx_->getAllocator(), &bufInfo, &allocInfo,
            &vertexBuffer_, &vertexAlloc_, &mapInfo) == VK_SUCCESS) {
        std::memcpy(mapInfo.pMappedData, verts.data(), verts.size() * sizeof(GizmoVertex));
    }
}

void TransformGizmo::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (!visible_ || !vertexBuffer_ || vertexCount_ == 0 || !pipeline_) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &perFrameSet, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdDraw(cmd, vertexCount_, 1, 0, 0);
}

bool TransformGizmo::createPipeline() {
    VkDevice dev = vkCtx_->getDevice();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &perFrameLayout_;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
        return false;

    rendering::VkShaderModule vertMod, fragMod;
    if (!vertMod.loadFromFile(dev, "assets/shaders/editor_water.vert.spv") ||
        !fragMod.loadFromFile(dev, "assets/shaders/editor_water.frag.spv")) {
        LOG_WARNING("Gizmo shaders not found");
        return true;
    }

    VkPipelineShaderStageCreateInfo stages[2] = { vertMod.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                                                   fragMod.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT) };

    VkVertexInputBindingDescription binding{}; binding.stride = sizeof(GizmoVertex);
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location=0; attrs[0].format=VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset=0;
    attrs[1].location=1; attrs[1].format=VK_FORMAT_R32G32B32A32_SFLOAT; attrs[1].offset=12;

    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding;
    vi.vertexAttributeDescriptionCount=2; vi.pVertexAttributeDescriptions=attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{}; vps.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount=1; vps.scissorCount=1;

    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode=VK_POLYGON_MODE_FILL; rast.cullMode=VK_CULL_MODE_NONE; rast.lineWidth=1;

    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples=vkCtx_->getMsaaSamples();

    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable=VK_FALSE; // Always on top

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable=VK_TRUE;
    blend.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp=VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp=VK_BLEND_OP_ADD;
    blend.colorWriteMask=0xF;

    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount=1; cb.pAttachments=&blend;

    VkDynamicState dynStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{}; dyn.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount=2; dyn.pDynamicStates=dynStates;

    VkGraphicsPipelineCreateInfo pci{}; pci.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount=2; pci.pStages=stages;
    pci.pVertexInputState=&vi; pci.pInputAssemblyState=&ia; pci.pViewportState=&vps;
    pci.pRasterizationState=&rast; pci.pMultisampleState=&ms; pci.pDepthStencilState=&ds;
    pci.pColorBlendState=&cb; pci.pDynamicState=&dyn;
    pci.layout=pipelineLayout_; pci.renderPass=renderPass_;

    vkCreateGraphicsPipelines(dev, vkCtx_->getPipelineCache(), 1, &pci, nullptr, &pipeline_);
    return true;
}

} // namespace editor
} // namespace wowee
