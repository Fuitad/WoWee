#include "asset_browser.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/asset_manifest.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <set>

namespace wowee {
namespace editor {

std::string AssetBrowser::extractFilename(const std::string& path) {
    auto pos = path.rfind('\\');
    return pos != std::string::npos ? path.substr(pos + 1) : path;
}

std::string AssetBrowser::extractDirectory(const std::string& path) {
    auto pos = path.rfind('\\');
    return pos != std::string::npos ? path.substr(0, pos) : "";
}

void AssetBrowser::initialize(pipeline::AssetManager* am) {
    if (initialized_ || !am) return;
    initialized_ = true;

    const auto& entries = am->getManifest().getEntries();

    std::set<std::string> texDirSet, m2DirSet, wmoDirSet;

    for (const auto& [path, entry] : entries) {
        // Tileset textures
        if (path.starts_with("tileset\\") && path.ends_with(".blp")) {
            // Skip specular/normal maps
            if (path.ends_with("_s.blp") || path.ends_with("_h.blp") ||
                path.ends_with("_n.blp")) continue;

            AssetEntry ae;
            ae.wowPath = path;
            ae.displayName = extractFilename(path);
            ae.directory = extractDirectory(path);
            textures_.push_back(ae);
            texDirSet.insert(ae.directory);
        }

        // M2 models (world doodads)
        if (path.ends_with(".m2")) {
            // Focus on world assets, skip character/creature/item models
            if (path.starts_with("world\\") || path.starts_with("dungeons\\")) {
                AssetEntry ae;
                ae.wowPath = path;
                ae.displayName = extractFilename(path);
                ae.directory = extractDirectory(path);
                m2Models_.push_back(ae);
                m2DirSet.insert(ae.directory);
            }
        }

        // WMOs
        if (path.ends_with(".wmo") && !path.ends_with("_lod.wmo")) {
            // Skip group files (_000.wmo, _001.wmo, etc.)
            bool isGroup = false;
            if (path.size() > 8) {
                auto base = path.substr(path.size() - 8);
                if (base[0] == '_' && std::isdigit(base[1]) && std::isdigit(base[2]) &&
                    std::isdigit(base[3]))
                    isGroup = true;
            }
            if (isGroup) continue;

            AssetEntry ae;
            ae.wowPath = path;
            ae.displayName = extractFilename(path);
            ae.directory = extractDirectory(path);
            wmos_.push_back(ae);
            wmoDirSet.insert(ae.directory);
        }
    }

    // Scan for available maps (WDT files)
    std::set<std::string> mapSet;
    for (const auto& [path, entry] : entries) {
        if (path.starts_with("world\\maps\\") && path.ends_with(".wdt")) {
            auto firstSlash = path.find('\\', 11); // after "world\\maps\\"
            if (firstSlash != std::string::npos) {
                std::string mapName = path.substr(11, firstSlash - 11);
                mapSet.insert(mapName);
            }
        }
    }
    mapNames_.assign(mapSet.begin(), mapSet.end());
    std::sort(mapNames_.begin(), mapNames_.end());

    LOG_INFO("  Maps found: ", mapNames_.size());

    std::sort(textures_.begin(), textures_.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.wowPath < b.wowPath; });
    std::sort(m2Models_.begin(), m2Models_.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.wowPath < b.wowPath; });
    std::sort(wmos_.begin(), wmos_.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.wowPath < b.wowPath; });

    textureDirs_.assign(texDirSet.begin(), texDirSet.end());
    m2Dirs_.assign(m2DirSet.begin(), m2DirSet.end());
    wmoDirs_.assign(wmoDirSet.begin(), wmoDirSet.end());

    std::sort(textureDirs_.begin(), textureDirs_.end());
    std::sort(m2Dirs_.begin(), m2Dirs_.end());
    std::sort(wmoDirs_.begin(), wmoDirs_.end());

    LOG_INFO("Asset browser: ", textures_.size(), " textures, ",
             m2Models_.size(), " M2s, ", wmos_.size(), " WMOs indexed, ",
             mapNames_.size(), " maps");
}

std::vector<std::pair<int,int>> AssetBrowser::getMapTiles(const std::string& mapName) const {
    // Build tile list by checking manifest for each possible ADT
    // Only called when user selects a map, so 64x64 is fine
    std::vector<std::pair<int,int>> tiles;
    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            std::string path = "world\\maps\\" + mapName + "\\" + mapName + "_" +
                               std::to_string(x) + "_" + std::to_string(y) + ".adt";
            // We stored entries as lowercase, so this should match
            // But we don't have direct access to entries here
            // Use a dummy check - the UI already does manifest lookups
        }
    }
    return tiles; // Empty for now - UI uses direct manifest check per tile
}

} // namespace editor
} // namespace wowee
