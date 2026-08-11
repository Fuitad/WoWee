#include "cli_realm_list_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_realm_list.hpp"
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

const char* realmTypeName(uint8_t t) {
    using R = wowee::pipeline::WoweeRealmList;
    switch (t) {
        case R::Normal: return "normal";
        case R::PvP:    return "pvp";
        case R::RP:     return "rp";
        case R::RPPvP:  return "rppvp";
        case R::Test:   return "test";
        default:        return "unknown";
    }
}

const char* realmCategoryName(uint8_t c) {
    using R = wowee::pipeline::WoweeRealmList;
    switch (c) {
        case R::Public:  return "public";
        case R::Private: return "private";
        case R::Beta:    return "beta";
        case R::Dev:     return "dev";
        default:         return "unknown";
    }
}

const char* expansionName(uint8_t e) {
    using R = wowee::pipeline::WoweeRealmList;
    switch (e) {
        case R::Vanilla: return "vanilla";
        case R::TBC:     return "tbc";
        case R::WotLK:   return "wotlk";
        case R::Cata:    return "cata";
        default:         return "unknown";
    }
}

const char* populationName(uint8_t p) {
    using R = wowee::pipeline::WoweeRealmList;
    switch (p) {
        case R::Low:    return "low";
        case R::Medium: return "medium";
        case R::High:   return "high";
        case R::Full:   return "full";
        case R::Locked: return "locked";
        default:        return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeRealmList& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmsp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  realms  : %zu\n", c.entries.size());
}

int handleGenSingle(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SingleRealm";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmsp");
    auto c = wowee::pipeline::WoweeRealmListLoader::makeSingleRealm(name);
    if (!saveOrError<wowee::pipeline::WoweeRealmListLoader>(c, base, "gen-msp", ".wmsp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCluster(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvPCluster";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmsp");
    auto c = wowee::pipeline::WoweeRealmListLoader::makePvPCluster(name);
    if (!saveOrError<wowee::pipeline::WoweeRealmListLoader>(c, base, "gen-msp-cluster", ".wmsp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMultiExp(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MultiExpansion";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wmsp");
    auto c = wowee::pipeline::WoweeRealmListLoader::makeMultiExpansion(name);
    if (!saveOrError<wowee::pipeline::WoweeRealmListLoader>(c, base, "gen-msp-multi", ".wmsp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmsp");
    if (!wowee::pipeline::WoweeRealmListLoader::exists(base)) {
        std::fprintf(stderr, "WMSP not found: %s.wmsp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRealmListLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmsp"] = base + ".wmsp";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            char ver[24];
            std::snprintf(ver, sizeof(ver), "%u.%u.%u",
                          e.versionMajor, e.versionMinor,
                          e.versionPatch);
            arr.push_back({
                {"realmId", e.realmId},
                {"name", e.name},
                {"description", e.description},
                {"address", e.address},
                {"realmType", e.realmType},
                {"realmTypeName", realmTypeName(e.realmType)},
                {"realmCategory", e.realmCategory},
                {"realmCategoryName",
                    realmCategoryName(e.realmCategory)},
                {"expansion", e.expansion},
                {"expansionName", expansionName(e.expansion)},
                {"population", e.population},
                {"populationName", populationName(e.population)},
                {"characterCap", e.characterCap},
                {"gmOnly", e.gmOnly != 0},
                {"timezone", e.timezone},
                {"versionMajor", e.versionMajor},
                {"versionMinor", e.versionMinor},
                {"versionPatch", e.versionPatch},
                {"versionString", ver},
                {"buildNumber", e.buildNumber},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMSP: %s.wmsp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  realms  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  type     category  expansion  pop      cap  gm   build   address\n");
    for (const auto& e : c.entries) {
        std::printf("  %3u  %-7s  %-7s   %-9s  %-7s  %3u  %s   %5u   %s\n",
                    e.realmId,
                    realmTypeName(e.realmType),
                    realmCategoryName(e.realmCategory),
                    expansionName(e.expansion),
                    populationName(e.population),
                    e.characterCap,
                    e.gmOnly ? "Y" : "n",
                    e.buildNumber,
                    e.address.c_str());
        std::printf("           %s\n", e.name.c_str());
    }
    return 0;
}

// Token parsers for the four WMSP enums. Each returns -1
// if the token doesn't match any known value.
int parseRealmTypeToken(const std::string& s) {
    using R = wowee::pipeline::WoweeRealmList;
    if (s == "normal") return R::Normal;
    if (s == "pvp")    return R::PvP;
    if (s == "rp")     return R::RP;
    if (s == "rppvp")  return R::RPPvP;
    if (s == "test")   return R::Test;
    return -1;
}

int parseRealmCategoryToken(const std::string& s) {
    using R = wowee::pipeline::WoweeRealmList;
    if (s == "public")  return R::Public;
    if (s == "private") return R::Private;
    if (s == "beta")    return R::Beta;
    if (s == "dev")     return R::Dev;
    return -1;
}

int parseExpansionToken(const std::string& s) {
    using R = wowee::pipeline::WoweeRealmList;
    if (s == "vanilla") return R::Vanilla;
    if (s == "tbc")     return R::TBC;
    if (s == "wotlk")   return R::WotLK;
    if (s == "cata")    return R::Cata;
    return -1;
}

int parsePopulationToken(const std::string& s) {
    using R = wowee::pipeline::WoweeRealmList;
    if (s == "low")    return R::Low;
    if (s == "medium") return R::Medium;
    if (s == "high")   return R::High;
    if (s == "full")   return R::Full;
    if (s == "locked") return R::Locked;
    return -1;
}

// Generic "int OR token string" coercion helper. Returns
// true if a value was successfully extracted; assigns
// into outValue. Reports parse failures via stderr with
// the supplied label so the operator knows which field of
// which entry failed.
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
                    "import-wmsp-json: unknown %s token "
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
    return true;   // field absent — leave outValue at default
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wmsp");
    if (out.empty()) out = base + ".wmsp.json";
    if (!wowee::pipeline::WoweeRealmListLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wmsp-json: WMSP not found: %s.wmsp\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRealmListLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WMSP";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        char ver[24];
        std::snprintf(ver, sizeof(ver), "%u.%u.%u",
                      e.versionMajor, e.versionMinor,
                      e.versionPatch);
        arr.push_back({
            {"realmId", e.realmId},
            {"name", e.name},
            {"description", e.description},
            {"address", e.address},
            {"realmType", e.realmType},
            {"realmTypeName", realmTypeName(e.realmType)},
            {"realmCategory", e.realmCategory},
            {"realmCategoryName",
                realmCategoryName(e.realmCategory)},
            {"expansion", e.expansion},
            {"expansionName", expansionName(e.expansion)},
            {"population", e.population},
            {"populationName", populationName(e.population)},
            {"characterCap", e.characterCap},
            {"gmOnly", e.gmOnly != 0},
            {"timezone", e.timezone},
            {"versionMajor", e.versionMajor},
            {"versionMinor", e.versionMinor},
            {"versionPatch", e.versionPatch},
            {"versionString", ver},
            {"buildNumber", e.buildNumber},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wmsp-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu realms)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wmsp");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wmsp-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wmsp-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeRealmList c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wmsp-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeRealmList::Entry e;
        e.realmId = je.value("realmId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.address = je.value("address", std::string{});
        if (!readEnumField(je, "realmType", "realmTypeName",
                            parseRealmTypeToken, "realmType",
                            e.realmId, e.realmType)) return 1;
        if (!readEnumField(je, "realmCategory",
                            "realmCategoryName",
                            parseRealmCategoryToken,
                            "realmCategory",
                            e.realmId, e.realmCategory)) return 1;
        if (!readEnumField(je, "expansion", "expansionName",
                            parseExpansionToken, "expansion",
                            e.realmId, e.expansion)) return 1;
        if (!readEnumField(je, "population", "populationName",
                            parsePopulationToken, "population",
                            e.realmId, e.population)) return 1;
        e.characterCap = static_cast<uint8_t>(
            je.value("characterCap", 10u));
        if (je.contains("gmOnly")) {
            const auto& g = je["gmOnly"];
            if (g.is_boolean())
                e.gmOnly = g.get<bool>() ? 1 : 0;
            else if (g.is_number_integer())
                e.gmOnly = static_cast<uint8_t>(
                    g.get<int>() != 0 ? 1 : 0);
        }
        e.timezone = static_cast<uint8_t>(
            je.value("timezone", 8u));
        e.versionMajor = static_cast<uint8_t>(
            je.value("versionMajor", 3u));
        e.versionMinor = static_cast<uint8_t>(
            je.value("versionMinor", 3u));
        e.versionPatch = static_cast<uint8_t>(
            je.value("versionPatch", 5u));
        e.buildNumber = je.value("buildNumber", 12340u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeRealmListLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmsp-json: failed to save %s.wmsp\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmsp (%zu realms)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wmsp");
    if (!wowee::pipeline::WoweeRealmListLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wmsp: WMSP not found: %s.wmsp\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeRealmListLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    std::set<std::string> namesSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.realmId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.realmId == 0)
            errors.push_back(ctx + ": realmId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.address.empty()) {
            errors.push_back(ctx +
                ": address is empty — login server cannot "
                "route session to this realm");
        }
        // Address must contain a colon-separated port.
        if (!e.address.empty() &&
            e.address.find(':') == std::string::npos) {
            warnings.push_back(ctx + ": address '" + e.address +
                "' has no port — login client typically "
                "expects 'host:port' form (defaults to "
                "8085 if absent)");
        }
        // Validate realmType against known enum values.
        using R = wowee::pipeline::WoweeRealmList;
        if (e.realmType != R::Normal && e.realmType != R::PvP &&
            e.realmType != R::RP && e.realmType != R::RPPvP &&
            e.realmType != R::Test) {
            errors.push_back(ctx + ": realmType " +
                std::to_string(e.realmType) +
                " is not a known value (0/1/4/6/8)");
        }
        if (e.realmCategory > 3) {
            errors.push_back(ctx + ": realmCategory " +
                std::to_string(e.realmCategory) +
                " out of range (must be 0..3)");
        }
        if (e.expansion > 3) {
            errors.push_back(ctx + ": expansion " +
                std::to_string(e.expansion) +
                " out of range (must be 0..3)");
        }
        if (e.population > 4) {
            errors.push_back(ctx + ": population " +
                std::to_string(e.population) +
                " out of range (must be 0..4)");
        }
        if (e.characterCap == 0) {
            errors.push_back(ctx +
                ": characterCap=0 — players can't create "
                "any character on this realm");
        }
        // Build number sanity check — known WoW build
        // numbers are at least 5000.
        if (e.buildNumber > 0 && e.buildNumber < 5000) {
            warnings.push_back(ctx + ": buildNumber " +
                std::to_string(e.buildNumber) +
                " < 5000 — known WoW client builds start "
                "at 5875 (Vanilla 1.12.1)");
        }
        // Realm names must be unique on the picker.
        if (!namesSeen.insert(e.name).second) {
            errors.push_back(ctx +
                ": duplicate realm name '" + e.name +
                "' — picker requires unique display names");
        }
        if (!idsSeen.add(e.realmId)) errors.push_back(ctx + ": duplicate realmId");
    }
    return cli::reportValidation("wmsp", base, jsonOut, errors, warnings,
                                 formatted("%zu realms, all realmIds + names "
                    "unique", c.entries.size()));
}

} // namespace

bool handleRealmListCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-msp") == 0 && i + 1 < argc) {
        outRc = handleGenSingle(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-msp-cluster") == 0 && i + 1 < argc) {
        outRc = handleGenCluster(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-msp-multi") == 0 && i + 1 < argc) {
        outRc = handleGenMultiExp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmsp") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmsp") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmsp-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmsp-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
