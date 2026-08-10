#include "cli_battleground_rewards_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_battleground_rewards.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* bgName(uint16_t bgId) {
    switch (bgId) {
        case 1: return "AV";
        case 2: return "WSG";
        case 3: return "AB";
        default: return "?";
    }
}

bool saveOrError(
    const wowee::pipeline::WoweeBattlegroundRewards& c,
    const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeBattlegroundRewardsLoader::save(
            c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wbrd\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(
    const wowee::pipeline::WoweeBattlegroundRewards& c,
    const std::string& base) {
    std::printf("Wrote %s.wbrd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  stages  : %zu\n", c.entries.size());
}

int handleGenAV(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AlteracValleyRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbrd");
    auto c = wowee::pipeline::WoweeBattlegroundRewardsLoader::
        makeAlteracValley(name);
    if (!saveOrError(c, base, "gen-brd-av")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWSG(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarsongRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbrd");
    auto c = wowee::pipeline::WoweeBattlegroundRewardsLoader::
        makeWarsong(name);
    if (!saveOrError(c, base, "gen-brd-wsg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAB(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ArathiBasinRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbrd");
    auto c = wowee::pipeline::WoweeBattlegroundRewardsLoader::
        makeArathiBasin(name);
    if (!saveOrError(c, base, "gen-brd-ab")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbrd");
    if (!wowee::pipeline::WoweeBattlegroundRewardsLoader::exists(base)) {
        std::fprintf(stderr, "WBRD not found: %s.wbrd\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeBattlegroundRewardsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wbrd"] = base + ".wbrd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"rewardId", e.rewardId},
                {"battlegroundId", e.battlegroundId},
                {"battlegroundName", bgName(e.battlegroundId)},
                {"bracketIndex", e.bracketIndex},
                {"minPlayersToStart", e.minPlayersToStart},
                {"winHonor", e.winHonor},
                {"lossHonor", e.lossHonor},
                {"markItemId", e.markItemId},
                {"winMarks", e.winMarks},
                {"lossMarks", e.lossMarks},
                {"bonusItemId", e.bonusItemId},
                {"bonusItemCount", e.bonusItemCount},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WBRD: %s.wbrd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  stages  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  bg     bracket  minPl  winHonor  lossHonor  markId  winM  lossM  bonusId  bonusN\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-3s    %5u    %3u    %5u     %5u    %5u   %2u    %3u   %5u    %3u\n",
                    e.rewardId, bgName(e.battlegroundId),
                    e.bracketIndex, e.minPlayersToStart,
                    e.winHonor, e.lossHonor,
                    e.markItemId, e.winMarks, e.lossMarks,
                    e.bonusItemId, e.bonusItemCount);
    }
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbrd");
    if (!wowee::pipeline::WoweeBattlegroundRewardsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wbrd: WBRD not found: %s.wbrd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeBattlegroundRewardsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    using B = wowee::pipeline::WoweeBattlegroundRewards;
    std::set<uint32_t> idsSeen;
    using Pair = std::pair<uint16_t, uint8_t>;
    std::set<Pair> bgBracketPairs;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.rewardId) +
                          " " + bgName(e.battlegroundId) +
                          " bracket=" +
                          std::to_string(e.bracketIndex) + ")";
        if (e.rewardId == 0)
            errors.push_back(ctx + ": rewardId is 0");
        if (e.battlegroundId == 0)
            errors.push_back(ctx +
                ": battlegroundId is 0");
        if (e.bracketIndex == 0 ||
            e.bracketIndex > B::kMaxBracketIndex) {
            errors.push_back(ctx + ": bracketIndex " +
                std::to_string(e.bracketIndex) +
                " out of range (1..6 vanilla)");
        }
        // CRITICAL invariant: minPlayersToStart > 0,
        // else BG queue would never start a match.
        if (e.minPlayersToStart == 0) {
            errors.push_back(ctx +
                ": minPlayersToStart is 0 — BG queue "
                "would never start a match");
        }
        // Loss honor should be < win honor (winning
        // should always be more rewarding than losing,
        // else there's no incentive to play to win).
        if (e.lossHonor > e.winHonor) {
            errors.push_back(ctx +
                ": lossHonor=" +
                std::to_string(e.lossHonor) +
                " > winHonor=" +
                std::to_string(e.winHonor) +
                " — losing rewards more than winning "
                "(no win incentive)");
        }
        // Mark = 0 on win is unusual — every BG win
        // grants at least 1 mark in vanilla. Warn.
        if (e.winMarks == 0 && e.markItemId != 0) {
            warnings.push_back(ctx +
                ": winMarks=0 but markItemId is set "
                "— win grants no marks; verify "
                "intentional (vanilla wins always "
                "gave at least 1 mark)");
        }
        // bonusItemCount > 0 with bonusItemId = 0 is
        // a contradiction (count of nothing).
        if (e.bonusItemCount > 0 && e.bonusItemId == 0) {
            errors.push_back(ctx +
                ": bonusItemCount=" +
                std::to_string(e.bonusItemCount) +
                " but bonusItemId is 0 — count of "
                "nothing");
        }
        // (battlegroundId, bracketIndex) MUST be
        // unique — runtime dispatch by this pair
        // would tie.
        Pair p{e.battlegroundId, e.bracketIndex};
        if (!bgBracketPairs.insert(p).second) {
            errors.push_back(ctx +
                ": duplicate (bgId=" +
                std::to_string(e.battlegroundId) +
                ", bracket=" +
                std::to_string(e.bracketIndex) +
                ") — runtime reward-lookup tie");
        }
        if (!idsSeen.insert(e.rewardId).second) {
            errors.push_back(ctx + ": duplicate rewardId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wbrd", base, errors, warnings);
    std::printf("validate-wbrd: %s.wbrd\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu stages, all rewardIds + "
                    "(bgId,bracket) pairs unique, "
                    "bracketIndex 1..6, minPlayersToStart "
                    "> 0, winHonor >= lossHonor, no "
                    "bonus-count without bonus-itemId\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wbrd");
    if (out.empty()) out = base + ".wbrd.json";
    if (!wowee::pipeline::WoweeBattlegroundRewardsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wbrd-json: WBRD not found: %s.wbrd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeBattlegroundRewardsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WBRD";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"rewardId", e.rewardId},
            {"battlegroundId", e.battlegroundId},
            {"battlegroundName", bgName(e.battlegroundId)},
            {"bracketIndex", e.bracketIndex},
            {"minPlayersToStart", e.minPlayersToStart},
            {"winHonor", e.winHonor},
            {"lossHonor", e.lossHonor},
            {"markItemId", e.markItemId},
            {"winMarks", e.winMarks},
            {"lossMarks", e.lossMarks},
            {"bonusItemId", e.bonusItemId},
            {"bonusItemCount", e.bonusItemCount},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wbrd-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu stages)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wbrd");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wbrd-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wbrd-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeBattlegroundRewards c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wbrd-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeBattlegroundRewards::Entry e;
        e.rewardId = je.value("rewardId", 0u);
        e.battlegroundId = static_cast<uint16_t>(
            je.value("battlegroundId", 0));
        e.bracketIndex = static_cast<uint8_t>(
            je.value("bracketIndex", 0));
        e.minPlayersToStart = static_cast<uint8_t>(
            je.value("minPlayersToStart", 0));
        e.winHonor = je.value("winHonor", 0u);
        e.lossHonor = je.value("lossHonor", 0u);
        e.markItemId = je.value("markItemId", 0u);
        e.winMarks = static_cast<uint16_t>(
            je.value("winMarks", 0));
        e.lossMarks = static_cast<uint16_t>(
            je.value("lossMarks", 0));
        e.bonusItemId = je.value("bonusItemId", 0u);
        e.bonusItemCount = static_cast<uint16_t>(
            je.value("bonusItemCount", 0));
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeBattlegroundRewardsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wbrd-json: failed to save %s.wbrd\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wbrd (%zu stages)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleBattlegroundRewardsCatalog(int& i, int argc, char** argv,
                                        int& outRc) {
    if (std::strcmp(argv[i], "--gen-brd-av") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAV(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-brd-wsg") == 0 &&
        i + 1 < argc) {
        outRc = handleGenWSG(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-brd-ab") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAB(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wbrd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wbrd") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wbrd-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wbrd-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
