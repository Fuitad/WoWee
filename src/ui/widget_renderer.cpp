#include "ui/widget_renderer.hpp"

#include "ui/widget_tree.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "rendering/vk_context.hpp"
#include "core/logger.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>

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

VkDescriptorSet WidgetRenderer::resident(const std::string& path) const {
    if (path.empty()) return kMissing;
    auto it = textures_.find(path);
    return (it == textures_.end()) ? kMissing : it->second;
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


void WidgetRenderer::drawBackdrop(ImDrawList* dl, const Widget& w,
                                  float x0, float y0, float x1, float y1) {
    // Background sits inside the insets, which is what keeps it from showing
    // through the border drawn over it.
    const float bx0 = x0 + w.insetLeft;
    const float by0 = y0 + w.insetTop;
    const float bx1 = x1 - w.insetRight;
    const float by1 = y1 - w.insetBottom;
    if (bx1 > bx0 && by1 > by0) {
        VkDescriptorSet bg = resident(w.bgFile);
        const uint32_t col = packColor(w.backdropColor, w.alpha);
        if (bg != kMissing) {
            // Tiling repeats the art at its own size instead of stretching it,
            // which is the difference between a stone wall and a smear.
            float u1 = 1.0f, v1 = 1.0f;
            if (w.tileBackground && w.edgeSize > 0.0f) {
                u1 = (bx1 - bx0) / w.edgeSize;
                v1 = (by1 - by0) / w.edgeSize;
            }
            dl->AddImage(reinterpret_cast<ImTextureID>(bg), ImVec2(bx0, by0), ImVec2(bx1, by1),
                         ImVec2(0.0f, 0.0f), ImVec2(u1, v1), col);
        }
        // A backdrop with no background file has no background — only its edge.
        // Filling the rect instead painted every such frame in the backdrop
        // colour, which defaults to opaque white, and a wide one is a white
        // slab across the screen. Nothing is drawn here either while the art is
        // still being read.
    }

    VkDescriptorSet edge = resident(w.edgeFile);
    if (edge == kMissing || w.edgeSize <= 0.0f) return;

    // The edge file is eight square tiles in a row. Measured against the art
    // rather than assumed: UI-Tooltip-Border is 128x16 and UI-DialogBox-Border
    // 256x32, both exactly eight tiles wide.
    const float e = w.edgeSize;
    const uint32_t col = packColor(w.borderColor, w.alpha);
    auto piece = [&](int index, float px0, float py0, float px1, float py1) {
        const float u0 = index / 8.0f, u1 = (index + 1) / 8.0f;
        dl->AddImage(reinterpret_cast<ImTextureID>(edge), ImVec2(px0, py0), ImVec2(px1, py1),
                     ImVec2(u0, 0.0f), ImVec2(u1, 1.0f), col);
    };
    // Edges first, then corners over them, so a corner is never clipped by the
    // run it meets.
    piece(0, x0,     y0 + e, x0 + e, y1 - e);   // left
    piece(1, x1 - e, y0 + e, x1,     y1 - e);   // right
    piece(2, x0 + e, y0,     x1 - e, y0 + e);   // top
    piece(3, x0 + e, y1 - e, x1 - e, y1);       // bottom
    piece(4, x0,     y0,     x0 + e, y0 + e);   // top-left
    piece(5, x1 - e, y0,     x1,     y0 + e);   // top-right
    piece(6, x0,     y1 - e, x0 + e, y1);       // bottom-left
    piece(7, x1 - e, y1 - e, x1,     y1);       // bottom-right
}

void WidgetRenderer::drawStatusBar(ImDrawList* dl, const Widget& w,
                                   float x0, float y0, float x1, float y1) {
    const float f = w.barFraction();
    if (f <= 0.0f) return;
    // Horizontal bars fill from the left; vertical ones from the bottom, which
    // in screen terms means growing upward from y1.
    const float fx1 = w.barVertical ? x1 : x0 + (x1 - x0) * f;
    const float fy0 = w.barVertical ? y1 - (y1 - y0) * f : y0;
    const uint32_t col = packColor(w.barColor, w.alpha);

    VkDescriptorSet tex = resident(w.barTexture);
    if (tex != kMissing) {
        // The texture is cropped to the filled part rather than squashed into
        // it, so a bar at half value shows half its art at its own scale.
        const float u1 = w.barVertical ? 1.0f : f;
        const float v0 = w.barVertical ? (1.0f - f) : 0.0f;
        dl->AddImage(reinterpret_cast<ImTextureID>(tex), ImVec2(x0, fy0), ImVec2(fx1, y1),
                     ImVec2(0.0f, v0), ImVec2(u1, 1.0f), col);
    } else if (w.barTexture.empty()) {
        dl->AddRectFilled(ImVec2(x0, fy0), ImVec2(fx1, y1), col);
    }
}

void WidgetRenderer::render(WidgetTree& tree, float screenW, float screenH) {
    tree.layout(screenW, screenH);
    const auto& order = tree.drawOrder();
    if (order.empty()) return;

    // Resolve textures before recording anything, and only a few per frame.
    //
    // Uploading one ends in vkDeviceWaitIdle. Doing that from inside the draw
    // loop stalls the whole device in the middle of building a frame, which is
    // the shape of problem this renderer has already been bitten by once —
    // enough synchronous submits in a row and the main loop stalls, a fence wait
    // fails, and the device is lost. Hoisting them out means the wait happens
    // between frames instead of during one, and the budget means a screen full
    // of new art costs several quiet frames rather than one very long one.
    constexpr int kUploadsPerFrame = 4;
    int budget = kUploadsPerFrame;
    auto want = [&](const std::string& path) {
        if (budget <= 0 || path.empty()) return;
        if (textures_.find(path) != textures_.end()) return;
        texture(path);
        --budget;
    };
    for (const Widget* w : order) {
        if (budget <= 0) break;
        if (w->kind == WidgetKind::Texture && !w->solidColor) want(w->texturePath);
        if (w->kind == WidgetKind::Frame) {
            if (w->hasBackdrop) { want(w->bgFile); want(w->edgeFile); }
            if (w->isStatusBar) want(w->barTexture);
        }
    }

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

        if (w->kind == WidgetKind::Frame) {
            if (w->hasBackdrop) drawBackdrop(dl, *w, x0, y0, x1, y1);
            if (w->isStatusBar) drawStatusBar(dl, *w, x0, y0, x1, y1);
            continue;
        }

        if (w->kind == WidgetKind::Texture) {
            // A colour set with SetTexture(r,g,b) fills; a texture with no
            // file at all draws nothing. Treating the second as the first
            // painted every undecided region in the default white, which for a
            // full-width backdrop is a white slab across the screen.
            if (w->solidColor) {
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                                  packColor(w->color, w->alpha));
                continue;
            }
            if (w->texturePath.empty()) continue;
            // Only what is already resident. Anything still queued draws on a
            // later frame rather than forcing an upload here.
            auto it = textures_.find(w->texturePath);
            if (it == textures_.end() || it->second == kMissing) continue;
            VkDescriptorSet tex = it->second;
            // SetTexCoord is left/right/top/bottom in WoW's own order, and its
            // vertical sense already matches the image, so it passes through.
            dl->AddImage(reinterpret_cast<ImTextureID>(tex),
                         ImVec2(x0, y0), ImVec2(x1, y1),
                         ImVec2(w->texCoord[0], w->texCoord[2]),
                         ImVec2(w->texCoord[1], w->texCoord[3]),
                         packColor(w->color, w->alpha));
        } else if (w->kind == WidgetKind::FontString) {
            // Font objects carry a height, and honouring it is most of what
            // makes a label look right — a heading and a footnote are the same
            // words at different sizes. The atlas holds one face, so this scales
            // it rather than swapping fonts; loading FRIZQT__ properly needs an
            // atlas rebuild, which cannot happen while a frame is being built.
            ImFont* font = ImGui::GetFont();
            const float base = ImGui::GetFontSize();
            const float size = (w->fontHeight > 0.0f) ? w->fontHeight : base;
            (void)base;
            const ImVec2 extent =
                font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, w->text.c_str())
                     : ImGui::CalcTextSize(w->text.c_str());
            float tx = x0;
            if (w->justifyH == "CENTER")     tx = x0 + (w->rectW - extent.x) * 0.5f;
            else if (w->justifyH == "RIGHT") tx = x1 - extent.x;
            const float ty = y0 + (w->rectH - extent.y) * 0.5f;
            dl->AddText(font, size, ImVec2(tx, ty),
                        packColor(w->color, w->alpha), w->text.c_str());
        }
    }
}

} // namespace ui
} // namespace wowee
