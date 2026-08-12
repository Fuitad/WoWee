#include "texture_exporter.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include "core/logger.hpp"
#include <filesystem>
#include <algorithm>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace wowee {
namespace editor {

std::vector<std::string> TextureExporter::collectUsedTextures(const pipeline::ADTTerrain& terrain) {
    std::unordered_set<std::string> unique;
    for (const auto& tex : terrain.textures)
        unique.insert(tex);
    std::vector<std::string> result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> TextureExporter::collectM2Textures(pipeline::AssetManager* am,
                                                              const std::string& m2Path) {
    std::vector<std::string> out;
    if (!am || m2Path.empty()) return out;

    auto data = am->readFile(m2Path);
    if (data.empty()) return out;
    auto m2 = pipeline::M2Loader::load(data);
    // Skin file holds geometry but textures live in the M2 header itself.
    // Even if isValid() is false (no skin loaded), the texture list is populated.

    std::unordered_set<std::string> unique;
    for (const auto& tex : m2.textures) {
        if (tex.filename.empty()) continue;
        std::string p = tex.filename;
        std::transform(p.begin(), p.end(), p.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        unique.insert(p);
    }
    out.assign(unique.begin(), unique.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> TextureExporter::collectWMOTextures(pipeline::AssetManager* am,
                                                              const std::string& wmoPath) {
    std::vector<std::string> out;
    if (!am || wmoPath.empty()) return out;

    auto rootData = am->readFile(wmoPath);
    if (rootData.empty()) return out;
    auto wmo = pipeline::WMOLoader::load(rootData);

    // Load group files so any group-only texture references are populated too.
    std::string base = wmoPath;
    if (base.size() > 4) base = base.substr(0, base.size() - 4);
    for (uint32_t gi = 0; gi < wmo.nGroups; gi++) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03u.wmo", gi);
        auto gd = am->readFile(base + suffix);
        if (!gd.empty()) pipeline::WMOLoader::loadGroup(gd, wmo, gi);
    }

    std::unordered_set<std::string> unique;
    for (const auto& tex : wmo.textures) {
        if (tex.empty()) continue;
        std::string p = tex;
        std::transform(p.begin(), p.end(), p.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        unique.insert(p);
    }

    // WMO doodads (props inside the building) are M2 models - their textures
    // also need to ship with the zone or the building will render with missing
    // chairs/decorations.
    std::unordered_set<std::string> seenDoodadM2;
    for (const auto& [offset, name] : wmo.doodadNames) {
        (void)offset;
        if (name.empty() || !seenDoodadM2.insert(name).second) continue;
        for (auto& t : collectM2Textures(am, name)) unique.insert(std::move(t));
    }

    out.assign(unique.begin(), unique.end());
    std::sort(out.begin(), out.end());
    return out;
}

int TextureExporter::exportTexturesAsPng(pipeline::AssetManager* am,
                                          const std::vector<std::string>& texturePaths,
                                          const std::string& outputDir) {
    namespace fs = std::filesystem;
    int exported = 0;

    int notFound = 0;
    for (const auto& texPath : texturePaths) {
        auto blpImage = am->loadTexture(texPath);
        if (!blpImage.isValid()) {
            // Many character/dynamic textures legitimately don't exist as files
            // (composed at runtime from CharSections.dbc); don't spam.
            ++notFound;
            if (notFound <= 5) LOG_WARNING("Texture not found or invalid: ", texPath);
            continue;
        }

        // Build output path: replace backslashes, change .blp to .png
        std::string outPath = texPath;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        // Lowercase
        std::transform(outPath.begin(), outPath.end(), outPath.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        // Change extension
        auto dotPos = outPath.rfind('.');
        if (dotPos != std::string::npos)
            outPath = outPath.substr(0, dotPos) + ".png";

        // Reject path-traversal attempts in the source path. Texture paths
        // come from M2/WMO files which a malicious zone could craft.
        if (outPath.find("..") != std::string::npos ||
            (!outPath.empty() && (outPath[0] == '/' || outPath[0] == '\\'))) {
            LOG_WARNING("Texture path rejected (traversal attempt): ", texPath);
            continue;
        }

        std::string fullPath = outputDir + "/" + outPath;
        fs::create_directories(fs::path(fullPath).parent_path());

        // Validate the loaded image before passing to stbi_write_png. A
        // corrupt BLP could produce mismatched dimensions vs data length,
        // and the data buffer needs to be at least width * height * 4 bytes.
        const size_t expectedBytes =
            static_cast<size_t>(blpImage.width) * blpImage.height * 4;
        if (blpImage.width <= 0 || blpImage.height <= 0 ||
            blpImage.width > 8192 || blpImage.height > 8192 ||
            blpImage.data.size() < expectedBytes) {
            LOG_WARNING("PNG export skipped - invalid image (",
                        blpImage.width, "x", blpImage.height,
                        " data=", blpImage.data.size(), "): ", texPath);
            continue;
        }

        // Write RGBA data as PNG
        if (stbi_write_png(fullPath.c_str(), blpImage.width, blpImage.height, 4,
                           blpImage.data.data(), blpImage.width * 4)) {
            exported++;
        } else {
            LOG_WARNING("Failed to write PNG: ", fullPath);
        }
    }

    LOG_INFO("Exported ", exported, "/", texturePaths.size(), " textures as PNG to ", outputDir,
             notFound > 5 ? " (" + std::to_string(notFound - 5) + " more not-found warnings suppressed)" : "");
    return exported;
}

} // namespace editor
} // namespace wowee
