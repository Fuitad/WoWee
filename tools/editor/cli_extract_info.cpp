#include "cli_extract_info.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoExtract(int& i, int argc, char** argv) {
    // Walk an extracted-asset directory and report counts by
    // extension + open-format coverage. Useful for seeing whether
    // a user ran asset_extract with --emit-open.
    std::string dataDir = argv[++i];
    // Optional --json after the dir for machine-readable output.
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(dataDir)) {
        std::fprintf(stderr, "info-extract: %s does not exist\n", dataDir.c_str());
        return 1;
    }
    // Per-format counts. Pair proprietary with open-format sidecar
    // so the report can show coverage percentages. Track bytes
    // separately for proprietary vs open so the user can see how
    // much disk a "purge proprietary after open conversion"
    // workflow would save (or cost - open formats are sometimes
    // larger, e.g. PNG vs DXT-compressed BLP).
    uint64_t blpCount = 0, pngSidecar = 0;
    uint64_t dbcCount = 0, jsonSidecar = 0;
    uint64_t m2Count  = 0, womSidecar = 0;
    uint64_t wmoCount = 0, wobSidecar = 0;
    uint64_t adtCount = 0, whmSidecar = 0;
    uint64_t totalBytes = 0;
    uint64_t propBytes = 0, openBytes = 0;
    for (auto& entry : fs::recursive_directory_iterator(dataDir)) {
        if (!entry.is_regular_file()) continue;
        uint64_t fsz = entry.file_size();
        totalBytes += fsz;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string base = entry.path().string();
        if (base.size() > ext.size()) base = base.substr(0, base.size() - ext.size());
        auto sidecarExists = [&](const char* sidecarExt) {
            return fs::exists(base + sidecarExt);
        };
        if      (ext == ".blp") { blpCount++; propBytes += fsz; if (sidecarExists(".png"))  pngSidecar++; }
        else if (ext == ".dbc") { dbcCount++; propBytes += fsz; if (sidecarExists(".json")) jsonSidecar++; }
        else if (ext == ".m2")  { m2Count++;  propBytes += fsz; if (sidecarExists(".wom"))  womSidecar++; }
        else if (ext == ".wmo") {
            propBytes += fsz;
            std::string fname = entry.path().filename().string();
            auto under = fname.rfind('_');
            bool isGroup = (under != std::string::npos &&
                            fname.size() - under == 8);
            if (!isGroup) {
                wmoCount++; if (sidecarExists(".wob")) wobSidecar++;
            }
        }
        else if (ext == ".adt") { adtCount++; propBytes += fsz; if (sidecarExists(".whm")) whmSidecar++; }
        else if (ext == ".png" || ext == ".json" || ext == ".wom" ||
                 ext == ".wob" || ext == ".whm" || ext == ".wot" ||
                 ext == ".woc") {
            openBytes += fsz;
        }
    }
    auto pct = [](uint64_t x, uint64_t total) {
        return total == 0 ? 0.0 : (100.0 * x) / total;
    };
    if (jsonOut) {
        // Machine-readable summary for CI scripts; matches the
        // structure of the human-readable lines below.
        nlohmann::json j;
        j["dir"] = dataDir;
        j["totalBytes"] = totalBytes;
        j["proprietaryBytes"] = propBytes;
        j["openBytes"] = openBytes;
        auto fmtFmt = [&](const char* name, uint64_t prop, uint64_t open) {
            nlohmann::json f;
            f["proprietary"] = prop;
            f["sidecar"] = open;
            f["coverage"] = pct(open, prop);
            j[name] = f;
        };
        fmtFmt("blp_png",   blpCount, pngSidecar);
        fmtFmt("dbc_json",  dbcCount, jsonSidecar);
        fmtFmt("m2_wom",    m2Count,  womSidecar);
        fmtFmt("wmo_wob",   wmoCount, wobSidecar);
        fmtFmt("adt_whm",   adtCount, whmSidecar);
        uint64_t openTotal = pngSidecar + jsonSidecar + womSidecar +
                             wobSidecar + whmSidecar;
        uint64_t propTotal = blpCount + dbcCount + m2Count +
                             wmoCount + adtCount;
        j["overallCoverage"] = pct(openTotal, propTotal);
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Extracted asset tree: %s\n", dataDir.c_str());
    std::printf("  total bytes  : %.2f GB\n", totalBytes / (1024.0 * 1024.0 * 1024.0));
    std::printf("  BLP textures : %" PRIu64 "  (%" PRIu64 " PNG sidecar = %.1f%% open)\n",
                blpCount, pngSidecar, pct(pngSidecar, blpCount));
    std::printf("  DBC tables   : %" PRIu64 "  (%" PRIu64 " JSON sidecar = %.1f%% open)\n",
                dbcCount, jsonSidecar, pct(jsonSidecar, dbcCount));
    std::printf("  M2 models    : %" PRIu64 "  (%" PRIu64 " WOM sidecar = %.1f%% open)\n",
                m2Count, womSidecar, pct(womSidecar, m2Count));
    std::printf("  WMO buildings: %" PRIu64 "  (%" PRIu64 " WOB sidecar = %.1f%% open)\n",
                wmoCount, wobSidecar, pct(wobSidecar, wmoCount));
    std::printf("  ADT terrain  : %" PRIu64 "  (%" PRIu64 " WHM sidecar = %.1f%% open)\n",
                adtCount, whmSidecar, pct(whmSidecar, adtCount));
    uint64_t openTotal = pngSidecar + jsonSidecar + womSidecar + wobSidecar + whmSidecar;
    uint64_t propTotal = blpCount + dbcCount + m2Count + wmoCount + adtCount;
    std::printf("  overall open-format coverage: %.1f%%\n", pct(openTotal, propTotal));
    // Disk-usage breakdown: shows roughly how big a purge-proprietary
    // workflow would shrink the tree (or how much extra a dual-format
    // extraction costs).
    const double mb = 1024.0 * 1024.0;
    std::printf("  proprietary bytes: %.1f MB\n", propBytes / mb);
    std::printf("  open-format bytes: %.1f MB", openBytes / mb);
    if (propBytes > 0) {
        std::printf(" (%.1f%% of proprietary)",
                    100.0 * static_cast<double>(openBytes) / propBytes);
    }
    std::printf("\n");
    std::printf("  (run `asset_extract --emit-open` to fill missing sidecars)\n");
    return 0;
}

int handleInfoExtractTree(int& i, int argc, char** argv) {
    // Hierarchical view of an extracted asset directory grouped
    // by top-level subdirectory and format. Useful for getting
    // oriented after asset_extract finishes - '17 dirs, 142k
    // files' is hard to reason about; this groups them for
    // at-a-glance comprehension.
    std::string dataDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(dataDir) || !fs::is_directory(dataDir)) {
        std::fprintf(stderr,
            "info-extract-tree: %s is not a directory\n", dataDir.c_str());
        return 1;
    }
    // Per-top-level-dir aggregation: per-extension count + bytes.
    // Top-level discovery: every immediate child dir of dataDir.
    struct ExtStats { int count = 0; uint64_t bytes = 0; };
    struct DirStats {
        std::string name;
        int totalFiles = 0;
        uint64_t totalBytes = 0;
        std::map<std::string, ExtStats> byExt;
    };
    std::vector<DirStats> dirs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
        if (entry.is_regular_file()) continue;  // skip top-level files
        if (!entry.is_directory()) continue;
        DirStats d;
        d.name = entry.path().filename().string();
        for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
            if (!f.is_regular_file()) continue;
            std::string ext = f.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext.empty()) ext = "(no-ext)";
            uint64_t sz = f.file_size(ec);
            if (ec) continue;
            d.totalFiles++;
            d.totalBytes += sz;
            auto& es = d.byExt[ext];
            es.count++;
            es.bytes += sz;
        }
        dirs.push_back(std::move(d));
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const DirStats& a, const DirStats& b) {
                  return a.totalBytes > b.totalBytes;
              });
    int totalDirs = static_cast<int>(dirs.size());
    int totalFiles = 0;
    uint64_t totalBytes = 0;
    for (const auto& d : dirs) {
        totalFiles += d.totalFiles;
        totalBytes += d.totalBytes;
    }
    std::printf("%s/  (%d dirs, %d files, %.1f MB)\n",
                dataDir.c_str(), totalDirs, totalFiles,
                totalBytes / (1024.0 * 1024.0));
    for (size_t k = 0; k < dirs.size(); ++k) {
        bool lastDir = (k == dirs.size() - 1);
        const auto& d = dirs[k];
        const char* dBranch = lastDir ? "└─ " : "├─ ";
        const char* dCont   = lastDir ? "   " : "│  ";
        std::printf("%s%s/  (%d files, %.1f MB)\n",
                    dBranch, d.name.c_str(), d.totalFiles,
                    d.totalBytes / (1024.0 * 1024.0));
        // Sort extensions by byte size descending - heaviest first.
        std::vector<std::pair<std::string, ExtStats>> exts(
            d.byExt.begin(), d.byExt.end());
        std::sort(exts.begin(), exts.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.bytes > b.second.bytes;
                  });
        for (size_t e = 0; e < exts.size(); ++e) {
            bool lastE = (e == exts.size() - 1);
            const char* eBranch = lastE ? "└─ " : "├─ ";
            const auto& [ext, st] = exts[e];
            std::printf("%s%s%-10s  %5d files  %8.1f KB\n",
                        dCont, eBranch, ext.c_str(),
                        st.count, st.bytes / 1024.0);
        }
    }
    return 0;
}

int handleInfoExtractBudget(int& i, int argc, char** argv) {
    // Per-extension byte breakdown of an extract dir, sorted
    // largest-first. Companion to --info-pack-budget (which
    // operates on .wcp archives) - this answers 'where did my
    // 31 GB extract go?' with a flat sortable table.
    std::string dataDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(dataDir) || !fs::is_directory(dataDir)) {
        std::fprintf(stderr,
            "info-extract-budget: %s is not a directory\n",
            dataDir.c_str());
        return 1;
    }
    std::map<std::string, std::pair<int, uint64_t>> byExt;
    uint64_t totalBytes = 0;
    int totalFiles = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dataDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c) { return std::tolower(c); });
        if (ext.empty()) ext = "(no-ext)";
        uint64_t sz = entry.file_size(ec);
        if (ec) continue;
        byExt[ext].first++;
        byExt[ext].second += sz;
        totalBytes += sz;
        totalFiles++;
    }
    std::vector<std::pair<std::string, std::pair<int, uint64_t>>> sorted(
        byExt.begin(), byExt.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  return a.second.second > b.second.second;
              });
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dataDir;
        j["totalFiles"] = totalFiles;
        j["totalBytes"] = totalBytes;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [ext, cb] : sorted) {
            arr.push_back({{"ext", ext},
                            {"count", cb.first},
                            {"bytes", cb.second}});
        }
        j["byExtension"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Extract budget: %s\n", dataDir.c_str());
    std::printf("  total: %d file(s), %.2f MB\n",
                totalFiles, totalBytes / (1024.0 * 1024.0));
    std::printf("\n  ext           count        bytes        MB    share\n");
    // Cap to top 30 to keep output manageable on huge extracts;
    // suppressed entries roll into 'other'.
    const size_t kTopN = 30;
    uint64_t otherBytes = 0;
    int otherCount = 0;
    for (size_t k = 0; k < sorted.size(); ++k) {
        if (k < kTopN) {
            const auto& [ext, cb] = sorted[k];
            double pct = totalBytes > 0
                ? 100.0 * cb.second / totalBytes : 0.0;
            std::printf("  %-12s %6d  %11llu  %8.1f  %5.1f%%\n",
                        ext.c_str(), cb.first,
                        static_cast<unsigned long long>(cb.second),
                        cb.second / (1024.0 * 1024.0), pct);
        } else {
            otherBytes += sorted[k].second.second;
            otherCount += sorted[k].second.first;
        }
    }
    if (otherCount > 0) {
        double pct = totalBytes > 0 ? 100.0 * otherBytes / totalBytes : 0.0;
        std::printf("  %-12s %6d  %11llu  %8.1f  %5.1f%%  (%zu more extensions)\n",
                    "(other)", otherCount,
                    static_cast<unsigned long long>(otherBytes),
                    otherBytes / (1024.0 * 1024.0), pct,
                    sorted.size() - kTopN);
    }
    return 0;
}

int handleListMissingSidecars(int& i, int argc, char** argv) {
    // Actionable counterpart to --info-extract: emit one line per
    // proprietary file lacking its open-format sidecar. Pipe into
    // xargs to drive a targeted re-extract:
    //   wowee_editor --list-missing-sidecars Data/ |
    //     awk '/\.blp$/ {print}' |
    //     xargs asset_extract --emit-png-only
    std::string dataDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(dataDir)) {
        std::fprintf(stderr, "list-missing-sidecars: %s does not exist\n",
                     dataDir.c_str());
        return 1;
    }
    std::vector<std::string> missingPng, missingJson, missingWom,
                             missingWob, missingWhm;
    for (auto& entry : fs::recursive_directory_iterator(dataDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string base = entry.path().string();
        if (base.size() > ext.size())
            base = base.substr(0, base.size() - ext.size());
        auto missing = [&](const char* sidecarExt) {
            return !fs::exists(base + sidecarExt);
        };
        if (ext == ".blp" && missing(".png"))
            missingPng.push_back(entry.path().string());
        else if (ext == ".dbc" && missing(".json"))
            missingJson.push_back(entry.path().string());
        else if (ext == ".m2" && missing(".wom"))
            missingWom.push_back(entry.path().string());
        else if (ext == ".wmo") {
            // Group files (Foo_NNN.wmo) don't get individual sidecars
            // - only the parent file gets a .wob.
            std::string fname = entry.path().filename().string();
            auto under = fname.rfind('_');
            bool isGroup = (under != std::string::npos &&
                            fname.size() - under == 8);
            if (!isGroup && missing(".wob"))
                missingWob.push_back(entry.path().string());
        }
        else if (ext == ".adt" && missing(".whm"))
            missingWhm.push_back(entry.path().string());
    }
    size_t total = missingPng.size() + missingJson.size() +
                   missingWom.size() + missingWob.size() +
                   missingWhm.size();
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dataDir;
        j["totalMissing"] = total;
        j["missing"] = {
            {"png",  missingPng},
            {"json", missingJson},
            {"wom",  missingWom},
            {"wob",  missingWob},
            {"whm",  missingWhm},
        };
        std::printf("%s\n", j.dump(2).c_str());
        return total == 0 ? 0 : 1;
    }
    // Plain mode: one path per line, sorted by group, prefixed with
    // the missing extension so awk/grep can filter.
    auto emit = [](const char* tag, const std::vector<std::string>& files) {
        for (const auto& f : files) std::printf("%s\t%s\n", tag, f.c_str());
    };
    emit("png",  missingPng);
    emit("json", missingJson);
    emit("wom",  missingWom);
    emit("wob",  missingWob);
    emit("whm",  missingWhm);
    std::fprintf(stderr,
        "%zu missing (PNG=%zu JSON=%zu WOM=%zu WOB=%zu WHM=%zu)\n",
        total, missingPng.size(), missingJson.size(),
        missingWom.size(), missingWob.size(), missingWhm.size());
    return total == 0 ? 0 : 1;
}


}  // namespace

bool handleExtractInfo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-extract") == 0 && i + 1 < argc) {
        outRc = handleInfoExtract(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-extract-tree") == 0 && i + 1 < argc) {
        outRc = handleInfoExtractTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-extract-budget") == 0 && i + 1 < argc) {
        outRc = handleInfoExtractBudget(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-missing-sidecars") == 0 && i + 1 < argc) {
        outRc = handleListMissingSidecars(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
