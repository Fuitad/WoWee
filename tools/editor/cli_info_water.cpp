#include "cli_info_water.hpp"

#include "zone_manifest.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoZoneWater(int& i, int argc, char** argv) {
    // Aggregate water-layer stats across all tiles in a zone.
    // Useful for confirming a 'lake zone' actually has water,
    // or for budget planning ('how many MH2O cells does my
    // archipelago zone carry?'). Liquid types: 0=water,
    // 1=ocean, 2=magma, 3=slime.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-water: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "info-zone-water: parse failed\n");
        return 1;
    }
    int waterChunks = 0, totalLayers = 0;
    std::map<uint16_t, int> typeHist;  // liquidType -> chunk count
    float minH = 1e30f, maxH = -1e30f;
    int loadedTiles = 0;
    for (const auto& [tx, ty] : zm.tiles) {
        std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                                std::to_string(tx) + "_" + std::to_string(ty);
        if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) continue;
        wowee::pipeline::ADTTerrain terrain;
        wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
        loadedTiles++;
        for (size_t c = 0; c < terrain.waterData.size(); ++c) {
            const auto& w = terrain.waterData[c];
            if (!w.hasWater()) continue;
            waterChunks++;
            totalLayers += static_cast<int>(w.layers.size());
            for (const auto& layer : w.layers) {
                typeHist[layer.liquidType]++;
                minH = std::min(minH, layer.minHeight);
                maxH = std::max(maxH, layer.maxHeight);
            }
        }
    }
    if (waterChunks == 0) { minH = 0; maxH = 0; }
    auto typeName = [](uint16_t t) {
        switch (t) {
            case 0: return "water";
            case 1: return "ocean";
            case 2: return "magma";
            case 3: return "slime";
        }
        return "?";
    };
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["loadedTiles"] = loadedTiles;
        j["waterChunks"] = waterChunks;
        j["totalLayers"] = totalLayers;
        j["heightRange"] = {minH, maxH};
        nlohmann::json types = nlohmann::json::array();
        for (const auto& [t, c] : typeHist) {
            types.push_back({{"type", t}, {"name", typeName(t)}, {"layerCount", c}});
        }
        j["types"] = types;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone water: %s\n", zoneDir.c_str());
    std::printf("  loaded tiles : %d\n", loadedTiles);
    std::printf("  water chunks : %d (out of %d possible)\n",
                waterChunks, loadedTiles * 256);
    std::printf("  total layers : %d\n", totalLayers);
    if (waterChunks > 0) {
        std::printf("  height range : %.2f to %.2f\n", minH, maxH);
        std::printf("\n  By liquid type:\n");
        for (const auto& [t, c] : typeHist) {
            std::printf("    %s (%u): %d layer(s)\n",
                        typeName(t), t, c);
        }
    } else {
        std::printf("  (no water in this zone)\n");
    }
    return 0;
}

int handleInfoProjectWater(int& i, int argc, char** argv) {
    // Project-wide water rollup. Walks every zone in projectDir,
    // sums water chunks/layers/types per zone, then totals
    // across the project. Useful for "do my coastal zones
    // actually carry ocean data" sanity checks and for budget
    // planning when many zones share liquid types.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-water: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    auto typeName = [](uint16_t t) {
        switch (t) {
            case 0: return "water";
            case 1: return "ocean";
            case 2: return "magma";
            case 3: return "slime";
        }
        return "?";
    };
    struct ZRow {
        std::string name;
        int loadedTiles = 0, waterChunks = 0, totalLayers = 0;
        std::map<uint16_t, int> typeHist;
    };
    std::vector<ZRow> rows;
    int gLoadedTiles = 0, gWaterChunks = 0, gTotalLayers = 0;
    std::map<uint16_t, int> gTypeHist;
    float gMinH = 1e30f, gMaxH = -1e30f;
    for (const auto& zoneDir : zones) {
        ZRow r;
        r.name = fs::path(zoneDir).filename().string();
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) {
            rows.push_back(r);
            continue;
        }
        for (const auto& [tx, ty] : zm.tiles) {
            std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                                    std::to_string(tx) + "_" + std::to_string(ty);
            if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) continue;
            wowee::pipeline::ADTTerrain terrain;
            wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
            r.loadedTiles++;
            for (const auto& w : terrain.waterData) {
                if (!w.hasWater()) continue;
                r.waterChunks++;
                r.totalLayers += static_cast<int>(w.layers.size());
                for (const auto& layer : w.layers) {
                    r.typeHist[layer.liquidType]++;
                    gMinH = std::min(gMinH, layer.minHeight);
                    gMaxH = std::max(gMaxH, layer.maxHeight);
                }
            }
        }
        gLoadedTiles += r.loadedTiles;
        gWaterChunks += r.waterChunks;
        gTotalLayers += r.totalLayers;
        for (const auto& [t, c] : r.typeHist) gTypeHist[t] += c;
        rows.push_back(r);
    }
    if (gWaterChunks == 0) { gMinH = 0; gMaxH = 0; }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["loadedTiles"] = gLoadedTiles;
        j["waterChunks"] = gWaterChunks;
        j["totalLayers"] = gTotalLayers;
        j["heightRange"] = {gMinH, gMaxH};
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& r : rows) {
            nlohmann::json types = nlohmann::json::array();
            for (const auto& [t, c] : r.typeHist) {
                types.push_back({{"type", t}, {"name", typeName(t)},
                                 {"layerCount", c}});
            }
            zarr.push_back({{"name", r.name},
                            {"loadedTiles", r.loadedTiles},
                            {"waterChunks", r.waterChunks},
                            {"totalLayers", r.totalLayers},
                            {"types", types}});
        }
        j["zones"] = zarr;
        nlohmann::json gtypes = nlohmann::json::array();
        for (const auto& [t, c] : gTypeHist) {
            gtypes.push_back({{"type", t}, {"name", typeName(t)},
                              {"layerCount", c}});
        }
        j["types"] = gtypes;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project water: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu\n", zones.size());
    std::printf("  loaded tiles : %d\n", gLoadedTiles);
    std::printf("  water chunks : %d (out of %d possible)\n",
                gWaterChunks, gLoadedTiles * 256);
    std::printf("  total layers : %d\n", gTotalLayers);
    if (gWaterChunks > 0) {
        std::printf("  height range : %.2f to %.2f\n", gMinH, gMaxH);
        std::printf("\n  By liquid type (project-wide):\n");
        for (const auto& [t, c] : gTypeHist) {
            std::printf("    %s (%u): %d layer(s)\n",
                        typeName(t), t, c);
        }
    }
    std::printf("\n  zone                  tiles  water-chunks  layers\n");
    for (const auto& r : rows) {
        std::printf("  %-20s  %5d  %12d  %6d\n",
                    r.name.substr(0, 20).c_str(),
                    r.loadedTiles, r.waterChunks, r.totalLayers);
    }
    return 0;
}

}  // namespace

bool handleInfoWater(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone-water") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneWater(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-water") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectWater(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
