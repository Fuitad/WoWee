#include "cli_tiles.hpp"

#include "zone_manifest.hpp"
#include "terrain_editor.hpp"
#include "terrain_biomes.hpp"
#include "wowee_terrain.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleAddTile(int& i, int argc, char** argv) {
    // Extend an existing zone with another ADT tile. Zones can
    // span multiple tiles (e.g. a continent fragment), but
    // --scaffold-zone only creates one. This adds another:
    //   wowee_editor --add-tile custom_zones/MyZone 29 30
    // Generates a fresh blank-flat WHM/WOT pair at the new tile
    // and appends to the zone manifest's tiles list.
    std::string zoneDir = argv[++i];
    int tx, ty;
    try {
        tx = std::stoi(argv[++i]);
        ty = std::stoi(argv[++i]);
    } catch (...) {
        std::fprintf(stderr, "add-tile: bad coordinates\n");
        return 1;
    }
    float baseHeight = 100.0f;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { baseHeight = std::stof(argv[++i]); }
        catch (...) {}
    }
    if (tx < 0 || tx >= 64 || ty < 0 || ty >= 64) {
        std::fprintf(stderr, "add-tile: tile coord (%d, %d) out of WoW grid [0, 64)\n",
                     tx, ty);
        return 1;
    }
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr, "add-tile: %s has no zone.json - not a zone dir\n",
                     zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "add-tile: failed to parse %s\n", manifestPath.c_str());
        return 1;
    }
    // Reject duplicates so we don't silently overwrite an existing
    // tile's heightmap when the user makes a typo.
    for (const auto& [ex, ey] : zm.tiles) {
        if (ex == tx && ey == ty) {
            std::fprintf(stderr,
                "add-tile: tile (%d, %d) already in manifest\n", tx, ty);
            return 1;
        }
    }
    // Also bail if the file would clobber an existing one outside
    // the manifest (e.g. user hand-created tiles without updating
    // zone.json). Catches drift between disk and manifest.
    std::string base = zoneDir + "/" + zm.mapName + "_" +
                       std::to_string(tx) + "_" + std::to_string(ty);
    if (fs::exists(base + ".whm") || fs::exists(base + ".wot")) {
        std::fprintf(stderr,
            "add-tile: %s.{whm,wot} already exists on disk (manifest out of sync?)\n",
            base.c_str());
        return 1;
    }
    // Generate the new heightmap. Reuses the same factory that
    // --scaffold-zone uses, so the output is consistent.
    auto terrain = wowee::editor::TerrainEditor::createBlankTerrain(
        tx, ty, baseHeight, wowee::editor::Biome::Grassland);
    wowee::editor::WoweeTerrain::exportOpen(terrain, base, tx, ty);
    // Append + save manifest. ZoneManifest::save rebuilds the
    // files block from the tiles list, so the new adt_tx_ty entry
    // appears automatically in zone.json.
    zm.tiles.push_back({tx, ty});
    if (!zm.save(manifestPath)) {
        std::fprintf(stderr, "add-tile: failed to save %s\n", manifestPath.c_str());
        return 1;
    }
    std::printf("Added tile (%d, %d) to %s\n", tx, ty, zoneDir.c_str());
    std::printf("  files     : %s.whm, %s.wot\n",
                (zm.mapName + "_" + std::to_string(tx) + "_" + std::to_string(ty)).c_str(),
                (zm.mapName + "_" + std::to_string(tx) + "_" + std::to_string(ty)).c_str());
    std::printf("  tiles now : %zu total\n", zm.tiles.size());
    return 0;
}

int handleRemoveTile(int& i, int /*argc*/, char** argv) {
    // Symmetric counterpart to --add-tile. Drops the entry from
    // ZoneManifest::tiles AND deletes the WHM/WOT/WOC files on
    // disk so the zone is left consistent (no orphan sidecars).
    std::string zoneDir = argv[++i];
    int tx, ty;
    try {
        tx = std::stoi(argv[++i]);
        ty = std::stoi(argv[++i]);
    } catch (...) {
        std::fprintf(stderr, "remove-tile: bad coordinates\n");
        return 1;
    }
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr, "remove-tile: %s has no zone.json - not a zone dir\n",
                     zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "remove-tile: failed to parse %s\n", manifestPath.c_str());
        return 1;
    }
    auto it = std::find_if(zm.tiles.begin(), zm.tiles.end(),
        [&](const std::pair<int,int>& p) { return p.first == tx && p.second == ty; });
    if (it == zm.tiles.end()) {
        std::fprintf(stderr,
            "remove-tile: tile (%d, %d) not in manifest\n", tx, ty);
        return 1;
    }
    // Don't strand a zone with zero tiles - server module gen and
    // pack-wcp both expect at least one. The user can --rename-zone
    // or rm -rf if they want the zone gone entirely.
    if (zm.tiles.size() == 1) {
        std::fprintf(stderr,
            "remove-tile: refusing to remove last tile (zone would be empty)\n");
        return 1;
    }
    zm.tiles.erase(it);
    // Delete the slug-prefixed files for this tile. Use error_code
    // so we don't throw on missing files - partial removal from
    // earlier failures shouldn't block cleanup of what's left.
    std::string base = zoneDir + "/" + zm.mapName + "_" +
                       std::to_string(tx) + "_" + std::to_string(ty);
    int deleted = 0;
    std::error_code ec;
    for (const char* ext : {".whm", ".wot", ".woc"}) {
        if (fs::remove(base + ext, ec)) deleted++;
    }
    if (!zm.save(manifestPath)) {
        std::fprintf(stderr, "remove-tile: failed to save %s\n", manifestPath.c_str());
        return 1;
    }
    std::printf("Removed tile (%d, %d) from %s\n", tx, ty, zoneDir.c_str());
    std::printf("  deleted   : %d file(s) (.whm/.wot/.woc)\n", deleted);
    std::printf("  tiles now : %zu remaining\n", zm.tiles.size());
    return 0;
}

int handleListTiles(int& i, int argc, char** argv) {
    // Enumerate every tile in the zone manifest with on-disk
    // file presence - useful for spotting missing/orphan files
    // before pack-wcp would fail.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr, "list-tiles: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "list-tiles: failed to parse %s\n", manifestPath.c_str());
        return 1;
    }
    auto baseFor = [&](int tx, int ty) {
        return zoneDir + "/" + zm.mapName + "_" +
               std::to_string(tx) + "_" + std::to_string(ty);
    };
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["mapName"] = zm.mapName;
        j["count"] = zm.tiles.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [tx, ty] : zm.tiles) {
            std::string b = baseFor(tx, ty);
            arr.push_back({
                {"x", tx}, {"y", ty},
                {"whm", fs::exists(b + ".whm")},
                {"wot", fs::exists(b + ".wot")},
                {"woc", fs::exists(b + ".woc")},
            });
        }
        j["tiles"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone: %s (%s, %zu tile(s))\n",
                zoneDir.c_str(), zm.mapName.c_str(), zm.tiles.size());
    std::printf("   tx   ty   whm  wot  woc\n");
    for (const auto& [tx, ty] : zm.tiles) {
        std::string b = baseFor(tx, ty);
        std::printf("  %3d  %3d   %s    %s    %s\n",
                    tx, ty,
                    fs::exists(b + ".whm") ? "y" : "-",
                    fs::exists(b + ".wot") ? "y" : "-",
                    fs::exists(b + ".woc") ? "y" : "-");
    }
    return 0;
}


}  // namespace

bool handleTiles(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--add-tile") == 0 && i + 3 < argc) {
        outRc = handleAddTile(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--remove-tile") == 0 && i + 3 < argc) {
        outRc = handleRemoveTile(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-tiles") == 0 && i + 1 < argc) {
        outRc = handleListTiles(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
