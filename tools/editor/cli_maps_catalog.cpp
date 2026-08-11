#include "cli_maps_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_maps.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeMaps& c,
                     const std::string& base) {
    std::printf("Wrote %s.wms\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  maps    : %zu  areas : %zu\n",
                c.maps.size(), c.areas.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterMaps";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wms");
    auto c = wowee::pipeline::WoweeMapsLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeMapsLoader>(c, base, "gen-maps", ".wms")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenClassic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ClassicMaps";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wms");
    auto c = wowee::pipeline::WoweeMapsLoader::makeClassic(name);
    if (!saveOrError<wowee::pipeline::WoweeMapsLoader>(c, base, "gen-maps-classic", ".wms")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBgArena(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BgArenaMaps";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wms");
    auto c = wowee::pipeline::WoweeMapsLoader::makeBgArena(name);
    if (!saveOrError<wowee::pipeline::WoweeMapsLoader>(c, base, "gen-maps-bgarena", ".wms")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wms");
    if (!wowee::pipeline::WoweeMapsLoader::exists(base)) {
        return reportMissing("WMS", base, ".wms");
    }
    auto c = wowee::pipeline::WoweeMapsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wms"] = base + ".wms";
        j["name"] = c.name;
        j["mapCount"] = c.maps.size();
        j["areaCount"] = c.areas.size();
        nlohmann::json ma = nlohmann::json::array();
        for (const auto& m : c.maps) {
            ma.push_back({
                {"mapId", m.mapId},
                {"name", m.name},
                {"shortName", m.shortName},
                {"mapType", m.mapType},
                {"mapTypeName", wowee::pipeline::WoweeMaps::mapTypeName(m.mapType)},
                {"expansionId", m.expansionId},
                {"expansionName", wowee::pipeline::WoweeMaps::expansionName(m.expansionId)},
                {"maxPlayers", m.maxPlayers},
            });
        }
        j["maps"] = ma;
        nlohmann::json aa = nlohmann::json::array();
        for (const auto& a : c.areas) {
            aa.push_back({
                {"areaId", a.areaId},
                {"mapId", a.mapId},
                {"parentAreaId", a.parentAreaId},
                {"name", a.name},
                {"minLevel", a.minLevel},
                {"maxLevel", a.maxLevel},
                {"factionGroup", a.factionGroup},
                {"factionGroupName", wowee::pipeline::WoweeMaps::factionGroupName(a.factionGroup)},
                {"explorationXP", a.explorationXP},
                {"ambienceSoundId", a.ambienceSoundId},
            });
        }
        j["areas"] = aa;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMS: %s.wms\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  maps    : %zu  areas : %zu\n",
                c.maps.size(), c.areas.size());
    if (!c.maps.empty()) {
        std::printf("\n  Maps:\n");
        std::printf("    id    type          expansion  max  short  name\n");
        for (const auto& m : c.maps) {
            std::printf("  %4u  %-12s  %-9s  %3u  %-5s  %s\n",
                        m.mapId,
                        wowee::pipeline::WoweeMaps::mapTypeName(m.mapType),
                        wowee::pipeline::WoweeMaps::expansionName(m.expansionId),
                        m.maxPlayers, m.shortName.c_str(),
                        m.name.c_str());
        }
    }
    if (!c.areas.empty()) {
        std::printf("\n  Areas:\n");
        std::printf("    id     map  parent  level    faction      xp    sound  name\n");
        for (const auto& a : c.areas) {
            std::printf("  %5u   %3u  %5u   %2u-%-2u  %-10s  %4u   %4u   %s\n",
                        a.areaId, a.mapId, a.parentAreaId,
                        a.minLevel, a.maxLevel,
                        wowee::pipeline::WoweeMaps::factionGroupName(a.factionGroup),
                        a.explorationXP, a.ambienceSoundId,
                        a.name.c_str());
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Two top-level arrays (maps / areas)
    // mirroring the binary layout. Three enum-typed fields
    // (mapType, expansionId, factionGroup) emit dual int +
    // name forms for hand-edit clarity.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wms");
    if (outPath.empty()) outPath = base + ".wms.json";
    if (!wowee::pipeline::WoweeMapsLoader::exists(base)) {
        return reportMissing("export-wms-json", "WMS", base, ".wms");
    }
    auto c = wowee::pipeline::WoweeMapsLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json ma = nlohmann::json::array();
    for (const auto& m : c.maps) {
        ma.push_back({
            {"mapId", m.mapId},
            {"name", m.name},
            {"shortName", m.shortName},
            {"mapType", m.mapType},
            {"mapTypeName", wowee::pipeline::WoweeMaps::mapTypeName(m.mapType)},
            {"expansionId", m.expansionId},
            {"expansionName", wowee::pipeline::WoweeMaps::expansionName(m.expansionId)},
            {"maxPlayers", m.maxPlayers},
        });
    }
    j["maps"] = ma;
    nlohmann::json aa = nlohmann::json::array();
    for (const auto& a : c.areas) {
        aa.push_back({
            {"areaId", a.areaId},
            {"mapId", a.mapId},
            {"parentAreaId", a.parentAreaId},
            {"name", a.name},
            {"minLevel", a.minLevel},
            {"maxLevel", a.maxLevel},
            {"factionGroup", a.factionGroup},
            {"factionGroupName", wowee::pipeline::WoweeMaps::factionGroupName(a.factionGroup)},
            {"explorationXP", a.explorationXP},
            {"ambienceSoundId", a.ambienceSoundId},
        });
    }
    j["areas"] = aa;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wms-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wms\n", base.c_str());
    std::printf("  maps   : %zu  areas : %zu\n",
                c.maps.size(), c.areas.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wms");
    outBase = cli::withoutExt(outBase, ".wms");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wms-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wms-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto mapTypeFromName = [](const std::string& s) -> uint8_t {
        if (s == "continent")    return wowee::pipeline::WoweeMaps::Continent;
        if (s == "instance")     return wowee::pipeline::WoweeMaps::Instance;
        if (s == "raid")         return wowee::pipeline::WoweeMaps::Raid;
        if (s == "battleground") return wowee::pipeline::WoweeMaps::Battleground;
        if (s == "arena")        return wowee::pipeline::WoweeMaps::Arena;
        return wowee::pipeline::WoweeMaps::Continent;
    };
    auto expansionFromName = [](const std::string& s) -> uint8_t {
        if (s == "classic") return wowee::pipeline::WoweeMaps::Classic;
        if (s == "tbc")     return wowee::pipeline::WoweeMaps::Tbc;
        if (s == "wotlk")   return wowee::pipeline::WoweeMaps::Wotlk;
        if (s == "cata")    return wowee::pipeline::WoweeMaps::Cata;
        if (s == "mop")     return wowee::pipeline::WoweeMaps::Mop;
        return wowee::pipeline::WoweeMaps::Classic;
    };
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "both")      return wowee::pipeline::WoweeMaps::FactionBoth;
        if (s == "alliance")  return wowee::pipeline::WoweeMaps::FactionAlliance;
        if (s == "horde")     return wowee::pipeline::WoweeMaps::FactionHorde;
        if (s == "contested") return wowee::pipeline::WoweeMaps::FactionContested;
        return wowee::pipeline::WoweeMaps::FactionBoth;
    };
    wowee::pipeline::WoweeMaps c;
    c.name = j.value("name", std::string{});
    if (j.contains("maps") && j["maps"].is_array()) {
        for (const auto& jm : j["maps"]) {
            wowee::pipeline::WoweeMaps::Map m;
            m.mapId = jm.value("mapId", 0u);
            m.name = jm.value("name", std::string{});
            m.shortName = jm.value("shortName", std::string{});
            if (jm.contains("mapType") && jm["mapType"].is_number_integer()) {
                m.mapType = static_cast<uint8_t>(jm["mapType"].get<int>());
            } else if (jm.contains("mapTypeName") && jm["mapTypeName"].is_string()) {
                m.mapType = mapTypeFromName(jm["mapTypeName"].get<std::string>());
            }
            if (jm.contains("expansionId") && jm["expansionId"].is_number_integer()) {
                m.expansionId = static_cast<uint8_t>(jm["expansionId"].get<int>());
            } else if (jm.contains("expansionName") && jm["expansionName"].is_string()) {
                m.expansionId = expansionFromName(jm["expansionName"].get<std::string>());
            }
            m.maxPlayers = static_cast<uint16_t>(jm.value("maxPlayers", 0));
            c.maps.push_back(m);
        }
    }
    if (j.contains("areas") && j["areas"].is_array()) {
        for (const auto& ja : j["areas"]) {
            wowee::pipeline::WoweeMaps::Area a;
            a.areaId = ja.value("areaId", 0u);
            a.mapId = ja.value("mapId", 0u);
            a.parentAreaId = ja.value("parentAreaId", 0u);
            a.name = ja.value("name", std::string{});
            a.minLevel = static_cast<uint16_t>(ja.value("minLevel", 1));
            a.maxLevel = static_cast<uint16_t>(ja.value("maxLevel", 1));
            if (ja.contains("factionGroup") &&
                ja["factionGroup"].is_number_integer()) {
                a.factionGroup = static_cast<uint8_t>(ja["factionGroup"].get<int>());
            } else if (ja.contains("factionGroupName") &&
                       ja["factionGroupName"].is_string()) {
                a.factionGroup = factionFromName(ja["factionGroupName"].get<std::string>());
            }
            a.explorationXP = ja.value("explorationXP", 0u);
            a.ambienceSoundId = ja.value("ambienceSoundId", 0u);
            c.areas.push_back(a);
        }
    }
    if (!wowee::pipeline::WoweeMapsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wms-json: failed to save %s.wms\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wms\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  maps   : %zu  areas : %zu\n",
                c.maps.size(), c.areas.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wms");
    if (!wowee::pipeline::WoweeMapsLoader::exists(base)) {
        return reportMissing("validate-wms", "WMS", base, ".wms");
    }
    auto c = wowee::pipeline::WoweeMapsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.maps.empty()) {
        warnings.push_back("catalog has zero maps");
    }
    std::vector<uint32_t> mapIdsSeen;
    for (size_t k = 0; k < c.maps.size(); ++k) {
        const auto& m = c.maps[k];
        std::string ctx = "map " + std::to_string(k) +
                          " (id=" + std::to_string(m.mapId);
        if (!m.name.empty()) ctx += " " + m.name;
        ctx += ")";
        if (m.name.empty()) {
            errors.push_back(ctx + ": name is empty");
        }
        if (m.mapType > wowee::pipeline::WoweeMaps::Arena) {
            errors.push_back(ctx + ": mapType " +
                std::to_string(m.mapType) + " not in 0..4");
        }
        if (m.expansionId > wowee::pipeline::WoweeMaps::Mop) {
            errors.push_back(ctx + ": expansionId " +
                std::to_string(m.expansionId) + " not in 0..4");
        }
        // Battleground / Arena need a player cap; continent/instance
        // can leave it 0 (unlimited / set by instance template).
        if ((m.mapType == wowee::pipeline::WoweeMaps::Battleground ||
             m.mapType == wowee::pipeline::WoweeMaps::Arena) &&
            m.maxPlayers == 0) {
            warnings.push_back(ctx +
                ": Battleground/Arena with maxPlayers=0 (no participant cap)");
        }
        for (uint32_t prev : mapIdsSeen) {
            if (prev == m.mapId) {
                errors.push_back(ctx + ": duplicate mapId");
                break;
            }
        }
        mapIdsSeen.push_back(m.mapId);
    }
    std::vector<uint32_t> areaIdsSeen;
    for (size_t k = 0; k < c.areas.size(); ++k) {
        const auto& a = c.areas[k];
        std::string ctx = "area " + std::to_string(k) +
                          " (id=" + std::to_string(a.areaId);
        if (!a.name.empty()) ctx += " " + a.name;
        ctx += ")";
        if (a.areaId == 0) {
            errors.push_back(ctx + ": areaId is 0");
        }
        if (a.name.empty()) {
            errors.push_back(ctx + ": name is empty");
        }
        if (a.maxLevel < a.minLevel) {
            errors.push_back(ctx + ": maxLevel < minLevel");
        }
        if (a.factionGroup > wowee::pipeline::WoweeMaps::FactionContested) {
            errors.push_back(ctx + ": factionGroup " +
                std::to_string(a.factionGroup) + " not in 0..3");
        }
        // Area must reference a real map.
        if (!c.findMap(a.mapId)) {
            errors.push_back(ctx + ": mapId " +
                std::to_string(a.mapId) +
                " does not exist in this catalog");
        }
        // parentAreaId (if non-zero) must reference a real area
        // and must be on the same map.
        if (a.parentAreaId != 0) {
            const auto* parent = c.findArea(a.parentAreaId);
            if (!parent) {
                errors.push_back(ctx + ": parentAreaId " +
                    std::to_string(a.parentAreaId) +
                    " does not exist");
            } else if (parent->mapId != a.mapId) {
                errors.push_back(ctx + ": parent area " +
                    std::to_string(a.parentAreaId) +
                    " is on a different map");
            } else if (a.parentAreaId == a.areaId) {
                errors.push_back(ctx + ": area lists itself as parent");
            }
        }
        for (uint32_t prev : areaIdsSeen) {
            if (prev == a.areaId) {
                errors.push_back(ctx + ": duplicate areaId");
                break;
            }
        }
        areaIdsSeen.push_back(a.areaId);
    }
    return cli::reportValidation("wms", base, jsonOut, errors, warnings,
                                 formatted("%zu maps, %zu areas, all IDs unique", c.maps.size(), c.areas.size()));
}

} // namespace

bool handleMapsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-maps") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-maps-classic") == 0 && i + 1 < argc) {
        outRc = handleGenClassic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-maps-bgarena") == 0 && i + 1 < argc) {
        outRc = handleGenBgArena(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wms") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wms") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wms-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wms-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
