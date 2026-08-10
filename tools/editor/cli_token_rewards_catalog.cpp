#include "cli_token_rewards_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_token_rewards.hpp"
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

std::string stripWtbrExt(std::string base) {
    stripExt(base, ".wtbr");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeTokenReward& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeTokenRewardLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wtbr\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeTokenReward& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtbr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rewards : %zu\n", c.entries.size());
}

int handleGenRaid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidTokenRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWtbrExt(base);
    auto c = wowee::pipeline::WoweeTokenRewardLoader::makeRaidTokens(name);
    if (!saveOrError(c, base, "gen-tbr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPvP(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvPTokenRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWtbrExt(base);
    auto c = wowee::pipeline::WoweeTokenRewardLoader::makePvP(name);
    if (!saveOrError(c, base, "gen-tbr-pvp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFaction(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FactionTokenRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWtbrExt(base);
    auto c = wowee::pipeline::WoweeTokenRewardLoader::makeFaction(name);
    if (!saveOrError(c, base, "gen-tbr-faction")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWtbrExt(base);
    if (!wowee::pipeline::WoweeTokenRewardLoader::exists(base)) {
        std::fprintf(stderr, "WTBR not found: %s.wtbr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTokenRewardLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtbr"] = base + ".wtbr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"tokenRewardId", e.tokenRewardId},
                {"name", e.name},
                {"description", e.description},
                {"spentTokenItemId", e.spentTokenItemId},
                {"spentTokenCount", e.spentTokenCount},
                {"rewardKind", e.rewardKind},
                {"rewardKindName", wowee::pipeline::WoweeTokenReward::rewardKindName(e.rewardKind)},
                {"rewardId", e.rewardId},
                {"rewardCount", e.rewardCount},
                {"requiredFactionId", e.requiredFactionId},
                {"requiredFactionStanding", e.requiredFactionStanding},
                {"requiredFactionStandingName", wowee::pipeline::WoweeTokenReward::factionStandingName(e.requiredFactionStanding)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTBR: %s.wtbr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rewards : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    spent(item x count)   kind        rewardId   faction@standing      name\n");
    for (const auto& e : c.entries) {
        char gateBuf[64] = "-";
        if (e.requiredFactionId != 0) {
            std::snprintf(gateBuf, sizeof(gateBuf), "%u@%s",
                          e.requiredFactionId,
                          wowee::pipeline::WoweeTokenReward::factionStandingName(e.requiredFactionStanding));
        }
        std::printf("  %4u   %5u x %5u        %-9s   %5u      %-20s  %s\n",
                    e.tokenRewardId,
                    e.spentTokenItemId, e.spentTokenCount,
                    wowee::pipeline::WoweeTokenReward::rewardKindName(e.rewardKind),
                    e.rewardId, gateBuf,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = stripWtbrExt(base);
    if (!wowee::pipeline::WoweeTokenRewardLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtbr-json: WTBR not found: %s.wtbr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTokenRewardLoader::load(base);
    if (outPath.empty()) outPath = base + ".wtbr.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["tokenRewardId"] = e.tokenRewardId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["spentTokenItemId"] = e.spentTokenItemId;
        je["spentTokenCount"] = e.spentTokenCount;
        je["rewardKind"] = e.rewardKind;
        je["rewardKindName"] =
            wowee::pipeline::WoweeTokenReward::rewardKindName(e.rewardKind);
        je["rewardId"] = e.rewardId;
        je["rewardCount"] = e.rewardCount;
        je["requiredFactionId"] = e.requiredFactionId;
        je["requiredFactionStanding"] = e.requiredFactionStanding;
        je["requiredFactionStandingName"] =
            wowee::pipeline::WoweeTokenReward::factionStandingName(e.requiredFactionStanding);
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wtbr-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rewards : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseRewardKindToken(const nlohmann::json& jv,
                             uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeTokenReward::Cosmetic)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "item")     return wowee::pipeline::WoweeTokenReward::Item;
        if (s == "spell")    return wowee::pipeline::WoweeTokenReward::Spell;
        if (s == "title")    return wowee::pipeline::WoweeTokenReward::Title;
        if (s == "mount")    return wowee::pipeline::WoweeTokenReward::Mount;
        if (s == "pet")      return wowee::pipeline::WoweeTokenReward::Pet;
        if (s == "currency") return wowee::pipeline::WoweeTokenReward::Currency;
        if (s == "heirloom") return wowee::pipeline::WoweeTokenReward::Heirloom;
        if (s == "cosmetic") return wowee::pipeline::WoweeTokenReward::Cosmetic;
    }
    return fallback;
}

uint8_t parseFactionStandingToken(const nlohmann::json& jv,
                                  uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeTokenReward::Exalted)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "hated")      return wowee::pipeline::WoweeTokenReward::Hated;
        if (s == "hostile")    return wowee::pipeline::WoweeTokenReward::Hostile;
        if (s == "unfriendly") return wowee::pipeline::WoweeTokenReward::Unfriendly;
        if (s == "neutral")    return wowee::pipeline::WoweeTokenReward::Neutral;
        if (s == "friendly")   return wowee::pipeline::WoweeTokenReward::Friendly;
        if (s == "honored")    return wowee::pipeline::WoweeTokenReward::Honored;
        if (s == "revered")    return wowee::pipeline::WoweeTokenReward::Revered;
        if (s == "exalted")    return wowee::pipeline::WoweeTokenReward::Exalted;
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
            "import-wtbr-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wtbr-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeTokenReward c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeTokenReward::Entry e;
            if (je.contains("tokenRewardId"))    e.tokenRewardId = je["tokenRewardId"].get<uint32_t>();
            if (je.contains("name"))             e.name = je["name"].get<std::string>();
            if (je.contains("description"))      e.description = je["description"].get<std::string>();
            if (je.contains("spentTokenItemId")) e.spentTokenItemId = je["spentTokenItemId"].get<uint32_t>();
            if (je.contains("spentTokenCount"))  e.spentTokenCount = je["spentTokenCount"].get<uint32_t>();
            uint8_t kind = wowee::pipeline::WoweeTokenReward::Item;
            if (je.contains("rewardKind"))
                kind = parseRewardKindToken(je["rewardKind"], kind);
            else if (je.contains("rewardKindName"))
                kind = parseRewardKindToken(je["rewardKindName"], kind);
            e.rewardKind = kind;
            if (je.contains("rewardId"))         e.rewardId = je["rewardId"].get<uint32_t>();
            if (je.contains("rewardCount"))      e.rewardCount = je["rewardCount"].get<uint32_t>();
            if (je.contains("requiredFactionId")) e.requiredFactionId = je["requiredFactionId"].get<uint32_t>();
            uint8_t standing = wowee::pipeline::WoweeTokenReward::Neutral;
            if (je.contains("requiredFactionStanding"))
                standing = parseFactionStandingToken(je["requiredFactionStanding"], standing);
            else if (je.contains("requiredFactionStandingName"))
                standing = parseFactionStandingToken(je["requiredFactionStandingName"], standing);
            e.requiredFactionStanding = standing;
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) {
        outBase = jsonPath;
        const std::string suffix1 = ".wtbr.json";
        const std::string suffix2 = ".json";
        if (outBase.size() >= suffix1.size() &&
            outBase.compare(outBase.size() - suffix1.size(),
                            suffix1.size(), suffix1) == 0) {
            outBase.resize(outBase.size() - suffix1.size());
        } else if (outBase.size() >= suffix2.size() &&
                   outBase.compare(outBase.size() - suffix2.size(),
                                   suffix2.size(), suffix2) == 0) {
            outBase.resize(outBase.size() - suffix2.size());
        }
    }
    outBase = stripWtbrExt(outBase);
    if (!wowee::pipeline::WoweeTokenRewardLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtbr-json: failed to save %s.wtbr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtbr\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rewards : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWtbrExt(base);
    if (!wowee::pipeline::WoweeTokenRewardLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtbr: WTBR not found: %s.wtbr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTokenRewardLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.tokenRewardId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.tokenRewardId == 0)
            errors.push_back(ctx + ": tokenRewardId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.spentTokenItemId == 0)
            errors.push_back(ctx +
                ": spentTokenItemId is 0 — missing token currency");
        if (e.spentTokenCount == 0)
            errors.push_back(ctx +
                ": spentTokenCount is 0 — would grant reward for free");
        if (e.rewardKind > wowee::pipeline::WoweeTokenReward::Cosmetic) {
            errors.push_back(ctx + ": rewardKind " +
                std::to_string(e.rewardKind) + " not in 0..7");
        }
        if (e.requiredFactionStanding > wowee::pipeline::WoweeTokenReward::Exalted) {
            errors.push_back(ctx + ": requiredFactionStanding " +
                std::to_string(e.requiredFactionStanding) +
                " not in 0..7");
        }
        if (e.rewardId == 0)
            warnings.push_back(ctx +
                ": rewardId is 0 — no actual reward target, "
                "vendor will offer the entry but grant nothing");
        // requiredFactionStanding > Neutral with no
        // requiredFactionId is contradictory — the gate
        // can't apply.
        if (e.requiredFactionStanding > wowee::pipeline::WoweeTokenReward::Neutral &&
            e.requiredFactionId == 0) {
            warnings.push_back(ctx +
                ": requiredFactionStanding=" +
                wowee::pipeline::WoweeTokenReward::factionStandingName(e.requiredFactionStanding) +
                " set but requiredFactionId=0 — rep gate "
                "has no faction to check, gate will be ignored");
        }
        // Currency conversion to same item is suspicious
        // (1 X -> N X is usually a config bug).
        if (e.rewardKind == wowee::pipeline::WoweeTokenReward::Currency &&
            e.rewardId == e.spentTokenItemId) {
            warnings.push_back(ctx +
                ": Currency conversion from item " +
                std::to_string(e.spentTokenItemId) +
                " to itself — usually a typo");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.tokenRewardId) {
                errors.push_back(ctx + ": duplicate tokenRewardId");
                break;
            }
        }
        idsSeen.push_back(e.tokenRewardId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wtbr", base, errors, warnings);
    std::printf("validate-wtbr: %s.wtbr\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu rewards, all tokenRewardIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleTokenRewardsCatalog(int& i, int argc, char** argv,
                               int& outRc) {
    if (std::strcmp(argv[i], "--gen-tbr") == 0 && i + 1 < argc) {
        outRc = handleGenRaid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tbr-pvp") == 0 && i + 1 < argc) {
        outRc = handleGenPvP(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tbr-faction") == 0 && i + 1 < argc) {
        outRc = handleGenFaction(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtbr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtbr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtbr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtbr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
