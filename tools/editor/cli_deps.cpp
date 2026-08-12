#include "cli_deps.hpp"
#include "zone_manifest.hpp"

#include "object_placer.hpp"
#include "pipeline/wowee_building.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleListZoneDeps(int& i, int argc, char** argv) {
    // Enumerate every external model path a zone references -
    // both directly placed (objects.json) and indirectly via
    // doodad placements inside any WOB sitting next to the
    // zone manifest. Useful when packaging a content pack to
    // confirm every needed asset will ship.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "list-zone-deps: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    // Collect with usage counts so duplicates report '×N' instead
    // of cluttering the table.
    std::map<std::string, int> directM2;   // m2 placements
    std::map<std::string, int> directWMO;  // wmo placements
    std::map<std::string, int> doodadM2;   // m2s referenced inside WOBs
    wowee::editor::ObjectPlacer op;
    if (op.loadFromFile(zoneDir + "/objects.json")) {
        for (const auto& o : op.getObjects()) {
            if (o.type == wowee::editor::PlaceableType::M2) directM2[o.path]++;
            else if (o.type == wowee::editor::PlaceableType::WMO) directWMO[o.path]++;
        }
    }
    // Walk WOBs in the zone directory recursively and pull in
    // their doodad model paths. Sub-dirs caught too in case the
    // user organizes buildings under a buildings/ subfolder.
    int wobCount = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext != ".wob") continue;
        wobCount++;
        std::string base = e.path().string();
        if (base.size() >= 4) base = base.substr(0, base.size() - 4);
        auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
        for (const auto& d : bld.doodads) {
            if (d.modelPath.empty()) continue;
            doodadM2[d.modelPath]++;
        }
    }
    // For each direct WMO placement, also recurse into the WOB
    // sitting at that path (relative to the zone) so transitive
    // doodad deps surface - this matches the runtime's actual
    // load chain.
    for (const auto& [path, count] : directWMO) {
        // Strip extension since loader takes a base path.
        std::string base = path;
        if (base.size() >= 4 && base.substr(base.size() - 4) == ".wmo")
            base = base.substr(0, base.size() - 4);
        // Try relative-to-zone first, then absolute.
        std::string trial = zoneDir + "/" + base;
        if (!wowee::pipeline::WoweeBuildingLoader::exists(trial)) trial = base;
        if (!wowee::pipeline::WoweeBuildingLoader::exists(trial)) continue;
        auto bld = wowee::pipeline::WoweeBuildingLoader::load(trial);
        for (const auto& d : bld.doodads) {
            if (d.modelPath.empty()) continue;
            doodadM2[d.modelPath]++;
        }
    }
    size_t totalUnique = directM2.size() + directWMO.size() + doodadM2.size();
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["wobCount"] = wobCount;
        j["totalUnique"] = totalUnique;
        auto toArr = [](const std::map<std::string, int>& m) {
            nlohmann::json a = nlohmann::json::array();
            for (const auto& [path, count] : m) {
                a.push_back({{"path", path}, {"count", count}});
            }
            return a;
        };
        j["directM2"] = toArr(directM2);
        j["directWMO"] = toArr(directWMO);
        j["doodadM2"] = toArr(doodadM2);
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone deps: %s\n", zoneDir.c_str());
    std::printf("  WOBs scanned        : %d\n", wobCount);
    std::printf("  unique paths total  : %zu\n", totalUnique);
    auto emit = [](const char* tag, const std::map<std::string, int>& m) {
        std::printf("\n  %s (%zu unique):\n", tag, m.size());
        if (m.empty()) {
            std::printf("    *none*\n");
            return;
        }
        for (const auto& [path, count] : m) {
            if (count > 1) std::printf("    %s ×%d\n", path.c_str(), count);
            else            std::printf("    %s\n",     path.c_str());
        }
    };
    emit("Direct M2 placements",  directM2);
    emit("Direct WMO placements", directWMO);
    emit("WOB doodad M2 refs",    doodadM2);
    return 0;
}

int handleListProjectOrphans(int& i, int argc, char** argv) {
    // Inverse of --list-zone-deps. Walks every zone in
    // <projectDir>, collects the set of .wom/.wob files
    // sitting on disk and the set of paths actually
    // referenced by objects.json placements + WOB doodad
    // lists. Files in the first set but not the second are
    // orphans - candidates for removal before --pack-wcp so
    // the archive doesn't carry dead weight.
    //
    // Comparison is by basename (extension stripped) since
    // the reference paths sometimes include the extension and
    // sometimes don't.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "list-project-orphans: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    // Project-wide reference set. Normalize by stripping
    // extension and any leading "./".
    auto normalize = [](std::string p) {
        while (p.size() >= 2 && p[0] == '.' && p[1] == '/') p.erase(0, 2);
        std::string ext = fs::path(p).extension().string();
        if (ext == ".wom" || ext == ".wob" || ext == ".m2" || ext == ".wmo") {
            p = p.substr(0, p.size() - ext.size());
        }
        return p;
    };
    std::set<std::string> referencedBases;  // normalized basenames
    for (const auto& zoneDir : zones) {
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile(zoneDir + "/objects.json")) {
            for (const auto& o : op.getObjects()) {
                if (o.path.empty()) continue;
                // Reference can be relative to zone or just a
                // bare model name; record both forms for the
                // membership test.
                std::string norm = normalize(o.path);
                referencedBases.insert(norm);
                // Also try the leaf basename so unqualified
                // refs match.
                referencedBases.insert(fs::path(norm).filename().string());
            }
        }
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".wob") continue;
            std::string base = e.path().string();
            if (base.size() >= 4) base = base.substr(0, base.size() - 4);
            auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
            for (const auto& d : bld.doodads) {
                if (d.modelPath.empty()) continue;
                std::string norm = normalize(d.modelPath);
                referencedBases.insert(norm);
                referencedBases.insert(fs::path(norm).filename().string());
            }
        }
    }
    // Now walk every zone again and flag orphan .wom/.wob files.
    struct Orphan { std::string zone, path; uint64_t bytes; };
    std::vector<Orphan> orphans;
    uint64_t totalOrphanBytes = 0;
    for (const auto& zoneDir : zones) {
        std::string zoneName = fs::path(zoneDir).filename().string();
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            if (ext != ".wom" && ext != ".wob") continue;
            std::string rel = fs::relative(e.path(), zoneDir, ec).string();
            if (ec) rel = e.path().filename().string();
            std::string normRel = rel.substr(0, rel.size() - ext.size());
            std::string leaf = e.path().stem().string();
            if (referencedBases.count(normRel) ||
                referencedBases.count(leaf)) {
                continue;  // referenced, not orphan
            }
            uint64_t sz = e.file_size(ec);
            if (ec) sz = 0;
            orphans.push_back({zoneName, rel, sz});
            totalOrphanBytes += sz;
        }
    }
    std::sort(orphans.begin(), orphans.end(),
              [](const Orphan& a, const Orphan& b) {
                  if (a.zone != b.zone) return a.zone < b.zone;
                  return a.path < b.path;
              });
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["referencedCount"] = referencedBases.size();
        j["orphanCount"] = orphans.size();
        j["orphanBytes"] = totalOrphanBytes;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& o : orphans) {
            arr.push_back({{"zone", o.zone},
                            {"path", o.path},
                            {"bytes", o.bytes}});
        }
        j["orphans"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project orphans: %s\n", projectDir.c_str());
    std::printf("  zones scanned    : %zu\n", zones.size());
    std::printf("  refs collected   : %zu (normalized basenames)\n",
                referencedBases.size());
    std::printf("  orphan .wom/.wob : %zu file(s), %.1f KB\n",
                orphans.size(), totalOrphanBytes / 1024.0);
    if (orphans.empty()) {
        std::printf("\n  (no orphans - every model file is referenced)\n");
        return 0;
    }
    std::printf("\n  zone                  bytes      path\n");
    for (const auto& o : orphans) {
        std::printf("  %-20s  %8llu   %s\n",
                    o.zone.substr(0, 20).c_str(),
                    static_cast<unsigned long long>(o.bytes),
                    o.path.c_str());
    }
    return 0;
}

int handleRemoveProjectOrphans(int& i, int argc, char** argv) {
    // Destructive companion to --list-project-orphans. Reuses
    // the same reference-collection + orphan-detection logic
    // and then deletes the resulting files. --dry-run shows
    // what would be removed without touching anything.
    std::string projectDir = argv[++i];
    bool dryRun = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--dry-run") == 0) {
        dryRun = true; i++;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "remove-project-orphans: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    // Same normalize + reference collection as --list-project-orphans.
    // Keep both functions in sync if the matching rules evolve.
    auto normalize = [](std::string p) {
        while (p.size() >= 2 && p[0] == '.' && p[1] == '/') p.erase(0, 2);
        std::string ext = fs::path(p).extension().string();
        if (ext == ".wom" || ext == ".wob" || ext == ".m2" || ext == ".wmo") {
            p = p.substr(0, p.size() - ext.size());
        }
        return p;
    };
    std::set<std::string> referencedBases;
    for (const auto& zoneDir : zones) {
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile(zoneDir + "/objects.json")) {
            for (const auto& o : op.getObjects()) {
                if (o.path.empty()) continue;
                std::string norm = normalize(o.path);
                referencedBases.insert(norm);
                referencedBases.insert(fs::path(norm).filename().string());
            }
        }
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".wob") continue;
            std::string base = e.path().string();
            if (base.size() >= 4) base = base.substr(0, base.size() - 4);
            auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
            for (const auto& d : bld.doodads) {
                if (d.modelPath.empty()) continue;
                std::string norm = normalize(d.modelPath);
                referencedBases.insert(norm);
                referencedBases.insert(fs::path(norm).filename().string());
            }
        }
    }
    int removed = 0, failed = 0;
    uint64_t freedBytes = 0;
    for (const auto& zoneDir : zones) {
        std::string zoneName = fs::path(zoneDir).filename().string();
        std::error_code ec;
        std::vector<fs::path> toRemove;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            if (ext != ".wom" && ext != ".wob") continue;
            std::string rel = fs::relative(e.path(), zoneDir, ec).string();
            if (ec) rel = e.path().filename().string();
            std::string normRel = rel.substr(0, rel.size() - ext.size());
            std::string leaf = e.path().stem().string();
            if (referencedBases.count(normRel) ||
                referencedBases.count(leaf)) continue;
            toRemove.push_back(e.path());
        }
        // Materialize the deletion list before removing so we
        // don't mutate the directory while iterating.
        for (const auto& p : toRemove) {
            uint64_t sz = fs::file_size(p, ec);
            if (ec) sz = 0;
            std::string rel = fs::relative(p, zoneDir, ec).string();
            if (ec) rel = p.filename().string();
            if (dryRun) {
                std::printf("  would remove: %s/%s (%llu bytes)\n",
                            zoneName.c_str(), rel.c_str(),
                            static_cast<unsigned long long>(sz));
                removed++;
                freedBytes += sz;
            } else {
                if (fs::remove(p, ec)) {
                    std::printf("  removed: %s/%s (%llu bytes)\n",
                                zoneName.c_str(), rel.c_str(),
                                static_cast<unsigned long long>(sz));
                    removed++;
                    freedBytes += sz;
                } else {
                    std::fprintf(stderr,
                        "  WARN: failed to remove %s (%s)\n",
                        p.c_str(), ec.message().c_str());
                    failed++;
                }
            }
        }
    }
    std::printf("\nremove-project-orphans: %s%s\n",
                projectDir.c_str(), dryRun ? " (dry-run)" : "");
    std::printf("  zones    : %zu\n", zones.size());
    std::printf("  refs     : %zu (normalized basenames)\n",
                referencedBases.size());
    std::printf("  %s : %d file(s)\n",
                dryRun ? "would remove" : "removed     ", removed);
    std::printf("  freed    : %.1f KB\n", freedBytes / 1024.0);
    if (failed > 0) {
        std::printf("  FAILED   : %d (see stderr)\n", failed);
    }
    if (dryRun && removed > 0) {
        std::printf("  re-run without --dry-run to apply\n");
    }
    return failed == 0 ? 0 : 1;
}


}  // namespace

bool handleDeps(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-zone-deps") == 0 && i + 1 < argc) {
        outRc = handleListZoneDeps(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-project-orphans") == 0 && i + 1 < argc) {
        outRc = handleListProjectOrphans(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--remove-project-orphans") == 0 && i + 1 < argc) {
        outRc = handleRemoveProjectOrphans(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
