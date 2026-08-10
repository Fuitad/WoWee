#include "cli_quests_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_quests.hpp"
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

void appendQuestFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeQuest::Daily)        s += "daily ";
    if (flags & wowee::pipeline::WoweeQuest::Weekly)       s += "weekly ";
    if (flags & wowee::pipeline::WoweeQuest::Raid)         s += "raid ";
    if (flags & wowee::pipeline::WoweeQuest::Group)        s += "group ";
    if (flags & wowee::pipeline::WoweeQuest::AutoComplete) s += "auto-complete ";
    if (flags & wowee::pipeline::WoweeQuest::AutoAccept)   s += "auto-accept ";
    if (flags & wowee::pipeline::WoweeQuest::Repeatable)   s += "repeatable ";
    if (flags & wowee::pipeline::WoweeQuest::ClassQuest)   s += "class ";
    if (flags & wowee::pipeline::WoweeQuest::Pvp)          s += "pvp ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

bool saveOrError(const wowee::pipeline::WoweeQuest& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeQuestLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wqt\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeQuest& c,
                     const std::string& base) {
    std::printf("Wrote %s.wqt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  quests  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterQuests";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wqt");
    auto c = wowee::pipeline::WoweeQuestLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-quests")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenChain(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestChain";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wqt");
    auto c = wowee::pipeline::WoweeQuestLoader::makeChain(name);
    if (!saveOrError(c, base, "gen-quests-chain")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDaily(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DailyQuests";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wqt");
    auto c = wowee::pipeline::WoweeQuestLoader::makeDaily(name);
    if (!saveOrError(c, base, "gen-quests-daily")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wqt");
    if (!wowee::pipeline::WoweeQuestLoader::exists(base)) {
        std::fprintf(stderr, "WQT not found: %s.wqt\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeQuestLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wqt"] = base + ".wqt";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendQuestFlagsStr(fs, e.flags);
            nlohmann::json je;
            je["questId"] = e.questId;
            je["title"] = e.title;
            je["objective"] = e.objective;
            je["description"] = e.description;
            je["minLevel"] = e.minLevel;
            je["questLevel"] = e.questLevel;
            je["maxLevel"] = e.maxLevel;
            je["requiredClassMask"] = e.requiredClassMask;
            je["requiredRaceMask"] = e.requiredRaceMask;
            je["prevQuestId"] = e.prevQuestId;
            je["nextQuestId"] = e.nextQuestId;
            je["giverCreatureId"] = e.giverCreatureId;
            je["turninCreatureId"] = e.turninCreatureId;
            nlohmann::json oa = nlohmann::json::array();
            for (const auto& o : e.objectives) {
                oa.push_back({
                    {"kind", o.kind},
                    {"kindName", wowee::pipeline::WoweeQuest::objectiveKindName(o.kind)},
                    {"targetId", o.targetId},
                    {"quantity", o.quantity},
                });
            }
            je["objectives"] = oa;
            je["xpReward"] = e.xpReward;
            je["moneyCopperReward"] = e.moneyCopperReward;
            nlohmann::json ra = nlohmann::json::array();
            for (const auto& r : e.rewardItems) {
                ra.push_back({
                    {"itemId", r.itemId},
                    {"qty", r.qty},
                    {"pickFlags", r.pickFlags},
                });
            }
            je["rewardItems"] = ra;
            je["flags"] = e.flags;
            je["flagsStr"] = fs;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WQT: %s.wqt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  quests  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::string fs;
        appendQuestFlagsStr(fs, e.flags);
        std::printf("\n  questId=%u  level=%u  giver=%u  flags=%s\n",
                    e.questId, e.questLevel, e.giverCreatureId, fs.c_str());
        std::printf("    title    : %s\n", e.title.c_str());
        std::printf("    objective: %s\n", e.objective.c_str());
        if (!e.objectives.empty()) {
            std::printf("    targets  : ");
            for (size_t k = 0; k < e.objectives.size(); ++k) {
                const auto& o = e.objectives[k];
                std::printf("%s%s id=%u x%u",
                            k > 0 ? ", " : "",
                            wowee::pipeline::WoweeQuest::objectiveKindName(o.kind),
                            o.targetId, o.quantity);
            }
            std::printf("\n");
        }
        std::printf("    reward   : %u xp + %u copper",
                    e.xpReward, e.moneyCopperReward);
        if (!e.rewardItems.empty()) {
            std::printf(" + items: ");
            for (size_t k = 0; k < e.rewardItems.size(); ++k) {
                const auto& r = e.rewardItems[k];
                std::printf("%sitem %u x%u%s",
                            k > 0 ? ", " : "",
                            r.itemId, r.qty,
                            (r.pickFlags & wowee::pipeline::WoweeQuest::PlayerChoice) ?
                                " (choice)" : "");
            }
        }
        std::printf("\n");
        if (e.prevQuestId != 0 || e.nextQuestId != 0) {
            std::printf("    chain    : prev=%u, next=%u\n",
                        e.prevQuestId, e.nextQuestId);
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each quest emits all 14 scalar fields plus
    // the variable-length objectives + rewards arrays.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wqt");
    if (outPath.empty()) outPath = base + ".wqt.json";
    if (!wowee::pipeline::WoweeQuestLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wqt-json: WQT not found: %s.wqt\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeQuestLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["questId"] = e.questId;
        je["title"] = e.title;
        je["objective"] = e.objective;
        je["description"] = e.description;
        je["minLevel"] = e.minLevel;
        je["questLevel"] = e.questLevel;
        je["maxLevel"] = e.maxLevel;
        je["requiredClassMask"] = e.requiredClassMask;
        je["requiredRaceMask"] = e.requiredRaceMask;
        je["prevQuestId"] = e.prevQuestId;
        je["nextQuestId"] = e.nextQuestId;
        je["giverCreatureId"] = e.giverCreatureId;
        je["turninCreatureId"] = e.turninCreatureId;
        nlohmann::json oa = nlohmann::json::array();
        for (const auto& o : e.objectives) {
            oa.push_back({
                {"kind", o.kind},
                {"kindName", wowee::pipeline::WoweeQuest::objectiveKindName(o.kind)},
                {"targetId", o.targetId},
                {"quantity", o.quantity},
            });
        }
        je["objectives"] = oa;
        je["xpReward"] = e.xpReward;
        je["moneyCopperReward"] = e.moneyCopperReward;
        nlohmann::json ra = nlohmann::json::array();
        for (const auto& r : e.rewardItems) {
            nlohmann::json jr;
            jr["itemId"] = r.itemId;
            jr["qty"] = r.qty;
            jr["pickFlags"] = r.pickFlags;
            nlohmann::json pa = nlohmann::json::array();
            if (r.pickFlags & wowee::pipeline::WoweeQuest::AutoGiven)
                pa.push_back("auto");
            if (r.pickFlags & wowee::pipeline::WoweeQuest::PlayerChoice)
                pa.push_back("choice");
            jr["pickFlagsList"] = pa;
            ra.push_back(jr);
        }
        je["rewardItems"] = ra;
        je["flags"] = e.flags;
        nlohmann::json fa = nlohmann::json::array();
        if (e.flags & wowee::pipeline::WoweeQuest::Daily)        fa.push_back("daily");
        if (e.flags & wowee::pipeline::WoweeQuest::Weekly)       fa.push_back("weekly");
        if (e.flags & wowee::pipeline::WoweeQuest::Raid)         fa.push_back("raid");
        if (e.flags & wowee::pipeline::WoweeQuest::Group)        fa.push_back("group");
        if (e.flags & wowee::pipeline::WoweeQuest::AutoComplete) fa.push_back("auto-complete");
        if (e.flags & wowee::pipeline::WoweeQuest::AutoAccept)   fa.push_back("auto-accept");
        if (e.flags & wowee::pipeline::WoweeQuest::Repeatable)   fa.push_back("repeatable");
        if (e.flags & wowee::pipeline::WoweeQuest::ClassQuest)   fa.push_back("class");
        if (e.flags & wowee::pipeline::WoweeQuest::Pvp)          fa.push_back("pvp");
        je["flagsList"] = fa;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wqt-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wqt\n", base.c_str());
    std::printf("  quests : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wqt.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wqt");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wqt-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wqt-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "kill")     return wowee::pipeline::WoweeQuest::KillCreature;
        if (s == "collect")  return wowee::pipeline::WoweeQuest::CollectItem;
        if (s == "interact") return wowee::pipeline::WoweeQuest::InteractObject;
        if (s == "visit")    return wowee::pipeline::WoweeQuest::VisitArea;
        if (s == "escort")   return wowee::pipeline::WoweeQuest::EscortNpc;
        if (s == "cast")     return wowee::pipeline::WoweeQuest::SpellCast;
        return wowee::pipeline::WoweeQuest::KillCreature;
    };
    auto pickFlagFromName = [](const std::string& s) -> uint8_t {
        if (s == "auto")   return wowee::pipeline::WoweeQuest::AutoGiven;
        if (s == "choice") return wowee::pipeline::WoweeQuest::PlayerChoice;
        return 0;
    };
    auto questFlagFromName = [](const std::string& s) -> uint32_t {
        if (s == "daily")         return wowee::pipeline::WoweeQuest::Daily;
        if (s == "weekly")        return wowee::pipeline::WoweeQuest::Weekly;
        if (s == "raid")          return wowee::pipeline::WoweeQuest::Raid;
        if (s == "group")         return wowee::pipeline::WoweeQuest::Group;
        if (s == "auto-complete") return wowee::pipeline::WoweeQuest::AutoComplete;
        if (s == "auto-accept")   return wowee::pipeline::WoweeQuest::AutoAccept;
        if (s == "repeatable")    return wowee::pipeline::WoweeQuest::Repeatable;
        if (s == "class")         return wowee::pipeline::WoweeQuest::ClassQuest;
        if (s == "pvp")           return wowee::pipeline::WoweeQuest::Pvp;
        return 0;
    };
    wowee::pipeline::WoweeQuest c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeQuest::Entry e;
            e.questId = je.value("questId", 0u);
            e.title = je.value("title", std::string{});
            e.objective = je.value("objective", std::string{});
            e.description = je.value("description", std::string{});
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            e.questLevel = static_cast<uint16_t>(je.value("questLevel", 1));
            e.maxLevel = static_cast<uint16_t>(je.value("maxLevel", 0));
            e.requiredClassMask = je.value("requiredClassMask", 0u);
            e.requiredRaceMask = je.value("requiredRaceMask", 0u);
            e.prevQuestId = je.value("prevQuestId", 0u);
            e.nextQuestId = je.value("nextQuestId", 0u);
            e.giverCreatureId = je.value("giverCreatureId", 0u);
            e.turninCreatureId = je.value("turninCreatureId", 0u);
            if (je.contains("objectives") && je["objectives"].is_array()) {
                for (const auto& jo : je["objectives"]) {
                    wowee::pipeline::WoweeQuest::Objective o;
                    if (jo.contains("kind") && jo["kind"].is_number_integer()) {
                        o.kind = static_cast<uint8_t>(jo["kind"].get<int>());
                    } else if (jo.contains("kindName") && jo["kindName"].is_string()) {
                        o.kind = kindFromName(jo["kindName"].get<std::string>());
                    }
                    o.targetId = jo.value("targetId", 0u);
                    o.quantity = static_cast<uint16_t>(jo.value("quantity", 1));
                    e.objectives.push_back(o);
                }
            }
            e.xpReward = je.value("xpReward", 0u);
            e.moneyCopperReward = je.value("moneyCopperReward", 0u);
            if (je.contains("rewardItems") && je["rewardItems"].is_array()) {
                for (const auto& jr : je["rewardItems"]) {
                    wowee::pipeline::WoweeQuest::RewardItem r;
                    r.itemId = jr.value("itemId", 0u);
                    r.qty = static_cast<uint8_t>(jr.value("qty", 1));
                    if (jr.contains("pickFlags") && jr["pickFlags"].is_number_integer()) {
                        r.pickFlags = static_cast<uint8_t>(jr["pickFlags"].get<int>());
                    } else if (jr.contains("pickFlagsList") && jr["pickFlagsList"].is_array()) {
                        r.pickFlags = 0;
                        for (const auto& f : jr["pickFlagsList"]) {
                            if (f.is_string())
                                r.pickFlags |= pickFlagFromName(f.get<std::string>());
                        }
                    }
                    e.rewardItems.push_back(r);
                }
            }
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string())
                        e.flags |= questFlagFromName(f.get<std::string>());
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeQuestLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wqt-json: failed to save %s.wqt\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wqt\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  quests : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wqt");
    if (!wowee::pipeline::WoweeQuestLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wqt: WQT not found: %s.wqt\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeQuestLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    idsSeen.reserve(c.entries.size());
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "quest " + std::to_string(e.questId);
        if (!e.title.empty()) ctx += " (" + e.title + ")";
        if (e.questId == 0) {
            errors.push_back(ctx + ": questId is 0");
        }
        if (e.minLevel == 0) {
            errors.push_back(ctx + ": minLevel is 0");
        }
        if (e.maxLevel != 0 && e.maxLevel < e.minLevel) {
            errors.push_back(ctx + ": maxLevel < minLevel");
        }
        if (e.title.empty()) {
            errors.push_back(ctx + ": title is empty");
        }
        // A quest with no objectives only makes sense if it's
        // a chain-bridge (auto-complete on dialogue).
        if (e.objectives.empty() &&
            !(e.flags & wowee::pipeline::WoweeQuest::AutoComplete)) {
            warnings.push_back(ctx +
                ": no objectives and not AutoComplete (player can't finish)");
        }
        // No reward at all is technically valid for chain bridges
        // but is usually a mistake.
        if (e.xpReward == 0 && e.moneyCopperReward == 0 &&
            e.rewardItems.empty()) {
            warnings.push_back(ctx + ": no rewards (xp / money / items)");
        }
        // Daily without Repeatable is contradictory.
        if ((e.flags & wowee::pipeline::WoweeQuest::Daily) &&
            !(e.flags & wowee::pipeline::WoweeQuest::Repeatable)) {
            warnings.push_back(ctx +
                ": Daily quest is not flagged Repeatable");
        }
        for (size_t oi = 0; oi < e.objectives.size(); ++oi) {
            const auto& o = e.objectives[oi];
            std::string octx = ctx + " obj " + std::to_string(oi);
            if (o.targetId == 0) {
                errors.push_back(octx + ": targetId is 0");
            }
            if (o.quantity == 0) {
                errors.push_back(octx + ": quantity is 0");
            }
            if (o.kind > wowee::pipeline::WoweeQuest::SpellCast) {
                errors.push_back(octx + ": kind " +
                    std::to_string(o.kind) + " not in known range 0..5");
            }
        }
        for (size_t ri = 0; ri < e.rewardItems.size(); ++ri) {
            const auto& r = e.rewardItems[ri];
            std::string rctx = ctx + " reward " + std::to_string(ri);
            if (r.itemId == 0) {
                errors.push_back(rctx + ": itemId is 0");
            }
            if (r.qty == 0) {
                errors.push_back(rctx + ": qty is 0");
            }
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.questId) {
                errors.push_back(ctx + ": duplicate questId");
                break;
            }
        }
        idsSeen.push_back(e.questId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wqt", base, errors, warnings);
    std::printf("validate-wqt: %s.wqt\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu quests, all questIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleQuestsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-quests") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-quests-chain") == 0 && i + 1 < argc) {
        outRc = handleGenChain(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-quests-daily") == 0 && i + 1 < argc) {
        outRc = handleGenDaily(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wqt") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wqt") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wqt-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wqt-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
