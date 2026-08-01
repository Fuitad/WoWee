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

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering { class VkContext; }

namespace ui {

class WidgetTree;

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
    VkDescriptorSet texture(const std::string& path);

    pipeline::AssetManager* assets_ = nullptr;
    rendering::VkContext* vkCtx_ = nullptr;
    std::unordered_map<std::string, VkDescriptorSet> textures_;
};

} // namespace ui
} // namespace wowee
