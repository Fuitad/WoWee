#include "cli_world_locations_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_world_locations.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* locKindName(uint8_t k) {
    using L = wowee::pipeline::WoweeWorldLocations;
    switch (k) {
        case L::POI:           return "poi";
        case L::RareSpawn:     return "rarespawn";
        case L::HerbNode:      return "herbnode";
        case L::MineralVein:   return "mineralvein";
        case L::FishingSpot:   return "fishingspot";
        case L::AreaTrigger:   return "areatrigger";
        case L::PortalLanding: return "portallanding";
        default:               return "?";
    }
}

const char* factionAccessName(uint8_t f) {
    using L = wowee::pipeline::WoweeWorldLocations;
    switch (f) {
        case L::Both:     return "both";
        case L::Alliance: return "alliance";
        case L::Horde:    return "horde";
        case L::Neutral:  return "neutral";
        default:          return "?";
    }
}

const char* skillIdName(uint16_t s) {
    switch (s) {
        case 0:   return "-";
        case 182: return "Herbalism";
        case 186: return "Mining";
        case 356: return "Fishing";
        default:  return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeWorldLocations& c,
                     const std::string& base) {
    std::printf("Wrote %s.wloc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  locations: %zu\n", c.entries.size());
}

int handleGenAlliancePOIs(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AlliancePOIs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wloc");
    auto c = wowee::pipeline::WoweeWorldLocationsLoader::
        makeAlliancePOIs(name);
    if (!saveOrError<wowee::pipeline::WoweeWorldLocationsLoader>(c, base, "gen-loc-poi", ".wloc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHerbalism(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HerbalismNodes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wloc");
    auto c = wowee::pipeline::WoweeWorldLocationsLoader::
        makeHerbalismNodes(name);
    if (!saveOrError<wowee::pipeline::WoweeWorldLocationsLoader>(c, base, "gen-loc-herb", ".wloc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRareSpawns(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RareSpawns";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wloc");
    auto c = wowee::pipeline::WoweeWorldLocationsLoader::
        makeRareSpawns(name);
    if (!saveOrError<wowee::pipeline::WoweeWorldLocationsLoader>(c, base, "gen-loc-rare", ".wloc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wloc");
    if (!wowee::pipeline::WoweeWorldLocationsLoader::exists(base)) {
        std::fprintf(stderr, "WLOC not found: %s.wloc\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeWorldLocationsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wloc"] = base + ".wloc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"locationId", e.locationId},
                {"name", e.name},
                {"mapId", e.mapId},
                {"areaId", e.areaId},
                {"x", e.x},
                {"y", e.y},
                {"z", e.z},
                {"locKind", e.locKind},
                {"locKindName", locKindName(e.locKind)},
                {"iconIndex", e.iconIndex},
                {"factionAccess", e.factionAccess},
                {"factionAccessName",
                    factionAccessName(e.factionAccess)},
                {"respawnSec", e.respawnSec},
                {"discoverableXp", e.discoverableXp},
                {"requiredSkillId", e.requiredSkillId},
                {"requiredSkillName",
                    skillIdName(e.requiredSkillId)},
                {"requiredSkillLevel", e.requiredSkillLevel},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLOC: %s.wloc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  locations: %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  map  area  kind            fact      respawn   xp   skill           lvl  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %3u  %4u  %-13s   %-8s  %7us  %4u   %-12s    %3u  %s\n",
                    e.locationId, e.mapId, e.areaId,
                    locKindName(e.locKind),
                    factionAccessName(e.factionAccess),
                    e.respawnSec, e.discoverableXp,
                    skillIdName(e.requiredSkillId),
                    e.requiredSkillLevel,
                    e.name.c_str());
    }
    return 0;
}

int parseLocKindToken(const std::string& s) {
    using L = wowee::pipeline::WoweeWorldLocations;
    if (s == "poi")           return L::POI;
    if (s == "rarespawn")     return L::RareSpawn;
    if (s == "herbnode")      return L::HerbNode;
    if (s == "mineralvein")   return L::MineralVein;
    if (s == "fishingspot")   return L::FishingSpot;
    if (s == "areatrigger")   return L::AreaTrigger;
    if (s == "portallanding") return L::PortalLanding;
    return -1;
}

int parseFactionAccessToken(const std::string& s) {
    using L = wowee::pipeline::WoweeWorldLocations;
    if (s == "both")     return L::Both;
    if (s == "alliance") return L::Alliance;
    if (s == "horde")    return L::Horde;
    if (s == "neutral")  return L::Neutral;
    return -1;
}

template <typename ParseFn>
bool readEnumField(const nlohmann::json& je,
                    const char* intKey,
                    const char* nameKey,
                    ParseFn parseFn,
                    const char* label,
                    uint32_t entryId,
                    uint8_t& outValue) {
    if (je.contains(intKey)) {
        const auto& v = je[intKey];
        if (v.is_string()) {
            int parsed = parseFn(v.get<std::string>());
            if (parsed < 0) {
                std::fprintf(stderr,
                    "import-wloc-json: unknown %s token "
                    "'%s' on entry id=%u\n",
                    label, v.get<std::string>().c_str(),
                    entryId);
                return false;
            }
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
        if (v.is_number_integer()) {
            outValue = static_cast<uint8_t>(v.get<int>());
            return true;
        }
    }
    if (je.contains(nameKey) && je[nameKey].is_string()) {
        int parsed = parseFn(je[nameKey].get<std::string>());
        if (parsed >= 0) {
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
    }
    return true;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wloc");
    if (!wowee::pipeline::WoweeWorldLocationsLoader::exists(base)) {
        return reportMissing("validate-wloc", "WLOC", base, ".wloc");
    }
    auto c = wowee::pipeline::WoweeWorldLocationsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    using L = wowee::pipeline::WoweeWorldLocations;
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.locationId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.locationId == 0)
            errors.push_back(ctx + ": locationId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.locKind > 6) {
            errors.push_back(ctx + ": locKind " +
                std::to_string(e.locKind) +
                " out of range (0..6)");
        }
        if (e.factionAccess > 3) {
            errors.push_back(ctx + ": factionAccess " +
                std::to_string(e.factionAccess) +
                " out of range (0..3)");
        }
        // Spawnable kinds REQUIRE respawnSec > 0
        // (otherwise they spawn once and never come
        // back). Static kinds don't.
        bool spawnable =
            e.locKind == L::RareSpawn ||
            e.locKind == L::HerbNode ||
            e.locKind == L::MineralVein ||
            e.locKind == L::FishingSpot;
        if (spawnable && e.respawnSec == 0) {
            errors.push_back(ctx +
                ": spawnable kind (" +
                std::string(locKindName(e.locKind)) +
                ") with respawnSec=0 — entity would "
                "spawn once and never come back");
        }
        // discoverableXp only meaningful for POI kind.
        if (e.discoverableXp > 0 && e.locKind != L::POI) {
            warnings.push_back(ctx +
                ": discoverableXp=" +
                std::to_string(e.discoverableXp) +
                " set but locKind is not POI — XP "
                "would never be awarded (the discovery "
                "flow only fires for POIs)");
        }
        // requiredSkill only meaningful for gather-
        // kinds.
        bool gatherKind =
            e.locKind == L::HerbNode ||
            e.locKind == L::MineralVein ||
            e.locKind == L::FishingSpot;
        if (e.requiredSkillId != 0 && !gatherKind) {
            warnings.push_back(ctx +
                ": requiredSkillId=" +
                std::to_string(e.requiredSkillId) +
                " set but locKind is not a gather "
                "kind — skill check will never fire");
        }
        // Gather kinds with zero requiredSkillLevel
        // BUT non-zero skillId is suspicious — usually
        // a typo.
        if (gatherKind && e.requiredSkillId != 0 &&
            e.requiredSkillLevel == 0) {
            warnings.push_back(ctx +
                ": gather kind with requiredSkillId=" +
                std::to_string(e.requiredSkillId) +
                " but requiredSkillLevel=0 — every "
                "player satisfies; verify intentional");
        }
        if (!idsSeen.insert(e.locationId).second) {
            errors.push_back(ctx + ": duplicate locationId");
        }
    }
    return cli::reportValidation("wloc", base, jsonOut, errors, warnings,
                                 formatted("%zu locations, all locationIds "
                    "unique, locKind 0..6, factionAccess "
                    "0..3, all spawnable kinds (Rare/Herb/"
                    "Mineral/Fishing) have respawnSec > 0", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wloc");
    if (out.empty()) out = base + ".wloc.json";
    if (!wowee::pipeline::WoweeWorldLocationsLoader::exists(base)) {
        return reportMissing("export-wloc-json", "WLOC", base, ".wloc");
    }
    auto c = wowee::pipeline::WoweeWorldLocationsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WLOC";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"locationId", e.locationId},
            {"name", e.name},
            {"mapId", e.mapId},
            {"areaId", e.areaId},
            {"x", e.x},
            {"y", e.y},
            {"z", e.z},
            {"locKind", e.locKind},
            {"locKindName", locKindName(e.locKind)},
            {"iconIndex", e.iconIndex},
            {"factionAccess", e.factionAccess},
            {"factionAccessName",
                factionAccessName(e.factionAccess)},
            {"respawnSec", e.respawnSec},
            {"discoverableXp", e.discoverableXp},
            {"requiredSkillId", e.requiredSkillId},
            {"requiredSkillName",
                skillIdName(e.requiredSkillId)},
            {"requiredSkillLevel", e.requiredSkillLevel},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wloc-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu locations)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wloc");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wloc-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wloc-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeWorldLocations c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wloc-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeWorldLocations::Entry e;
        e.locationId = je.value("locationId", 0u);
        e.name = je.value("name", std::string{});
        e.mapId = je.value("mapId", 0u);
        e.areaId = je.value("areaId", 0u);
        e.x = je.value("x", 0.f);
        e.y = je.value("y", 0.f);
        e.z = je.value("z", 0.f);
        if (!readEnumField(je, "locKind", "locKindName",
                            parseLocKindToken, "locKind",
                            e.locationId, e.locKind)) return 1;
        e.iconIndex = static_cast<uint8_t>(
            je.value("iconIndex", 0));
        if (!readEnumField(je, "factionAccess",
                            "factionAccessName",
                            parseFactionAccessToken,
                            "factionAccess", e.locationId,
                            e.factionAccess)) return 1;
        e.respawnSec = je.value("respawnSec", 0u);
        e.discoverableXp = je.value("discoverableXp", 0u);
        e.requiredSkillId = static_cast<uint16_t>(
            je.value("requiredSkillId", 0));
        e.requiredSkillLevel = static_cast<uint16_t>(
            je.value("requiredSkillLevel", 0));
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeWorldLocationsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wloc-json: failed to save %s.wloc\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wloc (%zu locations)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleWorldLocationsCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-loc-poi") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAlliancePOIs(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-loc-herb") == 0 &&
        i + 1 < argc) {
        outRc = handleGenHerbalism(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-loc-rare") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRareSpawns(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wloc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wloc") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wloc-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wloc-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
