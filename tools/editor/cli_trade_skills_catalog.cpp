#include "cli_trade_skills_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_trade_skills.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeTradeSkill& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtsk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  recipes : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterRecipes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtsk");
    auto c = wowee::pipeline::WoweeTradeSkillLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeTradeSkillLoader>(c, base, "gen-tsk", ".wtsk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBlacksmithing(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BlacksmithingRecipes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtsk");
    auto c = wowee::pipeline::WoweeTradeSkillLoader::makeBlacksmithing(name);
    if (!saveOrError<wowee::pipeline::WoweeTradeSkillLoader>(c, base, "gen-tsk-blacksmithing", ".wtsk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAlchemy(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AlchemyRecipes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtsk");
    auto c = wowee::pipeline::WoweeTradeSkillLoader::makeAlchemy(name);
    if (!saveOrError<wowee::pipeline::WoweeTradeSkillLoader>(c, base, "gen-tsk-alchemy", ".wtsk")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendEntryJson(nlohmann::json& arr,
                     const wowee::pipeline::WoweeTradeSkill::Entry& e) {
    nlohmann::json reagents = nlohmann::json::array();
    for (size_t k = 0;
         k < wowee::pipeline::WoweeTradeSkill::kMaxReagents; ++k) {
        if (e.reagentItemId[k] == 0 && e.reagentCount[k] == 0) continue;
        reagents.push_back({
            {"itemId", e.reagentItemId[k]},
            {"count", e.reagentCount[k]},
        });
    }
    arr.push_back({
        {"recipeId", e.recipeId},
        {"name", e.name},
        {"description", e.description},
        {"iconPath", e.iconPath},
        {"profession", e.profession},
        {"professionName", wowee::pipeline::WoweeTradeSkill::professionName(e.profession)},
        {"skillId", e.skillId},
        {"orangeRank", e.orangeRank},
        {"yellowRank", e.yellowRank},
        {"greenRank", e.greenRank},
        {"grayRank", e.grayRank},
        {"craftSpellId", e.craftSpellId},
        {"producedItemId", e.producedItemId},
        {"producedMinCount", e.producedMinCount},
        {"producedMaxCount", e.producedMaxCount},
        {"toolItemId", e.toolItemId},
        {"reagents", reagents},
    });
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtsk");
    if (!wowee::pipeline::WoweeTradeSkillLoader::exists(base)) {
        std::fprintf(stderr, "WTSK not found: %s.wtsk\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTradeSkillLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtsk"] = base + ".wtsk";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) appendEntryJson(arr, e);
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTSK: %s.wtsk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  recipes : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    profession      ranks(O/Y/G/Gr)         spell    item    qty   tool  rgts  name\n");
    for (const auto& e : c.entries) {
        size_t reagentCount = 0;
        for (size_t k = 0;
             k < wowee::pipeline::WoweeTradeSkill::kMaxReagents; ++k) {
            if (e.reagentItemId[k] != 0 || e.reagentCount[k] != 0)
                ++reagentCount;
        }
        std::printf("  %4u   %-13s  %3u/%3u/%3u/%3u    %5u   %5u   %u-%u   %5u  %4zu  %s\n",
                    e.recipeId,
                    wowee::pipeline::WoweeTradeSkill::professionName(e.profession),
                    e.orangeRank, e.yellowRank, e.greenRank, e.grayRank,
                    e.craftSpellId, e.producedItemId,
                    e.producedMinCount, e.producedMaxCount,
                    e.toolItemId, reagentCount, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each recipe emits all 14 scalar fields
    // plus a dual int + name form for profession, plus a
    // nested reagents[] array (only non-empty slots are
    // emitted to keep hand-edits compact).
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wtsk");
    if (outPath.empty()) outPath = base + ".wtsk.json";
    if (!wowee::pipeline::WoweeTradeSkillLoader::exists(base)) {
        return reportMissing("export-wtsk-json", "WTSK", base, ".wtsk");
    }
    auto c = wowee::pipeline::WoweeTradeSkillLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) appendEntryJson(arr, e);
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wtsk-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wtsk\n", base.c_str());
    std::printf("  recipes : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wtsk");
    outBase = cli::withoutExt(outBase, ".wtsk");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wtsk-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wtsk-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto profFromName = [](const std::string& s) -> uint8_t {
        if (s == "blacksmithing")  return wowee::pipeline::WoweeTradeSkill::Blacksmithing;
        if (s == "tailoring")      return wowee::pipeline::WoweeTradeSkill::Tailoring;
        if (s == "engineering")    return wowee::pipeline::WoweeTradeSkill::Engineering;
        if (s == "alchemy")        return wowee::pipeline::WoweeTradeSkill::Alchemy;
        if (s == "enchanting")     return wowee::pipeline::WoweeTradeSkill::Enchanting;
        if (s == "leatherworking") return wowee::pipeline::WoweeTradeSkill::Leatherworking;
        if (s == "jewelcrafting")  return wowee::pipeline::WoweeTradeSkill::Jewelcrafting;
        if (s == "inscription")    return wowee::pipeline::WoweeTradeSkill::Inscription;
        if (s == "mining")         return wowee::pipeline::WoweeTradeSkill::Mining;
        if (s == "skinning")       return wowee::pipeline::WoweeTradeSkill::Skinning;
        if (s == "herbalism")      return wowee::pipeline::WoweeTradeSkill::Herbalism;
        if (s == "cooking")        return wowee::pipeline::WoweeTradeSkill::Cooking;
        if (s == "first-aid")      return wowee::pipeline::WoweeTradeSkill::FirstAid;
        if (s == "fishing")        return wowee::pipeline::WoweeTradeSkill::Fishing;
        return wowee::pipeline::WoweeTradeSkill::Blacksmithing;
    };
    wowee::pipeline::WoweeTradeSkill c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeTradeSkill::Entry e;
            e.recipeId = je.value("recipeId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("profession") &&
                je["profession"].is_number_integer()) {
                e.profession = static_cast<uint8_t>(
                    je["profession"].get<int>());
            } else if (je.contains("professionName") &&
                       je["professionName"].is_string()) {
                e.profession = profFromName(
                    je["professionName"].get<std::string>());
            }
            e.skillId = je.value("skillId", 0u);
            e.orangeRank = static_cast<uint16_t>(je.value("orangeRank", 1));
            e.yellowRank = static_cast<uint16_t>(je.value("yellowRank", 25));
            e.greenRank  = static_cast<uint16_t>(je.value("greenRank",  50));
            e.grayRank   = static_cast<uint16_t>(je.value("grayRank",   75));
            e.craftSpellId = je.value("craftSpellId", 0u);
            e.producedItemId = je.value("producedItemId", 0u);
            e.producedMinCount = static_cast<uint8_t>(
                je.value("producedMinCount", 1));
            e.producedMaxCount = static_cast<uint8_t>(
                je.value("producedMaxCount", 1));
            e.toolItemId = je.value("toolItemId", 0u);
            // Reset to all-zero before parsing reagents — the
            // exporter only emits non-empty slots, so a
            // reagents[] of size 2 should leave slots 2 and
            // 3 clean.
            for (size_t k = 0;
                 k < wowee::pipeline::WoweeTradeSkill::kMaxReagents; ++k) {
                e.reagentItemId[k] = 0;
                e.reagentCount[k] = 0;
            }
            if (je.contains("reagents") && je["reagents"].is_array()) {
                size_t slot = 0;
                for (const auto& jr : je["reagents"]) {
                    if (slot >= wowee::pipeline::WoweeTradeSkill::kMaxReagents)
                        break;
                    e.reagentItemId[slot] = jr.value("itemId", 0u);
                    e.reagentCount[slot] = static_cast<uint8_t>(
                        jr.value("count", 0));
                    ++slot;
                }
            }
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeTradeSkillLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtsk-json: failed to save %s.wtsk\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtsk\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  recipes : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtsk");
    if (!wowee::pipeline::WoweeTradeSkillLoader::exists(base)) {
        return reportMissing("validate-wtsk", "WTSK", base, ".wtsk");
    }
    auto c = wowee::pipeline::WoweeTradeSkillLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.recipeId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.recipeId == 0)
            errors.push_back(ctx + ": recipeId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.profession > wowee::pipeline::WoweeTradeSkill::Fishing) {
            errors.push_back(ctx + ": profession " +
                std::to_string(e.profession) + " not in 0..13");
        }
        if (e.craftSpellId == 0)
            errors.push_back(ctx +
                ": craftSpellId is 0 (recipe has no craft action)");
        if (e.producedItemId == 0)
            errors.push_back(ctx +
                ": producedItemId is 0 (recipe produces nothing)");
        if (e.producedMinCount == 0 || e.producedMaxCount == 0) {
            errors.push_back(ctx +
                ": producedMin/MaxCount must be >= 1");
        }
        if (e.producedMinCount > e.producedMaxCount) {
            errors.push_back(ctx + ": producedMinCount " +
                std::to_string(e.producedMinCount) +
                " > producedMaxCount " +
                std::to_string(e.producedMaxCount));
        }
        // Skill-up bracket thresholds must be monotonic:
        // orange < yellow < green < gray.
        if (!(e.orangeRank <= e.yellowRank &&
              e.yellowRank <= e.greenRank &&
              e.greenRank  <= e.grayRank)) {
            errors.push_back(ctx +
                ": skill brackets non-monotonic (require "
                "orange <= yellow <= green <= gray, got " +
                std::to_string(e.orangeRank) + "/" +
                std::to_string(e.yellowRank) + "/" +
                std::to_string(e.greenRank)  + "/" +
                std::to_string(e.grayRank)   + ")");
        }
        if (e.skillId == 0)
            warnings.push_back(ctx +
                ": skillId=0 (recipe not bound to a WSKL skill line)");
        // A recipe with zero reagents and no tool is suspicious
        // — most crafts need at least one of the two.
        bool anyReagent = false;
        for (size_t r = 0;
             r < wowee::pipeline::WoweeTradeSkill::kMaxReagents; ++r) {
            if (e.reagentItemId[r] != 0 && e.reagentCount[r] > 0) {
                anyReagent = true; break;
            }
            if (e.reagentItemId[r] != 0 && e.reagentCount[r] == 0) {
                errors.push_back(ctx + ": reagent slot " +
                    std::to_string(r) + " has itemId=" +
                    std::to_string(e.reagentItemId[r]) +
                    " but count=0 (set count or clear itemId)");
            }
        }
        if (!anyReagent && e.toolItemId == 0) {
            warnings.push_back(ctx +
                ": no reagents and no tool — recipe is free");
        }
        if (!idsSeen.add(e.recipeId)) errors.push_back(ctx + ": duplicate recipeId");
    }
    return cli::reportValidation("wtsk", base, jsonOut, errors, warnings,
                                 formatted("%zu recipes, all recipeIds unique, all skill brackets monotonic", c.entries.size()));
}

} // namespace

bool handleTradeSkillsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-tsk") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tsk-blacksmithing") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBlacksmithing(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tsk-alchemy") == 0 && i + 1 < argc) {
        outRc = handleGenAlchemy(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtsk") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtsk") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtsk-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtsk-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
