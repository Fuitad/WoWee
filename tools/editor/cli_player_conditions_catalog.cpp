#include "cli_player_conditions_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_player_conditions.hpp"
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


void printGenSummary(const wowee::pipeline::WoweePlayerCondition& c,
                     const std::string& base) {
    std::printf("Wrote %s.wpcn\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  conditions : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterConditions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcn");
    auto c = wowee::pipeline::WoweePlayerConditionLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweePlayerConditionLoader>(c, base, "gen-pcn", ".wpcn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenQuestGates(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestGateConditions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcn");
    auto c = wowee::pipeline::WoweePlayerConditionLoader::makeQuestGates(name);
    if (!saveOrError<wowee::pipeline::WoweePlayerConditionLoader>(c, base, "gen-pcn-quest-gates", ".wpcn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenComposite(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CompositeConditions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcn");
    auto c = wowee::pipeline::WoweePlayerConditionLoader::makeComposite(name);
    if (!saveOrError<wowee::pipeline::WoweePlayerConditionLoader>(c, base, "gen-pcn-composite", ".wpcn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpcn");
    if (!wowee::pipeline::WoweePlayerConditionLoader::exists(base)) {
        return reportMissing("WPCN", base, ".wpcn");
    }
    auto c = wowee::pipeline::WoweePlayerConditionLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wpcn"] = base + ".wpcn";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"conditionId", e.conditionId},
                {"name", e.name},
                {"description", e.description},
                {"conditionKind", e.conditionKind},
                {"conditionKindName", wowee::pipeline::WoweePlayerCondition::conditionKindName(e.conditionKind)},
                {"comparisonOp", e.comparisonOp},
                {"comparisonOpName", wowee::pipeline::WoweePlayerCondition::comparisonOpName(e.comparisonOp)},
                {"chainOp", e.chainOp},
                {"chainOpName", wowee::pipeline::WoweePlayerCondition::chainOpName(e.chainOp)},
                {"targetIdA", e.targetIdA},
                {"targetIdB", e.targetIdB},
                {"intValueA", e.intValueA},
                {"intValueB", e.intValueB},
                {"chainNextId", e.chainNextId},
                {"failMessage", e.failMessage},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPCN: %s.wpcn\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  conditions : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id     kind             op          tgtA   tgtB   intA   intB   chain  next   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %-14s   %-10s   %4u   %4u  %5d  %5d   %-5s  %4u   %s\n",
                    e.conditionId,
                    wowee::pipeline::WoweePlayerCondition::conditionKindName(e.conditionKind),
                    wowee::pipeline::WoweePlayerCondition::comparisonOpName(e.comparisonOp),
                    e.targetIdA, e.targetIdB,
                    e.intValueA, e.intValueB,
                    wowee::pipeline::WoweePlayerCondition::chainOpName(e.chainOp),
                    e.chainNextId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each condition emits all 11 scalar fields
    // plus dual int + name forms for conditionKind /
    // comparisonOp / chainOp so hand-edits can use either.
    return cli::exportCatalogJson<wowee::pipeline::WoweePlayerConditionLoader>(
        i, argc, argv, "wpcn", "WPCN", "conditions ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"conditionId", e.conditionId},
                {"name", e.name},
                {"description", e.description},
                {"conditionKind", e.conditionKind},
                {"conditionKindName", wowee::pipeline::WoweePlayerCondition::conditionKindName(e.conditionKind)},
                {"comparisonOp", e.comparisonOp},
                {"comparisonOpName", wowee::pipeline::WoweePlayerCondition::comparisonOpName(e.comparisonOp)},
                {"chainOp", e.chainOp},
                {"chainOpName", wowee::pipeline::WoweePlayerCondition::chainOpName(e.chainOp)},
                {"targetIdA", e.targetIdA},
                {"targetIdB", e.targetIdB},
                {"intValueA", e.intValueA},
                {"intValueB", e.intValueB},
                {"chainNextId", e.chainNextId},
                {"failMessage", e.failMessage},
            });
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wpcn");
    outBase = cli::withoutExt(outBase, ".wpcn");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wpcn-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wpcn-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "always")          return wowee::pipeline::WoweePlayerCondition::Always;
        if (s == "race")            return wowee::pipeline::WoweePlayerCondition::Race;
        if (s == "class")           return wowee::pipeline::WoweePlayerCondition::Class;
        if (s == "level")           return wowee::pipeline::WoweePlayerCondition::Level;
        if (s == "zone")            return wowee::pipeline::WoweePlayerCondition::Zone;
        if (s == "map")             return wowee::pipeline::WoweePlayerCondition::Map;
        if (s == "reputation")      return wowee::pipeline::WoweePlayerCondition::Reputation;
        if (s == "achievement")     return wowee::pipeline::WoweePlayerCondition::AchievementWon;
        if (s == "quest-complete")  return wowee::pipeline::WoweePlayerCondition::QuestComplete;
        if (s == "quest-active")    return wowee::pipeline::WoweePlayerCondition::QuestActive;
        if (s == "spell-known")     return wowee::pipeline::WoweePlayerCondition::SpellKnown;
        if (s == "item-equipped")   return wowee::pipeline::WoweePlayerCondition::ItemEquipped;
        if (s == "faction")         return wowee::pipeline::WoweePlayerCondition::Faction;
        if (s == "in-combat")       return wowee::pipeline::WoweePlayerCondition::InCombat;
        if (s == "mounted")         return wowee::pipeline::WoweePlayerCondition::Mounted;
        if (s == "resting")         return wowee::pipeline::WoweePlayerCondition::Resting;
        return wowee::pipeline::WoweePlayerCondition::Always;
    };
    auto opFromName = [](const std::string& s) -> uint8_t {
        if (s == "==")         return wowee::pipeline::WoweePlayerCondition::Equal;
        if (s == "!=")         return wowee::pipeline::WoweePlayerCondition::NotEqual;
        if (s == ">")          return wowee::pipeline::WoweePlayerCondition::GreaterThan;
        if (s == ">=")         return wowee::pipeline::WoweePlayerCondition::GreaterOrEqual;
        if (s == "<")          return wowee::pipeline::WoweePlayerCondition::LessThan;
        if (s == "<=")         return wowee::pipeline::WoweePlayerCondition::LessOrEqual;
        if (s == "in-set")     return wowee::pipeline::WoweePlayerCondition::InSet;
        if (s == "not-in-set") return wowee::pipeline::WoweePlayerCondition::NotInSet;
        return wowee::pipeline::WoweePlayerCondition::Equal;
    };
    auto chainFromName = [](const std::string& s) -> uint8_t {
        if (s == "none") return wowee::pipeline::WoweePlayerCondition::ChainNone;
        if (s == "and")  return wowee::pipeline::WoweePlayerCondition::ChainAnd;
        if (s == "or")   return wowee::pipeline::WoweePlayerCondition::ChainOr;
        if (s == "not")  return wowee::pipeline::WoweePlayerCondition::ChainNot;
        return wowee::pipeline::WoweePlayerCondition::ChainNone;
    };
    wowee::pipeline::WoweePlayerCondition c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweePlayerCondition::Entry e;
            e.conditionId = je.value("conditionId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("conditionKind") &&
                je["conditionKind"].is_number_integer()) {
                e.conditionKind = static_cast<uint8_t>(
                    je["conditionKind"].get<int>());
            } else if (je.contains("conditionKindName") &&
                       je["conditionKindName"].is_string()) {
                e.conditionKind = kindFromName(
                    je["conditionKindName"].get<std::string>());
            }
            if (je.contains("comparisonOp") &&
                je["comparisonOp"].is_number_integer()) {
                e.comparisonOp = static_cast<uint8_t>(
                    je["comparisonOp"].get<int>());
            } else if (je.contains("comparisonOpName") &&
                       je["comparisonOpName"].is_string()) {
                e.comparisonOp = opFromName(
                    je["comparisonOpName"].get<std::string>());
            }
            if (je.contains("chainOp") &&
                je["chainOp"].is_number_integer()) {
                e.chainOp = static_cast<uint8_t>(
                    je["chainOp"].get<int>());
            } else if (je.contains("chainOpName") &&
                       je["chainOpName"].is_string()) {
                e.chainOp = chainFromName(
                    je["chainOpName"].get<std::string>());
            }
            e.targetIdA = je.value("targetIdA", 0u);
            e.targetIdB = je.value("targetIdB", 0u);
            e.intValueA = je.value("intValueA", 0);
            e.intValueB = je.value("intValueB", 0);
            e.chainNextId = je.value("chainNextId", 0u);
            e.failMessage = je.value("failMessage", std::string{});
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweePlayerConditionLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wpcn-json: failed to save %s.wpcn\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wpcn\n", outBase.c_str());
    std::printf("  source     : %s\n", jsonPath.c_str());
    std::printf("  conditions : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpcn");
    if (!wowee::pipeline::WoweePlayerConditionLoader::exists(base)) {
        return reportMissing("validate-wpcn", "WPCN", base, ".wpcn");
    }
    auto c = wowee::pipeline::WoweePlayerConditionLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (const auto& e : c.entries) idsSeen.push_back(e.conditionId);
    auto idExists = [&](uint32_t id) {
        for (uint32_t a : idsSeen) if (a == id) return true;
        return false;
    };
    std::vector<uint32_t> dupCheck;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.conditionId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.conditionId == 0)
            errors.push_back(ctx + ": conditionId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.conditionKind > wowee::pipeline::WoweePlayerCondition::Resting) {
            errors.push_back(ctx + ": conditionKind " +
                std::to_string(e.conditionKind) + " not in 0..15");
        }
        if (e.comparisonOp > wowee::pipeline::WoweePlayerCondition::NotInSet) {
            errors.push_back(ctx + ": comparisonOp " +
                std::to_string(e.comparisonOp) + " not in 0..7");
        }
        if (e.chainOp > wowee::pipeline::WoweePlayerCondition::ChainNot) {
            errors.push_back(ctx + ": chainOp " +
                std::to_string(e.chainOp) + " not in 0..3");
        }
        // chainOp != ChainNone requires a non-zero chainNextId
        // — and that ID must point at another condition in
        // this catalog.
        if (e.chainOp != wowee::pipeline::WoweePlayerCondition::ChainNone) {
            if (e.chainNextId == 0) {
                errors.push_back(ctx + ": chainOp '" +
                    wowee::pipeline::WoweePlayerCondition::chainOpName(e.chainOp) +
                    "' set but chainNextId=0 (chain has no tail)");
            } else if (e.chainNextId == e.conditionId) {
                errors.push_back(ctx +
                    ": chainNextId equals conditionId "
                    "(infinite loop)");
            } else if (!idExists(e.chainNextId)) {
                warnings.push_back(ctx + ": chainNextId=" +
                    std::to_string(e.chainNextId) +
                    " not found in this catalog (resolved at runtime)");
            }
        }
        // chainOp == ChainNone and chainNextId != 0 is dead
        // pointer — chainNextId is silently unused.
        if (e.chainOp == wowee::pipeline::WoweePlayerCondition::ChainNone &&
            e.chainNextId != 0) {
            warnings.push_back(ctx +
                ": chainNextId set but chainOp=none "
                "(silently ignored at runtime)");
        }
        // duplicates
        for (size_t m = 0; m < k; ++m) {
            if (c.entries[m].conditionId == e.conditionId) {
                errors.push_back(ctx + ": duplicate conditionId");
                break;
            }
        }
    }
    return cli::reportValidation("wpcn", base, jsonOut, errors, warnings,
                                 formatted("%zu conditions, all conditionIds unique, all chains resolved", c.entries.size()));
}

} // namespace

bool handlePlayerConditionsCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-pcn") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-pcn-quest-gates") == 0 &&
        i + 1 < argc) {
        outRc = handleGenQuestGates(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-pcn-composite") == 0 &&
        i + 1 < argc) {
        outRc = handleGenComposite(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wpcn") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wpcn") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wpcn-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wpcn-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
