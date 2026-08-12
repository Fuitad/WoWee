#include "cli_zone_list.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include "pipeline/custom_zone_discovery.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleListZones(int& i, int argc, char** argv) {
    // Optional --json after the flag for machine-readable output.
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    auto zones = wowee::pipeline::CustomZoneDiscovery::scan({"custom_zones", "output"});
    if (jsonOut) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& z : zones) {
            nlohmann::json zoneObj;
            zoneObj["name"] = z.name;
            zoneObj["directory"] = z.directory;
            zoneObj["mapId"] = z.mapId;
            zoneObj["author"] = z.author;
            zoneObj["description"] = z.description;
            zoneObj["hasCreatures"] = z.hasCreatures;
            zoneObj["hasQuests"] = z.hasQuests;
            nlohmann::json tiles = nlohmann::json::array();
            for (const auto& t : z.tiles) tiles.push_back({t.first, t.second});
            zoneObj["tiles"] = tiles;
            j.push_back(std::move(zoneObj));
        }
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    if (zones.empty()) {
        std::printf("No custom zones found in custom_zones/ or output/\n");
    } else {
        std::printf("Custom zones found:\n");
        for (const auto& z : zones) {
            std::printf("  %s - %s%s%s\n", z.name.c_str(), z.directory.c_str(),
                     z.hasCreatures ? " [NPCs]" : "",
                     z.hasQuests ? " [Quests]" : "");
        }
    }
    return 0;
}

int handleZoneStats(int& i, int argc, char** argv) {
    // Multi-zone aggregator. Walks <projectDir> for every dir
    // with a zone.json and emits totals across the project:
    // tile counts, creature/object/quest counts, on-disk byte
    // sizes per format. Useful for content-pack release notes
    // and capacity planning.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "zone-stats: %s is not a directory\n", projectDir.c_str());
        return 1;
    }
    // Collect zone dirs.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    // Aggregate.
    struct Totals {
        int zoneCount = 0;
        int tileCount = 0;
        int creatures = 0, objects = 0, quests = 0;
        int hostileCreatures = 0;
        int chainedQuests = 0;
        uint64_t totalXp = 0;
        uint64_t whmBytes = 0, wotBytes = 0, wocBytes = 0;
        uint64_t womBytes = 0, wobBytes = 0;
        uint64_t pngBytes = 0, jsonBytes = 0;
        uint64_t otherBytes = 0;
    } T;
    T.zoneCount = static_cast<int>(zones.size());
    // Per-zone breakdown for the table view (kept short - not
    // every field, just the high-signal ones).
    struct ZoneRow {
        std::string name;
        int tiles = 0, creatures = 0, objects = 0, quests = 0;
        uint64_t bytes = 0;
    };
    std::vector<ZoneRow> rows;
    for (const auto& zoneDir : zones) {
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) continue;
        wowee::editor::NpcSpawner sp;
        sp.loadFromFile(zoneDir + "/creatures.json");
        wowee::editor::ObjectPlacer op;
        op.loadFromFile(zoneDir + "/objects.json");
        wowee::editor::QuestEditor qe;
        qe.loadFromFile(zoneDir + "/quests.json");
        ZoneRow row;
        row.name = zm.mapName.empty()
            ? fs::path(zoneDir).filename().string()
            : zm.mapName;
        row.tiles = static_cast<int>(zm.tiles.size());
        row.creatures = static_cast<int>(sp.spawnCount());
        row.objects = static_cast<int>(op.getObjects().size());
        row.quests = static_cast<int>(qe.questCount());
        T.tileCount += row.tiles;
        T.creatures += row.creatures;
        T.objects += row.objects;
        T.quests += row.quests;
        for (const auto& s : sp.getSpawns()) {
            if (s.hostile) T.hostileCreatures++;
        }
        for (const auto& q : qe.getQuests()) {
            if (q.nextQuestId != 0) T.chainedQuests++;
            T.totalXp += q.reward.xp;
        }
        // Walk on-disk files in the zone (recursive - sub-dirs
        // like data/ may hold sidecars). Bucket by extension.
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            uint64_t sz = e.file_size(ec);
            if (ec) continue;
            row.bytes += sz;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if      (ext == ".whm")  T.whmBytes  += sz;
            else if (ext == ".wot")  T.wotBytes  += sz;
            else if (ext == ".woc")  T.wocBytes  += sz;
            else if (ext == ".wom")  T.womBytes  += sz;
            else if (ext == ".wob")  T.wobBytes  += sz;
            else if (ext == ".png")  T.pngBytes  += sz;
            else if (ext == ".json") T.jsonBytes += sz;
            else                     T.otherBytes += sz;
        }
        rows.push_back(row);
    }
    uint64_t totalBytes = T.whmBytes + T.wotBytes + T.wocBytes +
                           T.womBytes + T.wobBytes + T.pngBytes +
                           T.jsonBytes + T.otherBytes;
    if (jsonOut) {
        nlohmann::json j;
        j["projectDir"] = projectDir;
        j["zoneCount"] = T.zoneCount;
        j["tileCount"] = T.tileCount;
        j["creatures"] = T.creatures;
        j["hostileCreatures"] = T.hostileCreatures;
        j["objects"] = T.objects;
        j["quests"] = T.quests;
        j["chainedQuests"] = T.chainedQuests;
        j["totalXp"] = T.totalXp;
        j["bytes"] = {
            {"whm", T.whmBytes},  {"wot", T.wotBytes},
            {"woc", T.wocBytes},  {"wom", T.womBytes},
            {"wob", T.wobBytes},  {"png", T.pngBytes},
            {"json", T.jsonBytes}, {"other", T.otherBytes},
            {"total", totalBytes}
        };
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& r : rows) {
            zarr.push_back({
                {"name", r.name}, {"tiles", r.tiles},
                {"creatures", r.creatures}, {"objects", r.objects},
                {"quests", r.quests}, {"bytes", r.bytes}
            });
        }
        j["zones"] = zarr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone stats: %s\n", projectDir.c_str());
    std::printf("  zones      : %d\n", T.zoneCount);
    std::printf("  tiles      : %d total\n", T.tileCount);
    std::printf("  creatures  : %d (%d hostile)\n",
                T.creatures, T.hostileCreatures);
    std::printf("  objects    : %d\n", T.objects);
    std::printf("  quests     : %d (%d chained, %llu total XP)\n",
                T.quests, T.chainedQuests,
                static_cast<unsigned long long>(T.totalXp));
    constexpr double kKB = 1024.0;
    std::printf("  bytes      : %.1f KB total\n", totalBytes / kKB);
    std::printf("    whm/wot  : %.1f KB / %.1f KB\n",
                T.whmBytes / kKB, T.wotBytes / kKB);
    std::printf("    woc      : %.1f KB\n", T.wocBytes / kKB);
    std::printf("    wom/wob  : %.1f KB / %.1f KB\n",
                T.womBytes / kKB, T.wobBytes / kKB);
    std::printf("    png/json : %.1f KB / %.1f KB\n",
                T.pngBytes / kKB, T.jsonBytes / kKB);
    if (T.otherBytes > 0) {
        std::printf("    other    : %.1f KB\n", T.otherBytes / kKB);
    }
    std::printf("\n  per-zone breakdown:\n");
    std::printf("    name                tiles  creat  obj  quest    bytes\n");
    for (const auto& r : rows) {
        std::printf("    %-18s  %5d  %5d  %3d  %5d  %7.1f KB\n",
                    r.name.substr(0, 18).c_str(),
                    r.tiles, r.creatures, r.objects, r.quests,
                    r.bytes / kKB);
    }
    return 0;
}


}  // namespace

bool handleZoneList(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-zones") == 0) {
        outRc = handleListZones(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--zone-stats") == 0 && i + 1 < argc) {
        outRc = handleZoneStats(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
