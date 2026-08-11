#include "cli_spell_power_costs_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_power_costs.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeSpellPowerCost& c,
                     const std::string& base) {
    std::printf("Wrote %s.wspc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterPowerCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspc");
    auto c = wowee::pipeline::WoweeSpellPowerCostLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellPowerCostLoader>(c, base, "gen-spc", ".wspc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorRageCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspc");
    auto c = wowee::pipeline::WoweeSpellPowerCostLoader::makeRage(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellPowerCostLoader>(c, base, "gen-spc-rage", ".wspc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMixed(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MixedClassPowerCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspc");
    auto c = wowee::pipeline::WoweeSpellPowerCostLoader::makeMixed(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellPowerCostLoader>(c, base, "gen-spc-mixed", ".wspc")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendCostFlagNames(uint32_t flags, std::string& out) {
    using F = wowee::pipeline::WoweeSpellPowerCost;
    auto add = [&](const char* n) {
        if (!out.empty()) out += "|";
        out += n;
    };
    if (flags & F::RequiresCombatStance) add("RequiresCombatStance");
    if (flags & F::RefundOnMiss)         add("RefundOnMiss");
    if (flags & F::DoublesInForm)        add("DoublesInForm");
    if (flags & F::ScalesWithMastery)    add("ScalesWithMastery");
    if (out.empty()) out = "-";
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspc");
    if (!wowee::pipeline::WoweeSpellPowerCostLoader::exists(base)) {
        return reportMissing("WSPC", base, ".wspc");
    }
    auto c = wowee::pipeline::WoweeSpellPowerCostLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wspc"] = base + ".wspc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string flagNames;
            appendCostFlagNames(e.costFlags, flagNames);
            arr.push_back({
                {"powerCostId", e.powerCostId},
                {"name", e.name},
                {"description", e.description},
                {"powerType", e.powerType},
                {"powerTypeName", wowee::pipeline::WoweeSpellPowerCost::powerTypeName(e.powerType)},
                {"baseCost", e.baseCost},
                {"perLevelCost", e.perLevelCost},
                {"percentOfBase", e.percentOfBase},
                {"costFlags", e.costFlags},
                {"costFlagsLabels", flagNames},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPC: %s.wspc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    powerType    base   /lvl    %%max   flags                                  name\n");
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendCostFlagNames(e.costFlags, flagNames);
        std::printf("  %4u    %-11s  %5d  %5d  %5.2f   %-36s   %s\n",
                    e.powerCostId,
                    wowee::pipeline::WoweeSpellPowerCost::powerTypeName(e.powerType),
                    e.baseCost, e.perLevelCost,
                    e.percentOfBase,
                    flagNames.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wspc");
    if (!wowee::pipeline::WoweeSpellPowerCostLoader::exists(base)) {
        return reportMissing("export-wspc-json", "WSPC", base, ".wspc");
    }
    auto c = wowee::pipeline::WoweeSpellPowerCostLoader::load(base);
    if (outPath.empty()) outPath = base + ".wspc.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendCostFlagNames(e.costFlags, flagNames);
        nlohmann::json je;
        je["powerCostId"] = e.powerCostId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["powerType"] = e.powerType;
        je["powerTypeName"] =
            wowee::pipeline::WoweeSpellPowerCost::powerTypeName(e.powerType);
        je["baseCost"] = e.baseCost;
        je["perLevelCost"] = e.perLevelCost;
        je["percentOfBase"] = e.percentOfBase;
        je["costFlags"] = e.costFlags;
        je["costFlagsLabels"] = flagNames;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wspc-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

uint8_t parsePowerTypeToken(const nlohmann::json& jv,
                            uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellPowerCost::NoCost)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "mana")        return wowee::pipeline::WoweeSpellPowerCost::Mana;
        if (s == "rage")        return wowee::pipeline::WoweeSpellPowerCost::Rage;
        if (s == "focus")       return wowee::pipeline::WoweeSpellPowerCost::Focus;
        if (s == "energy")      return wowee::pipeline::WoweeSpellPowerCost::Energy;
        if (s == "happiness")   return wowee::pipeline::WoweeSpellPowerCost::Happiness;
        if (s == "runic-power" ||
            s == "runicpower")  return wowee::pipeline::WoweeSpellPowerCost::RunicPower;
        if (s == "runes")       return wowee::pipeline::WoweeSpellPowerCost::Runes;
        if (s == "soul-shards" ||
            s == "soulshards")  return wowee::pipeline::WoweeSpellPowerCost::SoulShards;
        if (s == "holy-power" ||
            s == "holypower")   return wowee::pipeline::WoweeSpellPowerCost::HolyPower;
        if (s == "eclipse")     return wowee::pipeline::WoweeSpellPowerCost::Eclipse;
        if (s == "health")      return wowee::pipeline::WoweeSpellPowerCost::Health;
        if (s == "no-cost" ||
            s == "nocost")      return wowee::pipeline::WoweeSpellPowerCost::NoCost;
    }
    return fallback;
}

uint32_t parseCostFlagsField(const nlohmann::json& jv) {
    using F = wowee::pipeline::WoweeSpellPowerCost;
    // The splitting is shared; the words and the bits are this
    // format's own.
    return cli::flagMaskFromJson(jv, [](const std::string& token) -> uint32_t {
        if (token == "requirescombatstance") return F::RequiresCombatStance;
        if (token == "refundonmiss") return F::RefundOnMiss;
        if (token == "doublesinform") return F::DoublesInForm;
        if (token == "scaleswithmastery") return F::ScalesWithMastery;
        return 0;
    });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wspc-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wspc-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellPowerCost c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellPowerCost::Entry e;
            if (je.contains("powerCostId")) e.powerCostId = je["powerCostId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            uint8_t type = wowee::pipeline::WoweeSpellPowerCost::Mana;
            if (je.contains("powerType"))
                type = parsePowerTypeToken(je["powerType"], type);
            else if (je.contains("powerTypeName"))
                type = parsePowerTypeToken(je["powerTypeName"], type);
            e.powerType = type;
            if (je.contains("baseCost"))      e.baseCost = je["baseCost"].get<int32_t>();
            if (je.contains("perLevelCost"))  e.perLevelCost = je["perLevelCost"].get<int32_t>();
            if (je.contains("percentOfBase")) e.percentOfBase = je["percentOfBase"].get<float>();
            if (je.contains("costFlags"))
                e.costFlags = parseCostFlagsField(je["costFlags"]);
            else if (je.contains("costFlagsLabels"))
                e.costFlags = parseCostFlagsField(je["costFlagsLabels"]);
            if (je.contains("iconColorRGBA"))
                e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wspc");
    outBase = cli::withoutExt(outBase, ".wspc");
    if (!wowee::pipeline::WoweeSpellPowerCostLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wspc-json: failed to save %s.wspc\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wspc\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeSpellPowerCostLoader>(
        i, argc, argv, "wspc", "WSPC",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        constexpr uint32_t kKnownFlagMask =
            wowee::pipeline::WoweeSpellPowerCost::RequiresCombatStance |
            wowee::pipeline::WoweeSpellPowerCost::RefundOnMiss |
            wowee::pipeline::WoweeSpellPowerCost::DoublesInForm |
            wowee::pipeline::WoweeSpellPowerCost::ScalesWithMastery;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.powerCostId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.powerCostId == 0)
                errors.push_back(ctx + ": powerCostId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.powerType > wowee::pipeline::WoweeSpellPowerCost::NoCost) {
                errors.push_back(ctx + ": powerType " +
                    std::to_string(e.powerType) + " not in 0..11");
            }
            if (e.percentOfBase < 0.0f || e.percentOfBase > 1.0f) {
                warnings.push_back(ctx +
                    ": percentOfBase " + std::to_string(e.percentOfBase) +
                    " is outside [0..1] — may overflow caster's max power");
            }
            if (e.costFlags & ~kKnownFlagMask) {
                warnings.push_back(ctx +
                    ": costFlags has bits outside known mask " +
                    "(0x" + std::to_string(e.costFlags & ~kKnownFlagMask) +
                    ") — engine will ignore unknown flags");
            }
            // NoCost type with non-zero cost values is
            // contradictory.
            if (e.powerType == wowee::pipeline::WoweeSpellPowerCost::NoCost &&
                (e.baseCost != 0 || e.perLevelCost != 0 ||
                 e.percentOfBase != 0.0f)) {
                warnings.push_back(ctx +
                    ": NoCost type with non-zero cost fields — "
                    "engine treats this as free regardless");
            }
            // Spell with no cost fields set when not NoCost — is
            // probably misconfigured (would be free).
            if (e.powerType != wowee::pipeline::WoweeSpellPowerCost::NoCost &&
                e.baseCost == 0 && e.perLevelCost == 0 &&
                e.percentOfBase == 0.0f) {
                warnings.push_back(ctx +
                    ": no cost fields set but powerType is " +
                    wowee::pipeline::WoweeSpellPowerCost::powerTypeName(e.powerType) +
                    " — spell will cast for free, switch to NoCost type if intended");
            }
            if (!idsSeen.add(e.powerCostId)) errors.push_back(ctx + ": duplicate powerCostId");
        }
            return formatted("%zu buckets, all powerCostIds unique", c.entries.size());
        });
}

} // namespace

bool handleSpellPowerCostsCatalog(int& i, int argc, char** argv,
                                  int& outRc) {
    if (std::strcmp(argv[i], "--gen-spc") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spc-rage") == 0 && i + 1 < argc) {
        outRc = handleGenRage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spc-mixed") == 0 && i + 1 < argc) {
        outRc = handleGenMixed(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wspc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wspc") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wspc-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wspc-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
