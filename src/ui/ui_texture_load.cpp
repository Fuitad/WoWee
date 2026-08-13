#include "ui/ui_texture_load.hpp"

#include "core/window.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "rendering/vk_context.hpp"

namespace wowee::ui {

VkDescriptorSet uploadUiTextureFromBlp(pipeline::AssetManager* assetManager,
                                       const std::string& path,
                                       core::Window* window,
                                       UiTextureLoad* why) {
    const auto fail = [&](UiTextureLoad reason) {
        if (why) *why = reason;
        return VK_NULL_HANDLE;
    };

    if (!assetManager) return fail(UiTextureLoad::NotFound);

    auto blpData = assetManager->readFile(path);
    if (blpData.empty()) return fail(UiTextureLoad::NotFound);

    auto image = pipeline::BLPLoader::load(blpData);
    if (!image.isValid()) return fail(UiTextureLoad::DecodeFailed);

    auto* vkCtx = window ? window->getVkContext() : nullptr;
    if (!vkCtx) return fail(UiTextureLoad::NoContext);

    if (why) *why = UiTextureLoad::Ok;
    return vkCtx->uploadImGuiTexture(image.data.data(), image.width, image.height);
}

}  // namespace wowee::ui
