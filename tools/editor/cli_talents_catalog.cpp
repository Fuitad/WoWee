#include "cli_talents_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_talents.hpp"
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


uint32_t totalTalents(const wowee::pipeline::WoweeTalent& c) {
    uint32_t n = 0;
    for (const auto& t : c.trees) n += static_cast<uint32_t>(t.talents.size());
    return n;
}

void printGenSummary(const wowee::pipeline::WoweeTalent& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtal\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  trees   : %zu (%u talents total)\n",
                c.trees.size(), totalTalents(c));
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterTalents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtal");
    auto c = wowee::pipeline::WoweeTalentLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeTalentLoader>(c, base, "gen-talents", ".wtal")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorTalents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtal");
    auto c = wowee::pipeline::WoweeTalentLoader::makeWarrior(name);
    if (!saveOrError<wowee::pipeline::WoweeTalentLoader>(c, base, "gen-talents-warrior", ".wtal")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageTalents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtal");
    auto c = wowee::pipeline::WoweeTalentLoader::makeMage(name);
    if (!saveOrError<wowee::pipeline::WoweeTalentLoader>(c, base, "gen-talents-mage", ".wtal")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtal");
    if (!wowee::pipeline::WoweeTalentLoader::exists(base)) {
        return reportMissing("WTAL", base, ".wtal");
    }
    auto c = wowee::pipeline::WoweeTalentLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtal"] = base + ".wtal";
        j["name"] = c.name;
        j["treeCount"] = c.trees.size();
        j["totalTalents"] = totalTalents(c);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : c.trees) {
            nlohmann::json jt;
            jt["treeId"] = t.treeId;
            jt["name"] = t.name;
            jt["iconPath"] = t.iconPath;
            jt["requiredClassMask"] = t.requiredClassMask;
            nlohmann::json ta = nlohmann::json::array();
            for (const auto& a : t.talents) {
                nlohmann::json ja;
                ja["talentId"] = a.talentId;
                ja["row"] = a.row;
                ja["col"] = a.col;
                ja["maxRank"] = a.maxRank;
                ja["prereqTalentId"] = a.prereqTalentId;
                ja["prereqRank"] = a.prereqRank;
                nlohmann::json sa = nlohmann::json::array();
                for (int r = 0; r < wowee::pipeline::WoweeTalent::kMaxRanks; ++r) {
                    sa.push_back(a.rankSpellIds[r]);
                }
                ja["rankSpellIds"] = sa;
                ta.push_back(ja);
            }
            jt["talents"] = ta;
            arr.push_back(jt);
        }
        j["trees"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTAL: %s.wtal\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  trees   : %zu (%u talents total)\n",
                c.trees.size(), totalTalents(c));
    if (c.trees.empty()) return 0;
    for (const auto& t : c.trees) {
        std::printf("\n  treeId=%u  classMask=0x%x  %s  (%zu talents)\n",
                    t.treeId, t.requiredClassMask,
                    t.name.c_str(), t.talents.size());
        if (t.talents.empty()) {
            std::printf("    *no talents*\n");
            continue;
        }
        std::printf("      id    row col  maxRank  prereq      spellAtR1\n");
        for (const auto& a : t.talents) {
            std::printf("    %5u   %u  %u    %u        %5u/%u    %u\n",
                        a.talentId, a.row, a.col, a.maxRank,
                        a.prereqTalentId, a.prereqRank,
                        a.rankSpellIds[0]);
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each tree emits scalar fields plus the
    // talent array; rankSpellIds becomes a 5-element JSON
    // array.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wtal");
    if (outPath.empty()) outPath = base + ".wtal.json";
    if (!wowee::pipeline::WoweeTalentLoader::exists(base)) {
        return reportMissing("export-wtal-json", "WTAL", base, ".wtal");
    }
    auto c = wowee::pipeline::WoweeTalentLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : c.trees) {
        nlohmann::json jt;
        jt["treeId"] = t.treeId;
        jt["name"] = t.name;
        jt["iconPath"] = t.iconPath;
        jt["requiredClassMask"] = t.requiredClassMask;
        nlohmann::json ta = nlohmann::json::array();
        for (const auto& a : t.talents) {
            nlohmann::json ja;
            ja["talentId"] = a.talentId;
            ja["row"] = a.row;
            ja["col"] = a.col;
            ja["maxRank"] = a.maxRank;
            ja["prereqTalentId"] = a.prereqTalentId;
            ja["prereqRank"] = a.prereqRank;
            nlohmann::json sa = nlohmann::json::array();
            for (int r = 0; r < wowee::pipeline::WoweeTalent::kMaxRanks; ++r) {
                sa.push_back(a.rankSpellIds[r]);
            }
            ja["rankSpellIds"] = sa;
            ta.push_back(ja);
        }
        jt["talents"] = ta;
        arr.push_back(jt);
    }
    j["trees"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wtal-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wtal\n", base.c_str());
    std::printf("  trees  : %zu\n", c.trees.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wtal");
    outBase = cli::withoutExt(outBase, ".wtal");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wtal-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wtal-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    wowee::pipeline::WoweeTalent c;
    c.name = j.value("name", std::string{});
    if (j.contains("trees") && j["trees"].is_array()) {
        for (const auto& jt : j["trees"]) {
            wowee::pipeline::WoweeTalent::Tree t;
            t.treeId = jt.value("treeId", 0u);
            t.name = jt.value("name", std::string{});
            t.iconPath = jt.value("iconPath", std::string{});
            t.requiredClassMask = jt.value("requiredClassMask", 0u);
            if (jt.contains("talents") && jt["talents"].is_array()) {
                for (const auto& ja : jt["talents"]) {
                    wowee::pipeline::WoweeTalent::Talent a;
                    a.talentId = ja.value("talentId", 0u);
                    a.row = static_cast<uint8_t>(ja.value("row", 0));
                    a.col = static_cast<uint8_t>(ja.value("col", 0));
                    a.maxRank = static_cast<uint8_t>(ja.value("maxRank", 1));
                    a.prereqTalentId = ja.value("prereqTalentId", 0u);
                    a.prereqRank = static_cast<uint8_t>(
                        ja.value("prereqRank", 0));
                    if (ja.contains("rankSpellIds") &&
                        ja["rankSpellIds"].is_array()) {
                        const auto& sa = ja["rankSpellIds"];
                        for (int r = 0;
                             r < wowee::pipeline::WoweeTalent::kMaxRanks &&
                             r < static_cast<int>(sa.size()); ++r) {
                            if (sa[r].is_number_integer()) {
                                a.rankSpellIds[r] = sa[r].get<uint32_t>();
                            }
                        }
                    }
                    t.talents.push_back(a);
                }
            }
            c.trees.push_back(std::move(t));
        }
    }
    if (!wowee::pipeline::WoweeTalentLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtal-json: failed to save %s.wtal\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtal\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  trees  : %zu\n", c.trees.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtal");
    if (!wowee::pipeline::WoweeTalentLoader::exists(base)) {
        return reportMissing("validate-wtal", "WTAL", base, ".wtal");
    }
    auto c = wowee::pipeline::WoweeTalentLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.trees.empty()) {
        warnings.push_back("catalog has zero trees");
    }
    std::vector<uint32_t> treeIdsSeen;
    std::vector<uint32_t> talentIdsSeen;
    for (size_t k = 0; k < c.trees.size(); ++k) {
        const auto& t = c.trees[k];
        std::string ctx = "tree " + std::to_string(k) +
                          " (id=" + std::to_string(t.treeId);
        if (!t.name.empty()) ctx += " " + t.name;
        ctx += ")";
        if (t.treeId == 0) errors.push_back(ctx + ": treeId is 0");
        if (t.name.empty()) errors.push_back(ctx + ": name is empty");
        if (t.requiredClassMask == 0) {
            warnings.push_back(ctx +
                ": requiredClassMask=0 (every class would see this tree)");
        }
        for (uint32_t prev : treeIdsSeen) {
            if (prev == t.treeId) {
                errors.push_back(ctx + ": duplicate treeId");
                break;
            }
        }
        treeIdsSeen.push_back(t.treeId);
        for (size_t ti = 0; ti < t.talents.size(); ++ti) {
            const auto& a = t.talents[ti];
            std::string actx = ctx + " talent " + std::to_string(ti) +
                               " (id=" + std::to_string(a.talentId) + ")";
            if (a.talentId == 0) {
                errors.push_back(actx + ": talentId is 0");
            }
            if (a.maxRank == 0 ||
                a.maxRank > wowee::pipeline::WoweeTalent::kMaxRanks) {
                errors.push_back(actx + ": maxRank " +
                    std::to_string(a.maxRank) + " not in 1..5");
            }
            // prereqRank check moved into the second pass below
            // where we have the prereq talent in hand to compare
            // against its actual maxRank.
            if (a.prereqTalentId == a.talentId) {
                errors.push_back(actx +
                    ": talent lists itself as prerequisite");
            }
            // Active spell talents typically have rankSpellIds[0]
            // set even at rank 1 - a passive (stat-modifier) talent
            // may legitimately leave them all 0. Just check for
            // ascending non-zero ordering: if rank N has a spell,
            // rank N-1 should too.
            for (int r = 1; r < wowee::pipeline::WoweeTalent::kMaxRanks; ++r) {
                if (a.rankSpellIds[r] != 0 && a.rankSpellIds[r - 1] == 0) {
                    warnings.push_back(actx +
                        ": rankSpellIds[" + std::to_string(r) +
                        "] set but rank " + std::to_string(r - 1) +
                        " is empty (gap in rank progression)");
                    break;
                }
            }
            for (uint32_t prev : talentIdsSeen) {
                if (prev == a.talentId) {
                    errors.push_back(actx + ": duplicate talentId");
                    break;
                }
            }
            talentIdsSeen.push_back(a.talentId);
        }
    }
    // Second pass: verify prereqTalentId references resolve
    // and prereqRank fits within the prereq talent's own
    // maxRank.
    for (const auto& t : c.trees) {
        for (const auto& a : t.talents) {
            if (a.prereqTalentId == 0) continue;
            const auto* prereq = c.findTalent(a.prereqTalentId);
            if (!prereq) {
                errors.push_back("talent " + std::to_string(a.talentId) +
                    ": prereqTalentId " +
                    std::to_string(a.prereqTalentId) +
                    " does not exist in this catalog");
                continue;
            }
            if (a.prereqRank == 0 || a.prereqRank > prereq->maxRank) {
                errors.push_back("talent " + std::to_string(a.talentId) +
                    ": prereqRank " + std::to_string(a.prereqRank) +
                    " not in 1.." + std::to_string(prereq->maxRank) +
                    " (prereq talent's maxRank)");
            }
        }
    }
    return cli::reportValidation("wtal", base, jsonOut, errors, warnings,
                                 formatted("%zu trees, %u talents, all IDs unique", c.trees.size(), totalTalents(c)));
}

} // namespace

bool handleTalentsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-talents") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-talents-warrior") == 0 && i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-talents-mage") == 0 && i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtal") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtal") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtal-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtal-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
