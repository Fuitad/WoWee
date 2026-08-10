#include "cli_pvp_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_pvp.hpp"
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

bool saveOrError(const wowee::pipeline::WoweePVPRank& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweePVPRankLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wpvp\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweePVPRank& c,
                     const std::string& base) {
    std::printf("Wrote %s.wpvp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranks   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterPvPRanks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpvp");
    auto c = wowee::pipeline::WoweePVPRankLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-pvp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAllianceFull(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllianceVanillaRanks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpvp");
    auto c = wowee::pipeline::WoweePVPRankLoader::makeAllianceFull(name);
    if (!saveOrError(c, base, "gen-pvp-alliance")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenArena(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ArenaTiers";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpvp");
    auto c = wowee::pipeline::WoweePVPRankLoader::makeArenaTiers(name);
    if (!saveOrError(c, base, "gen-pvp-arena")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpvp");
    if (!wowee::pipeline::WoweePVPRankLoader::exists(base)) {
        std::fprintf(stderr, "WPVP not found: %s.wpvp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePVPRankLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wpvp"] = base + ".wpvp";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"rankId", e.rankId},
                {"name", e.name},
                {"factionAllianceName", e.factionAllianceName},
                {"factionHordeName", e.factionHordeName},
                {"description", e.description},
                {"rankKind", e.rankKind},
                {"rankKindName", wowee::pipeline::WoweePVPRank::rankKindName(e.rankKind)},
                {"minBracketLevel", e.minBracketLevel},
                {"maxBracketLevel", e.maxBracketLevel},
                {"minHonorOrRating", e.minHonorOrRating},
                {"rewardEmblems", e.rewardEmblems},
                {"titleId", e.titleId},
                {"chestItemId", e.chestItemId},
                {"glovesItemId", e.glovesItemId},
                {"shouldersItemId", e.shouldersItemId},
                {"bracketBgId", e.bracketBgId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPVP: %s.wpvp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ranks   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind             threshold  emblem  title  chest  alliance / horde            \n");
    for (const auto& e : c.entries) {
        std::string both = e.factionAllianceName +
                           (e.factionAllianceName != e.factionHordeName
                              ? std::string(" / ") + e.factionHordeName
                              : std::string());
        std::printf("  %4u   %-15s  %8u   %3u    %3u   %5u  %s\n",
                    e.rankId,
                    wowee::pipeline::WoweePVPRank::rankKindName(e.rankKind),
                    e.minHonorOrRating, e.rewardEmblems,
                    e.titleId, e.chestItemId, both.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each rank emits all 12 scalar fields plus
    // a dual int + name form for rankKind so hand-edits can
    // use either representation.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wpvp");
    if (outPath.empty()) outPath = base + ".wpvp.json";
    if (!wowee::pipeline::WoweePVPRankLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wpvp-json: WPVP not found: %s.wpvp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePVPRankLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"rankId", e.rankId},
            {"name", e.name},
            {"factionAllianceName", e.factionAllianceName},
            {"factionHordeName", e.factionHordeName},
            {"description", e.description},
            {"rankKind", e.rankKind},
            {"rankKindName", wowee::pipeline::WoweePVPRank::rankKindName(e.rankKind)},
            {"minBracketLevel", e.minBracketLevel},
            {"maxBracketLevel", e.maxBracketLevel},
            {"minHonorOrRating", e.minHonorOrRating},
            {"rewardEmblems", e.rewardEmblems},
            {"titleId", e.titleId},
            {"chestItemId", e.chestItemId},
            {"glovesItemId", e.glovesItemId},
            {"shouldersItemId", e.shouldersItemId},
            {"bracketBgId", e.bracketBgId},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wpvp-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wpvp\n", base.c_str());
    std::printf("  ranks  : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wpvp");
    outBase = cli::withoutExt(outBase, ".wpvp");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wpvp-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wpvp-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "vanilla-honor") return wowee::pipeline::WoweePVPRank::VanillaHonor;
        if (s == "arena")         return wowee::pipeline::WoweePVPRank::ArenaRating;
        if (s == "rated-bg")      return wowee::pipeline::WoweePVPRank::BattlegroundRated;
        if (s == "world-pvp")     return wowee::pipeline::WoweePVPRank::WorldPvP;
        if (s == "conquest")      return wowee::pipeline::WoweePVPRank::ConquestPoint;
        return wowee::pipeline::WoweePVPRank::VanillaHonor;
    };
    wowee::pipeline::WoweePVPRank c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweePVPRank::Entry e;
            e.rankId = je.value("rankId", 0u);
            e.name = je.value("name", std::string{});
            e.factionAllianceName = je.value("factionAllianceName",
                                              std::string{});
            e.factionHordeName = je.value("factionHordeName",
                                           std::string{});
            e.description = je.value("description", std::string{});
            if (je.contains("rankKind") &&
                je["rankKind"].is_number_integer()) {
                e.rankKind = static_cast<uint8_t>(
                    je["rankKind"].get<int>());
            } else if (je.contains("rankKindName") &&
                       je["rankKindName"].is_string()) {
                e.rankKind = kindFromName(
                    je["rankKindName"].get<std::string>());
            }
            // Bracket-level defaults to 1..80 (no level gate)
            // when omitted — vanilla ranks weren't level-gated
            // beyond the cap.
            e.minBracketLevel = static_cast<uint8_t>(
                je.value("minBracketLevel", 1));
            e.maxBracketLevel = static_cast<uint8_t>(
                je.value("maxBracketLevel", 80));
            e.minHonorOrRating = je.value("minHonorOrRating", 0u);
            e.rewardEmblems = static_cast<uint16_t>(
                je.value("rewardEmblems", 0));
            e.titleId = je.value("titleId", 0u);
            e.chestItemId = je.value("chestItemId", 0u);
            e.glovesItemId = je.value("glovesItemId", 0u);
            e.shouldersItemId = je.value("shouldersItemId", 0u);
            e.bracketBgId = je.value("bracketBgId", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweePVPRankLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wpvp-json: failed to save %s.wpvp\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wpvp\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  ranks  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpvp");
    if (!wowee::pipeline::WoweePVPRankLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wpvp: WPVP not found: %s.wpvp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePVPRankLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    // Track threshold monotonicity within a single rankKind —
    // arena ratings should ascend (1500 < 1750 < ...), so a
    // catalog with two arena entries at the same rating or
    // a higher-id entry below a lower-id entry is suspicious.
    uint32_t prevHonorByKind[5] = {0, 0, 0, 0, 0};
    bool prevHonorSeen[5] = {false, false, false, false, false};
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.rankId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.rankId == 0)
            errors.push_back(ctx + ": rankId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.rankKind > wowee::pipeline::WoweePVPRank::ConquestPoint) {
            errors.push_back(ctx + ": rankKind " +
                std::to_string(e.rankKind) + " not in 0..4");
        }
        if (e.minBracketLevel > e.maxBracketLevel) {
            errors.push_back(ctx + ": minBracketLevel " +
                std::to_string(e.minBracketLevel) +
                " > maxBracketLevel " +
                std::to_string(e.maxBracketLevel));
        }
        // Vanilla honor ranks must use VanillaHonor kind and
        // have minHonor > 0 (rank 1 is the implicit baseline).
        if (e.rankKind == wowee::pipeline::WoweePVPRank::VanillaHonor &&
            e.minHonorOrRating == 0) {
            warnings.push_back(ctx +
                ": VanillaHonor kind with minHonor=0 "
                "(rank 1 baseline — verify intentional)");
        }
        // Arena ratings below 1500 don't unlock any reward —
        // 1500 is the WoW arena floor.
        if (e.rankKind == wowee::pipeline::WoweePVPRank::ArenaRating &&
            e.minHonorOrRating < 1500) {
            warnings.push_back(ctx +
                ": ArenaRating with rating " +
                std::to_string(e.minHonorOrRating) +
                " below 1500 floor");
        }
        // Faction alternate names — vanilla ranks have
        // distinct alliance / horde names; arena tiers share
        // the same name on both factions. Either is valid; an
        // empty alliance name + non-empty horde (or vice
        // versa) is a typo signal.
        if (e.factionAllianceName.empty() !=
            e.factionHordeName.empty()) {
            warnings.push_back(ctx +
                ": only one faction-alternate name set "
                "(alliance='" + e.factionAllianceName +
                "', horde='" + e.factionHordeName + "')");
        }
        // Threshold monotonicity within rankKind.
        if (e.rankKind < 5) {
            if (prevHonorSeen[e.rankKind] &&
                e.minHonorOrRating < prevHonorByKind[e.rankKind]) {
                warnings.push_back(ctx +
                    ": threshold " +
                    std::to_string(e.minHonorOrRating) +
                    " below previous " +
                    std::to_string(prevHonorByKind[e.rankKind]) +
                    " in same rankKind (non-monotonic)");
            }
            prevHonorByKind[e.rankKind] = e.minHonorOrRating;
            prevHonorSeen[e.rankKind] = true;
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.rankId) {
                errors.push_back(ctx + ": duplicate rankId");
                break;
            }
        }
        idsSeen.push_back(e.rankId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wpvp", base, errors, warnings);
    std::printf("validate-wpvp: %s.wpvp\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu ranks, all rankIds unique, all thresholds monotonic\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handlePVPCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-pvp") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-pvp-alliance") == 0 && i + 1 < argc) {
        outRc = handleGenAllianceFull(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-pvp-arena") == 0 && i + 1 < argc) {
        outRc = handleGenArena(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wpvp") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wpvp") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wpvp-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wpvp-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
