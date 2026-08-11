#include "cli_reputation_rewards_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_reputation_rewards.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* standingTierName(int32_t standing) {
    if (standing >= 42000) return "Exalted";
    if (standing >= 21000) return "Revered";
    if (standing >=  9000) return "Honored";
    if (standing >=  3000) return "Friendly";
    if (standing >=     0) return "Neutral";
    if (standing >= -3000) return "Unfriendly";
    if (standing >= -6000) return "Hostile";
    return "Hated";
}


void printGenSummary(const wowee::pipeline::WoweeReputationRewards& c,
                     const std::string& base) {
    size_t totalItems = 0;
    size_t totalRecipes = 0;
    for (const auto& e : c.entries) {
        totalItems += e.unlockedItemIds.size();
        totalRecipes += e.unlockedRecipeIds.size();
    }
    std::printf("Wrote %s.wrpr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu (%zu items, %zu recipes total)\n",
                c.entries.size(), totalItems, totalRecipes);
}

int handleGenArgent(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ArgentCrusadeRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wrpr");
    auto c = wowee::pipeline::WoweeReputationRewardsLoader::
        makeArgentCrusade(name);
    if (!saveOrError<wowee::pipeline::WoweeReputationRewardsLoader>(c, base, "gen-rpr", ".wrpr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenKaluak(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "KaluakRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wrpr");
    auto c = wowee::pipeline::WoweeReputationRewardsLoader::
        makeKaluak(name);
    if (!saveOrError<wowee::pipeline::WoweeReputationRewardsLoader>(c, base, "gen-rpr-kaluak", ".wrpr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAccord(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WyrmrestAccordRewards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wrpr");
    auto c = wowee::pipeline::WoweeReputationRewardsLoader::
        makeAccordTabard(name);
    if (!saveOrError<wowee::pipeline::WoweeReputationRewardsLoader>(c, base, "gen-rpr-accord", ".wrpr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wrpr");
    if (!wowee::pipeline::WoweeReputationRewardsLoader::exists(
            base)) {
        std::fprintf(stderr, "WRPR not found: %s.wrpr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeReputationRewardsLoader::load(
        base);
    if (jsonOut) {
        nlohmann::json j;
        j["wrpr"] = base + ".wrpr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"tierId", e.tierId},
                {"name", e.name},
                {"description", e.description},
                {"factionId", e.factionId},
                {"minStanding", e.minStanding},
                {"standingTier", standingTierName(e.minStanding)},
                {"discountPct", e.discountPct},
                {"grantsTabard", e.grantsTabard != 0},
                {"grantsMount", e.grantsMount != 0},
                {"iconColorRGBA", e.iconColorRGBA},
                {"unlockedItemIds", e.unlockedItemIds},
                {"unlockedRecipeIds", e.unlockedRecipeIds},
                {"unlockedItemCount", e.unlockedItemIds.size()},
                {"unlockedRecipeCount",
                    e.unlockedRecipeIds.size()},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WRPR: %s.wrpr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tiers   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   faction  standing  tier        discount  tab  mnt  items  recipes  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %5u   %+6d   %-9s    %3u%%      %s    %s   %4zu    %4zu     %s\n",
                    e.tierId, e.factionId,
                    e.minStanding,
                    standingTierName(e.minStanding),
                    e.discountPct,
                    e.grantsTabard ? "yes" : "no ",
                    e.grantsMount ? "yes" : "no ",
                    e.unlockedItemIds.size(),
                    e.unlockedRecipeIds.size(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wrpr");
    if (out.empty()) out = base + ".wrpr.json";
    if (!wowee::pipeline::WoweeReputationRewardsLoader::exists(
            base)) {
        std::fprintf(stderr,
            "export-wrpr-json: WRPR not found: %s.wrpr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeReputationRewardsLoader::load(
        base);
    nlohmann::json j;
    j["magic"] = "WRPR";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"tierId", e.tierId},
            {"name", e.name},
            {"description", e.description},
            {"factionId", e.factionId},
            {"minStanding", e.minStanding},
            {"standingTier", standingTierName(e.minStanding)},
            {"discountPct", e.discountPct},
            {"grantsTabard", e.grantsTabard != 0},
            {"grantsMount", e.grantsMount != 0},
            {"iconColorRGBA", e.iconColorRGBA},
            {"unlockedItemIds", e.unlockedItemIds},
            {"unlockedRecipeIds", e.unlockedRecipeIds},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wrpr-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu tiers)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wrpr");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wrpr-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wrpr-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeReputationRewards c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wrpr-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeReputationRewards::Entry e;
        e.tierId = je.value("tierId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.factionId = je.value("factionId", 0u);
        e.minStanding = je.value("minStanding", 0);
        e.discountPct = static_cast<uint8_t>(
            je.value("discountPct", 0u));
        if (je.contains("grantsTabard")) {
            const auto& v = je["grantsTabard"];
            if (v.is_boolean())
                e.grantsTabard = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.grantsTabard = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        if (je.contains("grantsMount")) {
            const auto& v = je["grantsMount"];
            if (v.is_boolean())
                e.grantsMount = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.grantsMount = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        if (je.contains("unlockedItemIds") &&
            je["unlockedItemIds"].is_array()) {
            for (const auto& s : je["unlockedItemIds"]) {
                if (s.is_number_integer())
                    e.unlockedItemIds.push_back(s.get<uint32_t>());
            }
        }
        if (je.contains("unlockedRecipeIds") &&
            je["unlockedRecipeIds"].is_array()) {
            for (const auto& s : je["unlockedRecipeIds"]) {
                if (s.is_number_integer())
                    e.unlockedRecipeIds.push_back(
                        s.get<uint32_t>());
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeReputationRewardsLoader::save(
            c, outBase)) {
        std::fprintf(stderr,
            "import-wrpr-json: failed to save %s.wrpr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wrpr (%zu tiers)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wrpr");
    if (!wowee::pipeline::WoweeReputationRewardsLoader::exists(
            base)) {
        std::fprintf(stderr,
            "validate-wrpr: WRPR not found: %s.wrpr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeReputationRewardsLoader::load(
        base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // (factionId, minStanding) tuple uniqueness — two
    // tiers binding the same (faction, standing) would
    // make the active-tier lookup ambiguous.
    std::set<uint64_t> tierTupleSeen;
    // Per-faction tier-monotonicity check: discountPct
    // should be non-decreasing as standing increases.
    std::map<uint32_t, std::vector<
        const wowee::pipeline::WoweeReputationRewards::Entry*>>
        byFaction;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.tierId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.tierId == 0)
            errors.push_back(ctx + ": tierId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.factionId == 0) {
            errors.push_back(ctx +
                ": factionId is 0 — tier is not bound "
                "to any WFAC faction");
        }
        if (e.minStanding < -42000 || e.minStanding > 42000) {
            errors.push_back(ctx + ": minStanding " +
                std::to_string(e.minStanding) +
                " outside [-42000, 42000] (Hated to "
                "Exalted) valid range");
        }
        if (e.discountPct > 20) {
            warnings.push_back(ctx + ": discountPct " +
                std::to_string(e.discountPct) +
                " > 20%% — exceeds typical max vendor "
                "discount (Exalted is canonically 20%%)");
        }
        // No item/recipe IDs may be 0.
        for (size_t s = 0; s < e.unlockedItemIds.size(); ++s) {
            if (e.unlockedItemIds[s] == 0) {
                errors.push_back(ctx +
                    ": unlockedItemIds[" + std::to_string(s) +
                    "] = 0");
            }
        }
        for (size_t s = 0; s < e.unlockedRecipeIds.size(); ++s) {
            if (e.unlockedRecipeIds[s] == 0) {
                errors.push_back(ctx +
                    ": unlockedRecipeIds[" + std::to_string(s) +
                    "] = 0");
            }
        }
        if (e.factionId != 0) {
            uint64_t key = (static_cast<uint64_t>(e.factionId)
                            << 32) |
                           static_cast<uint32_t>(e.minStanding);
            if (!tierTupleSeen.insert(key).second) {
                errors.push_back(ctx +
                    ": (factionId=" +
                    std::to_string(e.factionId) +
                    ", minStanding=" +
                    std::to_string(e.minStanding) +
                    ") combo already bound by another "
                    "tier — active-tier lookup would be "
                    "ambiguous");
            }
        }
        if (!idsSeen.insert(e.tierId).second) {
            errors.push_back(ctx + ": duplicate tierId");
        }
        byFaction[e.factionId].push_back(&e);
    }
    // Per-faction monotonicity: discountPct should be
    // non-decreasing as standing increases. Higher
    // standing should never give a worse discount.
    for (auto& [factionId, tiers] : byFaction) {
        if (tiers.size() < 2) continue;
        std::sort(tiers.begin(), tiers.end(),
            [](auto* a, auto* b) {
                return a->minStanding < b->minStanding;
            });
        for (size_t k = 1; k < tiers.size(); ++k) {
            if (tiers[k]->discountPct < tiers[k-1]->discountPct) {
                warnings.push_back("faction " +
                    std::to_string(factionId) +
                    " has decreasing discount: tier '" +
                    tiers[k-1]->name + "' (standing " +
                    std::to_string(tiers[k-1]->minStanding) +
                    ", discount " +
                    std::to_string(tiers[k-1]->discountPct) +
                    "%) > tier '" + tiers[k]->name +
                    "' (standing " +
                    std::to_string(tiers[k]->minStanding) +
                    ", discount " +
                    std::to_string(tiers[k]->discountPct) +
                    "%) — higher standing should not "
                    "have worse discount");
            }
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wrpr", base, errors, warnings);
    std::printf("validate-wrpr: %s.wrpr\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu tiers, all tierIds + "
                    "(faction,standing) tuples unique, "
                    "discounts monotonic per faction\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleReputationRewardsCatalog(int& i, int argc, char** argv,
                                      int& outRc) {
    if (std::strcmp(argv[i], "--gen-rpr") == 0 && i + 1 < argc) {
        outRc = handleGenArgent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-rpr-kaluak") == 0 &&
        i + 1 < argc) {
        outRc = handleGenKaluak(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-rpr-accord") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAccord(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wrpr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wrpr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wrpr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wrpr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
