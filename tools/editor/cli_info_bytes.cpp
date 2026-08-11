#include "cli_info_bytes.hpp"
#include "zone_manifest.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoZoneBytes(int& i, int argc, char** argv) {
    // Per-file size breakdown grouped by category, sorted by size
    // descending. Useful for capacity planning ('which file is
    // 80% of my zone?') and pre-strip-zone audits ('how much
    // would --strip-zone free?'). --zone-stats aggregates across
    // multiple zones; this drills into one zone's contents.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr,
            "info-zone-bytes: %s does not exist\n", zoneDir.c_str());
        return 1;
    }
    // Categorize by extension into source vs derived buckets so
    // the breakdown surfaces what would be stripped.
    struct Entry {
        std::string path;  // relative to zoneDir
        uint64_t bytes;
        std::string category;
    };
    std::vector<Entry> entries;
    uint64_t totalBytes = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::string name = e.path().filename().string();
        std::string rel = fs::relative(e.path(), zoneDir, ec).string();
        if (ec) rel = e.path().string();
        std::string cat;
        if (ext == ".whm" || ext == ".wot" || ext == ".woc") cat = "terrain";
        else if (ext == ".wom") cat = "model (open)";
        else if (ext == ".wob") cat = "building (open)";
        else if (ext == ".m2" || ext == ".skin") cat = "model (proprietary)";
        else if (ext == ".wmo") cat = "building (proprietary)";
        else if (ext == ".blp") cat = "texture (proprietary)";
        else if (ext == ".png") cat = "texture (open/derived)";
        else if (ext == ".dbc") cat = "DBC (proprietary)";
        else if (ext == ".json") cat = "json (source)";
        else if (ext == ".glb" || ext == ".obj" || ext == ".stl") cat = "3D export (derived)";
        else if (ext == ".html" || ext == ".dot" || ext == ".csv") cat = "doc (derived)";
        else if (name == "ZONE.md" || name == "DEPS.md") cat = "doc (derived)";
        else cat = "other";
        uint64_t sz = e.file_size(ec);
        if (ec) continue;
        totalBytes += sz;
        entries.push_back({rel, sz, cat});
    }
    // Sort largest first so the heaviest contributors are at the
    // top of the table.
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.bytes > b.bytes; });
    // Aggregate per-category for the summary footer.
    std::map<std::string, std::pair<uint64_t, int>> byCategory;
    for (const auto& e : entries) {
        byCategory[e.category].first += e.bytes;
        byCategory[e.category].second++;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["totalBytes"] = totalBytes;
        j["fileCount"] = entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : entries) {
            arr.push_back({{"path", e.path},
                           {"bytes", e.bytes},
                           {"category", e.category}});
        }
        j["files"] = arr;
        nlohmann::json catObj;
        for (const auto& [c, p] : byCategory) {
            catObj[c] = {{"bytes", p.first}, {"count", p.second}};
        }
        j["byCategory"] = catObj;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone bytes: %s\n", zoneDir.c_str());
    std::printf("  total: %llu bytes (%.1f KB) across %zu file(s)\n",
                static_cast<unsigned long long>(totalBytes),
                totalBytes / 1024.0, entries.size());
    std::printf("\n  Per-file (largest first):\n");
    std::printf("  %-50s %12s  category\n", "path", "bytes");
    for (const auto& e : entries) {
        std::printf("  %-50s %12llu  %s\n",
                    e.path.substr(0, 50).c_str(),
                    static_cast<unsigned long long>(e.bytes),
                    e.category.c_str());
    }
    std::printf("\n  Per-category:\n");
    for (const auto& [c, p] : byCategory) {
        std::printf("  %-26s %4d files  %12llu bytes  (%5.1f%%)\n",
                    c.c_str(), p.second,
                    static_cast<unsigned long long>(p.first),
                    totalBytes ? (100.0 * p.first / totalBytes) : 0.0);
    }
    return 0;
}

int handleInfoProjectBytes(int& i, int argc, char** argv) {
    // Project-wide byte audit. Walks every zone in projectDir,
    // re-uses --info-zone-bytes' categorization, and prints a
    // per-zone breakdown table plus aggregated category totals.
    // The headline number is the proprietary-vs-open size split
    // — surfaces how much disk a project still spends on .m2/
    // .wmo/.blp/.dbc payloads vs the open WOM/WOB/PNG/JSON
    // replacements.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-bytes: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // Same categorizer used by --info-zone-bytes — keep in sync
    // if categories evolve there.
    auto categorize = [](const fs::path& p) -> std::string {
        std::string ext = p.extension().string();
        std::string name = p.filename().string();
        if (ext == ".whm" || ext == ".wot" || ext == ".woc") return "terrain";
        if (ext == ".wom") return "model (open)";
        if (ext == ".wob") return "building (open)";
        if (ext == ".m2" || ext == ".skin") return "model (proprietary)";
        if (ext == ".wmo") return "building (proprietary)";
        if (ext == ".blp") return "texture (proprietary)";
        if (ext == ".png") return "texture (open/derived)";
        if (ext == ".dbc") return "DBC (proprietary)";
        if (ext == ".json") return "json (source)";
        if (ext == ".glb" || ext == ".obj" || ext == ".stl") return "3D export (derived)";
        if (ext == ".html" || ext == ".dot" || ext == ".csv") return "doc (derived)";
        if (name == "ZONE.md" || name == "DEPS.md") return "doc (derived)";
        return "other";
    };
    // The proprietary-vs-open split is a key quality metric for
    // the open-format migration push. Anything tagged "(open)"
    // or "(open/derived)" counts toward open; anything tagged
    // "(proprietary)" counts toward proprietary; everything
    // else ("terrain" / "json (source)" / derived docs) is
    // neutral.
    auto isOpen = [](const std::string& cat) {
        return cat.find("(open") != std::string::npos;
    };
    auto isProprietary = [](const std::string& cat) {
        return cat.find("(proprietary)") != std::string::npos;
    };
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    struct ZRow {
        std::string name;
        uint64_t totalBytes = 0;
        int fileCount = 0;
        uint64_t openBytes = 0;
        uint64_t propBytes = 0;
    };
    std::vector<ZRow> rows;
    std::map<std::string, std::pair<uint64_t, int>> globalCat;
    uint64_t projectBytes = 0;
    int projectFiles = 0;
    for (const auto& zoneDir : zones) {
        ZRow r;
        r.name = fs::path(zoneDir).filename().string();
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            uint64_t sz = e.file_size(ec);
            if (ec) continue;
            std::string cat = categorize(e.path());
            r.totalBytes += sz;
            r.fileCount++;
            if (isOpen(cat)) r.openBytes += sz;
            else if (isProprietary(cat)) r.propBytes += sz;
            globalCat[cat].first += sz;
            globalCat[cat].second++;
        }
        projectBytes += r.totalBytes;
        projectFiles += r.fileCount;
        rows.push_back(r);
    }
    uint64_t globalOpen = 0, globalProp = 0;
    for (const auto& [c, p] : globalCat) {
        if (isOpen(c)) globalOpen += p.first;
        else if (isProprietary(c)) globalProp += p.first;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["totalBytes"] = projectBytes;
        j["fileCount"] = projectFiles;
        j["openBytes"] = globalOpen;
        j["proprietaryBytes"] = globalProp;
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& r : rows) {
            zarr.push_back({{"name", r.name},
                            {"totalBytes", r.totalBytes},
                            {"fileCount", r.fileCount},
                            {"openBytes", r.openBytes},
                            {"proprietaryBytes", r.propBytes}});
        }
        j["zones"] = zarr;
        nlohmann::json catObj;
        for (const auto& [c, p] : globalCat) {
            catObj[c] = {{"bytes", p.first}, {"count", p.second}};
        }
        j["byCategory"] = catObj;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project bytes: %s\n", projectDir.c_str());
    std::printf("  total : %llu bytes (%.1f KB) across %d file(s) in %zu zone(s)\n",
                static_cast<unsigned long long>(projectBytes),
                projectBytes / 1024.0, projectFiles, zones.size());
    std::printf("\n  zone                   files       bytes   open(B)  prop(B)\n");
    for (const auto& r : rows) {
        std::printf("  %-22s %5d  %10llu  %8llu  %7llu\n",
                    r.name.substr(0, 22).c_str(),
                    r.fileCount,
                    static_cast<unsigned long long>(r.totalBytes),
                    static_cast<unsigned long long>(r.openBytes),
                    static_cast<unsigned long long>(r.propBytes));
    }
    std::printf("\n  Per-category (project-wide):\n");
    for (const auto& [c, p] : globalCat) {
        std::printf("  %-26s %4d files  %12llu bytes  (%5.1f%%)\n",
                    c.c_str(), p.second,
                    static_cast<unsigned long long>(p.first),
                    projectBytes ? (100.0 * p.first / projectBytes) : 0.0);
    }
    std::printf("\n  Open-vs-proprietary split:\n");
    std::printf("    open         : %12llu bytes\n",
                static_cast<unsigned long long>(globalOpen));
    std::printf("    proprietary  : %12llu bytes\n",
                static_cast<unsigned long long>(globalProp));
    uint64_t denom = globalOpen + globalProp;
    if (denom > 0) {
        std::printf("    open share   : %5.1f%%\n", 100.0 * globalOpen / denom);
    }
    return 0;
}

}  // namespace

bool handleInfoBytes(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone-bytes") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneBytes(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-bytes") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectBytes(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
