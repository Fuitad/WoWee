#include "cli_info_audio.hpp"

#include "zone_manifest.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
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

int handleInfoZoneAudio(int& i, int argc, char** argv) {
    // Print the audio configuration stored in zone.json:
    // music track, day/night ambience, volume sliders.
    // Useful for spot-checking that the zone has been wired
    // up to the right audio assets before bake/export.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-audio: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-audio: failed to parse %s\n",
            manifestPath.c_str());
        return 1;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["music"] = zm.musicTrack;
        j["ambienceDay"] = zm.ambienceDay;
        j["ambienceNight"] = zm.ambienceNight;
        j["musicVolume"] = zm.musicVolume;
        j["ambienceVolume"] = zm.ambienceVolume;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone audio: %s\n", zoneDir.c_str());
    std::printf("  music         : %s\n",
                zm.musicTrack.empty() ? "(none)" : zm.musicTrack.c_str());
    std::printf("  ambience day  : %s\n",
                zm.ambienceDay.empty() ? "(none)" : zm.ambienceDay.c_str());
    std::printf("  ambience night: %s\n",
                zm.ambienceNight.empty() ? "(none)" : zm.ambienceNight.c_str());
    std::printf("  music vol     : %.2f\n", zm.musicVolume);
    std::printf("  ambience vol  : %.2f\n", zm.ambienceVolume);
    return 0;
}

int handleInfoProjectAudio(int& i, int argc, char** argv) {
    // Project-wide audio rollup. Walks every zone in
    // <projectDir>, reads the audio fields out of zone.json,
    // emits a table showing which zones have music/ambience
    // configured. Useful for spotting zones still missing
    // audio assignment before a release pass.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-audio: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    struct Row {
        std::string name;
        std::string music;
        std::string ambDay;
        std::string ambNight;
        float musicVol, ambVol;
    };
    std::vector<Row> rows;
    int withMusic = 0, withAmbience = 0;
    for (const auto& zoneDir : zones) {
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) continue;
        Row r;
        r.name = fs::path(zoneDir).filename().string();
        r.music = zm.musicTrack;
        r.ambDay = zm.ambienceDay;
        r.ambNight = zm.ambienceNight;
        r.musicVol = zm.musicVolume;
        r.ambVol = zm.ambienceVolume;
        if (!r.music.empty()) withMusic++;
        if (!r.ambDay.empty() || !r.ambNight.empty()) withAmbience++;
        rows.push_back(r);
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["withMusic"] = withMusic;
        j["withAmbience"] = withAmbience;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({{"name", r.name},
                            {"music", r.music},
                            {"ambienceDay", r.ambDay},
                            {"ambienceNight", r.ambNight},
                            {"musicVolume", r.musicVol},
                            {"ambienceVolume", r.ambVol}});
        }
        j["zones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project audio: %s\n", projectDir.c_str());
    std::printf("  zones         : %zu\n", zones.size());
    std::printf("  with music    : %d\n", withMusic);
    std::printf("  with ambience : %d\n", withAmbience);
    std::printf("\n  zone                    music?  ambience?  m-vol  a-vol\n");
    for (const auto& r : rows) {
        std::string ambLabel = !r.ambDay.empty() ? r.ambDay :
                                !r.ambNight.empty() ? r.ambNight : "";
        std::printf("  %-22s  %-6s  %-9s  %5.2f  %5.2f\n",
                    r.name.substr(0, 22).c_str(),
                    r.music.empty() ? "no" : "yes",
                    ambLabel.empty() ? "no" : "yes",
                    r.musicVol, r.ambVol);
    }
    return 0;
}

}  // namespace

bool handleInfoAudio(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone-audio") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneAudio(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-audio") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectAudio(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
