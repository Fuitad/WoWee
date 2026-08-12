#include "cli_strip.hpp"
#include "zone_manifest.hpp"

#include <algorithm>
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

int handleStripZone(int& i, int argc, char** argv) {
    // Cleanup pass: remove the derived outputs (.glb/.obj/.stl/
    // .html/.dot/.csv/ZONE.md/DEPS.md) leaving only source files
    // (zone.json + content JSONs + open binary formats). Useful
    // before --pack-wcp so the archive doesn't carry redundant
    // exports, or before committing to git so derived blobs
    // don't bloat history.
    //
    // Optional --dry-run flag previews what would be removed
    // without actually deleting anything.
    std::string zoneDir = argv[++i];
    bool dryRun = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--dry-run") == 0) {
        dryRun = true;
        i++;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "strip-zone: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    // Whitelist of derived extensions. PNG is special-cased: it
    // can be either a derived export (heightmap preview at zone
    // root) or a source sidecar (BLP→PNG inside data/). Only
    // strip PNGs at the top-level zone dir.
    auto isDerivedExt = [](const std::string& ext) {
        return ext == ".glb" || ext == ".obj" || ext == ".stl" ||
               ext == ".html" || ext == ".dot" || ext == ".csv";
    };
    auto isDerivedFilename = [](const std::string& name) {
        return name == "ZONE.md" || name == "DEPS.md" ||
               name == "quests.dot";
    };
    int removed = 0;
    uint64_t bytesFreed = 0;
    std::error_code ec;
    // Top-level only - do NOT recurse into data/ (those are
    // source sidecars).
    for (const auto& e : fs::directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::string name = e.path().filename().string();
        bool kill = false;
        if (isDerivedExt(ext)) kill = true;
        if (isDerivedFilename(name)) kill = true;
        // PNG at zone root is derived (--export-png); PNGs inside
        // data/ are source. Top-level loop only sees the root
        // dir, so .png here is always derived.
        if (ext == ".png") kill = true;
        if (!kill) continue;
        uint64_t sz = e.file_size(ec);
        if (dryRun) {
            std::printf("  would remove: %s (%llu bytes)\n",
                        name.c_str(),
                        static_cast<unsigned long long>(sz));
        } else {
            if (fs::remove(e.path(), ec)) {
                std::printf("  removed: %s (%llu bytes)\n",
                            name.c_str(),
                            static_cast<unsigned long long>(sz));
                removed++;
                bytesFreed += sz;
            } else {
                std::fprintf(stderr,
                    "  WARN: failed to remove %s (%s)\n",
                    name.c_str(), ec.message().c_str());
            }
        }
    }
    std::printf("\nstrip-zone: %s%s\n",
                zoneDir.c_str(), dryRun ? " (dry-run)" : "");
    if (dryRun) {
        std::printf("  pass --dry-run off to actually delete\n");
    } else {
        std::printf("  removed  : %d file(s)\n", removed);
        std::printf("  freed    : %.1f KB\n", bytesFreed / 1024.0);
    }
    return 0;
}

int handleStripProject(int& i, int argc, char** argv) {
    // Project-wide wrapper around --strip-zone. Walks every zone
    // in <projectDir>, removes derived outputs at each zone's
    // top level, and reports per-zone removed/freed counts plus
    // an aggregate. Honors --dry-run for safe previews.
    std::string projectDir = argv[++i];
    bool dryRun = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--dry-run") == 0) {
        dryRun = true;
        i++;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "strip-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // Same derived-classifier as --strip-zone - keep in sync.
    auto isDerivedExt = [](const std::string& ext) {
        return ext == ".glb" || ext == ".obj" || ext == ".stl" ||
               ext == ".html" || ext == ".dot" || ext == ".csv";
    };
    auto isDerivedFilename = [](const std::string& name) {
        return name == "ZONE.md" || name == "DEPS.md" ||
               name == "quests.dot";
    };
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    struct ZRow { std::string name; int removed = 0; uint64_t freed = 0; };
    std::vector<ZRow> rows;
    int totalRemoved = 0;
    uint64_t totalFreed = 0;
    int totalFailed = 0;
    for (const auto& zoneDir : zones) {
        ZRow r;
        r.name = fs::path(zoneDir).filename().string();
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::string name = e.path().filename().string();
            bool kill = false;
            if (isDerivedExt(ext)) kill = true;
            if (isDerivedFilename(name)) kill = true;
            if (ext == ".png") kill = true;
            if (!kill) continue;
            uint64_t sz = e.file_size(ec);
            if (dryRun) {
                r.removed++;
                r.freed += sz;
            } else {
                if (fs::remove(e.path(), ec)) {
                    r.removed++;
                    r.freed += sz;
                } else {
                    std::fprintf(stderr,
                        "  WARN: failed to remove %s/%s (%s)\n",
                        r.name.c_str(), name.c_str(),
                        ec.message().c_str());
                    totalFailed++;
                }
            }
        }
        totalRemoved += r.removed;
        totalFreed += r.freed;
        rows.push_back(r);
    }
    std::printf("strip-project: %s%s\n",
                projectDir.c_str(), dryRun ? " (dry-run)" : "");
    std::printf("  zones    : %zu\n", zones.size());
    std::printf("\n  zone                       removed       freed\n");
    for (const auto& r : rows) {
        std::printf("  %-26s  %5d   %9.1f KB\n",
                    r.name.substr(0, 26).c_str(),
                    r.removed, r.freed / 1024.0);
    }
    std::printf("\n  totals%s : %d file(s), %.1f KB\n",
                dryRun ? " (would-remove)" : "          ",
                totalRemoved, totalFreed / 1024.0);
    if (dryRun) {
        std::printf("  pass --dry-run off to actually delete\n");
    }
    return totalFailed == 0 ? 0 : 1;
}


}  // namespace

bool handleStrip(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--strip-zone") == 0 && i + 1 < argc) {
        outRc = handleStripZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--strip-project") == 0 && i + 1 < argc) {
        outRc = handleStripProject(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
