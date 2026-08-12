#include "cli_zone_info.hpp"

#include "zone_manifest.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoZone(int& i, int argc, char** argv) {
    // Parse a zone.json and print every manifest field. Useful when
    // diffing two zones or auditing the audio/flag setup before
    // packing into a WCP.
    std::string zonePath = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    // Accept either a directory or the zone.json itself.
    if (fs::is_directory(zonePath)) zonePath += "/zone.json";
    wowee::editor::ZoneManifest manifest;
    if (!manifest.load(zonePath)) {
        std::fprintf(stderr, "Failed to load zone.json: %s\n", zonePath.c_str());
        return 1;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = zonePath;
        j["mapName"] = manifest.mapName;
        j["displayName"] = manifest.displayName;
        j["mapId"] = manifest.mapId;
        j["biome"] = manifest.biome;
        j["baseHeight"] = manifest.baseHeight;
        j["hasCreatures"] = manifest.hasCreatures;
        j["description"] = manifest.description;
        nlohmann::json tilesArr = nlohmann::json::array();
        for (const auto& t : manifest.tiles)
            tilesArr.push_back({t.first, t.second});
        j["tiles"] = tilesArr;
        j["flags"] = {{"allowFlying", manifest.allowFlying},
                       {"pvpEnabled", manifest.pvpEnabled},
                       {"isIndoor", manifest.isIndoor},
                       {"isSanctuary", manifest.isSanctuary}};
        if (!manifest.musicTrack.empty() || !manifest.ambienceDay.empty()) {
            nlohmann::json audio;
            if (!manifest.musicTrack.empty()) {
                audio["music"] = manifest.musicTrack;
                audio["musicVolume"] = manifest.musicVolume;
            }
            if (!manifest.ambienceDay.empty()) {
                audio["ambienceDay"] = manifest.ambienceDay;
                audio["ambienceVolume"] = manifest.ambienceVolume;
            }
            if (!manifest.ambienceNight.empty())
                audio["ambienceNight"] = manifest.ambienceNight;
            j["audio"] = audio;
        }
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("zone.json: %s\n", zonePath.c_str());
    std::printf("  mapName     : %s\n", manifest.mapName.c_str());
    std::printf("  displayName : %s\n", manifest.displayName.c_str());
    std::printf("  mapId       : %u\n", manifest.mapId);
    std::printf("  biome       : %s\n", manifest.biome.c_str());
    std::printf("  baseHeight  : %.2f\n", manifest.baseHeight);
    std::printf("  hasCreatures: %s\n", manifest.hasCreatures ? "yes" : "no");
    std::printf("  description : %s\n", manifest.description.c_str());
    std::printf("  tiles       : %zu\n", manifest.tiles.size());
    for (const auto& t : manifest.tiles)
        std::printf("    (%d, %d)\n", t.first, t.second);
    std::printf("  flags       : %s%s%s%s\n",
                manifest.allowFlying  ? "fly " : "",
                manifest.pvpEnabled   ? "pvp " : "",
                manifest.isIndoor     ? "indoor " : "",
                manifest.isSanctuary  ? "sanctuary" : "");
    if (!manifest.musicTrack.empty() || !manifest.ambienceDay.empty()) {
        std::printf("  audio       :\n");
        if (!manifest.musicTrack.empty())
            std::printf("    music     : %s (vol=%.2f)\n",
                        manifest.musicTrack.c_str(), manifest.musicVolume);
        if (!manifest.ambienceDay.empty())
            std::printf("    ambience  : %s (vol=%.2f)\n",
                        manifest.ambienceDay.c_str(), manifest.ambienceVolume);
        if (!manifest.ambienceNight.empty())
            std::printf("    night amb : %s\n", manifest.ambienceNight.c_str());
    }
    return 0;
}

int handleInfoZoneOverview(int& i, int argc, char** argv) {
    // One-line compact zone summary. Where --info-zone dumps
    // every manifest field, this gives a tweet-length status:
    // tile count, biome, content counts, audio status. Easy
    // to grep through `--for-each-zone` output to spot
    // outliers.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-overview: %s has no zone.json\n",
            zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-overview: failed to parse %s\n",
            manifestPath.c_str());
        return 1;
    }
    // Cheap content counts via direct JSON parse - avoids
    // standing up the full editor classes for an overview.
    auto countArray = [&](const std::string& fname,
                            const std::string& key) {
        std::string p = zoneDir + "/" + fname;
        if (!fs::exists(p)) return size_t{0};
        try {
            nlohmann::json doc;
            std::ifstream in(p);
            in >> doc;
            if (doc.is_array()) return doc.size();
            if (doc.contains(key) && doc[key].is_array())
                return doc[key].size();
        } catch (...) {}
        return size_t{0};
    };
    size_t creatures = countArray("creatures.json", "creatures");
    size_t objects = countArray("objects.json", "objects");
    size_t quests = countArray("quests.json", "quests");
    size_t items = countArray("items.json", "items");
    bool hasAudio = !zm.musicTrack.empty() ||
                     !zm.ambienceDay.empty() ||
                     !zm.ambienceNight.empty();
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = fs::path(zoneDir).filename().string();
        j["mapName"] = zm.mapName;
        j["biome"] = zm.biome;
        j["tileCount"] = zm.tiles.size();
        j["counts"] = {{"creatures", creatures},
                        {"objects", objects},
                        {"quests", quests},
                        {"items", items}};
        j["hasAudio"] = hasAudio;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("%s [%s] %zut/%zuc/%zuo/%zuq/%zui%s\n",
                fs::path(zoneDir).filename().string().c_str(),
                zm.biome.empty() ? "?" : zm.biome.c_str(),
                zm.tiles.size(), creatures, objects, quests, items,
                hasAudio ? " +audio" : "");
    return 0;
}

int handleInfoProjectOverview(int& i, int argc, char** argv) {
    // Project-wide overview table: one row per zone with the
    // same compact stats as --info-zone-overview. Single-page
    // health check for "what's in this project."
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-overview: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    auto countArray = [](const std::string& path,
                          const std::string& key) {
        if (!fs::exists(path)) return size_t{0};
        try {
            nlohmann::json doc;
            std::ifstream in(path);
            in >> doc;
            if (doc.is_array()) return doc.size();
            if (doc.contains(key) && doc[key].is_array())
                return doc[key].size();
        } catch (...) {}
        return size_t{0};
    };
    struct Row {
        std::string name, biome;
        size_t tiles, creatures, objects, quests, items;
        bool hasAudio;
    };
    std::vector<Row> rows;
    size_t totC = 0, totO = 0, totQ = 0, totI = 0, totT = 0;
    int audioCount = 0;
    for (const auto& zoneDir : zones) {
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) continue;
        Row r;
        r.name = fs::path(zoneDir).filename().string();
        r.biome = zm.biome;
        r.tiles = zm.tiles.size();
        r.creatures = countArray(zoneDir + "/creatures.json", "creatures");
        r.objects = countArray(zoneDir + "/objects.json", "objects");
        r.quests = countArray(zoneDir + "/quests.json", "quests");
        r.items = countArray(zoneDir + "/items.json", "items");
        r.hasAudio = !zm.musicTrack.empty() ||
                      !zm.ambienceDay.empty() ||
                      !zm.ambienceNight.empty();
        if (r.hasAudio) audioCount++;
        totT += r.tiles;
        totC += r.creatures;
        totO += r.objects;
        totQ += r.quests;
        totI += r.items;
        rows.push_back(r);
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["totals"] = {{"tiles", totT}, {"creatures", totC},
                        {"objects", totO}, {"quests", totQ},
                        {"items", totI}, {"withAudio", audioCount}};
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({{"name", r.name},
                            {"biome", r.biome},
                            {"tiles", r.tiles},
                            {"creatures", r.creatures},
                            {"objects", r.objects},
                            {"quests", r.quests},
                            {"items", r.items},
                            {"hasAudio", r.hasAudio}});
        }
        j["zones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project overview: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu\n", zones.size());
    std::printf("  totals       : %zut, %zuc, %zuo, %zuq, %zui (%d with audio)\n",
                totT, totC, totO, totQ, totI, audioCount);
    std::printf("\n  zone                  biome      tiles  creat  obj  quest  items  audio\n");
    for (const auto& r : rows) {
        std::printf("  %-20s  %-10s %5zu  %5zu  %3zu  %5zu  %5zu  %s\n",
                    r.name.substr(0, 20).c_str(),
                    r.biome.empty() ? "?" : r.biome.substr(0, 10).c_str(),
                    r.tiles, r.creatures, r.objects, r.quests, r.items,
                    r.hasAudio ? "yes" : "no");
    }
    return 0;
}


}  // namespace

bool handleZoneInfo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone") == 0 && i + 1 < argc) {
        outRc = handleInfoZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-zone-overview") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneOverview(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-overview") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectOverview(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
