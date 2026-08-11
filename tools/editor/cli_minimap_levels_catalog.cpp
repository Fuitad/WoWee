#include "cli_minimap_levels_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_minimap_levels.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeMinimapLevels& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmnl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  levels  : %zu\n", c.entries.size());
}

int handleGenStormwind(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StormwindMinimapLevels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmnl");
    auto c = wowee::pipeline::WoweeMinimapLevelsLoader::makeStormwind(name);
    if (!saveOrError<wowee::pipeline::WoweeMinimapLevelsLoader>(c, base, "gen-mnl", ".wmnl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDalaran(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DalaranMinimapLevels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmnl");
    auto c = wowee::pipeline::WoweeMinimapLevelsLoader::makeDalaran(name);
    if (!saveOrError<wowee::pipeline::WoweeMinimapLevelsLoader>(c, base, "gen-mnl-dalaran", ".wmnl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUndercity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UndercityMinimapLevels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmnl");
    auto c = wowee::pipeline::WoweeMinimapLevelsLoader::makeUndercity(name);
    if (!saveOrError<wowee::pipeline::WoweeMinimapLevelsLoader>(c, base, "gen-mnl-undercity", ".wmnl")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmnl");
    if (!wowee::pipeline::WoweeMinimapLevelsLoader::exists(base)) {
        std::fprintf(stderr, "WMNL not found: %s.wmnl\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMinimapLevelsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmnl"] = base + ".wmnl";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"levelId", e.levelId},
                {"name", e.name},
                {"description", e.description},
                {"mapId", e.mapId},
                {"areaId", e.areaId},
                {"levelIndex", e.levelIndex},
                {"minZ", e.minZ},
                {"maxZ", e.maxZ},
                {"texturePath", e.texturePath},
                {"displayName", e.displayName},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMNL: %s.wmnl\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  levels  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   map   area   idx     minZ      maxZ    name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %4u   %4u    %2u   %7.1f  %7.1f   %s\n",
                    e.levelId, e.mapId, e.areaId,
                    e.levelIndex, e.minZ, e.maxZ,
                    e.displayName.c_str());
        std::printf("           %s\n", e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wmnl");
    if (out.empty()) out = base + ".wmnl.json";
    if (!wowee::pipeline::WoweeMinimapLevelsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wmnl-json: WMNL not found: %s.wmnl\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMinimapLevelsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WMNL";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"levelId", e.levelId},
            {"name", e.name},
            {"description", e.description},
            {"mapId", e.mapId},
            {"areaId", e.areaId},
            {"levelIndex", e.levelIndex},
            {"minZ", e.minZ},
            {"maxZ", e.maxZ},
            {"texturePath", e.texturePath},
            {"displayName", e.displayName},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wmnl-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu levels)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wmnl");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wmnl-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wmnl-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeMinimapLevels c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wmnl-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeMinimapLevels::Entry e;
        e.levelId = je.value("levelId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.mapId = je.value("mapId", 0u);
        e.areaId = je.value("areaId", 0u);
        e.levelIndex = static_cast<uint8_t>(
            je.value("levelIndex", 0u));
        e.minZ = je.value("minZ", 0.0f);
        e.maxZ = je.value("maxZ", 0.0f);
        e.texturePath = je.value("texturePath", std::string{});
        e.displayName = je.value("displayName", std::string{});
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeMinimapLevelsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmnl-json: failed to save %s.wmnl\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmnl (%zu levels)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmnl");
    if (!wowee::pipeline::WoweeMinimapLevelsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wmnl: WMNL not found: %s.wmnl\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMinimapLevelsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Group entries by (mapId, areaId) so we can check
    // per-area constraints (Z-range overlap, levelIndex
    // gap).
    std::map<uint64_t, std::vector<
        const wowee::pipeline::WoweeMinimapLevels::Entry*>>
        byArea;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.levelId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.levelId == 0)
            errors.push_back(ctx + ": levelId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.areaId == 0) {
            errors.push_back(ctx +
                ": areaId is 0 — level is unbound to "
                "any WMS sub-area");
        }
        if (e.minZ >= e.maxZ) {
            errors.push_back(ctx + ": minZ " +
                std::to_string(e.minZ) +
                " >= maxZ " + std::to_string(e.maxZ) +
                " — Z-range is empty or inverted");
        }
        if (e.texturePath.empty()) {
            warnings.push_back(ctx +
                ": texturePath is empty — minimap "
                "overlay layer would render untextured");
        }
        if (e.displayName.empty()) {
            warnings.push_back(ctx +
                ": displayName is empty — UI level "
                "picker would show blank entry");
        }
        if (!idsSeen.insert(e.levelId).second) {
            errors.push_back(ctx + ": duplicate levelId");
        }
        if (e.areaId != 0) {
            uint64_t key = (static_cast<uint64_t>(e.mapId)
                            << 32) | e.areaId;
            byArea[key].push_back(&e);
        }
    }
    // Per-area cross-checks: levelIndex uniqueness and
    // Z-range non-overlap. Two levels at the same
    // levelIndex would confuse the picker UI; overlapping
    // Z-ranges would cause minimap-flicker as the player
    // crosses the overlap region.
    for (auto& [key, levels] : byArea) {
        if (levels.size() < 2) continue;
        uint32_t mapId = static_cast<uint32_t>(key >> 32);
        uint32_t areaId = static_cast<uint32_t>(key & 0xFFFFFFFFu);
        std::sort(levels.begin(), levels.end(),
            [](auto* a, auto* b) {
                return a->levelIndex < b->levelIndex;
            });
        std::set<uint8_t> indicesSeen;
        for (const auto* L : levels) {
            if (!indicesSeen.insert(L->levelIndex).second) {
                errors.push_back("area (mapId=" +
                    std::to_string(mapId) + ", areaId=" +
                    std::to_string(areaId) +
                    "): two levels at levelIndex " +
                    std::to_string(L->levelIndex) +
                    " — picker would show duplicate slot");
            }
        }
        // Z-overlap check: for every pair of levels in
        // this area, their [minZ, maxZ) intervals must
        // not overlap.
        for (size_t a = 0; a < levels.size(); ++a) {
            for (size_t b = a + 1; b < levels.size(); ++b) {
                const auto* La = levels[a];
                const auto* Lb = levels[b];
                if (La->minZ < Lb->maxZ &&
                    Lb->minZ < La->maxZ) {
                    errors.push_back("area (mapId=" +
                        std::to_string(mapId) +
                        ", areaId=" +
                        std::to_string(areaId) +
                        "): Z-range overlap between "
                        "levelIndex " +
                        std::to_string(La->levelIndex) +
                        " (Z " +
                        std::to_string(La->minZ) + "-" +
                        std::to_string(La->maxZ) +
                        ") and levelIndex " +
                        std::to_string(Lb->levelIndex) +
                        " (Z " +
                        std::to_string(Lb->minZ) + "-" +
                        std::to_string(Lb->maxZ) +
                        ") — minimap renderer would "
                        "flicker between layers in the "
                        "overlap region");
                }
            }
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wmnl", base, errors, warnings);
    std::printf("validate-wmnl: %s.wmnl\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu levels, all levelIds + "
                    "per-area levelIndices unique, no "
                    "Z-range overlaps\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleMinimapLevelsCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-mnl") == 0 && i + 1 < argc) {
        outRc = handleGenStormwind(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mnl-dalaran") == 0 &&
        i + 1 < argc) {
        outRc = handleGenDalaran(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mnl-undercity") == 0 &&
        i + 1 < argc) {
        outRc = handleGenUndercity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmnl") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmnl") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmnl-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmnl-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
