#include "cli_currency_types_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_currency_types.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
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


void printGenSummary(const wowee::pipeline::WoweeCurrencyType& c,
                     const std::string& base) {
    std::printf("Wrote %s.wctr\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  currencies : %zu\n", c.entries.size());
}

int handleGenPvP(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvPCurrencies";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wctr");
    auto c = wowee::pipeline::WoweeCurrencyTypeLoader::makePvP(name);
    if (!saveOrError<wowee::pipeline::WoweeCurrencyTypeLoader>(c, base, "gen-ctr", ".wctr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPvE(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvECurrencies";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wctr");
    auto c = wowee::pipeline::WoweeCurrencyTypeLoader::makePvE(name);
    if (!saveOrError<wowee::pipeline::WoweeCurrencyTypeLoader>(c, base, "gen-ctr-pve", ".wctr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFactionTokens(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FactionTokens";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wctr");
    auto c = wowee::pipeline::WoweeCurrencyTypeLoader::makeFactionTokens(name);
    if (!saveOrError<wowee::pipeline::WoweeCurrencyTypeLoader>(c, base, "gen-ctr-faction", ".wctr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wctr");
    if (!wowee::pipeline::WoweeCurrencyTypeLoader::exists(base)) {
        std::fprintf(stderr, "WCTR not found: %s.wctr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCurrencyTypeLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wctr"] = base + ".wctr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"currencyId", e.currencyId},
                {"name", e.name},
                {"description", e.description},
                {"itemId", e.itemId},
                {"maxQuantity", e.maxQuantity},
                {"maxQuantityWeekly", e.maxQuantityWeekly},
                {"categoryId", e.categoryId},
                {"currencyKind", e.currencyKind},
                {"currencyKindName", wowee::pipeline::WoweeCurrencyType::currencyKindName(e.currencyKind)},
                {"isAccountWide", e.isAccountWide != 0},
                {"iconPath", e.iconPath},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCTR: %s.wctr\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  currencies : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id      kind            item    maxQ    maxWeek  cat   acct  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %7u   %-13s   %5u   %6u    %6u  %4u   %s    %s\n",
                    e.currencyId,
                    wowee::pipeline::WoweeCurrencyType::currencyKindName(e.currencyKind),
                    e.itemId,
                    e.maxQuantity, e.maxQuantityWeekly,
                    e.categoryId,
                    e.isAccountWide ? "yes" : "no ",
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wctr");
    if (!wowee::pipeline::WoweeCurrencyTypeLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wctr-json: WCTR not found: %s.wctr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCurrencyTypeLoader::load(base);
    if (outPath.empty()) outPath = base + ".wctr.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["currencyId"] = e.currencyId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["itemId"] = e.itemId;
        je["maxQuantity"] = e.maxQuantity;
        je["maxQuantityWeekly"] = e.maxQuantityWeekly;
        je["categoryId"] = e.categoryId;
        je["currencyKind"] = e.currencyKind;
        je["currencyKindName"] =
            wowee::pipeline::WoweeCurrencyType::currencyKindName(e.currencyKind);
        je["isAccountWide"] = e.isAccountWide != 0;
        je["iconPath"] = e.iconPath;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wctr-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  currencies : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseCurrencyKindToken(const nlohmann::json& jv,
                               uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeCurrencyType::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "pvp-honor" ||
            s == "pvphonor")     return wowee::pipeline::WoweeCurrencyType::PvPHonor;
        if (s == "pve-raid" ||
            s == "pveraid")      return wowee::pipeline::WoweeCurrencyType::PvERaid;
        if (s == "faction-token" ||
            s == "factiontoken") return wowee::pipeline::WoweeCurrencyType::FactionToken;
        if (s == "event-token" ||
            s == "eventtoken")   return wowee::pipeline::WoweeCurrencyType::EventToken;
        if (s == "crafting")     return wowee::pipeline::WoweeCurrencyType::Crafting;
        if (s == "misc")         return wowee::pipeline::WoweeCurrencyType::Misc;
    }
    return fallback;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wctr-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wctr-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCurrencyType c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCurrencyType::Entry e;
            if (je.contains("currencyId"))   e.currencyId = je["currencyId"].get<uint32_t>();
            if (je.contains("name"))         e.name = je["name"].get<std::string>();
            if (je.contains("description"))  e.description = je["description"].get<std::string>();
            if (je.contains("itemId"))       e.itemId = je["itemId"].get<uint32_t>();
            if (je.contains("maxQuantity"))  e.maxQuantity = je["maxQuantity"].get<uint32_t>();
            if (je.contains("maxQuantityWeekly")) e.maxQuantityWeekly = je["maxQuantityWeekly"].get<uint32_t>();
            if (je.contains("categoryId"))   e.categoryId = je["categoryId"].get<uint32_t>();
            uint8_t kind = wowee::pipeline::WoweeCurrencyType::PvPHonor;
            if (je.contains("currencyKind"))
                kind = parseCurrencyKindToken(je["currencyKind"], kind);
            else if (je.contains("currencyKindName"))
                kind = parseCurrencyKindToken(je["currencyKindName"], kind);
            e.currencyKind = kind;
            if (je.contains("isAccountWide")) {
                if (je["isAccountWide"].is_boolean())
                    e.isAccountWide = je["isAccountWide"].get<bool>() ? 1 : 0;
                else
                    e.isAccountWide = je["isAccountWide"].get<uint8_t>() ? 1 : 0;
            }
            if (je.contains("iconPath"))     e.iconPath = je["iconPath"].get<std::string>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wctr");
    outBase = cli::withoutExt(outBase, ".wctr");
    if (!wowee::pipeline::WoweeCurrencyTypeLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wctr-json: failed to save %s.wctr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wctr\n", outBase.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  currencies : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wctr");
    if (!wowee::pipeline::WoweeCurrencyTypeLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wctr: WCTR not found: %s.wctr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCurrencyTypeLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.currencyId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.currencyId == 0)
            errors.push_back(ctx + ": currencyId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.currencyKind > wowee::pipeline::WoweeCurrencyType::Misc) {
            errors.push_back(ctx + ": currencyKind " +
                std::to_string(e.currencyKind) + " not in 0..5");
        }
        if (e.maxQuantity != 0 &&
            e.maxQuantityWeekly != 0 &&
            e.maxQuantityWeekly > e.maxQuantity) {
            warnings.push_back(ctx +
                ": maxQuantityWeekly " +
                std::to_string(e.maxQuantityWeekly) +
                " > maxQuantity " +
                std::to_string(e.maxQuantity) +
                " — weekly cap exceeds absolute cap, "
                "weekly cap will never be reached");
        }
        // Faction tokens with no categoryId can't reference
        // a faction — break the rep gate.
        if (e.currencyKind == wowee::pipeline::WoweeCurrencyType::FactionToken &&
            e.categoryId == 0) {
            warnings.push_back(ctx +
                ": FactionToken kind with categoryId=0 — "
                "no faction is associated, rep gate will not "
                "trigger");
        }
        // Currencies with no caps at all and no item backing
        // are likely misconfigured.
        if (e.maxQuantity == 0 && e.maxQuantityWeekly == 0 &&
            e.itemId == 0 && e.iconPath.empty()) {
            warnings.push_back(ctx +
                ": no caps + no itemId + no iconPath — "
                "currency has no display data and unbounded "
                "earn rate");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.currencyId) {
                errors.push_back(ctx + ": duplicate currencyId");
                break;
            }
        }
        idsSeen.push_back(e.currencyId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wctr", base, errors, warnings);
    std::printf("validate-wctr: %s.wctr\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu currencies, all currencyIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleCurrencyTypesCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-ctr") == 0 && i + 1 < argc) {
        outRc = handleGenPvP(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ctr-pve") == 0 && i + 1 < argc) {
        outRc = handleGenPvE(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ctr-faction") == 0 && i + 1 < argc) {
        outRc = handleGenFactionTokens(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wctr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wctr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wctr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wctr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
