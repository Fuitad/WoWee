#include "cli_runes_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_runes.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeRuneCost& c,
                     const std::string& base) {
    std::printf("Wrote %s.wrun\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  costs   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterRuneCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wrun");
    auto c = wowee::pipeline::WoweeRuneCostLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeRuneCostLoader>(c, base, "gen-rune", ".wrun")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBlood(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BloodTreeRuneCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wrun");
    auto c = wowee::pipeline::WoweeRuneCostLoader::makeBlood(name);
    if (!saveOrError<wowee::pipeline::WoweeRuneCostLoader>(c, base, "gen-rune-blood", ".wrun")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFrost(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FrostTreeRuneCosts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wrun");
    auto c = wowee::pipeline::WoweeRuneCostLoader::makeFrost(name);
    if (!saveOrError<wowee::pipeline::WoweeRuneCostLoader>(c, base, "gen-rune-frost", ".wrun")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wrun");
    if (!wowee::pipeline::WoweeRuneCostLoader::exists(base)) {
        return reportMissing("WRUN", base, ".wrun");
    }
    auto c = wowee::pipeline::WoweeRuneCostLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wrun"] = base + ".wrun";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"runeCostId", e.runeCostId},
                {"spellId", e.spellId},
                {"name", e.name},
                {"description", e.description},
                {"bloodCost", e.bloodCost},
                {"frostCost", e.frostCost},
                {"unholyCost", e.unholyCost},
                {"anyDeathConvertCost", e.anyDeathConvertCost},
                {"runicPowerCost", e.runicPowerCost},
                {"spellTreeBranch", e.spellTreeBranch},
                {"spellTreeBranchName", wowee::pipeline::WoweeRuneCost::spellTreeBranchName(e.spellTreeBranch)},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WRUN: %s.wrun\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  costs   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    spell    B  F  U  Any   RP    branch    name\n");
    for (const auto& e : c.entries) {
        const char* rpKind = e.runicPowerCost == 0
            ? "  "
            : (e.runicPowerCost > 0 ? "→" : "←");
        std::printf("  %4u    %5u   %u  %u  %u   %u   %s%4d  %-8s  %s\n",
                    e.runeCostId, e.spellId,
                    e.bloodCost, e.frostCost, e.unholyCost,
                    e.anyDeathConvertCost,
                    rpKind, e.runicPowerCost,
                    wowee::pipeline::WoweeRuneCost::spellTreeBranchName(e.spellTreeBranch),
                    e.name.c_str());
    }
    std::printf("  legend: B/F/U/Any = blood/frost/unholy/death-convertible costs; "
                "RP arrow shows generator (←) vs spender (→)\n");
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each rune cost emits all 9 scalar fields
    // plus a dual int + name form for spellTreeBranch so
    // hand-edits can use either representation.
    return cli::exportCatalogJson<wowee::pipeline::WoweeRuneCostLoader>(
        i, argc, argv, "wrun", "WRUN", "costs  ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"runeCostId", e.runeCostId},
                {"spellId", e.spellId},
                {"name", e.name},
                {"description", e.description},
                {"bloodCost", e.bloodCost},
                {"frostCost", e.frostCost},
                {"unholyCost", e.unholyCost},
                {"anyDeathConvertCost", e.anyDeathConvertCost},
                {"runicPowerCost", e.runicPowerCost},
                {"spellTreeBranch", e.spellTreeBranch},
                {"spellTreeBranchName", wowee::pipeline::WoweeRuneCost::spellTreeBranchName(e.spellTreeBranch)},
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
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wrun");
    outBase = cli::withoutExt(outBase, ".wrun");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wrun-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wrun-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto branchFromName = [](const std::string& s) -> uint8_t {
        if (s == "blood")   return wowee::pipeline::WoweeRuneCost::BloodTree;
        if (s == "frost")   return wowee::pipeline::WoweeRuneCost::FrostTree;
        if (s == "unholy")  return wowee::pipeline::WoweeRuneCost::UnholyTree;
        if (s == "generic") return wowee::pipeline::WoweeRuneCost::Generic;
        return wowee::pipeline::WoweeRuneCost::Generic;
    };
    wowee::pipeline::WoweeRuneCost c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeRuneCost::Entry e;
            e.runeCostId = je.value("runeCostId", 0u);
            e.spellId = je.value("spellId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.bloodCost = static_cast<uint8_t>(je.value("bloodCost", 0));
            e.frostCost = static_cast<uint8_t>(je.value("frostCost", 0));
            e.unholyCost = static_cast<uint8_t>(je.value("unholyCost", 0));
            e.anyDeathConvertCost = static_cast<uint8_t>(
                je.value("anyDeathConvertCost", 0));
            e.runicPowerCost = static_cast<int16_t>(
                je.value("runicPowerCost", 0));
            if (je.contains("spellTreeBranch") &&
                je["spellTreeBranch"].is_number_integer()) {
                e.spellTreeBranch = static_cast<uint8_t>(
                    je["spellTreeBranch"].get<int>());
            } else if (je.contains("spellTreeBranchName") &&
                       je["spellTreeBranchName"].is_string()) {
                e.spellTreeBranch = branchFromName(
                    je["spellTreeBranchName"].get<std::string>());
            }
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeRuneCostLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wrun-json: failed to save %s.wrun\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wrun\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  costs  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeRuneCostLoader>(
        i, argc, argv, "wrun", "WRUN",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.runeCostId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.runeCostId == 0)
                errors.push_back(ctx + ": runeCostId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.spellId == 0)
                errors.push_back(ctx +
                    ": spellId is 0 (rune cost not bound to a WSPL spell)");
            if (e.spellTreeBranch > wowee::pipeline::WoweeRuneCost::Generic) {
                errors.push_back(ctx + ": spellTreeBranch " +
                    std::to_string(e.spellTreeBranch) + " not in 0..3");
            }
            // A DK has 6 runes total (2 of each kind) — a single
            // ability cost shouldn't exceed 2 of any one type
            // because the system can't satisfy it.
            if (e.bloodCost > 2)
                errors.push_back(ctx + ": bloodCost " +
                    std::to_string(e.bloodCost) + " exceeds 2 (DK only "
                    "has 2 blood runes)");
            if (e.frostCost > 2)
                errors.push_back(ctx + ": frostCost " +
                    std::to_string(e.frostCost) + " exceeds 2");
            if (e.unholyCost > 2)
                errors.push_back(ctx + ": unholyCost " +
                    std::to_string(e.unholyCost) + " exceeds 2");
            // A spell with no rune cost AND no runic-power cost
            // is weird — every DK ability either consumes
            // resources, generates them, or applies a stance.
            // We don't have stance info here, so warn.
            bool noResourceCost = e.bloodCost == 0 && e.frostCost == 0 &&
                                  e.unholyCost == 0 &&
                                  e.anyDeathConvertCost == 0 &&
                                  e.runicPowerCost == 0;
            if (noResourceCost) {
                warnings.push_back(ctx +
                    ": no rune or runic-power cost — verify this is "
                    "intentional (passive / stance / forms only)");
            }
            // RP cost > 100 isn't possible — max RP cap is 100.
            if (e.runicPowerCost > 100) {
                errors.push_back(ctx +
                    ": runicPowerCost " +
                    std::to_string(e.runicPowerCost) +
                    " exceeds 100 (DK runic power max)");
            }
            if (e.runicPowerCost < -25) {
                warnings.push_back(ctx +
                    ": runicPowerCost " +
                    std::to_string(e.runicPowerCost) +
                    " generates more than 25 RP per cast — unusual");
            }
            if (!idsSeen.add(e.runeCostId)) errors.push_back(ctx + ": duplicate runeCostId");
        }
            return formatted("%zu rune costs, all runeCostIds unique, all costs within DK budget", c.entries.size());
        });
}

} // namespace

bool handleRunesCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-rune") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-rune-blood") == 0 && i + 1 < argc) {
        outRc = handleGenBlood(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-rune-frost") == 0 && i + 1 < argc) {
        outRc = handleGenFrost(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wrun") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wrun") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wrun-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wrun-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
