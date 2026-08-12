#include "cli_project_actions.hpp"

#include "content_pack.hpp"
#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleCopyProject(int& i, int argc, char** argv) {
    // Recursively copy an entire project tree. Refuses to
    // overwrite an existing destination so a typo doesn't
    // silently merge into the wrong project.
    std::string fromDir = argv[++i];
    std::string toDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(fromDir) || !fs::is_directory(fromDir)) {
        std::fprintf(stderr,
            "copy-project: %s is not a directory\n", fromDir.c_str());
        return 1;
    }
    if (fs::exists(toDir)) {
        std::fprintf(stderr,
            "copy-project: destination %s already exists "
            "(delete it first if intentional)\n", toDir.c_str());
        return 1;
    }
    std::error_code ec;
    fs::copy(fromDir, toDir,
              fs::copy_options::recursive | fs::copy_options::copy_symlinks,
              ec);
    if (ec) {
        std::fprintf(stderr,
            "copy-project: copy failed (%s)\n", ec.message().c_str());
        return 1;
    }
    // Count what was copied for the report.
    int zoneCount = 0, fileCount = 0;
    uint64_t totalBytes = 0;
    for (const auto& entry : fs::directory_iterator(toDir, ec)) {
        if (entry.is_directory() &&
            fs::exists(entry.path() / "zone.json")) zoneCount++;
    }
    for (const auto& e : fs::recursive_directory_iterator(toDir, ec)) {
        if (e.is_regular_file()) {
            fileCount++;
            totalBytes += e.file_size(ec);
        }
    }
    std::printf("Copied %s -> %s\n", fromDir.c_str(), toDir.c_str());
    std::printf("  zones        : %d\n", zoneCount);
    std::printf("  files        : %d\n", fileCount);
    std::printf("  total bytes  : %llu (%.1f MB)\n",
                static_cast<unsigned long long>(totalBytes),
                totalBytes / (1024.0 * 1024.0));
    return 0;
}

int handleZoneSummary(int& i, int argc, char** argv) {
    // One-shot zone overview: validate + creature/object/quest counts.
    // Collapses the most common multi-step inspection into a single
    // command; useful for CI reports and quick sanity checks.
    std::string zoneDir = argv[++i];
    // Optional --json after the dir for machine-readable output.
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr, "zone-summary: %s does not exist\n", zoneDir.c_str());
        return 1;
    }
    auto v = wowee::editor::ContentPacker::validateZone(zoneDir);

    // Read creature/object/quest data once so both human and JSON
    // outputs share the same numbers.
    int creatureTotal = 0, hostile = 0, qg = 0, vendor = 0;
    int objectTotal = 0, m2Count = 0, wmoCount = 0;
    int questTotal = 0, chainWarnings = 0;
    std::string creaturesPath = zoneDir + "/creatures.json";
    if (fs::exists(creaturesPath)) {
        wowee::editor::NpcSpawner sp;
        if (sp.loadFromFile(creaturesPath)) {
            creatureTotal = static_cast<int>(sp.getSpawns().size());
            for (const auto& s : sp.getSpawns()) {
                if (s.hostile) hostile++;
                if (s.questgiver) qg++;
                if (s.vendor) vendor++;
            }
        }
    }
    std::string objectsPath = zoneDir + "/objects.json";
    if (fs::exists(objectsPath)) {
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile(objectsPath)) {
            objectTotal = static_cast<int>(op.getObjects().size());
            for (const auto& o : op.getObjects()) {
                if (o.type == wowee::editor::PlaceableType::M2) m2Count++;
                else wmoCount++;
            }
        }
    }
    std::string questsPath = zoneDir + "/quests.json";
    if (fs::exists(questsPath)) {
        wowee::editor::QuestEditor qe;
        if (qe.loadFromFile(questsPath)) {
            questTotal = static_cast<int>(qe.getQuests().size());
            std::vector<std::string> errors;
            qe.validateChains(errors);
            chainWarnings = static_cast<int>(errors.size());
        }
    }

    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["score"] = v.openFormatScore();
        j["maxScore"] = 7;
        j["formats"] = v.summary();
        j["counts"] = {
            {"wot", v.wotCount}, {"whm", v.whmCount},
            {"wom", v.womCount}, {"wob", v.wobCount},
            {"woc", v.wocCount}, {"png", v.pngCount},
        };
        j["creatures"] = {
            {"total", creatureTotal},
            {"hostile", hostile},
            {"questgiver", qg},
            {"vendor", vendor},
        };
        j["objects"] = {
            {"total", objectTotal},
            {"m2", m2Count},
            {"wmo", wmoCount},
        };
        j["quests"] = {
            {"total", questTotal},
            {"chainWarnings", chainWarnings},
        };
        std::printf("%s\n", j.dump(2).c_str());
        return v.openFormatScore() == 7 ? 0 : 1;
    }
    std::printf("Zone: %s\n", zoneDir.c_str());
    std::printf("  open formats : %d/7  (%s)\n",
                v.openFormatScore(), v.summary().c_str());
    std::printf("  WOT/WHM      : %d/%d   WOM: %d   WOB: %d   WOC: %d   PNG: %d\n",
                v.wotCount, v.whmCount, v.womCount, v.wobCount,
                v.wocCount, v.pngCount);
    if (creatureTotal > 0) {
        std::printf("  creatures    : %d  (%d hostile, %d quest, %d vendor)\n",
                    creatureTotal, hostile, qg, vendor);
    }
    if (objectTotal > 0) {
        std::printf("  objects      : %d  (%d M2, %d WMO)\n",
                    objectTotal, m2Count, wmoCount);
    }
    if (questTotal > 0) {
        std::printf("  quests       : %d  (%d chain warnings)\n",
                    questTotal, chainWarnings);
    }
    return v.openFormatScore() == 7 ? 0 : 1;
}

int handleBenchBakeProject(int& i, int argc, char** argv) {
    // Time WHM/WOT load (the dominant cost in --bake-zone-glb/obj/
    // stl) per zone. The actual write side adds ~constant cost
    // proportional to vertex count, so load time is a strong
    // proxy. Useful for tracking 'has my latest geometry change
    // made baking 3× slower?' across releases.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "bench-bake-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    struct Timing {
        std::string name;
        int tiles;
        double loadMs;
        int chunks;
    };
    std::vector<Timing> timings;
    double totalMs = 0;
    for (const auto& zoneDir : zones) {
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) continue;
        Timing t{fs::path(zoneDir).filename().string(), 0, 0.0, 0};
        auto t0 = std::chrono::steady_clock::now();
        for (const auto& [tx, ty] : zm.tiles) {
            std::string base = zoneDir + "/" + zm.mapName + "_" +
                                std::to_string(tx) + "_" + std::to_string(ty);
            if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) continue;
            wowee::pipeline::ADTTerrain terrain;
            wowee::pipeline::WoweeTerrainLoader::load(base, terrain);
            t.tiles++;
            for (const auto& chunk : terrain.chunks) {
                if (chunk.heightMap.isLoaded()) t.chunks++;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        t.loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalMs += t.loadMs;
        timings.push_back(t);
    }
    double avgMs = !timings.empty() ? totalMs / timings.size() : 0.0;
    double minMs = 1e30, maxMs = 0;
    std::string slowest;
    for (const auto& t : timings) {
        if (t.loadMs < minMs) minMs = t.loadMs;
        if (t.loadMs > maxMs) { maxMs = t.loadMs; slowest = t.name; }
    }
    if (timings.empty()) { minMs = 0; maxMs = 0; }
    if (jsonOut) {
        nlohmann::json j;
        j["projectDir"] = projectDir;
        j["totalMs"] = totalMs;
        j["zoneCount"] = timings.size();
        j["avgMs"] = avgMs;
        j["minMs"] = minMs;
        j["maxMs"] = maxMs;
        j["slowestZone"] = slowest;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : timings) {
            arr.push_back({{"zone", t.name},
                            {"loadMs", t.loadMs},
                            {"tiles", t.tiles},
                            {"chunks", t.chunks}});
        }
        j["perZone"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Bench bake (load-only): %s\n", projectDir.c_str());
    std::printf("  zones    : %zu\n", timings.size());
    std::printf("  total    : %.2f ms (terrain load)\n", totalMs);
    std::printf("  per zone : avg=%.2f min=%.2f max=%.2f ms\n",
                avgMs, minMs, maxMs);
    if (!slowest.empty()) {
        std::printf("  slowest  : %s (%.2f ms)\n", slowest.c_str(), maxMs);
    }
    std::printf("\n  Per-zone:\n");
    std::printf("    zone                       ms     tiles  chunks  ms/tile\n");
    for (const auto& t : timings) {
        double mspt = t.tiles > 0 ? t.loadMs / t.tiles : 0.0;
        std::printf("    %-26s %7.2f  %5d   %5d   %6.2f\n",
                    t.name.substr(0, 26).c_str(),
                    t.loadMs, t.tiles, t.chunks, mspt);
    }
    return 0;
}


}  // namespace

bool handleProjectActions(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--copy-project") == 0 && i + 2 < argc) {
        outRc = handleCopyProject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--zone-summary") == 0 && i + 1 < argc) {
        outRc = handleZoneSummary(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bench-bake-project") == 0 && i + 1 < argc) {
        outRc = handleBenchBakeProject(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
