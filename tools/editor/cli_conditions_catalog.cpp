#include "cli_conditions_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_conditions.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCondition& c,
                     const std::string& base) {
    std::printf("Wrote %s.wpcd\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  conditions : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterConditions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcd");
    auto c = wowee::pipeline::WoweeConditionLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeConditionLoader>(c, base, "gen-conditions", ".wpcd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenGated(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "GatedConditions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcd");
    auto c = wowee::pipeline::WoweeConditionLoader::makeGated(name);
    if (!saveOrError<wowee::pipeline::WoweeConditionLoader>(c, base, "gen-conditions-gated", ".wpcd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenEvent(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "EventConditions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcd");
    auto c = wowee::pipeline::WoweeConditionLoader::makeEvent(name);
    if (!saveOrError<wowee::pipeline::WoweeConditionLoader>(c, base, "gen-conditions-event", ".wpcd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpcd");
    if (!wowee::pipeline::WoweeConditionLoader::exists(base)) {
        std::fprintf(stderr, "WPCD not found: %s.wpcd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeConditionLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wpcd"] = base + ".wpcd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"conditionId", e.conditionId},
                {"groupId", e.groupId},
                {"name", e.name},
                {"description", e.description},
                {"kind", e.kind},
                {"kindName", wowee::pipeline::WoweeCondition::kindName(e.kind)},
                {"aggregator", e.aggregator},
                {"aggregatorName", wowee::pipeline::WoweeCondition::aggregatorName(e.aggregator)},
                {"negated", e.negated},
                {"targetId", e.targetId},
                {"minValue", e.minValue},
                {"maxValue", e.maxValue},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPCD: %s.wpcd\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  conditions : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    grp   kind          agg  neg  target   min/max         name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %4u  %-12s  %-3s  %u    %5u    %5d/%-5d  %s\n",
                    e.conditionId, e.groupId,
                    wowee::pipeline::WoweeCondition::kindName(e.kind),
                    wowee::pipeline::WoweeCondition::aggregatorName(e.aggregator),
                    e.negated, e.targetId,
                    e.minValue, e.maxValue, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each condition emits all 9 scalar fields
    // plus dual int + name forms for kind and aggregator.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wpcd");
    if (outPath.empty()) outPath = base + ".wpcd.json";
    if (!wowee::pipeline::WoweeConditionLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wpcd-json: WPCD not found: %s.wpcd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeConditionLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"conditionId", e.conditionId},
            {"groupId", e.groupId},
            {"name", e.name},
            {"description", e.description},
            {"kind", e.kind},
            {"kindName", wowee::pipeline::WoweeCondition::kindName(e.kind)},
            {"aggregator", e.aggregator},
            {"aggregatorName", wowee::pipeline::WoweeCondition::aggregatorName(e.aggregator)},
            {"negated", e.negated},
            {"targetId", e.targetId},
            {"minValue", e.minValue},
            {"maxValue", e.maxValue},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wpcd-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source     : %s.wpcd\n", base.c_str());
    std::printf("  conditions : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wpcd");
    outBase = cli::withoutExt(outBase, ".wpcd");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wpcd-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wpcd-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "true")         return wowee::pipeline::WoweeCondition::AlwaysTrue;
        if (s == "false")        return wowee::pipeline::WoweeCondition::AlwaysFalse;
        if (s == "quest-done")   return wowee::pipeline::WoweeCondition::QuestCompleted;
        if (s == "quest-active") return wowee::pipeline::WoweeCondition::QuestActive;
        if (s == "has-item")     return wowee::pipeline::WoweeCondition::HasItem;
        if (s == "has-spell")    return wowee::pipeline::WoweeCondition::HasSpell;
        if (s == "min-level")    return wowee::pipeline::WoweeCondition::MinLevel;
        if (s == "max-level")    return wowee::pipeline::WoweeCondition::MaxLevel;
        if (s == "class")        return wowee::pipeline::WoweeCondition::ClassMatch;
        if (s == "race")         return wowee::pipeline::WoweeCondition::RaceMatch;
        if (s == "rep")          return wowee::pipeline::WoweeCondition::FactionRep;
        if (s == "achievement")  return wowee::pipeline::WoweeCondition::HasAchievement;
        if (s == "team-size")    return wowee::pipeline::WoweeCondition::TeamSize;
        if (s == "guild-level")  return wowee::pipeline::WoweeCondition::GuildLevel;
        if (s == "event")        return wowee::pipeline::WoweeCondition::EventActive;
        if (s == "area")         return wowee::pipeline::WoweeCondition::AreaId;
        if (s == "title")        return wowee::pipeline::WoweeCondition::HasTitle;
        return wowee::pipeline::WoweeCondition::AlwaysTrue;
    };
    auto aggFromName = [](const std::string& s) -> uint8_t {
        if (s == "and") return wowee::pipeline::WoweeCondition::And;
        if (s == "or")  return wowee::pipeline::WoweeCondition::Or;
        return wowee::pipeline::WoweeCondition::And;
    };
    wowee::pipeline::WoweeCondition c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCondition::Entry e;
            e.conditionId = je.value("conditionId", 0u);
            e.groupId = je.value("groupId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("kind") && je["kind"].is_number_integer()) {
                e.kind = static_cast<uint8_t>(je["kind"].get<int>());
            } else if (je.contains("kindName") && je["kindName"].is_string()) {
                e.kind = kindFromName(je["kindName"].get<std::string>());
            }
            if (je.contains("aggregator") && je["aggregator"].is_number_integer()) {
                e.aggregator = static_cast<uint8_t>(je["aggregator"].get<int>());
            } else if (je.contains("aggregatorName") &&
                       je["aggregatorName"].is_string()) {
                e.aggregator = aggFromName(je["aggregatorName"].get<std::string>());
            }
            e.negated = static_cast<uint8_t>(je.value("negated", 0));
            e.targetId = je.value("targetId", 0u);
            e.minValue = je.value("minValue", 0);
            e.maxValue = je.value("maxValue", 0);
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeConditionLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wpcd-json: failed to save %s.wpcd\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wpcd\n", outBase.c_str());
    std::printf("  source     : %s\n", jsonPath.c_str());
    std::printf("  conditions : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpcd");
    if (!wowee::pipeline::WoweeConditionLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wpcd: WPCD not found: %s.wpcd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeConditionLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.conditionId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.conditionId == 0) {
            errors.push_back(ctx + ": conditionId is 0");
        }
        if (e.kind > wowee::pipeline::WoweeCondition::HasTitle) {
            errors.push_back(ctx + ": kind " +
                std::to_string(e.kind) + " not in 0..16");
        }
        if (e.aggregator > wowee::pipeline::WoweeCondition::Or) {
            errors.push_back(ctx + ": aggregator " +
                std::to_string(e.aggregator) + " not in 0..1");
        }
        // Most kinds need a non-zero targetId. Exceptions:
        // AlwaysTrue / AlwaysFalse (no target), MinLevel /
        // MaxLevel (use minValue), TeamSize (uses min/max),
        // GuildLevel (uses minValue).
        bool needsTarget =
            e.kind != wowee::pipeline::WoweeCondition::AlwaysTrue &&
            e.kind != wowee::pipeline::WoweeCondition::AlwaysFalse &&
            e.kind != wowee::pipeline::WoweeCondition::MinLevel &&
            e.kind != wowee::pipeline::WoweeCondition::MaxLevel &&
            e.kind != wowee::pipeline::WoweeCondition::TeamSize &&
            e.kind != wowee::pipeline::WoweeCondition::GuildLevel;
        if (needsTarget && e.targetId == 0) {
            errors.push_back(ctx +
                ": kind needs a non-zero targetId");
        }
        if (e.kind == wowee::pipeline::WoweeCondition::TeamSize &&
            e.minValue > 0 && e.maxValue > 0 &&
            e.minValue > e.maxValue) {
            errors.push_back(ctx + ": team-size minValue > maxValue");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.conditionId) {
                errors.push_back(ctx + ": duplicate conditionId");
                break;
            }
        }
        idsSeen.push_back(e.conditionId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wpcd", base, errors, warnings);
    std::printf("validate-wpcd: %s.wpcd\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu conditions, all conditionIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleConditionsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-conditions") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-conditions-gated") == 0 && i + 1 < argc) {
        outRc = handleGenGated(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-conditions-event") == 0 && i + 1 < argc) {
        outRc = handleGenEvent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wpcd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wpcd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wpcd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wpcd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
