#include "cli_info_extents.hpp"

#include "zone_manifest.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoZoneExtents(int& i, int argc, char** argv) {
    // Compute the zone's spatial bounding box. XY from manifest
    // tile coords (each tile is 533.33 yards); Z from height
    // range across all loaded chunks. Useful for sizing the
    // camera frustum, planning where new tiles can fit
    // contiguously, or quick sanity-checks ('this zone is 4km
    // across? that seems wrong').
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-extents: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "info-zone-extents: parse failed\n");
        return 1;
    }
    // Tile XY range - straightforward integer min/max.
    int tileMinX = 64, tileMaxX = -1;
    int tileMinY = 64, tileMaxY = -1;
    for (const auto& [tx, ty] : zm.tiles) {
        tileMinX = std::min(tileMinX, tx);
        tileMaxX = std::max(tileMaxX, tx);
        tileMinY = std::min(tileMinY, ty);
        tileMaxY = std::max(tileMaxY, ty);
    }
    // Z range from loaded chunks. Walk every WHM tile; this is
    // the same scan --info-whm does per-tile but rolled up.
    float zMin = 1e30f, zMax = -1e30f;
    int loadedTiles = 0, missingTiles = 0;
    for (const auto& [tx, ty] : zm.tiles) {
        std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                                std::to_string(tx) + "_" + std::to_string(ty);
        if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) {
            missingTiles++;
            continue;
        }
        wowee::pipeline::ADTTerrain terrain;
        wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
        loadedTiles++;
        for (const auto& chunk : terrain.chunks) {
            if (!chunk.heightMap.isLoaded()) continue;
            float baseZ = chunk.position[2];
            for (float h : chunk.heightMap.heights) {
                if (!std::isfinite(h)) continue;
                zMin = std::min(zMin, baseZ + h);
                zMax = std::max(zMax, baseZ + h);
            }
        }
    }
    if (zMin > zMax) { zMin = 0; zMax = 0; }
    // Convert tile coords to world-space yards. WoW grid centers
    // tile (32, 32) at world origin; +X tile = -X world (north),
    // +Y tile = -Y world (west).
    constexpr float kTileSize = 533.33333f;
    float worldMinX = (32.0f - tileMaxY - 1) * kTileSize;
    float worldMaxX = (32.0f - tileMinY)     * kTileSize;
    float worldMinY = (32.0f - tileMaxX - 1) * kTileSize;
    float worldMaxY = (32.0f - tileMinX)     * kTileSize;
    float widthX = worldMaxX - worldMinX;
    float widthY = worldMaxY - worldMinY;
    float heightZ = zMax - zMin;
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["tileCount"] = zm.tiles.size();
        j["loadedTiles"] = loadedTiles;
        j["missingTiles"] = missingTiles;
        j["tileRange"] = {{"x", {tileMinX, tileMaxX}},
                           {"y", {tileMinY, tileMaxY}}};
        j["worldBox"] = {{"min", {worldMinX, worldMinY, zMin}},
                          {"max", {worldMaxX, worldMaxY, zMax}}};
        j["sizeYards"] = {widthX, widthY, heightZ};
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone extents: %s\n", zoneDir.c_str());
    std::printf("  tile count   : %zu (%d loaded, %d missing on disk)\n",
                zm.tiles.size(), loadedTiles, missingTiles);
    if (zm.tiles.empty()) {
        std::printf("  *no tiles in manifest*\n");
        return 0;
    }
    std::printf("  tile range   : x=[%d, %d]  y=[%d, %d]\n",
                tileMinX, tileMaxX, tileMinY, tileMaxY);
    std::printf("  world box    : (%.1f, %.1f, %.1f) - (%.1f, %.1f, %.1f) yards\n",
                worldMinX, worldMinY, zMin,
                worldMaxX, worldMaxY, zMax);
    std::printf("  size         : %.1f x %.1f x %.1f yards (%.0fm x %.0fm x %.1fm)\n",
                widthX, widthY, heightZ,
                widthX * 0.9144f, widthY * 0.9144f, heightZ * 0.9144f);
    return 0;
}

int handleInfoProjectExtents(int& i, int argc, char** argv) {
    // Combined spatial bounding box across every zone in
    // <projectDir>. Per-zone XY tile range + Z height range,
    // unioned into a project-wide world box. Useful for
    // understanding total project area, sizing the world map
    // overview, or sanity-checking that zones don't overlap
    // (the union should equal the sum of disjoint per-zone
    // boxes).
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-extents: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    constexpr float kTileSize = 533.33333f;
    struct ZBox {
        std::string name;
        int tileCount = 0;
        float wMinX = 1e30f, wMaxX = -1e30f;
        float wMinY = 1e30f, wMaxY = -1e30f;
        float zMin = 1e30f, zMax = -1e30f;
    };
    std::vector<ZBox> rows;
    float gMinX = 1e30f, gMaxX = -1e30f;
    float gMinY = 1e30f, gMaxY = -1e30f;
    float gZMin = 1e30f, gZMax = -1e30f;
    int totalTiles = 0;
    for (const auto& zoneDir : zones) {
        ZBox b;
        b.name = fs::path(zoneDir).filename().string();
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) {
            rows.push_back(b);
            continue;
        }
        b.tileCount = static_cast<int>(zm.tiles.size());
        if (zm.tiles.empty()) {
            rows.push_back(b);
            continue;
        }
        int tMinX = 64, tMaxX = -1, tMinY = 64, tMaxY = -1;
        for (const auto& [tx, ty] : zm.tiles) {
            tMinX = std::min(tMinX, tx);
            tMaxX = std::max(tMaxX, tx);
            tMinY = std::min(tMinY, ty);
            tMaxY = std::max(tMaxY, ty);
        }
        b.wMinX = (32.0f - tMaxY - 1) * kTileSize;
        b.wMaxX = (32.0f - tMinY)     * kTileSize;
        b.wMinY = (32.0f - tMaxX - 1) * kTileSize;
        b.wMaxY = (32.0f - tMinX)     * kTileSize;
        for (const auto& [tx, ty] : zm.tiles) {
            std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                                    std::to_string(tx) + "_" + std::to_string(ty);
            if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) continue;
            wowee::pipeline::ADTTerrain terrain;
            wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
            for (const auto& chunk : terrain.chunks) {
                if (!chunk.heightMap.isLoaded()) continue;
                float baseZ = chunk.position[2];
                for (float h : chunk.heightMap.heights) {
                    if (!std::isfinite(h)) continue;
                    b.zMin = std::min(b.zMin, baseZ + h);
                    b.zMax = std::max(b.zMax, baseZ + h);
                }
            }
        }
        if (b.zMin > b.zMax) { b.zMin = 0; b.zMax = 0; }
        gMinX = std::min(gMinX, b.wMinX);
        gMaxX = std::max(gMaxX, b.wMaxX);
        gMinY = std::min(gMinY, b.wMinY);
        gMaxY = std::max(gMaxY, b.wMaxY);
        gZMin = std::min(gZMin, b.zMin);
        gZMax = std::max(gZMax, b.zMax);
        totalTiles += b.tileCount;
        rows.push_back(b);
    }
    if (totalTiles == 0) {
        gMinX = gMaxX = gMinY = gMaxY = gZMin = gZMax = 0.0f;
    }
    float gWidthX = gMaxX - gMinX;
    float gWidthY = gMaxY - gMinY;
    float gHeightZ = gZMax - gZMin;
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["totalTiles"] = totalTiles;
        j["worldBox"] = {{"min", {gMinX, gMinY, gZMin}},
                          {"max", {gMaxX, gMaxY, gZMax}}};
        j["sizeYards"] = {gWidthX, gWidthY, gHeightZ};
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& b : rows) {
            zarr.push_back({{"name", b.name},
                            {"tileCount", b.tileCount},
                            {"worldBox", {{"min", {b.wMinX, b.wMinY, b.zMin}},
                                           {"max", {b.wMaxX, b.wMaxY, b.zMax}}}}});
        }
        j["zones"] = zarr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project extents: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu\n", zones.size());
    std::printf("  total tiles  : %d\n", totalTiles);
    if (totalTiles == 0) {
        std::printf("  *no tiles in any zone manifest*\n");
        return 0;
    }
    std::printf("  world union  : (%.1f, %.1f, %.1f) - (%.1f, %.1f, %.1f) yards\n",
                gMinX, gMinY, gZMin, gMaxX, gMaxY, gZMax);
    std::printf("  total size   : %.1f x %.1f x %.1f yards (%.0fm x %.0fm x %.1fm)\n",
                gWidthX, gWidthY, gHeightZ,
                gWidthX * 0.9144f, gWidthY * 0.9144f, gHeightZ * 0.9144f);
    std::printf("\n  zone                  tiles      worldX (min..max)        worldY (min..max)\n");
    for (const auto& b : rows) {
        if (b.tileCount == 0) {
            std::printf("  %-20s  %5d  (no tiles)\n",
                        b.name.substr(0, 20).c_str(), b.tileCount);
            continue;
        }
        std::printf("  %-20s  %5d  %9.1f .. %9.1f   %9.1f .. %9.1f\n",
                    b.name.substr(0, 20).c_str(), b.tileCount,
                    b.wMinX, b.wMaxX, b.wMinY, b.wMaxY);
    }
    return 0;
}

}  // namespace

bool handleInfoExtents(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone-extents") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneExtents(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-extents") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectExtents(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
