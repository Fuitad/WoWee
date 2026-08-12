#include "cli_random.hpp"
#include "cli_subprocess.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleRandomPopulateZone(int& i, int argc, char** argv) {
    // Randomly add creatures and/or objects to a zone for
    // playtest scenarios. Reads the zone manifest's tile
    // bounds so spawn positions stay inside the actual
    // playable area. Seeded LCG for reproducibility - same
    // seed always produces the same population.
    //
    // Flags:
    //   --seed N      (default 42)
    //   --creatures N (default 20)
    //   --objects N   (default 10)
    std::string zoneDir = argv[++i];
    uint32_t seed = 42;
    int creatureCount = 20;
    int objectCount = 10;
    while (i + 2 < argc && argv[i + 1][0] == '-') {
        std::string flag = argv[++i];
        if (flag == "--seed") {
            try { seed = static_cast<uint32_t>(std::stoul(argv[++i])); }
            catch (...) {}
        } else if (flag == "--creatures") {
            try { creatureCount = std::stoi(argv[++i]); }
            catch (...) {}
        } else if (flag == "--objects") {
            try { objectCount = std::stoi(argv[++i]); }
            catch (...) {}
        } else {
            std::fprintf(stderr,
                "random-populate-zone: unknown flag '%s'\n", flag.c_str());
            return 1;
        }
    }
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "random-populate-zone: %s has no zone.json\n",
            zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "random-populate-zone: failed to parse %s\n",
            manifestPath.c_str());
        return 1;
    }
    if (zm.tiles.empty()) {
        std::fprintf(stderr,
            "random-populate-zone: zone has no tiles to populate\n");
        return 1;
    }
    // Compute the world AABB the zone occupies so spawns land
    // inside it. Each tile is 533.33y; WoW grid centers tile
    // (32, 32) at world origin.
    constexpr float kTileSize = 533.33333f;
    int tMinX = 64, tMaxX = -1, tMinY = 64, tMaxY = -1;
    for (const auto& [tx, ty] : zm.tiles) {
        tMinX = std::min(tMinX, tx); tMaxX = std::max(tMaxX, tx);
        tMinY = std::min(tMinY, ty); tMaxY = std::max(tMaxY, ty);
    }
    float wMinX = (32.0f - tMaxY - 1) * kTileSize;
    float wMaxX = (32.0f - tMinY)     * kTileSize;
    float wMinY = (32.0f - tMaxX - 1) * kTileSize;
    float wMaxY = (32.0f - tMinX)     * kTileSize;
    float baseZ = zm.baseHeight;

    uint32_t rng = seed ? seed : 1u;
    auto next01 = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return (rng >> 8) / float(1 << 24);
    };
    auto rangeF = [&](float a, float b) { return a + next01() * (b - a); };
    auto rangeI = [&](int a, int b) {
        return a + static_cast<int>(next01() * (b - a + 1));
    };

    // Tiny bestiary so the random output reads as plausible
    // rather than "Creature1 / Creature2".
    static const std::vector<std::pair<const char*, uint32_t>> kRandomCreatures = {
        {"Wolf", 5},      {"Boar", 4},     {"Bear", 7},
        {"Spider", 3},    {"Bandit", 6},   {"Kobold", 4},
        {"Murloc", 5},    {"Skeleton", 5}, {"Wisp", 3},
        {"Goblin", 5},    {"Stag", 4},     {"Crab", 3},
    };
    static const std::vector<const char*> kRandomObjects = {
        "World/Generic/Tree01.wmo",
        "World/Generic/Boulder.wmo",
        "World/Generic/Bush.wmo",
        "World/Generic/Stump.wmo",
        "World/Generic/Mushroom.wmo",
    };

    // Creatures.
    wowee::editor::NpcSpawner spawner;
    std::string cpath = zoneDir + "/creatures.json";
    if (fs::exists(cpath)) spawner.loadFromFile(cpath);
    int placedCreatures = 0;
    for (int n = 0; n < creatureCount; ++n) {
        const auto& [name, baseLvl] = kRandomCreatures[
            rangeI(0, static_cast<int>(kRandomCreatures.size()) - 1)];
        wowee::editor::CreatureSpawn s;
        s.name = name;
        s.position.x = rangeF(wMinX, wMaxX);
        s.position.y = rangeF(wMinY, wMaxY);
        s.position.z = baseZ;
        int lvl = std::max(1, static_cast<int>(baseLvl) + rangeI(-1, 2));
        s.level = static_cast<uint32_t>(lvl);
        s.health = 50 + s.level * 10;
        s.orientation = rangeF(0.0f, 360.0f);
        spawner.placeCreature(s);
        placedCreatures++;
    }
    if (placedCreatures > 0) spawner.saveToFile(cpath);
    // Objects.
    wowee::editor::ObjectPlacer placer;
    std::string opath = zoneDir + "/objects.json";
    if (fs::exists(opath)) placer.loadFromFile(opath);
    int placedObjects = 0;
    // Push PlacedObject directly into the placer's vector so
    // we don't fight placeObject()'s early-return on empty
    // activePath_. uniqueId starts after any existing objects
    // to keep IDs collision-free.
    auto& objs = placer.getObjects();
    uint32_t maxUid = 0;
    for (const auto& o : objs) maxUid = std::max(maxUid, o.uniqueId);
    for (int n = 0; n < objectCount; ++n) {
        wowee::editor::PlacedObject o;
        o.path = kRandomObjects[
            rangeI(0, static_cast<int>(kRandomObjects.size()) - 1)];
        o.type = wowee::editor::PlaceableType::WMO;
        o.position.x = rangeF(wMinX, wMaxX);
        o.position.y = rangeF(wMinY, wMaxY);
        o.position.z = baseZ;
        o.rotation = glm::vec3(0.0f, rangeF(0.0f, 6.28f), 0.0f);
        o.scale = rangeF(0.8f, 1.4f);
        o.uniqueId = ++maxUid;
        o.nameId = 0;
        o.selected = false;
        objs.push_back(o);
        placedObjects++;
    }
    if (placedObjects > 0) placer.saveToFile(opath);
    std::printf("random-populate-zone: %s\n", zoneDir.c_str());
    std::printf("  seed       : %u\n", seed);
    std::printf("  zone bbox  : (%.0f, %.0f) - (%.0f, %.0f)\n",
                wMinX, wMinY, wMaxX, wMaxY);
    std::printf("  creatures  : %d added (%zu total)\n",
                placedCreatures, spawner.spawnCount());
    std::printf("  objects    : %d added (%zu total)\n",
                placedObjects, placer.getObjects().size());
    return 0;
}

int handleRandomPopulateItems(int& i, int argc, char** argv) {
    // Seeded random items.json populator. Pulls a base name
    // and a noun from inline word lists, picks a quality up
    // to maxQuality, randomizes itemLevel and stack size
    // around plausible defaults. Useful for playtest loot
    // tables that need bulk content without hand-typing each
    // entry.
    //
    // Flags: --seed N (default 7), --count N (default 30),
    //        --max-quality Q (default 4 = epic; 0..6 valid).
    std::string zoneDir = argv[++i];
    uint32_t seed = 7;
    int count = 30;
    int maxQuality = 4;
    while (i + 2 < argc && argv[i + 1][0] == '-') {
        std::string flag = argv[++i];
        if (flag == "--seed") {
            try { seed = static_cast<uint32_t>(std::stoul(argv[++i])); }
            catch (...) {}
        } else if (flag == "--count") {
            try { count = std::stoi(argv[++i]); } catch (...) {}
        } else if (flag == "--max-quality") {
            try { maxQuality = std::stoi(argv[++i]); } catch (...) {}
        } else {
            std::fprintf(stderr,
                "random-populate-items: unknown flag '%s'\n", flag.c_str());
            return 1;
        }
    }
    if (maxQuality < 0 || maxQuality > 6) maxQuality = 4;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "random-populate-items: %s has no zone.json\n",
            zoneDir.c_str());
        return 1;
    }
    uint32_t rng = seed ? seed : 1u;
    auto next01 = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return (rng >> 8) / float(1 << 24);
    };
    auto rangeI = [&](int a, int b) {
        return a + static_cast<int>(next01() * (b - a + 1));
    };
    // Inline name lexicon. {prefix, noun} → "Glowing Sword".
    // Quality ramps prefix selection; rare+ items get fancier
    // adjectives.
    static const std::vector<const char*> kPrefixes[5] = {
        {"Worn", "Tattered", "Cracked", "Dented", "Faded"},      // poor
        {"Common", "Plain", "Basic", "Simple", "Standard"},      // common
        {"Sharp", "Sturdy", "Polished", "Reinforced", "Fine"},   // uncommon
        {"Glowing", "Runed", "Enchanted", "Storm", "Mystic"},    // rare
        {"Ancient", "Eternal", "Heroic", "Vengeful", "Soul"},    // epic
    };
    static const std::vector<const char*> kNouns = {
        "Sword", "Mace", "Axe", "Dagger", "Staff",
        "Bow", "Helm", "Cuirass", "Greaves", "Gauntlets",
        "Ring", "Amulet", "Cloak", "Belt", "Boots",
        "Potion", "Scroll", "Tome", "Wand", "Shield",
    };
    // Open the items doc.
    std::string ipath = zoneDir + "/items.json";
    nlohmann::json doc = nlohmann::json::object({{"items",
                          nlohmann::json::array()}});
    if (fs::exists(ipath)) {
        std::ifstream in(ipath);
        try { in >> doc; } catch (...) {}
        if (!doc.contains("items") || !doc["items"].is_array()) {
            doc["items"] = nlohmann::json::array();
        }
    }
    std::set<uint32_t> used;
    for (const auto& it : doc["items"]) {
        if (it.contains("id") && it["id"].is_number_unsigned())
            used.insert(it["id"].get<uint32_t>());
    }
    int added = 0;
    for (int n = 0; n < count; ++n) {
        int q = std::min(maxQuality, rangeI(0, maxQuality));
        int qBucket = std::min(q, 4);
        const auto& prefixes = kPrefixes[qBucket];
        std::string name = prefixes[rangeI(0,
            static_cast<int>(prefixes.size()) - 1)];
        name += " ";
        name += kNouns[rangeI(0, static_cast<int>(kNouns.size()) - 1)];
        uint32_t id = 1;
        while (used.count(id)) ++id;
        used.insert(id);
        int ilvl = std::max(1,
            rangeI(1, 5) + q * 12 + rangeI(-3, 3));
        doc["items"].push_back({
            {"id", id},
            {"name", name},
            {"quality", q},
            {"displayId", rangeI(1000, 9999)},
            {"itemLevel", ilvl},
            {"stackable", q == 0 || q == 1 ? rangeI(1, 20) : 1},
        });
        added++;
    }
    std::ofstream out(ipath);
    if (!out) {
        std::fprintf(stderr,
            "random-populate-items: failed to write %s\n",
            ipath.c_str());
        return 1;
    }
    out << doc.dump(2);
    out.close();
    std::printf("random-populate-items: %s\n", ipath.c_str());
    std::printf("  seed         : %u\n", seed);
    std::printf("  added        : %d\n", added);
    std::printf("  total items  : %zu\n", doc["items"].size());
    std::printf("  max quality  : %d\n", maxQuality);
    return 0;
}

int handleGenRandomZone(int& i, int argc, char** argv) {
    // End-to-end random zone generator. Composes scaffold-zone
    // + random-populate-zone + random-populate-items in one
    // invocation. Useful for "I just want a complete test
    // zone, don't make me chain three commands."
    //
    // Args:
    //   <name>                   required (becomes the slug)
    //   [tx ty]                  optional (default 32 32)
    //   --seed N                 default 42
    //   --creatures N            default 20
    //   --objects N              default 10
    //   --items N                default 25
    //
    // Honors --random-populate-zone's hard caps + the existing
    // scaffold-zone validation. Sub-commands' output streams
    // through.
    std::string name = argv[++i];
    int tx = 32, ty = 32;
    uint32_t seed = 42;
    int creatures = 20, objects = 10, items = 25;
    // Optional positional tx/ty (must be before any --flags).
    if (i + 2 < argc && argv[i + 1][0] != '-' && argv[i + 2][0] != '-') {
        try { tx = std::stoi(argv[++i]); ty = std::stoi(argv[++i]); }
        catch (...) {}
    }
    while (i + 2 < argc && argv[i + 1][0] == '-') {
        std::string flag = argv[++i];
        if (flag == "--seed")
            try { seed = static_cast<uint32_t>(std::stoul(argv[++i])); } catch (...) {}
        else if (flag == "--creatures")
            try { creatures = std::stoi(argv[++i]); } catch (...) {}
        else if (flag == "--objects")
            try { objects = std::stoi(argv[++i]); } catch (...) {}
        else if (flag == "--items")
            try { items = std::stoi(argv[++i]); } catch (...) {}
        else {
            std::fprintf(stderr,
                "gen-random-zone: unknown flag '%s'\n", flag.c_str());
            return 1;
        }
    }
    // Slug-clean the name to match scaffold-zone's expectations.
    std::string slug;
    for (char c : name) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            slug += c;
        } else if (c == ' ') {
            slug += '_';
        }
    }
    if (slug.empty()) {
        std::fprintf(stderr,
            "gen-random-zone: name '%s' has no valid characters\n",
            name.c_str());
        return 1;
    }
    std::string self = argv[0];
    namespace fs = std::filesystem;
    std::string zoneDir = "custom_zones/" + slug;
    std::printf("gen-random-zone: %s (tile %d, %d)\n",
                slug.c_str(), tx, ty);
    std::fflush(stdout);
    // 1. Scaffold.
    int rc = wowee::editor::cli::runChild(self,
        {"--scaffold-zone", slug, std::to_string(tx), std::to_string(ty)});
    if (rc != 0) {
        std::fprintf(stderr,
            "gen-random-zone: scaffold step failed (rc=%d)\n", rc);
        return 1;
    }
    // 2. Random populate.
    std::fflush(stdout);
    rc = wowee::editor::cli::runChild(self, {
        "--random-populate-zone", zoneDir,
        "--seed", std::to_string(seed),
        "--creatures", std::to_string(creatures),
        "--objects", std::to_string(objects)});
    if (rc != 0) {
        std::fprintf(stderr,
            "gen-random-zone: populate step failed (rc=%d)\n", rc);
        return 1;
    }
    // 3. Random items.
    std::fflush(stdout);
    rc = wowee::editor::cli::runChild(self, {
        "--random-populate-items", zoneDir,
        "--seed", std::to_string(seed + 1),
        "--count", std::to_string(items)});
    if (rc != 0) {
        std::fprintf(stderr,
            "gen-random-zone: items step failed (rc=%d)\n", rc);
        return 1;
    }
    std::printf("\ngen-random-zone: complete\n");
    std::printf("  zone dir  : %s\n", zoneDir.c_str());
    std::printf("  creatures : %d\n", creatures);
    std::printf("  objects   : %d\n", objects);
    std::printf("  items     : %d\n", items);
    return 0;
}

int handleGenRandomProject(int& i, int argc, char** argv) {
    // Project-wide companion: spawn N random zones in one
    // pass. Names default to "Zone1, Zone2..."; tile
    // coordinates step from (32, 32) outward in a simple
    // raster so they don't overlap. Each zone gets a unique
    // sub-seed so its random content differs.
    int count = 0;
    try { count = std::stoi(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-random-project: <count> must be an integer\n");
        return 1;
    }
    if (count < 1 || count > 100) {
        std::fprintf(stderr,
            "gen-random-project: count %d out of range (1..100)\n",
            count);
        return 1;
    }
    std::string prefix = "Zone";
    uint32_t seed = 100;
    int creatures = 20, objects = 10, items = 25;
    while (i + 2 < argc && argv[i + 1][0] == '-') {
        std::string flag = argv[++i];
        if (flag == "--prefix") prefix = argv[++i];
        else if (flag == "--seed")
            try { seed = static_cast<uint32_t>(std::stoul(argv[++i])); } catch (...) {}
        else if (flag == "--creatures")
            try { creatures = std::stoi(argv[++i]); } catch (...) {}
        else if (flag == "--objects")
            try { objects = std::stoi(argv[++i]); } catch (...) {}
        else if (flag == "--items")
            try { items = std::stoi(argv[++i]); } catch (...) {}
        else {
            std::fprintf(stderr,
                "gen-random-project: unknown flag '%s'\n", flag.c_str());
            return 1;
        }
    }
    std::string self = argv[0];
    int produced = 0, failed = 0;
    std::printf("gen-random-project: %d zone(s) with prefix '%s'\n",
                count, prefix.c_str());
    for (int n = 0; n < count; ++n) {
        // Step outward from (32, 32) in a small raster so the
        // tiles don't coincide. (-1,0,1,...) X (-1,0,1,...).
        int side = 1;
        while ((2 * side + 1) * (2 * side + 1) <= n) side++;
        int idx = n;
        int dx = idx % (2 * side + 1) - side;
        int dy = (idx / (2 * side + 1)) - side;
        int tx = std::max(0, std::min(63, 32 + dx));
        int ty = std::max(0, std::min(63, 32 + dy));
        std::string zoneName = prefix + std::to_string(n + 1);
        std::printf("\n=== %s (tile %d, %d) ===\n",
                    zoneName.c_str(), tx, ty);
        std::fflush(stdout);
        int rc = wowee::editor::cli::runChild(self, {
            "--gen-random-zone", zoneName,
            std::to_string(tx), std::to_string(ty),
            "--seed", std::to_string(seed + n),
            "--creatures", std::to_string(creatures),
            "--objects", std::to_string(objects),
            "--items", std::to_string(items)});
        if (rc == 0) produced++;
        else failed++;
    }
    std::printf("\n--- summary ---\n");
    std::printf("  produced : %d\n", produced);
    std::printf("  failed   : %d\n", failed);
    return failed == 0 ? 0 : 1;
}


}  // namespace

bool handleRandom(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--random-populate-zone") == 0 && i + 1 < argc) {
        outRc = handleRandomPopulateZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--random-populate-items") == 0 && i + 1 < argc) {
        outRc = handleRandomPopulateItems(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-random-zone") == 0 && i + 1 < argc) {
        outRc = handleGenRandomZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-random-project") == 0 && i + 1 < argc) {
        outRc = handleGenRandomProject(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
