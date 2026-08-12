#include "cli_zone_create.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include "terrain_editor.hpp"
#include "terrain_biomes.hpp"
#include "wowee_terrain.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleScaffoldZone(int& i, int argc, char** argv) {
    // Generate a minimal valid empty zone - useful for kickstarting
    // a new authoring session without needing to launch the GUI.
    std::string rawName = argv[++i];
    int sx = 32, sy = 32;
    if (i + 2 < argc) {
        int parsedX = std::atoi(argv[i + 1]);
        int parsedY = std::atoi(argv[i + 2]);
        if (parsedX >= 0 && parsedX <= 63 &&
            parsedY >= 0 && parsedY <= 63) {
            sx = parsedX; sy = parsedY;
            i += 2;
        }
    }
    // Slugify name to match unpackZone / server module rules.
    std::string slug;
    for (char c : rawName) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            slug += c;
        } else if (c == ' ') {
            slug += '_';
        }
    }
    if (slug.empty()) {
        std::fprintf(stderr, "--scaffold-zone: name '%s' has no valid characters\n",
                     rawName.c_str());
        return 1;
    }
    namespace fs = std::filesystem;
    std::string dir = "custom_zones/" + slug;
    if (fs::exists(dir)) {
        std::fprintf(stderr, "--scaffold-zone: directory already exists: %s\n",
                     dir.c_str());
        return 1;
    }
    fs::create_directories(dir);

    // Blank flat terrain at the requested tile.
    auto terrain = wowee::editor::TerrainEditor::createBlankTerrain(
        sx, sy, 100.0f, wowee::editor::Biome::Grassland);
    std::string base = dir + "/" + slug + "_" +
                       std::to_string(sx) + "_" + std::to_string(sy);
    wowee::editor::WoweeTerrain::exportOpen(terrain, base, sx, sy);

    // Minimal zone.json
    wowee::editor::ZoneManifest manifest;
    manifest.mapName = slug;
    manifest.displayName = rawName;
    manifest.mapId = 9000;
    manifest.baseHeight = 100.0f;
    manifest.tiles.push_back({sx, sy});
    manifest.save(dir + "/zone.json");

    std::printf("Scaffolded zone: %s\n", dir.c_str());
    std::printf("  tile     : (%d, %d)\n", sx, sy);
    std::printf("  files    : %s.wot, %s.whm, zone.json\n",
                slug.c_str(), slug.c_str());
    std::printf("  next step: run editor without args, then File → Open Zone\n");
    return 0;
}

int handleMvpZone(int& i, int argc, char** argv) {
    // Quick-start: scaffold + populate one of each content type
    // (1 creature, 1 object, 1 quest with objective + reward).
    // Useful for demos, screenshot bait, smoke tests of the
    // bake/validate pipeline. The zone goes from empty to
    // 'something to look at' in one command.
    std::string rawName = argv[++i];
    int sx = 32, sy = 32;
    if (i + 2 < argc) {
        int parsedX = std::atoi(argv[i + 1]);
        int parsedY = std::atoi(argv[i + 2]);
        if (parsedX >= 0 && parsedX <= 63 &&
            parsedY >= 0 && parsedY <= 63) {
            sx = parsedX; sy = parsedY;
            i += 2;
        }
    }
    // Reuse scaffold-zone's slug logic.
    std::string slug;
    for (char c : rawName) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') slug += c;
        else if (c == ' ') slug += '_';
    }
    if (slug.empty()) {
        std::fprintf(stderr,
            "mvp-zone: name '%s' has no valid characters\n",
            rawName.c_str());
        return 1;
    }
    namespace fs = std::filesystem;
    std::string dir = "custom_zones/" + slug;
    if (fs::exists(dir)) {
        std::fprintf(stderr,
            "mvp-zone: directory already exists: %s\n", dir.c_str());
        return 1;
    }
    fs::create_directories(dir);
    // Scaffold terrain.
    auto terrain = wowee::editor::TerrainEditor::createBlankTerrain(
        sx, sy, 100.0f, wowee::editor::Biome::Grassland);
    std::string base = dir + "/" + slug + "_" +
                        std::to_string(sx) + "_" + std::to_string(sy);
    wowee::editor::WoweeTerrain::exportOpen(terrain, base, sx, sy);
    // Manifest.
    wowee::editor::ZoneManifest zm;
    zm.mapName = slug;
    zm.displayName = rawName;
    zm.mapId = 9000;
    zm.baseHeight = 100.0f;
    zm.tiles.push_back({sx, sy});
    zm.hasCreatures = true;
    zm.save(dir + "/zone.json");
    // Position the demo content roughly centered in the tile.
    // Tile (32, 32) is the WoW map origin; tile centers are at
    // 533.33-yard intervals from there.
    float centerX = (32.0f - sy) * 533.33333f - 266.667f;
    float centerY = (32.0f - sx) * 533.33333f - 266.667f;
    float centerZ = 100.0f;
    // Demo creature.
    wowee::editor::NpcSpawner sp;
    wowee::editor::CreatureSpawn c;
    c.name = "Demo Wolf";
    c.position = {centerX, centerY, centerZ};
    c.level = 5;
    c.health = 100;
    c.minDamage = 5; c.maxDamage = 10;
    c.displayId = 11430;  // any valid id; renderer falls back if absent
    sp.getSpawns().push_back(c);
    sp.saveToFile(dir + "/creatures.json");
    // Demo object - a tree placement near the creature.
    wowee::editor::ObjectPlacer op;
    wowee::editor::PlacedObject po;
    po.type = wowee::editor::PlaceableType::M2;
    po.path = "World/Generic/Tree.m2";
    po.position = {centerX + 5.0f, centerY, centerZ};
    po.scale = 1.0f;
    op.getObjects().push_back(po);
    op.saveToFile(dir + "/objects.json");
    // Demo quest with objective + XP reward.
    wowee::editor::QuestEditor qe;
    wowee::editor::Quest q;
    q.title = "Welcome to " + rawName;
    q.requiredLevel = 1;
    q.questGiverNpcId = c.id;  // self-referential so refs check passes
    q.turnInNpcId = c.id;
    q.reward.xp = 100;
    wowee::editor::QuestObjective obj;
    obj.type = wowee::editor::QuestObjectiveType::KillCreature;
    obj.targetName = "Demo Wolf";
    obj.targetCount = 1;
    obj.description = "Slay the Demo Wolf";
    q.objectives.push_back(obj);
    qe.addQuest(q);
    qe.saveToFile(dir + "/quests.json");
    std::printf("Created demo zone: %s\n", dir.c_str());
    std::printf("  tile     : (%d, %d)\n", sx, sy);
    std::printf("  contents : 1 creature, 1 object, 1 quest (with objective + reward)\n");
    std::printf("  next     : wowee_editor --info-zone-tree %s\n", dir.c_str());
    (void)argc;
    return 0;
}

}  // namespace

bool handleZoneCreate(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--scaffold-zone") == 0 && i + 1 < argc) {
        outRc = handleScaffoldZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--mvp-zone") == 0 && i + 1 < argc) {
        outRc = handleMvpZone(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
