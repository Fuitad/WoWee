#include "cli_combat_ratings_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_combat_ratings.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCombatRating& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcrr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ratings : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCombatRatings";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcrr");
    auto c = wowee::pipeline::WoweeCombatRatingLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatRatingLoader>(c, base, "gen-crr", ".wcrr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDefensive(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DefensiveCombatRatings";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcrr");
    auto c = wowee::pipeline::WoweeCombatRatingLoader::makeDefensive(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatRatingLoader>(c, base, "gen-crr-defensive", ".wcrr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenSpell(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SpellCombatRatings";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcrr");
    auto c = wowee::pipeline::WoweeCombatRatingLoader::makeSpell(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatRatingLoader>(c, base, "gen-crr-spell", ".wcrr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcrr");
    if (!wowee::pipeline::WoweeCombatRatingLoader::exists(base)) {
        return reportMissing("WCRR", base, ".wcrr");
    }
    auto c = wowee::pipeline::WoweeCombatRatingLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcrr"] = base + ".wcrr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"ratingType", e.ratingType},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"ratingKind", e.ratingKind},
                {"ratingKindName", wowee::pipeline::WoweeCombatRating::ratingKindName(e.ratingKind)},
                {"pointsAtL1", e.pointsAtL1},
                {"pointsAtL60", e.pointsAtL60},
                {"pointsAtL70", e.pointsAtL70},
                {"pointsAtL80", e.pointsAtL80},
                {"maxBenefitPercent", e.maxBenefitPercent},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCRR: %s.wcrr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  ratings : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind        L1     L60    L70    L80     maxPct  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-9s   %5.2f  %5.2f  %5.2f  %6.2f   %5.1f   %s\n",
                    e.ratingType,
                    wowee::pipeline::WoweeCombatRating::ratingKindName(e.ratingKind),
                    e.pointsAtL1, e.pointsAtL60, e.pointsAtL70,
                    e.pointsAtL80, e.maxBenefitPercent,
                    e.name.c_str());
    }
    std::printf("  legend: pointsAtLN = how many rating points = 1%% benefit at level N\n");
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    return cli::exportCatalogJson<wowee::pipeline::WoweeCombatRatingLoader>(
        i, argc, argv, "wcrr", "WCRR", "ratings ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"ratingType", e.ratingType},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"ratingKind", e.ratingKind},
                {"ratingKindName", wowee::pipeline::WoweeCombatRating::ratingKindName(e.ratingKind)},
                {"pointsAtL1", e.pointsAtL1},
                {"pointsAtL60", e.pointsAtL60},
                {"pointsAtL70", e.pointsAtL70},
                {"pointsAtL80", e.pointsAtL80},
                {"maxBenefitPercent", e.maxBenefitPercent},
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
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wcrr");
    outBase = cli::withoutExt(outBase, ".wcrr");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wcrr-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wcrr-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "combat")     return wowee::pipeline::WoweeCombatRating::Combat;
        if (s == "defense")    return wowee::pipeline::WoweeCombatRating::Defense;
        if (s == "spell")      return wowee::pipeline::WoweeCombatRating::Spell;
        if (s == "resilience") return wowee::pipeline::WoweeCombatRating::Resilience;
        if (s == "other")      return wowee::pipeline::WoweeCombatRating::Other;
        return wowee::pipeline::WoweeCombatRating::Combat;
    };
    wowee::pipeline::WoweeCombatRating c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCombatRating::Entry e;
            e.ratingType = je.value("ratingType", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("ratingKind") &&
                je["ratingKind"].is_number_integer()) {
                e.ratingKind = static_cast<uint8_t>(
                    je["ratingKind"].get<int>());
            } else if (je.contains("ratingKindName") &&
                       je["ratingKindName"].is_string()) {
                e.ratingKind = kindFromName(
                    je["ratingKindName"].get<std::string>());
            }
            e.pointsAtL1 = je.value("pointsAtL1", 1.0f);
            e.pointsAtL60 = je.value("pointsAtL60", 14.0f);
            e.pointsAtL70 = je.value("pointsAtL70", 22.0f);
            e.pointsAtL80 = je.value("pointsAtL80", 45.0f);
            e.maxBenefitPercent = je.value("maxBenefitPercent",
                                            100.0f);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeCombatRatingLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcrr-json: failed to save %s.wcrr\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcrr\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  ratings : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeCombatRatingLoader>(
        i, argc, argv, "wcrr", "WCRR",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.ratingType);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.ratingType == 0)
                errors.push_back(ctx + ": ratingType is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.ratingKind > wowee::pipeline::WoweeCombatRating::Other) {
                errors.push_back(ctx + ": ratingKind " +
                    std::to_string(e.ratingKind) + " not in 0..4");
            }
            // Conversion floor must be > 0 — division by zero
            // would crash the stat resolver.
            if (e.pointsAtL1 <= 0.0f || e.pointsAtL60 <= 0.0f ||
                e.pointsAtL70 <= 0.0f || e.pointsAtL80 <= 0.0f) {
                errors.push_back(ctx +
                    ": one or more pointsAtLN values <= 0 "
                    "(divide-by-zero risk in stat resolver)");
            }
            if (e.maxBenefitPercent <= 0.0f) {
                errors.push_back(ctx +
                    ": maxBenefitPercent " +
                    std::to_string(e.maxBenefitPercent) +
                    " <= 0 (rating would never grant any benefit)");
            }
            // The conversion curve should be monotonic-ascending
            // (more rating needed at higher levels). A flat or
            // descending curve is plausible only for direct 1:1
            // ratings (SpellPower / SpellPenetration / MP5).
            bool flat = e.pointsAtL1 == e.pointsAtL60 &&
                         e.pointsAtL60 == e.pointsAtL70 &&
                         e.pointsAtL70 == e.pointsAtL80;
            bool ascending = e.pointsAtL1 <= e.pointsAtL60 &&
                              e.pointsAtL60 <= e.pointsAtL70 &&
                              e.pointsAtL70 <= e.pointsAtL80;
            if (!flat && !ascending) {
                warnings.push_back(ctx +
                    ": conversion curve non-monotonic (" +
                    std::to_string(e.pointsAtL1) + " / " +
                    std::to_string(e.pointsAtL60) + " / " +
                    std::to_string(e.pointsAtL70) + " / " +
                    std::to_string(e.pointsAtL80) +
                    ") — typically rating cost ascends with level");
            }
            if (!idsSeen.add(e.ratingType)) errors.push_back(ctx + ": duplicate ratingType");
        }
            return formatted("%zu ratings, all ratingTypes unique, all curves monotonic", c.entries.size());
        });
}

} // namespace

bool handleCombatRatingsCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-crr") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-crr-defensive") == 0 &&
        i + 1 < argc) {
        outRc = handleGenDefensive(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-crr-spell") == 0 && i + 1 < argc) {
        outRc = handleGenSpell(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcrr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcrr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcrr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcrr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
