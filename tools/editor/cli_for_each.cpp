#include "cli_for_each.hpp"

#include "zone_manifest.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleForEachZone(int& i, int argc, char** argv) {
    // Batch runner: enumerates zones in <projectDir> and runs the
    // command after '--' for each one. '{}' in the command is
    // substituted with the zone path (find -exec convention).
    //
    //   wowee_editor --for-each-zone custom_zones -- (continued)
    //     wowee_editor --validate-all {}
    //
    // Returns the count of failed runs as the exit code (capped
    // at 255 so the shell can still see it).
    std::string projectDir = argv[++i];
    // The literal '--' separates the projectDir from the command.
    // Skip it; everything after is the command template.
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--") == 0) ++i;
    if (i + 1 >= argc) {
        std::fprintf(stderr,
            "for-each-zone: need command after '--'\n");
        return 1;
    }
    // Collect command tokens until end of argv. Don't try to be
    // clever about quoting — just escape each token for shell
    // safety using single quotes (' inside is escaped as '\\'').
    std::vector<std::string> cmdTokens;
    for (int k = i + 1; k < argc; ++k) cmdTokens.push_back(argv[k]);
    i = argc - 1;  // consume rest of argv
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr, "for-each-zone: %s is not a directory\n",
                     projectDir.c_str());
        return 1;
    }
    // Find every child dir that contains a zone.json — that's the
    // canonical 'is this a zone?' test the rest of the editor uses.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    if (zones.empty()) {
        std::fprintf(stderr, "for-each-zone: no zones found in %s\n",
                     projectDir.c_str());
        return 1;
    }
    auto shellEscape = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    };
    int failed = 0;
    for (const auto& zone : zones) {
        std::string cmd;
        for (size_t k = 0; k < cmdTokens.size(); ++k) {
            if (k > 0) cmd += " ";
            std::string token = cmdTokens[k];
            // Replace {} with zone path (every occurrence).
            size_t pos;
            while ((pos = token.find("{}")) != std::string::npos) {
                token.replace(pos, 2, zone);
            }
            cmd += shellEscape(token);
        }
        std::printf("[%s]\n", zone.c_str());
        // Flush before std::system so the header lands above the
        // child's output rather than after (parent stdout is line-
        // buffered, child writes go straight to the terminal).
        std::fflush(stdout);
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            failed++;
            std::fprintf(stderr,
                "for-each-zone: command exited %d for %s\n",
                rc, zone.c_str());
        }
    }
    std::printf("\nfor-each-zone: %zu zones, %d failed\n",
                zones.size(), failed);
    return failed > 255 ? 255 : failed;
}

int handleForEachTile(int& i, int argc, char** argv) {
    // Per-tile batch runner. --for-each-zone iterates zones in
    // a project; this iterates tiles within a zone. The '{}' in
    // the command template is replaced with the tile-base path
    // (zoneDir/mapName_TX_TY) — the form most tile-level
    // editor commands take.
    //
    //   wowee_editor --for-each-tile MyZone -- (continued)
    //     wowee_editor --build-woc {}
    //   wowee_editor --for-each-tile MyZone -- (continued)
    //     wowee_editor --validate-whm {}
    std::string zoneDir = argv[++i];
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--") == 0) ++i;
    if (i + 1 >= argc) {
        std::fprintf(stderr,
            "for-each-tile: need command after '--'\n");
        return 1;
    }
    std::vector<std::string> cmdTokens;
    for (int k = i + 1; k < argc; ++k) cmdTokens.push_back(argv[k]);
    i = argc - 1;
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "for-each-tile: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "for-each-tile: parse failed\n");
        return 1;
    }
    if (zm.tiles.empty()) {
        std::fprintf(stderr, "for-each-tile: zone has no tiles\n");
        return 1;
    }
    // Same shell-escape + cmd-substitution as --for-each-zone.
    auto shellEscape = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    };
    int failed = 0;
    // Sort tiles so order is deterministic across runs.
    auto tiles = zm.tiles;
    std::sort(tiles.begin(), tiles.end());
    for (const auto& [tx, ty] : tiles) {
        std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                                std::to_string(tx) + "_" + std::to_string(ty);
        std::string cmd;
        for (size_t k = 0; k < cmdTokens.size(); ++k) {
            if (k > 0) cmd += " ";
            std::string token = cmdTokens[k];
            size_t pos;
            while ((pos = token.find("{}")) != std::string::npos) {
                token.replace(pos, 2, tileBase);
            }
            cmd += shellEscape(token);
        }
        std::printf("[%s (%d, %d)]\n", tileBase.c_str(), tx, ty);
        std::fflush(stdout);
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            failed++;
            std::fprintf(stderr,
                "for-each-tile: command exited %d for (%d, %d)\n",
                rc, tx, ty);
        }
    }
    std::printf("\nfor-each-tile: %zu tiles, %d failed\n",
                tiles.size(), failed);
    return failed > 255 ? 255 : failed;
}


}  // namespace

bool handleForEach(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--for-each-zone") == 0 && i + 1 < argc) {
        outRc = handleForEachZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--for-each-tile") == 0 && i + 1 < argc) {
        outRc = handleForEachTile(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
