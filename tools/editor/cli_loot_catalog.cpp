#include "cli_loot_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_loot.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
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

bool saveOrError(const wowee::pipeline::WoweeLoot& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeLootLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wlot\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

uint32_t totalDrops(const wowee::pipeline::WoweeLoot& c) {
    uint32_t n = 0;
    for (const auto& e : c.entries) n += static_cast<uint32_t>(e.itemDrops.size());
    return n;
}

void printGenSummary(const wowee::pipeline::WoweeLoot& c,
                     const std::string& base) {
    std::printf("Wrote %s.wlot\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tables  : %zu (%u drop entries total)\n",
                c.entries.size(), totalDrops(c));
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterLoot";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlot");
    auto c = wowee::pipeline::WoweeLootLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-loot")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBandit(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BanditLoot";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlot");
    auto c = wowee::pipeline::WoweeLootLoader::makeBandit(name);
    if (!saveOrError(c, base, "gen-loot-bandit")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBoss(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BossLoot";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlot");
    auto c = wowee::pipeline::WoweeLootLoader::makeBoss(name);
    if (!saveOrError(c, base, "gen-loot-boss")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendDropFlagsStr(std::string& s, uint8_t flags) {
    if (flags & wowee::pipeline::WoweeLoot::QuestRequired)   s += "quest ";
    if (flags & wowee::pipeline::WoweeLoot::GroupRollOnly)   s += "group ";
    if (flags & wowee::pipeline::WoweeLoot::AlwaysDrop)      s += "always ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

void appendTableFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeLoot::QuestOnly)  s += "quest-only ";
    if (flags & wowee::pipeline::WoweeLoot::GroupOnly)  s += "group-only ";
    if (flags & wowee::pipeline::WoweeLoot::Pickpocket) s += "pickpocket ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlot");
    if (!wowee::pipeline::WoweeLootLoader::exists(base)) {
        std::fprintf(stderr, "WLOT not found: %s.wlot\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLootLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wlot"] = base + ".wlot";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        j["totalDrops"] = totalDrops(c);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendTableFlagsStr(fs, e.flags);
            nlohmann::json je;
            je["creatureId"] = e.creatureId;
            je["flags"] = e.flags;
            je["flagsStr"] = fs;
            je["dropCount"] = e.dropCount;
            je["moneyMinCopper"] = e.moneyMinCopper;
            je["moneyMaxCopper"] = e.moneyMaxCopper;
            nlohmann::json drops = nlohmann::json::array();
            for (const auto& d : e.itemDrops) {
                std::string dfs;
                appendDropFlagsStr(dfs, d.flags);
                drops.push_back({
                    {"itemId", d.itemId},
                    {"chancePercent", d.chancePercent},
                    {"minQty", d.minQty},
                    {"maxQty", d.maxQty},
                    {"flags", d.flags},
                    {"flagsStr", dfs},
                });
            }
            je["itemDrops"] = drops;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLOT: %s.wlot\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tables  : %zu (%u drop entries total)\n",
                c.entries.size(), totalDrops(c));
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::string fs;
        appendTableFlagsStr(fs, e.flags);
        std::printf("\n  creatureId=%u  dropCount=%u  money=%u..%uc  flags=%s\n",
                    e.creatureId, e.dropCount,
                    e.moneyMinCopper, e.moneyMaxCopper,
                    fs.c_str());
        if (e.itemDrops.empty()) {
            std::printf("    *no item drops*\n");
            continue;
        }
        std::printf("      itemId  chance%%   qty   flags\n");
        for (const auto& d : e.itemDrops) {
            std::string dfs;
            appendDropFlagsStr(dfs, d.flags);
            std::printf("    %6u    %5.1f   %u..%u   %s\n",
                        d.itemId, d.chancePercent,
                        d.minQty, d.maxQty, dfs.c_str());
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors WOL/WOW/WOMX/WSND/WSPN/WIT JSON pairs. Each
    // table emits creatureId / dropCount / money range plus
    // a sub-array of per-drop entries; flags also emit a
    // string-array form so a hand-author can write
    // ["quest", "always"] instead of "5".
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wlot");
    if (outPath.empty()) outPath = base + ".wlot.json";
    if (!wowee::pipeline::WoweeLootLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wlot-json: WLOT not found: %s.wlot\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLootLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        std::string fs;
        appendTableFlagsStr(fs, e.flags);
        nlohmann::json je;
        je["creatureId"] = e.creatureId;
        je["flags"] = e.flags;
        nlohmann::json fa = nlohmann::json::array();
        if (e.flags & wowee::pipeline::WoweeLoot::QuestOnly)  fa.push_back("quest-only");
        if (e.flags & wowee::pipeline::WoweeLoot::GroupOnly)  fa.push_back("group-only");
        if (e.flags & wowee::pipeline::WoweeLoot::Pickpocket) fa.push_back("pickpocket");
        je["flagsList"] = fa;
        je["dropCount"] = e.dropCount;
        je["moneyMinCopper"] = e.moneyMinCopper;
        je["moneyMaxCopper"] = e.moneyMaxCopper;
        nlohmann::json drops = nlohmann::json::array();
        for (const auto& d : e.itemDrops) {
            nlohmann::json jd;
            jd["itemId"] = d.itemId;
            jd["chancePercent"] = d.chancePercent;
            jd["minQty"] = d.minQty;
            jd["maxQty"] = d.maxQty;
            jd["flags"] = d.flags;
            nlohmann::json dfa = nlohmann::json::array();
            if (d.flags & wowee::pipeline::WoweeLoot::QuestRequired)
                dfa.push_back("quest");
            if (d.flags & wowee::pipeline::WoweeLoot::GroupRollOnly)
                dfa.push_back("group");
            if (d.flags & wowee::pipeline::WoweeLoot::AlwaysDrop)
                dfa.push_back("always");
            jd["flagsList"] = dfa;
            drops.push_back(jd);
        }
        je["itemDrops"] = drops;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wlot-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wlot\n", base.c_str());
    std::printf("  tables : %zu (%u drops total)\n",
                c.entries.size(), totalDrops(c));
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wlot.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wlot");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wlot-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wlot-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto tableFlagFromName = [](const std::string& s) -> uint32_t {
        if (s == "quest-only") return wowee::pipeline::WoweeLoot::QuestOnly;
        if (s == "group-only") return wowee::pipeline::WoweeLoot::GroupOnly;
        if (s == "pickpocket") return wowee::pipeline::WoweeLoot::Pickpocket;
        return 0;
    };
    auto dropFlagFromName = [](const std::string& s) -> uint8_t {
        if (s == "quest")  return wowee::pipeline::WoweeLoot::QuestRequired;
        if (s == "group")  return wowee::pipeline::WoweeLoot::GroupRollOnly;
        if (s == "always") return wowee::pipeline::WoweeLoot::AlwaysDrop;
        return 0;
    };
    wowee::pipeline::WoweeLoot c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeLoot::Entry e;
            e.creatureId = je.value("creatureId", 0u);
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= tableFlagFromName(f.get<std::string>());
                }
            }
            e.dropCount = static_cast<uint8_t>(je.value("dropCount", 1));
            e.moneyMinCopper = je.value("moneyMinCopper", 0u);
            e.moneyMaxCopper = je.value("moneyMaxCopper", 0u);
            if (je.contains("itemDrops") && je["itemDrops"].is_array()) {
                for (const auto& jd : je["itemDrops"]) {
                    wowee::pipeline::WoweeLoot::ItemDrop d;
                    d.itemId = jd.value("itemId", 0u);
                    d.chancePercent = jd.value("chancePercent", 100.0f);
                    d.minQty = static_cast<uint8_t>(jd.value("minQty", 1));
                    d.maxQty = static_cast<uint8_t>(jd.value("maxQty", 1));
                    if (jd.contains("flags") && jd["flags"].is_number_integer()) {
                        d.flags = static_cast<uint8_t>(jd["flags"].get<int>());
                    } else if (jd.contains("flagsList") && jd["flagsList"].is_array()) {
                        for (const auto& f : jd["flagsList"]) {
                            if (f.is_string())
                                d.flags |= dropFlagFromName(f.get<std::string>());
                        }
                    }
                    e.itemDrops.push_back(d);
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeLootLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wlot-json: failed to save %s.wlot\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wlot\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  tables : %zu (%u drops total)\n",
                c.entries.size(), totalDrops(c));
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlot");
    if (!wowee::pipeline::WoweeLootLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wlot: WLOT not found: %s.wlot\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLootLoader::load(base);
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
                          " (creatureId=" + std::to_string(e.creatureId) + ")";
        if (e.creatureId == 0) {
            errors.push_back(ctx + ": creatureId is 0");
        }
        if (e.moneyMinCopper > e.moneyMaxCopper) {
            errors.push_back(ctx + ": moneyMin > moneyMax");
        }
        if (e.dropCount == 0 && !e.itemDrops.empty()) {
            warnings.push_back(ctx +
                ": dropCount=0 but item drops are defined (none will be rolled)");
        }
        for (size_t di = 0; di < e.itemDrops.size(); ++di) {
            const auto& d = e.itemDrops[di];
            std::string dctx = ctx + " drop " + std::to_string(di) +
                                " (itemId=" + std::to_string(d.itemId) + ")";
            if (d.itemId == 0) {
                errors.push_back(dctx + ": itemId is 0");
            }
            if (!std::isfinite(d.chancePercent) ||
                d.chancePercent < 0.0f || d.chancePercent > 100.0f) {
                errors.push_back(dctx +
                    ": chancePercent must be in 0..100, got " +
                    std::to_string(d.chancePercent));
            }
            if (d.minQty == 0) {
                warnings.push_back(dctx + ": minQty=0 (drop with zero quantity)");
            }
            if (d.minQty > d.maxQty) {
                errors.push_back(dctx + ": minQty > maxQty");
            }
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.creatureId) {
                errors.push_back(ctx + ": duplicate creatureId");
                break;
            }
        }
        idsSeen.push_back(e.creatureId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wlot", base, errors, warnings);
    std::printf("validate-wlot: %s.wlot\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu tables, %u total drops, all creatureIds unique\n",
                    c.entries.size(), totalDrops(c));
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleLootCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-loot") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-loot-bandit") == 0 && i + 1 < argc) {
        outRc = handleGenBandit(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-loot-boss") == 0 && i + 1 < argc) {
        outRc = handleGenBoss(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wlot") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wlot") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wlot-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wlot-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
