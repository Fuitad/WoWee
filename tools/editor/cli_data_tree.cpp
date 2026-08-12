#include "cli_data_tree.hpp"
#include "cli_subprocess.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleMigrateDataTree(int& i, int argc, char** argv) {
    // End-to-end open-format migration. Runs all four bulk
    // converters (m2/wmo/blp/dbc → wom/wob/png/json) in order
    // on a single extracted Data tree. Each step's full
    // output streams through; aggregate exit code is failure
    // if any sub-converter fails.
    //
    // Idempotent: re-running on a partially-converted tree
    // re-attempts the originals (which still produce the
    // same sidecar) without removing any prior outputs.
    std::string srcDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "migrate-data-tree: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    std::string self = argv[0];
    struct Step { const char* name; const char* flag; int rc; };
    std::vector<Step> steps = {
        {"M2  → WOM ", "--convert-m2-batch",   0},
        {"WMO → WOB ", "--convert-wmo-batch",  0},
        {"BLP → PNG ", "--convert-blp-batch",  0},
        {"DBC → JSON", "--convert-dbc-batch",  0},
    };
    int totalFailed = 0;
    std::printf("migrate-data-tree: %s\n", srcDir.c_str());
    for (auto& s : steps) {
        std::printf("\n=== %s (%s) ===\n", s.name, s.flag);
        std::fflush(stdout);
        s.rc = wowee::editor::cli::runChild(self, {s.flag, srcDir});
        if (s.rc != 0) totalFailed++;
    }
    std::printf("\n=== migrate-data-tree summary ===\n");
    for (const auto& s : steps) {
        std::printf("  [%s] %s  (rc=%d)\n",
                    s.rc == 0 ? "PASS" : "FAIL", s.name, s.rc);
    }
    if (totalFailed == 0) {
        std::printf("\n  ALL FOUR PASSED - open-format migration complete\n");
        return 0;
    }
    std::printf("\n  %d step(s) reported failures (re-run individually for detail)\n",
                totalFailed);
    return 1;
}

int handleBenchMigrateDataTree(int& i, int argc, char** argv) {
    // Time each --migrate-data-tree step end-to-end. Useful
    // for capacity planning ("how long will the full extracted
    // Data tree take?") and regression detection (a recent
    // change shouldn't make M2 conversion 2x slower).
    //
    // Sub-batches are dispatched the same way --migrate-data-
    // tree dispatches them - so the timings here are exactly
    // what the user will experience running the migration.
    std::string srcDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "bench-migrate-data-tree: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    std::string self = argv[0];
    struct Step {
        const char* name;
        const char* flag;
        double ms = 0;
        int rc = 0;
    };
    std::vector<Step> steps = {
        {"M2  → WOM ", "--convert-m2-batch",  0, 0},
        {"WMO → WOB ", "--convert-wmo-batch", 0, 0},
        {"BLP → PNG ", "--convert-blp-batch", 0, 0},
        {"DBC → JSON", "--convert-dbc-batch", 0, 0},
    };
    double totalMs = 0;
    for (auto& s : steps) {
        auto t0 = std::chrono::steady_clock::now();
        s.rc = wowee::editor::cli::runChild(self, {s.flag, srcDir}, /*quiet=*/true);
        auto t1 = std::chrono::steady_clock::now();
        s.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalMs += s.ms;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["srcDir"] = srcDir;
        j["totalMs"] = totalMs;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : steps) {
            double share = totalMs > 0 ? 100.0 * s.ms / totalMs : 0.0;
            arr.push_back({{"name", s.name},
                            {"flag", s.flag},
                            {"ms", s.ms},
                            {"share", share},
                            {"rc", s.rc}});
        }
        j["steps"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("bench-migrate-data-tree: %s\n", srcDir.c_str());
    std::printf("  total : %.1f ms (%.2f s)\n", totalMs, totalMs / 1000.0);
    std::printf("\n  step               wall-clock     share   status\n");
    for (const auto& s : steps) {
        double share = totalMs > 0 ? 100.0 * s.ms / totalMs : 0.0;
        std::printf("  %-15s   %8.1f ms   %5.1f%%   %s (rc=%d)\n",
                    s.name, s.ms, share,
                    s.rc == 0 ? "ok" : "FAIL", s.rc);
    }
    return 0;
}

int handleListDataTreeLargest(int& i, int argc, char** argv) {
    // Top-N largest proprietary files (.m2/.wmo/.blp/.dbc).
    // Helps prioritize migration: convert the biggest files
    // first to free the most disk space sooner. Annotates
    // each file with whether an open sidecar already exists,
    // so users can see at a glance which heavy hitters are
    // already migrated vs still pending.
    //
    // Default N = 20. Sized for a terminal page; use --json
    // (or pass a larger N) for full lists.
    std::string srcDir = argv[++i];
    int N = 20;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { N = std::stoi(argv[++i]); } catch (...) {}
        if (N < 1) N = 20;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "list-data-tree-largest: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    static const std::vector<std::pair<std::string, std::string>>
        kPairs = {
            {".m2",  ".wom"},
            {".wmo", ".wob"},
            {".blp", ".png"},
            {".dbc", ".json"},
        };
    // Open sidecar set for the migration-status annotation.
    std::map<std::string, std::set<std::pair<std::string, std::string>>>
        openSets;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        for (const auto& [_, openExt] : kPairs) {
            if (ext == openExt) {
                openSets[openExt].insert(
                    {e.path().parent_path().string(),
                     e.path().stem().string()});
                break;
            }
        }
    }
    struct Entry {
        std::string path;
        uint64_t bytes;
        std::string ext;
        bool migrated;
    };
    std::vector<Entry> entries;
    uint64_t totalBytes = 0;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string openExt;
        for (const auto& [propExt, oExt] : kPairs) {
            if (ext == propExt) { openExt = oExt; break; }
        }
        if (openExt.empty()) continue;
        uint64_t sz = e.file_size(ec);
        if (ec) sz = 0;
        std::pair<std::string, std::string> key{
            e.path().parent_path().string(),
            e.path().stem().string()};
        bool migrated = openSets[openExt].count(key) > 0;
        entries.push_back({e.path().string(), sz, ext, migrated});
        totalBytes += sz;
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) {
                  return a.bytes > b.bytes;
              });
    int shown = std::min(static_cast<int>(entries.size()), N);
    uint64_t shownBytes = 0;
    for (int k = 0; k < shown; ++k) shownBytes += entries[k].bytes;
    std::printf("list-data-tree-largest: %s\n", srcDir.c_str());
    std::printf("  proprietary files : %zu (total %.1f MB)\n",
                entries.size(), totalBytes / (1024.0 * 1024.0));
    std::printf("  showing top       : %d (%.1f MB, %.1f%% of total)\n",
                shown, shownBytes / (1024.0 * 1024.0),
                totalBytes ? 100.0 * shownBytes / totalBytes : 0.0);
    if (entries.empty()) {
        std::printf("\n  (no proprietary files found)\n");
        return 0;
    }
    std::printf("\n  rank   ext     bytes      status   path\n");
    for (int k = 0; k < shown; ++k) {
        const auto& e = entries[k];
        std::printf("  %4d   %-4s  %10llu  %-7s  %s\n",
                    k + 1, e.ext.c_str(),
                    static_cast<unsigned long long>(e.bytes),
                    e.migrated ? "migrate" : "pending",
                    e.path.c_str());
    }
    return 0;
}

int handleExportDataTreeMd(int& i, int argc, char** argv) {
    // Markdown migration-progress report. Drops cleanly into
    // PR descriptions, CI artifacts, or status pages on
    // GitHub Pages. Same numbers as --info-data-tree but
    // formatted as a Markdown table with a status badge,
    // bytes summary, and recommended next steps so a reader
    // can act on the report without consulting the CLI help.
    std::string srcDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "export-data-tree-md: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = srcDir + "/MIGRATION.md";
    static const std::vector<std::pair<std::string, std::string>>
        kPairs = {
            {".m2",  ".wom"},
            {".wmo", ".wob"},
            {".blp", ".png"},
            {".dbc", ".json"},
        };
    // Same scan as --info-data-tree.
    std::map<std::string, std::set<std::pair<std::string, std::string>>>
        byExt;
    std::map<std::string, uint64_t> bytesByExt;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        byExt[ext].insert({e.path().parent_path().string(),
                           e.path().stem().string()});
        uint64_t sz = e.file_size(ec);
        if (!ec) bytesByExt[ext] += sz;
    }
    struct Row {
        std::string prop, open;
        int propCount, sidecarCount, orphanOpenCount;
        uint64_t propBytes;
        double share;
    };
    std::vector<Row> rows;
    int totalProp = 0, totalSidecar = 0, totalOrphan = 0;
    uint64_t totalPropBytes = 0;
    for (const auto& [propExt, openExt] : kPairs) {
        Row r{propExt, openExt, 0, 0, 0, 0, 0.0};
        const auto& propSet = byExt[propExt];
        const auto& openSet = byExt[openExt];
        r.propCount = static_cast<int>(propSet.size());
        for (const auto& key : openSet) {
            if (propSet.count(key)) r.sidecarCount++;
            else r.orphanOpenCount++;
        }
        r.propBytes = bytesByExt[propExt];
        r.share = r.propCount > 0
                  ? 100.0 * r.sidecarCount / r.propCount
                  : 100.0;
        totalProp += r.propCount;
        totalSidecar += r.sidecarCount;
        totalOrphan += r.orphanOpenCount;
        totalPropBytes += r.propBytes;
        rows.push_back(r);
    }
    double overallShare = totalProp > 0
                          ? 100.0 * totalSidecar / totalProp
                          : 100.0;
    const char* badge =
        overallShare >= 100.0 ? "**100% migrated**" :
        overallShare >= 75.0  ? "**Mostly migrated**" :
        overallShare >= 25.0  ? "*Partially migrated*" :
                                "*Migration pending*";
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-data-tree-md: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << "# Data Tree Migration Report\n\n";
    out << "Source: `" << srcDir << "`\n\n";
    out << "Status: " << badge << " (" << std::fixed;
    out.precision(1);
    out << overallShare << "% sidecar coverage)\n\n";
    out << "## Summary\n\n";
    out << "- Proprietary files: **" << totalProp << "** ("
        << std::fixed;
    out.precision(2);
    out << (totalPropBytes / (1024.0 * 1024.0)) << " MB)\n";
    out << "- Open sidecars present: **" << totalSidecar << "**\n";
    out << "- Orphan open files (no proprietary source): **"
        << totalOrphan << "**\n\n";
    out << "## Per-format pairs\n\n";
    out << "| Pair | Proprietary | Sidecars | Orphan open | Prop bytes | Share |\n";
    out << "|------|------------:|---------:|------------:|-----------:|------:|\n";
    for (const auto& r : rows) {
        out << "| " << r.prop << " → " << r.open << " | "
            << r.propCount << " | "
            << r.sidecarCount << " | "
            << r.orphanOpenCount << " | "
            << r.propBytes << " | "
            << std::fixed;
        out.precision(1);
        out << r.share << "% |\n";
    }
    out << "\n## Recommended next steps\n\n";
    if (overallShare < 100.0) {
        out << "1. Run `wowee_editor --migrate-data-tree " << srcDir
            << "` to fill in the missing sidecars.\n";
        out << "2. Run `wowee_editor --audit-data-tree " << srcDir
            << "` to confirm 100% coverage.\n";
        out << "3. Run `wowee_editor --strip-data-tree " << srcDir
            << "` to delete the proprietary originals.\n";
    } else {
        out << "All proprietary files are migrated. Run "
            << "`wowee_editor --strip-data-tree " << srcDir
            << "` to delete the originals and ship the open-only tree.\n";
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  status      : %s\n", badge);
    std::printf("  share       : %.1f%%\n", overallShare);
    std::printf("  proprietary : %d files, %.2f MB\n",
                totalProp, totalPropBytes / (1024.0 * 1024.0));
    return 0;
}

int handleInfoDataTree(int& i, int argc, char** argv) {
    // Non-destructive companion to --migrate-data-tree. Walks
    // <srcDir> recursively, counts files per format pair
    // (proprietary vs open replacement), and reports per-pair
    // counts plus an overall "migration share" - the fraction
    // of source files that already have an open sidecar
    // present.
    //
    // Designed to drop into CI dashboards: a 100% share
    // means every proprietary asset has a deterministic open
    // counterpart on disk and you can drop the originals.
    std::string srcDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "info-data-tree: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    // Each pair: proprietary extension + open extension. The
    // open file is considered a "sidecar" when it sits next
    // to the proprietary file with the same stem.
    struct Pair {
        const char* prop;   // ".m2"
        const char* open;   // ".wom"
        int propCount = 0;
        int sidecarCount = 0;     // .wom next to a .m2
        int orphanOpenCount = 0;  // .wom with no matching .m2
    };
    std::vector<Pair> pairs = {
        {".m2",  ".wom"},
        {".wmo", ".wob"},
        {".blp", ".png"},
        {".dbc", ".json"},
    };
    // First pass: collect filenames by extension. Use a set
    // of (parent, stem) for the sidecar lookup so the test is
    // O(log n) per file rather than O(n).
    std::map<std::string, std::set<std::pair<std::string, std::string>>> byExt;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        byExt[ext].insert({e.path().parent_path().string(),
                           e.path().stem().string()});
    }
    for (auto& p : pairs) {
        const auto& propSet = byExt[p.prop];
        const auto& openSet = byExt[p.open];
        p.propCount = static_cast<int>(propSet.size());
        for (const auto& key : openSet) {
            if (propSet.count(key)) p.sidecarCount++;
            else p.orphanOpenCount++;
        }
    }
    int totalProp = 0, totalSidecar = 0, totalOrphanOpen = 0;
    for (const auto& p : pairs) {
        totalProp += p.propCount;
        totalSidecar += p.sidecarCount;
        totalOrphanOpen += p.orphanOpenCount;
    }
    double overallShare = totalProp > 0
                          ? 100.0 * totalSidecar / totalProp
                          : 100.0;
    if (jsonOut) {
        nlohmann::json j;
        j["srcDir"] = srcDir;
        j["totalProprietary"] = totalProp;
        j["totalSidecars"] = totalSidecar;
        j["totalOrphanOpen"] = totalOrphanOpen;
        j["migrationShare"] = overallShare;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : pairs) {
            double share = p.propCount > 0
                           ? 100.0 * p.sidecarCount / p.propCount
                           : 100.0;
            arr.push_back({{"proprietary", p.prop},
                            {"open", p.open},
                            {"propCount", p.propCount},
                            {"sidecarCount", p.sidecarCount},
                            {"orphanOpenCount", p.orphanOpenCount},
                            {"share", share}});
        }
        j["pairs"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("info-data-tree: %s\n", srcDir.c_str());
    std::printf("  total proprietary : %d\n", totalProp);
    std::printf("  total sidecars    : %d (open files matched to a proprietary)\n",
                totalSidecar);
    std::printf("  orphan open files : %d (no matching proprietary - already-stripped)\n",
                totalOrphanOpen);
    std::printf("  migration share   : %.1f%% (sidecars / proprietary)\n",
                overallShare);
    std::printf("\n  pair             prop   open-side   orphan   share\n");
    for (const auto& p : pairs) {
        double share = p.propCount > 0
                       ? 100.0 * p.sidecarCount / p.propCount
                       : 100.0;
        char label[32];
        std::snprintf(label, sizeof(label), "%-4s → %-5s", p.prop, p.open);
        std::printf("  %-14s  %5d   %9d   %6d   %5.1f%%\n",
                    label, p.propCount, p.sidecarCount,
                    p.orphanOpenCount, share);
    }
    return 0;
}

int handleStripDataTree(int& i, int argc, char** argv) {
    // Destructive cleanup. Walks <srcDir>, finds every
    // proprietary file (.m2/.wmo/.blp/.dbc) that already has
    // a matching open sidecar at the same (parent, stem),
    // and deletes the proprietary file. Sidecar match uses
    // case-insensitive extension comparison.
    //
    // Honors --dry-run for safe previews. Mirrors the
    // --strip-zone convention (defaults to actually delete).
    //
    // Recommended workflow: --info-data-tree to see the
    // share, --migrate-data-tree to fill in missing sidecars,
    // --strip-data-tree --dry-run to confirm the kill list,
    // then --strip-data-tree to apply.
    std::string srcDir = argv[++i];
    bool dryRun = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--dry-run") == 0) {
        dryRun = true; i++;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "strip-data-tree: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    // Build the (parent, stem) set of every open file first.
    // The proprietary→open ext map serves both as the strip
    // target list and as the per-pair routing table.
    static const std::vector<std::pair<std::string, std::string>>
        kPairs = {
            {".m2",  ".wom"},
            {".wmo", ".wob"},
            {".blp", ".png"},
            {".dbc", ".json"},
        };
    std::map<std::string, std::set<std::pair<std::string, std::string>>>
        openSets;  // open ext -> set of (parent, stem)
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        for (const auto& [_, openExt] : kPairs) {
            if (ext == openExt) {
                openSets[openExt].insert(
                    {e.path().parent_path().string(),
                     e.path().stem().string()});
                break;
            }
        }
    }
    // Walk again, this time deleting (or previewing) each
    // proprietary file whose key appears in its pair's open
    // set.
    int removed = 0, failed = 0;
    uint64_t freedBytes = 0;
    std::map<std::string, int> perExtRemoved;
    for (const auto& [propExt, openExt] : kPairs) {
        const auto& openSet = openSets[openExt];
        if (openSet.empty()) continue;
        for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext != propExt) continue;
            std::pair<std::string, std::string> key{
                e.path().parent_path().string(),
                e.path().stem().string()};
            if (!openSet.count(key)) continue;  // no sidecar - keep
            uint64_t sz = e.file_size(ec);
            if (ec) sz = 0;
            if (dryRun) {
                std::printf("  would remove: %s (%llu bytes)\n",
                            e.path().string().c_str(),
                            static_cast<unsigned long long>(sz));
                removed++;
                perExtRemoved[propExt]++;
                freedBytes += sz;
            } else {
                if (fs::remove(e.path(), ec)) {
                    std::printf("  removed: %s (%llu bytes)\n",
                                e.path().string().c_str(),
                                static_cast<unsigned long long>(sz));
                    removed++;
                    perExtRemoved[propExt]++;
                    freedBytes += sz;
                } else {
                    std::fprintf(stderr,
                        "  WARN: failed to remove %s (%s)\n",
                        e.path().string().c_str(), ec.message().c_str());
                    failed++;
                }
            }
        }
    }
    std::printf("\nstrip-data-tree: %s%s\n",
                srcDir.c_str(), dryRun ? " (dry-run)" : "");
    std::printf("  %s : %d file(s)\n",
                dryRun ? "would remove" : "removed     ", removed);
    std::printf("  freed        : %.1f KB\n", freedBytes / 1024.0);
    if (!perExtRemoved.empty()) {
        std::printf("\n  Per-extension:\n");
        for (const auto& [ext, count] : perExtRemoved) {
            std::printf("    %-5s : %d\n", ext.c_str(), count);
        }
    }
    if (failed > 0) {
        std::printf("\n  FAILED       : %d (see stderr)\n", failed);
    }
    if (dryRun && removed > 0) {
        std::printf("\n  re-run without --dry-run to apply\n");
    }
    return failed == 0 ? 0 : 1;
}

int handleAuditDataTree(int& i, [[maybe_unused]] int argc, char** argv) {
    // Non-destructive CI gate. Walks <srcDir> and exits 1 if
    // any proprietary file (.m2/.wmo/.blp/.dbc) lacks a
    // matching open sidecar at the same (parent, stem). The
    // pre-strip safety check: don't run --strip-data-tree
    // until this returns exit 0.
    //
    // Lists missing sidecars (capped at 50) so the user can
    // re-run --migrate-data-tree to fill them in.
    std::string srcDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr,
            "audit-data-tree: %s is not a directory\n",
            srcDir.c_str());
        return 1;
    }
    static const std::vector<std::pair<std::string, std::string>>
        kPairs = {
            {".m2",  ".wom"},
            {".wmo", ".wob"},
            {".blp", ".png"},
            {".dbc", ".json"},
        };
    // Build (parent, stem) sets per open ext for fast lookup.
    std::map<std::string, std::set<std::pair<std::string, std::string>>>
        openSets;
    std::map<std::string, std::vector<std::string>> propByExt;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        bool isOpen = false;
        for (const auto& [propExt, openExt] : kPairs) {
            if (ext == openExt) {
                openSets[openExt].insert(
                    {e.path().parent_path().string(),
                     e.path().stem().string()});
                isOpen = true;
                break;
            }
        }
        if (isOpen) continue;
        for (const auto& [propExt, _] : kPairs) {
            if (ext == propExt) {
                propByExt[propExt].push_back(e.path().string());
                break;
            }
        }
    }
    // Check each proprietary file for its sidecar.
    int totalProp = 0, totalMissing = 0;
    std::vector<std::string> missing;
    std::map<std::string, int> missingPerExt;
    for (const auto& [propExt, openExt] : kPairs) {
        const auto& openSet = openSets[openExt];
        for (const auto& fullPath : propByExt[propExt]) {
            totalProp++;
            fs::path p(fullPath);
            std::pair<std::string, std::string> key{
                p.parent_path().string(), p.stem().string()};
            if (openSet.count(key)) continue;
            totalMissing++;
            missingPerExt[propExt]++;
            missing.push_back(fullPath);
        }
    }
    std::sort(missing.begin(), missing.end());
    std::printf("audit-data-tree: %s\n", srcDir.c_str());
    std::printf("  proprietary files : %d\n", totalProp);
    std::printf("  missing sidecars  : %d\n", totalMissing);
    if (totalMissing == 0) {
        if (totalProp > 0) {
            std::printf("\n  PASSED - every proprietary file has an open sidecar\n");
        } else {
            std::printf("\n  PASSED - no proprietary files present\n");
        }
        return 0;
    }
    std::printf("\n  FAILED - re-run --migrate-data-tree to fill the gaps\n");
    std::printf("\n  Per-extension missing:\n");
    for (const auto& [ext, count] : missingPerExt) {
        std::printf("    %-5s : %d\n", ext.c_str(), count);
    }
    std::printf("\n  Missing sidecars (sorted):\n");
    size_t shown = 0;
    for (const auto& m : missing) {
        if (shown >= 50) {
            std::printf("    ... and %zu more\n", missing.size() - shown);
            break;
        }
        std::printf("    - %s\n", m.c_str());
        shown++;
    }
    return 1;
}


}  // namespace

bool handleDataTree(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--migrate-data-tree") == 0 && i + 1 < argc) {
        outRc = handleMigrateDataTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bench-migrate-data-tree") == 0 && i + 1 < argc) {
        outRc = handleBenchMigrateDataTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-data-tree-largest") == 0 && i + 1 < argc) {
        outRc = handleListDataTreeLargest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-data-tree-md") == 0 && i + 1 < argc) {
        outRc = handleExportDataTreeMd(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-data-tree") == 0 && i + 1 < argc) {
        outRc = handleInfoDataTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--strip-data-tree") == 0 && i + 1 < argc) {
        outRc = handleStripDataTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--audit-data-tree") == 0 && i + 1 < argc) {
        outRc = handleAuditDataTree(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
