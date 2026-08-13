#include "ui/ui_upload_budget.hpp"
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


VkDescriptorSet cachedIconTexture(
    uint32_t iconId, pipeline::AssetManager* assetManager, core::Window* window,
    const std::unordered_map<uint32_t, std::string>& paths,
    std::unordered_map<uint32_t, VkDescriptorSet>& cache) {
    if (iconId == 0 || !assetManager) return VK_NULL_HANDLE;

    auto cit = cache.find(iconId);
    if (cit != cache.end()) return cit->second;

    // Not cached: the budget is per frame, and an icon that misses it shows
    // blank this frame and is asked for again next one. Caching a null here
    // would blacklist it for the life of the panel.
    if (!claimUiTextureUpload()) return VK_NULL_HANDLE;

    auto pit = paths.find(iconId);
    if (pit == paths.end()) {
        cache[iconId] = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }

    // Cached either way, failures included: the file is either there or it is
    // not, and looking again every frame will not change that.
    VkDescriptorSet ds =
        uploadUiTextureFromBlp(assetManager, pit->second + ".blp", window);
    cache[iconId] = ds;
    return ds;
}

}  // namespace wowee::ui
