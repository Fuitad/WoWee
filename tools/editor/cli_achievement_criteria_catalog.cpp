#include "cli_achievement_criteria_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_achievement_criteria.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeAchievementCriteria& c,
                     const std::string& base) {
    std::printf("Wrote %s.wacr\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  criteria : %zu\n", c.entries.size());
}

int handleGenKill(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "KillCriteria";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wacr");
    auto c = wowee::pipeline::WoweeAchievementCriteriaLoader::makeKill(name);
    if (!saveOrError<wowee::pipeline::WoweeAchievementCriteriaLoader>(c, base, "gen-acr", ".wacr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenQuest(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestCriteria";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wacr");
    auto c = wowee::pipeline::WoweeAchievementCriteriaLoader::makeQuest(name);
    if (!saveOrError<wowee::pipeline::WoweeAchievementCriteriaLoader>(c, base, "gen-acr-quest", ".wacr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMixed(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MixedCriteria";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wacr");
    auto c = wowee::pipeline::WoweeAchievementCriteriaLoader::makeMixed(name);
    if (!saveOrError<wowee::pipeline::WoweeAchievementCriteriaLoader>(c, base, "gen-acr-mixed", ".wacr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wacr");
    if (!wowee::pipeline::WoweeAchievementCriteriaLoader::exists(base)) {
        std::fprintf(stderr, "WACR not found: %s.wacr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAchievementCriteriaLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wacr"] = base + ".wacr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"criteriaId", e.criteriaId},
                {"name", e.name},
                {"description", e.description},
                {"achievementId", e.achievementId},
                {"targetId", e.targetId},
                {"requiredCount", e.requiredCount},
                {"timeLimitMs", e.timeLimitMs},
                {"criteriaType", e.criteriaType},
                {"criteriaTypeName", wowee::pipeline::WoweeAchievementCriteria::criteriaTypeName(e.criteriaType)},
                {"progressOrder", e.progressOrder},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WACR: %s.wacr\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  criteria : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    ach     type             targetId    count   timeMs   ord   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %5u    %-15s  %8u   %5u    %5u   %u    %s\n",
                    e.criteriaId, e.achievementId,
                    wowee::pipeline::WoweeAchievementCriteria::criteriaTypeName(e.criteriaType),
                    e.targetId, e.requiredCount, e.timeLimitMs,
                    e.progressOrder, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wacr");
    if (!wowee::pipeline::WoweeAchievementCriteriaLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wacr-json: WACR not found: %s.wacr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAchievementCriteriaLoader::load(base);
    if (outPath.empty()) outPath = base + ".wacr.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["criteriaId"] = e.criteriaId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["achievementId"] = e.achievementId;
        je["targetId"] = e.targetId;
        je["requiredCount"] = e.requiredCount;
        je["timeLimitMs"] = e.timeLimitMs;
        je["criteriaType"] = e.criteriaType;
        je["criteriaTypeName"] =
            wowee::pipeline::WoweeAchievementCriteria::criteriaTypeName(e.criteriaType);
        je["progressOrder"] = e.progressOrder;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wacr-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  criteria : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseCriteriaTypeToken(const nlohmann::json& jv,
                               uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeAchievementCriteria::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "kill-creature" ||
            s == "killcreature")    return wowee::pipeline::WoweeAchievementCriteria::KillCreature;
        if (s == "reach-level" ||
            s == "reachlevel")      return wowee::pipeline::WoweeAchievementCriteria::ReachLevel;
        if (s == "complete-quest" ||
            s == "completequest")   return wowee::pipeline::WoweeAchievementCriteria::CompleteQuest;
        if (s == "earn-gold" ||
            s == "earngold")        return wowee::pipeline::WoweeAchievementCriteria::EarnGold;
        if (s == "gain-honor" ||
            s == "gainhonor")       return wowee::pipeline::WoweeAchievementCriteria::GainHonor;
        if (s == "earn-reputation" ||
            s == "earnreputation")  return wowee::pipeline::WoweeAchievementCriteria::EarnReputation;
        if (s == "explore-zone" ||
            s == "explorezone")     return wowee::pipeline::WoweeAchievementCriteria::ExploreZone;
        if (s == "loot-item" ||
            s == "lootitem")        return wowee::pipeline::WoweeAchievementCriteria::LootItem;
        if (s == "use-item" ||
            s == "useitem")         return wowee::pipeline::WoweeAchievementCriteria::UseItem;
        if (s == "cast-spell" ||
            s == "castspell")       return wowee::pipeline::WoweeAchievementCriteria::CastSpell;
        if (s == "pvp-kill" ||
            s == "pvpkill")         return wowee::pipeline::WoweeAchievementCriteria::PvPKill;
        if (s == "dungeon-run" ||
            s == "dungeonrun")      return wowee::pipeline::WoweeAchievementCriteria::DungeonRun;
        if (s == "misc")            return wowee::pipeline::WoweeAchievementCriteria::Misc;
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
            "import-wacr-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wacr-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeAchievementCriteria c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeAchievementCriteria::Entry e;
            if (je.contains("criteriaId"))    e.criteriaId = je["criteriaId"].get<uint32_t>();
            if (je.contains("name"))          e.name = je["name"].get<std::string>();
            if (je.contains("description"))   e.description = je["description"].get<std::string>();
            if (je.contains("achievementId")) e.achievementId = je["achievementId"].get<uint32_t>();
            if (je.contains("targetId"))      e.targetId = je["targetId"].get<uint32_t>();
            if (je.contains("requiredCount")) e.requiredCount = je["requiredCount"].get<uint32_t>();
            if (je.contains("timeLimitMs"))   e.timeLimitMs = je["timeLimitMs"].get<uint32_t>();
            uint8_t type = wowee::pipeline::WoweeAchievementCriteria::KillCreature;
            if (je.contains("criteriaType"))
                type = parseCriteriaTypeToken(je["criteriaType"], type);
            else if (je.contains("criteriaTypeName"))
                type = parseCriteriaTypeToken(je["criteriaTypeName"], type);
            e.criteriaType = type;
            if (je.contains("progressOrder")) e.progressOrder = je["progressOrder"].get<uint8_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wacr");
    outBase = cli::withoutExt(outBase, ".wacr");
    if (!wowee::pipeline::WoweeAchievementCriteriaLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wacr-json: failed to save %s.wacr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wacr\n", outBase.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  criteria : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wacr");
    if (!wowee::pipeline::WoweeAchievementCriteriaLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wacr: WACR not found: %s.wacr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAchievementCriteriaLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.criteriaId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.criteriaId == 0)
            errors.push_back(ctx + ": criteriaId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.achievementId == 0)
            errors.push_back(ctx +
                ": achievementId is 0 — missing WACH cross-ref");
        if (e.criteriaType > wowee::pipeline::WoweeAchievementCriteria::Misc) {
            errors.push_back(ctx + ": criteriaType " +
                std::to_string(e.criteriaType) + " not in 0..12");
        }
        if (e.requiredCount == 0)
            warnings.push_back(ctx +
                ": requiredCount is 0 — criteria completes "
                "instantly on first progress event");
        // Type-specific cross-ref checks.
        switch (e.criteriaType) {
            case wowee::pipeline::WoweeAchievementCriteria::KillCreature:
            case wowee::pipeline::WoweeAchievementCriteria::CompleteQuest:
            case wowee::pipeline::WoweeAchievementCriteria::EarnReputation:
            case wowee::pipeline::WoweeAchievementCriteria::ExploreZone:
            case wowee::pipeline::WoweeAchievementCriteria::LootItem:
            case wowee::pipeline::WoweeAchievementCriteria::UseItem:
            case wowee::pipeline::WoweeAchievementCriteria::CastSpell:
            case wowee::pipeline::WoweeAchievementCriteria::DungeonRun:
                if (e.targetId == 0) {
                    warnings.push_back(ctx +
                        ": " +
                        wowee::pipeline::WoweeAchievementCriteria::criteriaTypeName(e.criteriaType) +
                        " kind requires targetId — engine cannot "
                        "track progression without it");
                }
                break;
            case wowee::pipeline::WoweeAchievementCriteria::ReachLevel:
                if (e.requiredCount > 80) {
                    warnings.push_back(ctx +
                        ": ReachLevel with requiredCount=" +
                        std::to_string(e.requiredCount) +
                        " > 80 — character cap is 80 in WotLK");
                }
                break;
            case wowee::pipeline::WoweeAchievementCriteria::EarnGold:
            case wowee::pipeline::WoweeAchievementCriteria::GainHonor:
            case wowee::pipeline::WoweeAchievementCriteria::PvPKill:
            case wowee::pipeline::WoweeAchievementCriteria::Misc:
                break;     // no specific cross-ref required
        }
        // timeLimitMs > 0 with non-time-sensitive criteria
        // is suspicious — the engine ignores it for kinds
        // like ReachLevel.
        if (e.timeLimitMs != 0 &&
            (e.criteriaType == wowee::pipeline::WoweeAchievementCriteria::ReachLevel ||
             e.criteriaType == wowee::pipeline::WoweeAchievementCriteria::EarnGold)) {
            warnings.push_back(ctx +
                ": timeLimitMs " + std::to_string(e.timeLimitMs) +
                " set on a non-time-sensitive criteria type — "
                "engine will ignore");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.criteriaId) {
                errors.push_back(ctx + ": duplicate criteriaId");
                break;
            }
        }
        idsSeen.push_back(e.criteriaId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wacr", base, errors, warnings);
    std::printf("validate-wacr: %s.wacr\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu criteria, all criteriaIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleAchievementCriteriaCatalog(int& i, int argc, char** argv,
                                      int& outRc) {
    if (std::strcmp(argv[i], "--gen-acr") == 0 && i + 1 < argc) {
        outRc = handleGenKill(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-acr-quest") == 0 && i + 1 < argc) {
        outRc = handleGenQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-acr-mixed") == 0 && i + 1 < argc) {
        outRc = handleGenMixed(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wacr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wacr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wacr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wacr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
