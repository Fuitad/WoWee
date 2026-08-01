#include "ui/widget_renderer.hpp"

#include "ui/widget_tree.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "rendering/vk_context.hpp"
#include "core/logger.hpp"

#include "imgui.h"

#include <algorithm>

namespace wowee {
namespace ui {

namespace {

/// A path that resolved to nothing. Stored rather than retried, so one bad
/// SetTexture in an addon does not read a missing file every frame forever.
constexpr VkDescriptorSet kMissing = VK_NULL_HANDLE;

uint32_t packColor(const float rgba[4], float alpha) {
    auto ch = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return IM_COL32(ch(rgba[0]), ch(rgba[1]), ch(rgba[2]), ch(rgba[3] * alpha));
}

} // namespace

void WidgetRenderer::initialize(pipeline::AssetManager* assets,
                                rendering::VkContext* vkCtx) {
    assets_ = assets;
    vkCtx_ = vkCtx;
}

VkDescriptorSet WidgetRenderer::texture(const std::string& path) {
    auto it = textures_.find(path);
    if (it != textures_.end()) return it->second;
    if (!assets_ || !vkCtx_ || path.empty()) return kMissing;

    // Addons write "Interface\\Foo\\Bar" without the extension as often as with
    // it, and the real client accepts both.
    std::string resolved = path;
    const bool hasExt = resolved.size() > 4 &&
        (resolved.compare(resolved.size() - 4, 4, ".blp") == 0 ||
         resolved.compare(resolved.size() - 4, 4, ".BLP") == 0);
    if (!hasExt) resolved += ".blp";

    auto data = assets_->readFile(resolved);
    if (data.empty()) {
        LOG_WARNING("Widget texture not found: ", path);
        textures_[path] = kMissing;
        return kMissing;
    }
    auto image = pipeline::BLPLoader::load(data);
    if (!image.isValid()) {
        LOG_WARNING("Widget texture unreadable: ", resolved);
        textures_[path] = kMissing;
        return kMissing;
    }
    VkDescriptorSet set = vkCtx_->uploadImGuiTexture(image.data.data(),
                                                     image.width, image.height);
    textures_[path] = set;
    return set;
}

void WidgetRenderer::render(WidgetTree& tree, float screenW, float screenH) {
    tree.layout(screenW, screenH);
    const auto& order = tree.drawOrder();
    if (order.empty()) return;

    // Behind ImGui's own windows, so the existing interface stays on top while
    // the two coexist, but still over the 3D scene.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    for (const Widget* w : order) {
        // WoW measures from the bottom-left and upward; the screen measures from
        // the top-left and downward. Flip here, at the one place it matters, so
        // every anchor rule upstream reads the way Blizzard documents it.
        const float x0 = w->left;
        const float y0 = screenH - (w->bottom + w->rectH);
        const float x1 = w->left + w->rectW;
        const float y1 = screenH - w->bottom;

        if (w->kind == WidgetKind::Texture) {
            if (w->solidColor || w->texturePath.empty()) {
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                                  packColor(w->color, w->alpha));
                continue;
            }
            VkDescriptorSet tex = texture(w->texturePath);
            if (tex == kMissing) continue;
            // SetTexCoord is left/right/top/bottom in WoW's own order, and its
            // vertical sense already matches the image, so it passes through.
            dl->AddImage(reinterpret_cast<ImTextureID>(tex),
                         ImVec2(x0, y0), ImVec2(x1, y1),
                         ImVec2(w->texCoord[0], w->texCoord[2]),
                         ImVec2(w->texCoord[1], w->texCoord[3]),
                         packColor(w->color, w->alpha));
        } else if (w->kind == WidgetKind::FontString) {
            const ImVec2 extent = ImGui::CalcTextSize(w->text.c_str());
            float tx = x0;
            if (w->justifyH == "CENTER")     tx = x0 + (w->rectW - extent.x) * 0.5f;
            else if (w->justifyH == "RIGHT") tx = x1 - extent.x;
            const float ty = y0 + (w->rectH - extent.y) * 0.5f;
            dl->AddText(ImVec2(tx, ty), packColor(w->color, w->alpha), w->text.c_str());
        }
    }
}

} // namespace ui
} // namespace wowee
