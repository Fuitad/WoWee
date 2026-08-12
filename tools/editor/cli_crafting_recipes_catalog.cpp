#include "cli_crafting_recipes_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_crafting_recipes.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* tradeSkillName(uint16_t s) {
    switch (s) {
        case 164: return "Blacksmithing";
        case 165: return "LeatherWorking";
        case 171: return "Alchemy";
        case 197: return "Tailoring";
        case 202: return "Engineering";
        case 333: return "Enchanting";
        case 185: return "Cooking";
        case 129: return "FirstAid";
        default:  return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeCraftingRecipes& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcra\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  recipes : %zu\n", c.entries.size());
}

int handleGenAlchemy(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AlchemyPotions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcra");
    auto c = wowee::pipeline::WoweeCraftingRecipesLoader::
        makeAlchemyPotions(name);
    if (!saveOrError<wowee::pipeline::WoweeCraftingRecipesLoader>(c, base, "gen-cra-alchemy", ".wcra")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenEngineering(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "EngineeringRecipes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcra");
    auto c = wowee::pipeline::WoweeCraftingRecipesLoader::
        makeEngineering(name);
    if (!saveOrError<wowee::pipeline::WoweeCraftingRecipesLoader>(c, base, "gen-cra-engineering", ".wcra")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBlacksmithing(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BlacksmithingRecipes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcra");
    auto c = wowee::pipeline::WoweeCraftingRecipesLoader::
        makeBlacksmithing(name);
    if (!saveOrError<wowee::pipeline::WoweeCraftingRecipesLoader>(c, base, "gen-cra-blacksmithing", ".wcra")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcra");
    if (!wowee::pipeline::WoweeCraftingRecipesLoader::exists(base)) {
        std::fprintf(stderr, "WCRA not found: %s.wcra\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCraftingRecipesLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcra"] = base + ".wcra";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json reagents = nlohmann::json::array();
            for (const auto& r : e.reagents) {
                reagents.push_back({
                    {"itemId", r.itemId},
                    {"count", r.count},
                });
            }
            arr.push_back({
                {"recipeId", e.recipeId},
                {"spellId", e.spellId},
                {"name", e.name},
                {"tradeSkillId", e.tradeSkillId},
                {"tradeSkillName",
                    tradeSkillName(e.tradeSkillId)},
                {"requiredSkillLevel", e.requiredSkillLevel},
                {"producedItemId", e.producedItemId},
                {"producedCount", e.producedCount},
                {"categoryId", e.categoryId},
                {"learnedFromItemId", e.learnedFromItemId},
                {"reagents", reagents},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCRA: %s.wcra\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  recipes : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    spell  trade-skill      req  produced  count  reag  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %7u  %-15s  %3u  %8u  %5u  %4zu  %s\n",
                    e.recipeId, e.spellId,
                    tradeSkillName(e.tradeSkillId),
                    e.requiredSkillLevel,
                    e.producedItemId, e.producedCount,
                    e.reagents.size(), e.name.c_str());
    }
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcra");
    if (!wowee::pipeline::WoweeCraftingRecipesLoader::exists(base)) {
        return reportMissing("validate-wcra", "WCRA", base, ".wcra");
    }
    auto c = wowee::pipeline::WoweeCraftingRecipesLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<uint32_t> spellIdsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.recipeId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.recipeId == 0)
            errors.push_back(ctx + ": recipeId is 0");
        if (e.spellId == 0)
            errors.push_back(ctx +
                ": spellId is 0 (no cast spell)");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.tradeSkillId == 0) {
            errors.push_back(ctx +
                ": tradeSkillId is 0 (recipe must "
                "belong to a trade skill)");
        }
        if (e.producedItemId == 0)
            errors.push_back(ctx +
                ": producedItemId is 0 (recipe produces "
                "nothing)");
        if (e.producedCount == 0) {
            errors.push_back(ctx +
                ": producedCount is 0 (recipe yields 0 "
                "items)");
        }
        // Vanilla skill cap is 300. Skill > 300 is
        // valid for TBC (375) and WotLK (450) - only
        // warn if absurdly above all expansions.
        if (e.requiredSkillLevel > 450) {
            warnings.push_back(ctx +
                ": requiredSkillLevel=" +
                std::to_string(e.requiredSkillLevel) +
                " exceeds WotLK cap of 450 (likely typo)");
        }
        // Empty reagent list: technically valid (some
        // alchemy transmutes have only catalysts) but
        // unusual. Warn.
        if (e.reagents.empty()) {
            warnings.push_back(ctx +
                ": no reagents - recipe is "
                "free-to-craft (unusual; verify "
                "intentional)");
        }
        // Per-reagent checks: zero itemId or zero
        // count is a typo.
        std::set<uint32_t> reagentItems;
        for (size_t r = 0; r < e.reagents.size(); ++r) {
            const auto& reagent = e.reagents[r];
            if (reagent.itemId == 0) {
                errors.push_back(ctx +
                    ": reagent[" + std::to_string(r) +
                    "].itemId is 0");
            }
            if (reagent.count == 0) {
                errors.push_back(ctx +
                    ": reagent[" + std::to_string(r) +
                    "].count is 0");
            }
            // Same itemId listed twice in the reagent
            // array - should be merged into one
            // reagent with summed count.
            if (reagent.itemId != 0 &&
                !reagentItems.insert(reagent.itemId).second) {
                warnings.push_back(ctx +
                    ": reagent itemId " +
                    std::to_string(reagent.itemId) +
                    " appears twice - should be merged "
                    "into single entry with summed count");
            }
        }
        // Self-reagent: a recipe consuming its own
        // produced item is a perpetual-motion bug.
        for (const auto& reagent : e.reagents) {
            if (reagent.itemId == e.producedItemId &&
                reagent.itemId != 0) {
                errors.push_back(ctx +
                    ": reagent itemId equals "
                    "producedItemId=" +
                    std::to_string(e.producedItemId) +
                    " - recipe consumes what it makes "
                    "(perpetual-motion bug)");
            }
        }
        // Duplicate spellId - recipe-cast handler
        // would resolve ambiguously.
        if (e.spellId != 0 &&
            !spellIdsSeen.insert(e.spellId).second) {
            errors.push_back(ctx +
                ": duplicate spellId " +
                std::to_string(e.spellId) +
                " - two recipes would respond to the "
                "same cast");
        }
        if (!idsSeen.insert(e.recipeId).second) {
            errors.push_back(ctx + ": duplicate recipeId");
        }
    }
    return cli::reportValidation("wcra", base, jsonOut, errors, warnings,
                                 formatted("%zu recipes, all recipeIds + "
                    "spellIds unique, non-zero spellId/"
                    "tradeSkillId/producedItemId/"
                    "producedCount, no zero-item/zero-count "
                    "reagents, no duplicate reagent itemIds "
                    "in same recipe, no self-reagent", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcra");
    if (out.empty()) out = base + ".wcra.json";
    if (!wowee::pipeline::WoweeCraftingRecipesLoader::exists(base)) {
        return reportMissing("export-wcra-json", "WCRA", base, ".wcra");
    }
    auto c = wowee::pipeline::WoweeCraftingRecipesLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCRA";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json reagents = nlohmann::json::array();
        for (const auto& r : e.reagents) {
            reagents.push_back({
                {"itemId", r.itemId},
                {"count", r.count},
            });
        }
        arr.push_back({
            {"recipeId", e.recipeId},
            {"spellId", e.spellId},
            {"name", e.name},
            {"tradeSkillId", e.tradeSkillId},
            {"tradeSkillName",
                tradeSkillName(e.tradeSkillId)},
            {"requiredSkillLevel", e.requiredSkillLevel},
            {"producedItemId", e.producedItemId},
            {"producedCount", e.producedCount},
            {"categoryId", e.categoryId},
            {"learnedFromItemId", e.learnedFromItemId},
            {"reagents", reagents},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcra-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu recipes)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcra");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcra-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcra-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCraftingRecipes c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcra-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeCraftingRecipes::Entry e;
        e.recipeId = je.value("recipeId", 0u);
        e.spellId = je.value("spellId", 0u);
        e.name = je.value("name", std::string{});
        e.tradeSkillId = static_cast<uint16_t>(
            je.value("tradeSkillId", 0));
        e.requiredSkillLevel = static_cast<uint16_t>(
            je.value("requiredSkillLevel", 0));
        e.producedItemId = je.value("producedItemId", 0u);
        e.producedCount = static_cast<uint16_t>(
            je.value("producedCount", 1));
        e.categoryId = static_cast<uint16_t>(
            je.value("categoryId", 0));
        e.learnedFromItemId = je.value("learnedFromItemId", 0u);
        if (je.contains("reagents") &&
            je["reagents"].is_array()) {
            for (const auto& r : je["reagents"]) {
                wowee::pipeline::WoweeCraftingRecipes::
                    Reagent reagent;
                reagent.itemId = r.value("itemId", 0u);
                reagent.count = r.value("count", 0u);
                e.reagents.push_back(reagent);
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeCraftingRecipesLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcra-json: failed to save %s.wcra\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcra (%zu recipes)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleCraftingRecipesCatalog(int& i, int argc, char** argv,
                                    int& outRc) {
    if (std::strcmp(argv[i], "--gen-cra-alchemy") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAlchemy(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cra-engineering") == 0 &&
        i + 1 < argc) {
        outRc = handleGenEngineering(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cra-blacksmithing") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBlacksmithing(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcra") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcra") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcra-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcra-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
