#include "cli_repair.hpp"
#include "cli_subprocess.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleRepairZone(int& i, int argc, char** argv) {
    // Auto-fix the common manifest-vs-disk drift issues that
    // accumulate when a zone is hand-edited or partially copied:
    //   - WHM/WOT files exist on disk but tile not in manifest
    //     -> add to tiles
    //   - manifest hasCreatures=false but creatures.json exists
    //     and is non-empty -> set true
    //   - manifest hasCreatures=true but no creatures.json or
    //     empty -> clear false
    //
    // Tiles in manifest with NO disk files are NOT auto-removed
    // (they may indicate work-in-progress); they're warned about
    // so the user can decide.
    //
    // --dry-run flag previews changes without writing.
    std::string zoneDir = argv[++i];
    bool dryRun = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--dry-run") == 0) {
        dryRun = true; i++;
    }
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "repair-zone: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "repair-zone: parse failed\n");
        return 1;
    }
    int fixes = 0, warnings = 0;
    // Pass 1: scan disk for WHM files matching mapName_X_Y.whm
    // pattern. Match against manifest tiles. Anything on disk
    // but missing from manifest gets queued for addition.
    std::set<std::pair<int,int>> manifestTiles(
        zm.tiles.begin(), zm.tiles.end());
    std::set<std::pair<int,int>> diskTiles;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string name = e.path().filename().string();
        if (e.path().extension() != ".whm") continue;
        // Expect "<mapName>_TX_TY.whm". Parse out the two
        // integers between the last two underscores.
        std::string stem = name.substr(0, name.size() - 4);
        std::string prefix = zm.mapName + "_";
        if (stem.size() <= prefix.size() ||
            stem.substr(0, prefix.size()) != prefix) {
            continue;  // doesn't match map slug
        }
        std::string coords = stem.substr(prefix.size());
        auto under = coords.find('_');
        if (under == std::string::npos) continue;
        try {
            int tx = std::stoi(coords.substr(0, under));
            int ty = std::stoi(coords.substr(under + 1));
            diskTiles.insert({tx, ty});
        } catch (...) {}
    }
    // Tiles on disk but not in manifest -> add.
    std::vector<std::pair<int,int>> toAdd;
    for (const auto& d : diskTiles) {
        if (manifestTiles.count(d) == 0) toAdd.push_back(d);
    }
    for (const auto& [tx, ty] : toAdd) {
        std::printf("  %s tile (%d, %d) to manifest\n",
                    dryRun ? "would add" : "added", tx, ty);
        if (!dryRun) zm.tiles.push_back({tx, ty});
        fixes++;
    }
    // Tiles in manifest but no .whm on disk -> warn (not auto-removed).
    for (const auto& m : manifestTiles) {
        if (diskTiles.count(m) == 0) {
            std::printf("  WARN: tile (%d, %d) in manifest but no %s_%d_%d.whm on disk\n",
                        m.first, m.second, zm.mapName.c_str(),
                        m.first, m.second);
            warnings++;
        }
    }
    // hasCreatures flag sync.
    bool creaturesPresent = false;
    wowee::editor::NpcSpawner sp;
    if (sp.loadFromFile(zoneDir + "/creatures.json") &&
        sp.spawnCount() > 0) {
        creaturesPresent = true;
    }
    if (zm.hasCreatures != creaturesPresent) {
        std::printf("  %s hasCreatures: %s -> %s\n",
                    dryRun ? "would set" : "set",
                    zm.hasCreatures ? "true" : "false",
                    creaturesPresent ? "true" : "false");
        if (!dryRun) zm.hasCreatures = creaturesPresent;
        fixes++;
    }
    if (!dryRun && fixes > 0) {
        if (!zm.save(manifestPath)) {
            std::fprintf(stderr,
                "repair-zone: failed to write %s\n", manifestPath.c_str());
            return 1;
        }
    }
    std::printf("\nrepair-zone: %s%s\n",
                zoneDir.c_str(), dryRun ? " (dry-run)" : "");
    std::printf("  fixes    : %d\n", fixes);
    std::printf("  warnings : %d (manual decision needed)\n", warnings);
    if (dryRun && fixes > 0) {
        std::printf("  re-run without --dry-run to apply\n");
    }
    return 0;
}

int handleRepairProject(int& i, int argc, char** argv) {
    // Project-wide wrapper around --repair-zone. Spawns the
    // binary per-zone so each zone's full repair report
    // streams through, then aggregates a final tally. Honors
    // --dry-run for safe previews.
    std::string projectDir = argv[++i];
    bool dryRun = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--dry-run") == 0) {
        dryRun = true; i++;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "repair-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    std::string self = argv[0];
    int totalFailed = 0;
    std::printf("repair-project: %s%s\n",
                projectDir.c_str(), dryRun ? " (dry-run)" : "");
    std::printf("  zones : %zu\n", zones.size());
    for (const auto& zoneDir : zones) {
        std::printf("\n--- %s ---\n",
                    fs::path(zoneDir).filename().string().c_str());
        // Flush so the section marker lands before the spawned
        // child's stdout — exec inherits FDs but each process has
        // its own buffer.
        std::fflush(stdout);
        std::vector<std::string> args = {"--repair-zone", zoneDir};
        if (dryRun) args.push_back("--dry-run");
        int rc = wowee::editor::cli::runChild(self, args);
        if (rc != 0) totalFailed++;
    }
    std::printf("\n--- summary ---\n");
    std::printf("  zones processed : %zu\n", zones.size());
    std::printf("  failures        : %d\n", totalFailed);
    if (dryRun) {
        std::printf("  re-run without --dry-run to apply changes\n");
    }
    return totalFailed == 0 ? 0 : 1;
}


}  // namespace

bool handleRepair(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--repair-zone") == 0 && i + 1 < argc) {
        outRc = handleRepairZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--repair-project") == 0 && i + 1 < argc) {
        outRc = handleRepairProject(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
