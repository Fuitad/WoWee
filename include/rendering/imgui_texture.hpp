#pragma once

#include <memory>
#include <string>

#include "pipeline/asset_manager.hpp"
#include "rendering/vk_texture.hpp"

namespace wowee {
namespace rendering {

class VkContext;

/// A texture together with the descriptor set that lets ImGui draw it.
///
/// The two are one thing: the descriptor set refers to the texture's image
/// view and sampler, so it stops being valid the moment the texture goes.
/// Holding them apart is what made three copies of the loading code all have
/// to remember to destroy the texture on each of their failure paths.
struct ImGuiTexture {
    std::unique_ptr<VkTexture> texture;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    explicit operator bool() const { return texture != nullptr; }
};

/// Uploads a decoded image and registers it with the ImGui backend.
///
/// Answers an empty result if any step fails, having already destroyed
/// whatever it had built, so a caller has nothing to unwind.
///
/// Clamped rather than repeated, because everything drawn this way is a
/// discrete piece of art: a map marker, a zone highlight. Repeating puts the
/// opposite edge's pixels along the seam of a rotated marker.
ImGuiTexture makeImGuiTexture(VkContext& ctx, const pipeline::BLPImage& image);

/// The same, for the common case of a BLP named by path. An image that will
/// not load answers an empty result exactly as a failed upload does; the
/// caller decides whether that is worth a log line.
ImGuiTexture loadImGuiTexture(pipeline::AssetManager& assets, VkContext& ctx,
                              const std::string& path);

}  // namespace rendering
}  // namespace wowee
