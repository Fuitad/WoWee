#include "cli_items.hpp"
#include "zone_manifest.hpp"
#include "cli_subprocess.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
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

int handleListItems(int& i, int argc, char** argv) {
    // Inspect <zoneDir>/items.json. Pretty-prints id / quality
    // / item level / display id / name as a table; also
    // supports --json for machine-readable output.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string path = zoneDir + "/items.json";
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "list-items: %s has no items.json\n", zoneDir.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        std::ifstream in(path);
        in >> doc;
    } catch (...) {
        std::fprintf(stderr,
            "list-items: %s is not valid JSON\n", path.c_str());
        return 1;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        std::fprintf(stderr,
            "list-items: %s has no 'items' array\n", path.c_str());
        return 1;
    }
    const auto& items = doc["items"];
    if (jsonOut) {
        std::printf("%s\n", items.dump(2).c_str());
        return 0;
    }
    static const char* qualityNames[] = {
        "poor", "common", "uncommon", "rare", "epic",
        "legendary", "artifact"
    };
    std::printf("Zone items: %s\n", path.c_str());
    std::printf("  count : %zu\n\n", items.size());
    if (items.empty()) {
        std::printf("  *no items*\n");
        return 0;
    }
    std::printf("  idx   id     ilvl   stack   quality      displayId   name\n");
    for (size_t k = 0; k < items.size(); ++k) {
        const auto& it = items[k];
        uint32_t id = it.value("id", 0u);
        uint32_t quality = it.value("quality", 1u);
        uint32_t ilvl = it.value("itemLevel", 1u);
        uint32_t displayId = it.value("displayId", 0u);
        uint32_t stack = it.value("stackable", 1u);
        std::string name = it.value("name", std::string());
        if (quality > 6) quality = 0;
        std::printf("  %3zu   %5u   %4u   %5u   %-10s   %9u   %s\n",
                    k, id, ilvl, stack,
                    qualityNames[quality], displayId, name.c_str());
    }
    return 0;
}

int handleInfoItem(int& i, int argc, char** argv) {
    // Single-item detail view. Lookup is by id by default;
    // prefix the argument with '#' (e.g., "#3") to look up by
    // 0-based array index instead. Useful for inspecting all
    // fields of a single record without sifting through the
    // full --list-items table.
    std::string zoneDir = argv[++i];
    std::string lookup = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::string path = zoneDir + "/items.json";
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "info-item: %s has no items.json\n", zoneDir.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        std::ifstream in(path);
        in >> doc;
    } catch (...) {
        std::fprintf(stderr,
            "info-item: %s is not valid JSON\n", path.c_str());
        return 1;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        std::fprintf(stderr,
            "info-item: %s has no 'items' array\n", path.c_str());
        return 1;
    }
    const auto& items = doc["items"];
    int foundIdx = -1;
    if (!lookup.empty() && lookup[0] == '#') {
        try {
            int idx = std::stoi(lookup.substr(1));
            if (idx >= 0 && static_cast<size_t>(idx) < items.size())
                foundIdx = idx;
        } catch (...) {}
    } else {
        uint32_t targetId = 0;
        try { targetId = static_cast<uint32_t>(std::stoul(lookup)); }
        catch (...) {
            std::fprintf(stderr,
                "info-item: lookup '%s' is not a number "
                "(use '#N' for index lookup)\n", lookup.c_str());
            return 1;
        }
        for (size_t k = 0; k < items.size(); ++k) {
            if (items[k].contains("id") &&
                items[k]["id"].is_number_unsigned() &&
                items[k]["id"].get<uint32_t>() == targetId) {
                foundIdx = static_cast<int>(k);
                break;
            }
        }
    }
    if (foundIdx < 0) {
        std::fprintf(stderr,
            "info-item: no match for '%s' in %s\n",
            lookup.c_str(), path.c_str());
        return 1;
    }
    const auto& it = items[foundIdx];
    if (jsonOut) {
        std::printf("%s\n", it.dump(2).c_str());
        return 0;
    }
    static const char* qualityNames[] = {
        "poor", "common", "uncommon", "rare", "epic",
        "legendary", "artifact"
    };
    uint32_t quality = it.value("quality", 1u);
    if (quality > 6) quality = 0;
    std::printf("Item %d in %s\n", foundIdx, path.c_str());
    std::printf("  id          : %u\n", it.value("id", 0u));
    std::printf("  name        : %s\n",
                it.value("name", std::string("(unnamed)")).c_str());
    std::printf("  quality     : %u (%s)\n",
                quality, qualityNames[quality]);
    std::printf("  itemLevel   : %u\n", it.value("itemLevel", 1u));
    std::printf("  displayId   : %u\n", it.value("displayId", 0u));
    std::printf("  stackable   : %u\n", it.value("stackable", 1u));
    // Surface any extra fields the user added by hand so
    // info-item stays useful as the schema evolves.
    std::vector<std::string> extras;
    for (auto& [k, v] : it.items()) {
        if (k == "id" || k == "name" || k == "quality" ||
            k == "itemLevel" || k == "displayId" ||
            k == "stackable") continue;
        extras.push_back(k);
    }
    if (!extras.empty()) {
        std::printf("\n  Extra fields:\n");
        for (const auto& k : extras) {
            std::printf("    %s = %s\n",
                        k.c_str(), it[k].dump().c_str());
        }
    }
    return 0;
}

int handleValidateItems(int& i, int argc, char** argv) {
    // Schema validator for items.json. Catches what
    // --add-item / --clone-item only enforce on insertion
    // (e.g., duplicate ids if the file was hand-edited),
    // plus general field-range issues. Exit 1 if any error.
    std::string zoneDir = argv[++i];
    namespace fs = std::filesystem;
    std::string path = zoneDir + "/items.json";
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "validate-items: %s has no items.json\n", zoneDir.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        std::ifstream in(path);
        in >> doc;
    } catch (...) {
        std::fprintf(stderr,
            "validate-items: %s is not valid JSON\n", path.c_str());
        return 1;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        std::fprintf(stderr,
            "validate-items: %s has no 'items' array\n", path.c_str());
        return 1;
    }
    const auto& items = doc["items"];
    std::vector<std::string> errors;
    std::map<uint32_t, std::vector<size_t>> idIndices;  // id -> [item indices]
    for (size_t k = 0; k < items.size(); ++k) {
        const auto& it = items[k];
        if (!it.is_object()) {
            errors.push_back("item " + std::to_string(k) +
                              ": not a JSON object");
            continue;
        }
        if (!it.contains("id") || !it["id"].is_number_unsigned() ||
            it["id"].get<uint32_t>() == 0) {
            errors.push_back("item " + std::to_string(k) +
                              ": missing/invalid 'id' (must be positive uint)");
        } else {
            idIndices[it["id"].get<uint32_t>()].push_back(k);
        }
        if (!it.contains("name") || !it["name"].is_string() ||
            it["name"].get<std::string>().empty()) {
            errors.push_back("item " + std::to_string(k) +
                              ": missing/empty 'name'");
        }
        if (it.contains("quality") && it["quality"].is_number_unsigned()) {
            uint32_t q = it["quality"].get<uint32_t>();
            if (q > 6) {
                errors.push_back("item " + std::to_string(k) +
                                  ": quality " + std::to_string(q) +
                                  " out of range (must be 0..6)");
            }
        }
        // itemLevel / stackable should be reasonable; flag
        // pathological values that almost certainly indicate
        // a typo (e.g., million-level item).
        if (it.contains("itemLevel") &&
            it["itemLevel"].is_number_unsigned()) {
            uint32_t lvl = it["itemLevel"].get<uint32_t>();
            if (lvl > 1000) {
                errors.push_back("item " + std::to_string(k) +
                                  ": itemLevel " + std::to_string(lvl) +
                                  " is suspiciously high (>1000)");
            }
        }
        if (it.contains("stackable") &&
            it["stackable"].is_number_unsigned()) {
            uint32_t s = it["stackable"].get<uint32_t>();
            if (s == 0 || s > 1000) {
                errors.push_back("item " + std::to_string(k) +
                                  ": stackable " + std::to_string(s) +
                                  " out of range (must be 1..1000)");
            }
        }
    }
    for (const auto& [id, indices] : idIndices) {
        if (indices.size() > 1) {
            std::string idxList;
            for (size_t v : indices) {
                if (!idxList.empty()) idxList += ", ";
                idxList += std::to_string(v);
            }
            errors.push_back("duplicate id " + std::to_string(id) +
                              " at item indices [" + idxList + "]");
        }
    }
    std::printf("validate-items: %s\n", path.c_str());
    std::printf("  items checked : %zu\n", items.size());
    std::printf("  errors        : %zu\n", errors.size());
    if (errors.empty()) {
        std::printf("\n  PASSED\n");
        return 0;
    }
    std::printf("\n  Errors:\n");
    for (const auto& e : errors) {
        std::printf("    - %s\n", e.c_str());
    }
    return 1;
}

int handleValidateProjectItems(int& i, int argc, char** argv) {
    // Project-wide wrapper around --validate-items. Spawns
    // the binary per-zone (only zones that have items.json)
    // so each zone's full error report streams through, then
    // aggregates a final tally. Exit 1 if any zone fails.
    //
    // Skips zones without items.json - those have nothing to
    // validate and shouldn't count as failures.
    std::string projectDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "validate-project-items: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        if (!fs::exists(entry.path() / "items.json")) continue;
        zones.push_back(entry.path().string());
    }
    std::sort(zones.begin(), zones.end());
    if (zones.empty()) {
        std::printf("validate-project-items: %s\n", projectDir.c_str());
        std::printf("  no zones with items.json - nothing to validate\n");
        return 0;
    }
    std::string self = argv[0];
    int passed = 0, failed = 0;
    std::printf("validate-project-items: %s\n", projectDir.c_str());
    std::printf("  zones with items : %zu\n\n", zones.size());
    for (const auto& zoneDir : zones) {
        std::printf("--- %s ---\n",
                    fs::path(zoneDir).filename().string().c_str());
        std::fflush(stdout);
        int rc = wowee::editor::cli::runChild(self,
            {"--validate-items", zoneDir});
        if (rc == 0) passed++;
        else failed++;
    }
    std::printf("\n--- summary ---\n");
    std::printf("  passed : %d\n", passed);
    std::printf("  failed : %d\n", failed);
    if (failed == 0) {
        std::printf("\n  ALL ZONES PASSED\n");
        return 0;
    }
    return 1;
}

int handleInfoProjectItems(int& i, int argc, char** argv) {
    // Project-wide rollup of items.json across zones. Reports
    // per-zone item counts plus project-wide totals and a
    // quality histogram. Useful for "do my zones have enough
    // loot variety?" capacity checks.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-items: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    static const char* qualityNames[] = {
        "poor", "common", "uncommon", "rare", "epic",
        "legendary", "artifact"
    };
    struct ZRow {
        std::string name;
        int count = 0;
        int qHist[7] = {};
    };
    std::vector<ZRow> rows;
    int totalItems = 0;
    int globalQHist[7] = {};
    for (const auto& zoneDir : zones) {
        ZRow r;
        r.name = fs::path(zoneDir).filename().string();
        std::string path = zoneDir + "/items.json";
        if (fs::exists(path)) {
            nlohmann::json doc;
            try {
                std::ifstream in(path);
                in >> doc;
            } catch (...) {}
            if (doc.contains("items") && doc["items"].is_array()) {
                r.count = static_cast<int>(doc["items"].size());
                for (const auto& it : doc["items"]) {
                    uint32_t q = it.value("quality", 1u);
                    if (q > 6) q = 0;
                    r.qHist[q]++;
                    globalQHist[q]++;
                }
            }
        }
        totalItems += r.count;
        rows.push_back(r);
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["totalItems"] = totalItems;
        nlohmann::json qual;
        for (int q = 0; q <= 6; ++q) qual[qualityNames[q]] = globalQHist[q];
        j["quality"] = qual;
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& r : rows) {
            nlohmann::json zq;
            for (int q = 0; q <= 6; ++q) zq[qualityNames[q]] = r.qHist[q];
            zarr.push_back({{"name", r.name},
                            {"count", r.count},
                            {"quality", zq}});
        }
        j["zones"] = zarr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project items: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu\n", zones.size());
    std::printf("  total items  : %d\n\n", totalItems);
    std::printf("  Quality histogram (project-wide):\n");
    for (int q = 0; q <= 6; ++q) {
        if (globalQHist[q] == 0) continue;
        std::printf("    %-10s : %d\n", qualityNames[q], globalQHist[q]);
    }
    std::printf("\n  zone                  items   poor common uncommon rare epic legend art\n");
    for (const auto& r : rows) {
        std::printf("  %-20s  %5d  %5d  %6d  %8d  %4d  %4d  %6d  %3d\n",
                    r.name.substr(0, 20).c_str(), r.count,
                    r.qHist[0], r.qHist[1], r.qHist[2],
                    r.qHist[3], r.qHist[4], r.qHist[5], r.qHist[6]);
    }
    return 0;
}


}  // namespace

bool handleItems(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-items") == 0 && i + 1 < argc) {
        outRc = handleListItems(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-item") == 0 && i + 2 < argc) {
        outRc = handleInfoItem(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-items") == 0 && i + 1 < argc) {
        outRc = handleValidateItems(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-project-items") == 0 && i + 1 < argc) {
        outRc = handleValidateProjectItems(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-items") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectItems(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
