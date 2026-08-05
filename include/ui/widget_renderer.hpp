#pragma once

// Draws a WidgetTree.
//
// Kept apart from the tree itself so the layout rules stay testable without a
// Vulkan device. This half is the part that needs one.
//
// Textures come from the game's own Interface\ art through the existing asset
// path — read the BLP, upload it, hand ImGui the descriptor set — which is the
// same route the action bar already takes for its backpack button. Nothing new
// is shipped; it is the player's own install being drawn.

#include <cstdint>
#include <string>
#include <unordered_map>

#include <vulkan/vulkan.h>

struct ImDrawList;   // global, as ImGui declares it
struct ImFont;
struct ImVec2;

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering { class VkContext; }

namespace ui {

class WidgetTree;
struct Widget;

class WidgetRenderer {
public:
    void initialize(pipeline::AssetManager* assets, rendering::VkContext* vkCtx);

    /// Lay the tree out for this screen and draw it. Safe to call with no
    /// device or assets — it simply does nothing, which is what the headless
    /// tests want.
    void render(WidgetTree& tree, float screenW, float screenH);

    /// Number of distinct textures resident. Cheap diagnostic; the cache never
    /// evicts, because Interface\ art is small, bounded and reused constantly.
    size_t textureCount() const { return textures_.size(); }

private:
    /// Descriptor set for an Interface\ path, loading it on first use. Returns
    /// VK_NULL_HANDLE for anything missing, and remembers the failure so a
    /// mistyped path is not re-read every frame.
    VkDescriptorSet texture(const std::string& path, bool add = false);
    /// Already-uploaded texture for a path, without triggering an upload.
    VkDescriptorSet resident(const std::string& path, bool add = false) const;

    /// scale is pixels per interface unit. The rect arrives in pixels, but a
    /// backdrop's insets and edge size are authored in units like everything
    /// else, so they have to make the same trip or a border comes out the
    /// wrong thickness on any display that is not 768 pixels tall.
    /// Sizes every tooltip to the lines it holds, before layout runs. A
    /// tooltip has no size until it has something to say.
    /// Give every unsized label the size of the text in it.
    ///
    /// A FontString with no <Size> takes the size of its string, as it does in
    /// WoW. Leaving it at zero lays it out to nothing and draws nothing, so the
    /// text is set, correct, and invisible — the player frame's level number
    /// read text="14" in a rect of 0x0.
    void sizeFontStrings(WidgetTree& tree);

    void sizeTooltips(WidgetTree& tree);

    /// Draw a string that may carry WoW's inline colour markup, as runs.
    /// wrapWidth of zero draws one line, which is what an auto-sized label
    /// and a tooltip row want; a positive one breaks the text to fit.
    void drawMarkupText(ImDrawList* dl, ImFont* font, float size, ImVec2 at,
                        uint32_t fallback, float alpha, const std::string& text,
                        float wrapWidth = 0.0f, bool nonSpaceWrap = false,
                        const char* justifyH = nullptr, bool forceColor = false);
    void drawBackdrop(ImDrawList* dl, const Widget& w, float scale,
                      float x0, float y0, float x1, float y1);
    void drawStatusBar(ImDrawList* dl, const Widget& w,
                       float x0, float y0, float x1, float y1);
    void drawSlider(ImDrawList* dl, const Widget& w,
                    float x0, float y0, float x1, float y1);
    /// One of a colour picker's four regions: the hue-saturation wheel, the
    /// brightness bar, or either thumb. None of them has art on disk — the
    /// wheel and the bar are generated from the colour `picker` holds, and the
    /// thumbs are placed by it rather than anchored, since where they belong is
    /// the answer rather than the question.
    void drawColorPicker(ImDrawList* dl, const WidgetTree& tree, const Widget& w,
                         const Widget& picker, float screenH,
                         float x0, float y0, float x1, float y1);
    void drawThumb(ImDrawList* dl, const Widget& w,
                   float x0, float y0, float x1, float y1);
    void drawCooldown(ImDrawList* dl, const Widget& w,
                      float x0, float y0, float x1, float y1);

    pipeline::AssetManager* assets_ = nullptr;
    rendering::VkContext* vkCtx_ = nullptr;
    std::unordered_map<std::string, VkDescriptorSet> textures_;
    /// Which incarnation of ImGui's backend the cache above belongs to.
    uint32_t uiTextureGenerationSeen_ = 0;
};

} // namespace ui
} // namespace wowee
