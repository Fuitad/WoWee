#include "cli_info_density.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoZoneDensity(int& i, int argc, char** argv) {
    // Per-tile content density. Catches sparse zones (5 mobs
    // across 16 tiles → boring) and over-stuffed ones (200 mobs
    // in 1 tile → frame-rate bomb). Per-tile bucket uses tile
    // (tx, ty) computed from world position by reversing the
    // WoW grid transform.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-density: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "info-zone-density: parse failed\n");
        return 1;
    }
    // Per-(tx, ty) bucket of counts.
    struct TileBucket { int creatures = 0, objects = 0; };
    std::map<std::pair<int,int>, TileBucket> tiles;
    for (const auto& [tx, ty] : zm.tiles) tiles[{tx, ty}] = {};
    // Reverse the WoW grid transform: world (X, Y) -> tile (tx, ty).
    // From --info-zone-extents:
    //   worldX = (32 - tileY) * 533.33 - subX
    //   worldY = (32 - tileX) * 533.33 - subY
    // So:
    //   tileX = floor(32 - worldY / 533.33)
    //   tileY = floor(32 - worldX / 533.33)
    constexpr float kTileSize = 533.33333f;
    auto worldToTile = [](float wx, float wy) -> std::pair<int,int> {
        int tx = static_cast<int>(std::floor(32.0f - wy / kTileSize));
        int ty = static_cast<int>(std::floor(32.0f - wx / kTileSize));
        return {tx, ty};
    };
    wowee::editor::NpcSpawner sp;
    int totalCreat = 0;
    if (sp.loadFromFile(zoneDir + "/creatures.json")) {
        totalCreat = static_cast<int>(sp.spawnCount());
        for (const auto& s : sp.getSpawns()) {
            auto t = worldToTile(s.position.x, s.position.y);
            auto it = tiles.find(t);
            if (it != tiles.end()) it->second.creatures++;
            // Out-of-zone spawns silently dropped — they'll
            // surface in --check-zone-refs / --check-zone-content.
        }
    }
    wowee::editor::ObjectPlacer op;
    int totalObj = 0;
    if (op.loadFromFile(zoneDir + "/objects.json")) {
        totalObj = static_cast<int>(op.getObjects().size());
        for (const auto& o : op.getObjects()) {
            auto t = worldToTile(o.position.x, o.position.y);
            auto it = tiles.find(t);
            if (it != tiles.end()) it->second.objects++;
        }
    }
    wowee::editor::QuestEditor qe;
    int totalQ = 0;
    if (qe.loadFromFile(zoneDir + "/quests.json")) {
        totalQ = static_cast<int>(qe.questCount());
    }
    int tileCount = static_cast<int>(tiles.size());
    double avgCreatPerTile = tileCount > 0 ? double(totalCreat) / tileCount : 0.0;
    double avgObjPerTile = tileCount > 0 ? double(totalObj) / tileCount : 0.0;
    double questsPerTile = tileCount > 0 ? double(totalQ) / tileCount : 0.0;
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["tileCount"] = tileCount;
        j["totals"] = {{"creatures", totalCreat},
                        {"objects", totalObj},
                        {"quests", totalQ}};
        j["averages"] = {{"creaturesPerTile", avgCreatPerTile},
                          {"objectsPerTile", avgObjPerTile},
                          {"questsPerTile", questsPerTile}};
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [coord, b] : tiles) {
            arr.push_back({{"tile", {coord.first, coord.second}},
                            {"creatures", b.creatures},
                            {"objects", b.objects}});
        }
        j["perTile"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone density: %s\n", zoneDir.c_str());
    std::printf("  tiles      : %d\n", tileCount);
    std::printf("  totals     : %d creatures, %d objects, %d quests\n",
                totalCreat, totalObj, totalQ);
    std::printf("  per-tile   : %.2f creatures, %.2f objects, %.2f quests\n",
                avgCreatPerTile, avgObjPerTile, questsPerTile);
    std::printf("\n  Per-tile breakdown:\n");
    std::printf("    tile        creatures  objects\n");
    for (const auto& [coord, b] : tiles) {
        std::printf("    (%2d, %2d)         %5d    %5d\n",
                    coord.first, coord.second, b.creatures, b.objects);
    }
    return 0;
}

int handleInfoProjectDensity(int& i, int argc, char** argv) {
    // Project-wide content density. Sums creatures/objects/
    // quests across every zone, computes per-tile averages
    // both per-zone and project-wide. Helps spot zones that
    // are abnormally sparse vs the project median, and
    // surfaces the project's overall content footprint.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-density: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    struct ZRow {
        std::string name;
        int tileCount = 0;
        int creatures = 0, objects = 0, quests = 0;
    };
    std::vector<ZRow> rows;
    int gTiles = 0, gCreat = 0, gObj = 0, gQ = 0;
    for (const auto& zoneDir : zones) {
        ZRow r;
        r.name = fs::path(zoneDir).filename().string();
        wowee::editor::ZoneManifest zm;
        if (zm.load(zoneDir + "/zone.json")) {
            r.tileCount = static_cast<int>(zm.tiles.size());
        }
        wowee::editor::NpcSpawner sp;
        if (sp.loadFromFile(zoneDir + "/creatures.json")) {
            r.creatures = static_cast<int>(sp.spawnCount());
        }
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile(zoneDir + "/objects.json")) {
            r.objects = static_cast<int>(op.getObjects().size());
        }
        wowee::editor::QuestEditor qe;
        if (qe.loadFromFile(zoneDir + "/quests.json")) {
            r.quests = static_cast<int>(qe.questCount());
        }
        gTiles += r.tileCount;
        gCreat += r.creatures;
        gObj += r.objects;
        gQ += r.quests;
        rows.push_back(r);
    }
    double gAvgCreat = gTiles > 0 ? double(gCreat) / gTiles : 0.0;
    double gAvgObj = gTiles > 0 ? double(gObj) / gTiles : 0.0;
    double gAvgQ = gTiles > 0 ? double(gQ) / gTiles : 0.0;
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["totalTiles"] = gTiles;
        j["totals"] = {{"creatures", gCreat},
                        {"objects", gObj},
                        {"quests", gQ}};
        j["averages"] = {{"creaturesPerTile", gAvgCreat},
                          {"objectsPerTile", gAvgObj},
                          {"questsPerTile", gAvgQ}};
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& r : rows) {
            double zCreat = r.tileCount > 0 ? double(r.creatures) / r.tileCount : 0.0;
            double zObj = r.tileCount > 0 ? double(r.objects) / r.tileCount : 0.0;
            zarr.push_back({{"name", r.name},
                            {"tileCount", r.tileCount},
                            {"creatures", r.creatures},
                            {"objects", r.objects},
                            {"quests", r.quests},
                            {"creaturesPerTile", zCreat},
                            {"objectsPerTile", zObj}});
        }
        j["zones"] = zarr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project density: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu\n", zones.size());
    std::printf("  total tiles  : %d\n", gTiles);
    std::printf("  totals       : %d creatures, %d objects, %d quests\n",
                gCreat, gObj, gQ);
    std::printf("  per-tile     : %.2f creatures, %.2f objects, %.2f quests\n",
                gAvgCreat, gAvgObj, gAvgQ);
    std::printf("\n  zone                  tiles   creat   obj  quest   creat/tile  obj/tile\n");
    for (const auto& r : rows) {
        double zCreat = r.tileCount > 0 ? double(r.creatures) / r.tileCount : 0.0;
        double zObj = r.tileCount > 0 ? double(r.objects) / r.tileCount : 0.0;
        std::printf("  %-20s  %5d  %5d  %4d  %5d   %9.2f   %7.2f\n",
                    r.name.substr(0, 20).c_str(),
                    r.tileCount, r.creatures, r.objects, r.quests,
                    zCreat, zObj);
    }
    return 0;
}

}  // namespace

bool handleInfoDensity(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone-density") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneDensity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-density") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectDensity(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
