#include "cli_zone_data.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/adt_loader.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleFixZone(int& i, int argc, char** argv) {
    // Re-parse + re-save every JSON/binary file in a zone to apply
    // the editor's load-time scrubs and save-time caps. Useful when
    // an old zone was created before recent hardening - running
    // this once cleans up NaN/oversize fields without touching
    // the editor GUI.
    std::string zoneDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr, "fix-zone: %s does not exist\n", zoneDir.c_str());
        return 1;
    }
    int touched = 0;
    // zone.json
    {
        wowee::editor::ZoneManifest m;
        std::string p = zoneDir + "/zone.json";
        if (fs::exists(p) && m.load(p) && m.save(p)) touched++;
    }
    // creatures.json
    {
        wowee::editor::NpcSpawner sp;
        std::string p = zoneDir + "/creatures.json";
        if (fs::exists(p) && sp.loadFromFile(p) && sp.saveToFile(p)) touched++;
    }
    // objects.json
    {
        wowee::editor::ObjectPlacer op;
        std::string p = zoneDir + "/objects.json";
        if (fs::exists(p) && op.loadFromFile(p) && op.saveToFile(p)) touched++;
    }
    // quests.json
    {
        wowee::editor::QuestEditor qe;
        std::string p = zoneDir + "/quests.json";
        if (fs::exists(p) && qe.loadFromFile(p) && qe.saveToFile(p)) touched++;
    }
    // WHM/WOT pairs and WoB files would need full pipeline access;
    // skip them - the editor opens them on next zone load anyway,
    // and the load-time scrubs run then.
    std::printf("fix-zone: cleaned %d files in %s\n", touched, zoneDir.c_str());
    return 0;
}

int handleRegenCollision(int& i, int argc, char** argv) {
    // Find all WHM/WOT pairs under a zone dir and rebuild WOC for each.
    // Useful after sculpting changes when you want to re-derive
    // collision in batch instead of one tile at a time.
    std::string zoneDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr, "regen-collision: %s does not exist\n",
                     zoneDir.c_str());
        return 1;
    }
    int rebuilt = 0, failed = 0;
    for (auto& entry : fs::recursive_directory_iterator(zoneDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".whm") continue;
        std::string base = entry.path().string();
        base = base.substr(0, base.size() - 4); // strip .whm
        wowee::pipeline::ADTTerrain terrain;
        if (!wowee::pipeline::WoweeTerrainLoader::load(base, terrain)) {
            std::fprintf(stderr, "  FAILED to load: %s\n", base.c_str());
            failed++;
            continue;
        }
        auto col = wowee::pipeline::WoweeCollisionBuilder::fromTerrain(terrain);
        std::string outPath = base + ".woc";
        if (wowee::pipeline::WoweeCollisionBuilder::save(col, outPath)) {
            std::printf("  WOC rebuilt: %s (%zu triangles)\n",
                        outPath.c_str(), col.triangles.size());
            rebuilt++;
        } else {
            std::fprintf(stderr, "  FAILED to save: %s\n", outPath.c_str());
            failed++;
        }
    }
    std::printf("regen-collision: %d rebuilt, %d failed\n", rebuilt, failed);
    return failed > 0 ? 1 : 0;
}

int handleBuildWoc(int& i, int argc, char** argv) {
    // Generate a WOC collision mesh from a WHM/WOT terrain pair.
    // Uses terrain triangles only (no WMO overlays); useful as a
    // first-pass collision build before the editor adds buildings.
    std::string base = argv[++i];
    for (const char* ext : {".wot", ".whm", ".woc"}) {
        if (base.size() >= 4 && base.substr(base.size() - 4) == ext) {
            base = base.substr(0, base.size() - 4);
            break;
        }
    }
    if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) {
        std::fprintf(stderr, "WOT/WHM not found at base: %s\n", base.c_str());
        return 1;
    }
    wowee::pipeline::ADTTerrain terrain;
    if (!wowee::pipeline::WoweeTerrainLoader::load(base, terrain)) {
        std::fprintf(stderr, "Failed to load terrain: %s\n", base.c_str());
        return 1;
    }
    auto col = wowee::pipeline::WoweeCollisionBuilder::fromTerrain(terrain);
    std::string outPath = base + ".woc";
    if (!wowee::pipeline::WoweeCollisionBuilder::save(col, outPath)) {
        std::fprintf(stderr, "WOC save failed: %s\n", outPath.c_str());
        return 1;
    }
    std::printf("WOC built: %s (%zu triangles, %zu walkable, %zu steep)\n",
                outPath.c_str(),
                col.triangles.size(), col.walkableCount(), col.steepCount());
    return 0;
}


}  // namespace

bool handleZoneData(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--fix-zone") == 0 && i + 1 < argc) {
        outRc = handleFixZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--regen-collision") == 0 && i + 1 < argc) {
        outRc = handleRegenCollision(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--build-woc") == 0 && i + 1 < argc) {
        outRc = handleBuildWoc(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
