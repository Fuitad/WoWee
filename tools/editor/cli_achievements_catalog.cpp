#include "cli_achievements_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_achievements.hpp"
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

void appendAchFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeAchievement::HiddenUntilEarned) s += "hidden ";
    if (flags & wowee::pipeline::WoweeAchievement::ServerFirst)       s += "server-first ";
    if (flags & wowee::pipeline::WoweeAchievement::RealmFirst)        s += "realm-first ";
    if (flags & wowee::pipeline::WoweeAchievement::Tracking)          s += "tracking ";
    if (flags & wowee::pipeline::WoweeAchievement::Counter)           s += "counter ";
    if (flags & wowee::pipeline::WoweeAchievement::Account)           s += "account ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

bool saveOrError(const wowee::pipeline::WoweeAchievement& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeAchievementLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wach\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

uint32_t totalCriteria(const wowee::pipeline::WoweeAchievement& c) {
    uint32_t n = 0;
    for (const auto& e : c.entries) n += static_cast<uint32_t>(e.criteria.size());
    return n;
}

void printGenSummary(const wowee::pipeline::WoweeAchievement& c,
                     const std::string& base) {
    std::printf("Wrote %s.wach\n", base.c_str());
    std::printf("  catalog      : %s\n", c.name.c_str());
    std::printf("  achievements : %zu (%u criteria total)\n",
                c.entries.size(), totalCriteria(c));
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterAchievements";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wach");
    auto c = wowee::pipeline::WoweeAchievementLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-achievements")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBandit(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BanditAchievements";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wach");
    auto c = wowee::pipeline::WoweeAchievementLoader::makeBandit(name);
    if (!saveOrError(c, base, "gen-achievements-bandit")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMeta(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MetaAchievements";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wach");
    auto c = wowee::pipeline::WoweeAchievementLoader::makeMeta(name);
    if (!saveOrError(c, base, "gen-achievements-meta")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wach");
    if (!wowee::pipeline::WoweeAchievementLoader::exists(base)) {
        std::fprintf(stderr, "WACH not found: %s.wach\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAchievementLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wach"] = base + ".wach";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        j["totalCriteria"] = totalCriteria(c);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendAchFlagsStr(fs, e.flags);
            nlohmann::json je;
            je["achievementId"] = e.achievementId;
            je["categoryId"] = e.categoryId;
            je["name"] = e.name;
            je["description"] = e.description;
            je["iconPath"] = e.iconPath;
            je["titleReward"] = e.titleReward;
            je["points"] = e.points;
            je["minLevel"] = e.minLevel;
            je["faction"] = e.faction;
            je["factionName"] = wowee::pipeline::WoweeAchievement::factionName(e.faction);
            je["flags"] = e.flags;
            je["flagsStr"] = fs;
            nlohmann::json ca = nlohmann::json::array();
            for (const auto& cr : e.criteria) {
                ca.push_back({
                    {"criteriaId", cr.criteriaId},
                    {"kind", cr.kind},
                    {"kindName", wowee::pipeline::WoweeAchievement::criteriaKindName(cr.kind)},
                    {"targetId", cr.targetId},
                    {"quantity", cr.quantity},
                    {"description", cr.description},
                });
            }
            je["criteria"] = ca;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WACH: %s.wach\n", base.c_str());
    std::printf("  catalog      : %s\n", c.name.c_str());
    std::printf("  achievements : %zu (%u criteria total)\n",
                c.entries.size(), totalCriteria(c));
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::string fs;
        appendAchFlagsStr(fs, e.flags);
        std::printf("\n  achievementId=%u  points=%u  faction=%s  flags=%s%s%s\n",
                    e.achievementId, e.points,
                    wowee::pipeline::WoweeAchievement::factionName(e.faction),
                    fs.c_str(),
                    e.titleReward.empty() ? "" : "  title=",
                    e.titleReward.c_str());
        std::printf("    name : %s\n", e.name.c_str());
        if (e.criteria.empty()) {
            std::printf("    *no criteria*\n");
            continue;
        }
        for (const auto& cr : e.criteria) {
            std::printf("    [%-5s] target=%-5u  qty=%u   %s\n",
                        wowee::pipeline::WoweeAchievement::criteriaKindName(cr.kind),
                        cr.targetId, cr.quantity,
                        cr.description.c_str());
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each achievement emits scalar fields plus
    // criteria array; criterion.kind, faction, and flags emit
    // dual int + name forms.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wach");
    if (outPath.empty()) outPath = base + ".wach.json";
    if (!wowee::pipeline::WoweeAchievementLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wach-json: WACH not found: %s.wach\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAchievementLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["achievementId"] = e.achievementId;
        je["categoryId"] = e.categoryId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["iconPath"] = e.iconPath;
        je["titleReward"] = e.titleReward;
        je["points"] = e.points;
        je["minLevel"] = e.minLevel;
        je["faction"] = e.faction;
        je["factionName"] = wowee::pipeline::WoweeAchievement::factionName(e.faction);
        je["flags"] = e.flags;
        nlohmann::json fa = nlohmann::json::array();
        if (e.flags & wowee::pipeline::WoweeAchievement::HiddenUntilEarned) fa.push_back("hidden");
        if (e.flags & wowee::pipeline::WoweeAchievement::ServerFirst)       fa.push_back("server-first");
        if (e.flags & wowee::pipeline::WoweeAchievement::RealmFirst)        fa.push_back("realm-first");
        if (e.flags & wowee::pipeline::WoweeAchievement::Tracking)          fa.push_back("tracking");
        if (e.flags & wowee::pipeline::WoweeAchievement::Counter)           fa.push_back("counter");
        if (e.flags & wowee::pipeline::WoweeAchievement::Account)           fa.push_back("account");
        je["flagsList"] = fa;
        nlohmann::json ca = nlohmann::json::array();
        for (const auto& cr : e.criteria) {
            ca.push_back({
                {"criteriaId", cr.criteriaId},
                {"kind", cr.kind},
                {"kindName", wowee::pipeline::WoweeAchievement::criteriaKindName(cr.kind)},
                {"targetId", cr.targetId},
                {"quantity", cr.quantity},
                {"description", cr.description},
            });
        }
        je["criteria"] = ca;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wach-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source       : %s.wach\n", base.c_str());
    std::printf("  achievements : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wach");
    outBase = cli::withoutExt(outBase, ".wach");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wach-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wach-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "kill")    return wowee::pipeline::WoweeAchievement::KillCreature;
        if (s == "quest")   return wowee::pipeline::WoweeAchievement::CompleteQuest;
        if (s == "loot")    return wowee::pipeline::WoweeAchievement::LootItem;
        if (s == "level")   return wowee::pipeline::WoweeAchievement::ReachLevel;
        if (s == "rep")     return wowee::pipeline::WoweeAchievement::EarnReputation;
        if (s == "cast")    return wowee::pipeline::WoweeAchievement::CastSpell;
        if (s == "skill")   return wowee::pipeline::WoweeAchievement::ReachSkillLevel;
        if (s == "visit")   return wowee::pipeline::WoweeAchievement::VisitArea;
        if (s == "meta")    return wowee::pipeline::WoweeAchievement::CompleteAchievement;
        return wowee::pipeline::WoweeAchievement::KillCreature;
    };
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "both")     return wowee::pipeline::WoweeAchievement::FactionBoth;
        if (s == "alliance") return wowee::pipeline::WoweeAchievement::FactionAlliance;
        if (s == "horde")    return wowee::pipeline::WoweeAchievement::FactionHorde;
        return wowee::pipeline::WoweeAchievement::FactionBoth;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "hidden")       return wowee::pipeline::WoweeAchievement::HiddenUntilEarned;
        if (s == "server-first") return wowee::pipeline::WoweeAchievement::ServerFirst;
        if (s == "realm-first")  return wowee::pipeline::WoweeAchievement::RealmFirst;
        if (s == "tracking")     return wowee::pipeline::WoweeAchievement::Tracking;
        if (s == "counter")      return wowee::pipeline::WoweeAchievement::Counter;
        if (s == "account")      return wowee::pipeline::WoweeAchievement::Account;
        return 0;
    };
    wowee::pipeline::WoweeAchievement c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeAchievement::Entry e;
            e.achievementId = je.value("achievementId", 0u);
            e.categoryId = je.value("categoryId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            e.titleReward = je.value("titleReward", std::string{});
            e.points = je.value("points", 10u);
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            if (je.contains("faction") && je["faction"].is_number_integer()) {
                e.faction = static_cast<uint8_t>(je["faction"].get<int>());
            } else if (je.contains("factionName") && je["factionName"].is_string()) {
                e.faction = factionFromName(je["factionName"].get<std::string>());
            }
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= flagFromName(f.get<std::string>());
                }
            }
            if (je.contains("criteria") && je["criteria"].is_array()) {
                for (const auto& jc : je["criteria"]) {
                    wowee::pipeline::WoweeAchievement::Criterion cr;
                    cr.criteriaId = jc.value("criteriaId", 0u);
                    if (jc.contains("kind") && jc["kind"].is_number_integer()) {
                        cr.kind = static_cast<uint8_t>(jc["kind"].get<int>());
                    } else if (jc.contains("kindName") && jc["kindName"].is_string()) {
                        cr.kind = kindFromName(jc["kindName"].get<std::string>());
                    }
                    cr.targetId = jc.value("targetId", 0u);
                    cr.quantity = jc.value("quantity", 1u);
                    cr.description = jc.value("description", std::string{});
                    e.criteria.push_back(cr);
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeAchievementLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wach-json: failed to save %s.wach\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wach\n", outBase.c_str());
    std::printf("  source       : %s\n", jsonPath.c_str());
    std::printf("  achievements : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wach");
    if (!wowee::pipeline::WoweeAchievementLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wach: WACH not found: %s.wach\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAchievementLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    idsSeen.reserve(c.entries.size());
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.achievementId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.achievementId == 0) {
            errors.push_back(ctx + ": achievementId is 0");
        }
        if (e.name.empty()) {
            errors.push_back(ctx + ": name is empty");
        }
        if (e.faction > wowee::pipeline::WoweeAchievement::FactionHorde) {
            errors.push_back(ctx + ": faction " +
                std::to_string(e.faction) + " not in 0..2");
        }
        if (e.criteria.empty()) {
            warnings.push_back(ctx +
                ": no criteria (achievement can never be earned)");
        }
        for (size_t ci = 0; ci < e.criteria.size(); ++ci) {
            const auto& cr = e.criteria[ci];
            std::string cctx = ctx + " criterion " + std::to_string(ci);
            if (cr.kind > wowee::pipeline::WoweeAchievement::CompleteAchievement) {
                errors.push_back(cctx + ": kind " +
                    std::to_string(cr.kind) + " not in 0..8");
            }
            if (cr.quantity == 0) {
                errors.push_back(cctx + ": quantity is 0");
            }
            // ReachLevel and Counter-style criteria can have
            // targetId=0; everything else needs a real target.
            bool needsTarget =
                cr.kind != wowee::pipeline::WoweeAchievement::ReachLevel;
            if (needsTarget && cr.targetId == 0) {
                errors.push_back(cctx + ": targetId is 0 (no resource referenced)");
            }
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.achievementId) {
                errors.push_back(ctx + ": duplicate achievementId");
                break;
            }
        }
        idsSeen.push_back(e.achievementId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wach", base, errors, warnings);
    std::printf("validate-wach: %s.wach\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu achievements (%u criteria), all IDs unique\n",
                    c.entries.size(), totalCriteria(c));
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleAchievementsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-achievements") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-achievements-bandit") == 0 && i + 1 < argc) {
        outRc = handleGenBandit(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-achievements-meta") == 0 && i + 1 < argc) {
        outRc = handleGenMeta(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wach") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wach") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wach-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wach-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
