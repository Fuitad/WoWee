#include "cli_lfg_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_lfg.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeLFGDungeon& c,
                     const std::string& base) {
    std::printf("Wrote %s.wlfg\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  dungeons : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterLFG";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlfg");
    auto c = wowee::pipeline::WoweeLFGDungeonLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeLFGDungeonLoader>(c, base, "gen-lfg", ".wlfg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHeroic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HeroicLFG";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlfg");
    auto c = wowee::pipeline::WoweeLFGDungeonLoader::makeHeroic(name);
    if (!saveOrError<wowee::pipeline::WoweeLFGDungeonLoader>(c, base, "gen-lfg-heroic", ".wlfg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidLFG";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlfg");
    auto c = wowee::pipeline::WoweeLFGDungeonLoader::makeRaid(name);
    if (!saveOrError<wowee::pipeline::WoweeLFGDungeonLoader>(c, base, "gen-lfg-raid", ".wlfg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlfg");
    if (!wowee::pipeline::WoweeLFGDungeonLoader::exists(base)) {
        return reportMissing("WLFG", base, ".wlfg");
    }
    auto c = wowee::pipeline::WoweeLFGDungeonLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wlfg"] = base + ".wlfg";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"dungeonId", e.dungeonId},
                {"name", e.name},
                {"description", e.description},
                {"mapId", e.mapId},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"recommendedLevel", e.recommendedLevel},
                {"minGearLevel", e.minGearLevel},
                {"difficulty", e.difficulty},
                {"difficultyName", wowee::pipeline::WoweeLFGDungeon::difficultyName(e.difficulty)},
                {"groupSize", e.groupSize},
                {"requiredRolesMask", e.requiredRolesMask},
                {"expansionRequired", e.expansionRequired},
                {"expansionRequiredName", wowee::pipeline::WoweeLFGDungeon::expansionRequiredName(e.expansionRequired)},
                {"queueRewardItemId", e.queueRewardItemId},
                {"queueRewardEmblemCount", e.queueRewardEmblemCount},
                {"firstClearAchievement", e.firstClearAchievement},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLFG: %s.wlfg\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  dungeons : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    map    levels   ilvl  diff       group  roles  exp     emblem  ach     name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %4u   %3u-%3u  %3u  %-9s   %3u    0x%02x  %-7s  %3u    %5u   %s\n",
                    e.dungeonId, e.mapId,
                    e.minLevel, e.maxLevel, e.minGearLevel,
                    wowee::pipeline::WoweeLFGDungeon::difficultyName(e.difficulty),
                    e.groupSize, e.requiredRolesMask,
                    wowee::pipeline::WoweeLFGDungeon::expansionRequiredName(e.expansionRequired),
                    e.queueRewardEmblemCount,
                    e.firstClearAchievement,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each dungeon emits all 13 scalar fields
    // plus dual int + name forms for difficulty (4 values)
    // and expansionRequired (4 values) so hand-edits can
    // use either representation.
    return cli::exportCatalogJson<wowee::pipeline::WoweeLFGDungeonLoader>(
        i, argc, argv, "wlfg", "WLFG", "dungeons ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"dungeonId", e.dungeonId},
                {"name", e.name},
                {"description", e.description},
                {"mapId", e.mapId},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"recommendedLevel", e.recommendedLevel},
                {"minGearLevel", e.minGearLevel},
                {"difficulty", e.difficulty},
                {"difficultyName", wowee::pipeline::WoweeLFGDungeon::difficultyName(e.difficulty)},
                {"groupSize", e.groupSize},
                {"requiredRolesMask", e.requiredRolesMask},
                {"expansionRequired", e.expansionRequired},
                {"expansionRequiredName", wowee::pipeline::WoweeLFGDungeon::expansionRequiredName(e.expansionRequired)},
                {"queueRewardItemId", e.queueRewardItemId},
                {"queueRewardEmblemCount", e.queueRewardEmblemCount},
                {"firstClearAchievement", e.firstClearAchievement},
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
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wlfg");
    outBase = cli::withoutExt(outBase, ".wlfg");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wlfg-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wlfg-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto difficultyFromName = [](const std::string& s) -> uint8_t {
        if (s == "normal")   return wowee::pipeline::WoweeLFGDungeon::Normal;
        if (s == "heroic")   return wowee::pipeline::WoweeLFGDungeon::Heroic;
        if (s == "mythic")   return wowee::pipeline::WoweeLFGDungeon::Mythic;
        if (s == "hardmode") return wowee::pipeline::WoweeLFGDungeon::Hardmode;
        return wowee::pipeline::WoweeLFGDungeon::Normal;
    };
    auto expansionFromName = [](const std::string& s) -> uint8_t {
        if (s == "classic") return wowee::pipeline::WoweeLFGDungeon::Classic;
        if (s == "tbc")     return wowee::pipeline::WoweeLFGDungeon::TBC;
        if (s == "wotlk")   return wowee::pipeline::WoweeLFGDungeon::WotLK;
        if (s == "turtle")  return wowee::pipeline::WoweeLFGDungeon::TurtleWoW;
        return wowee::pipeline::WoweeLFGDungeon::Classic;
    };
    wowee::pipeline::WoweeLFGDungeon c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeLFGDungeon::Entry e;
            e.dungeonId = je.value("dungeonId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.mapId = je.value("mapId", 0u);
            e.minLevel = static_cast<uint16_t>(je.value("minLevel", 1));
            e.maxLevel = static_cast<uint16_t>(je.value("maxLevel", 80));
            e.recommendedLevel = static_cast<uint16_t>(
                je.value("recommendedLevel", 0));
            e.minGearLevel = static_cast<uint16_t>(
                je.value("minGearLevel", 0));
            if (je.contains("difficulty") &&
                je["difficulty"].is_number_integer()) {
                e.difficulty = static_cast<uint8_t>(
                    je["difficulty"].get<int>());
            } else if (je.contains("difficultyName") &&
                       je["difficultyName"].is_string()) {
                e.difficulty = difficultyFromName(
                    je["difficultyName"].get<std::string>());
            }
            // groupSize defaults to 5 (dungeon) if omitted.
            e.groupSize = static_cast<uint8_t>(
                je.value("groupSize", 5));
            // requiredRolesMask defaults to kRoleAll so queue
            // forms balanced groups by default.
            e.requiredRolesMask = static_cast<uint8_t>(
                je.value("requiredRolesMask",
                    wowee::pipeline::WoweeLFGDungeon::kRoleAll));
            if (je.contains("expansionRequired") &&
                je["expansionRequired"].is_number_integer()) {
                e.expansionRequired = static_cast<uint8_t>(
                    je["expansionRequired"].get<int>());
            } else if (je.contains("expansionRequiredName") &&
                       je["expansionRequiredName"].is_string()) {
                e.expansionRequired = expansionFromName(
                    je["expansionRequiredName"].get<std::string>());
            }
            e.queueRewardItemId = je.value("queueRewardItemId", 0u);
            e.queueRewardEmblemCount = static_cast<uint16_t>(
                je.value("queueRewardEmblemCount", 0));
            e.firstClearAchievement =
                je.value("firstClearAchievement", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeLFGDungeonLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wlfg-json: failed to save %s.wlfg\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wlfg\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  dungeons : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeLFGDungeonLoader>(
        i, argc, argv, "wlfg", "WLFG",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.dungeonId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.dungeonId == 0)
                errors.push_back(ctx + ": dungeonId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.mapId == 0)
                errors.push_back(ctx +
                    ": mapId is 0 (dungeon has no instance map)");
            if (e.difficulty > wowee::pipeline::WoweeLFGDungeon::Hardmode) {
                errors.push_back(ctx + ": difficulty " +
                    std::to_string(e.difficulty) + " not in 0..3");
            }
            if (e.expansionRequired > wowee::pipeline::WoweeLFGDungeon::TurtleWoW) {
                errors.push_back(ctx + ": expansionRequired " +
                    std::to_string(e.expansionRequired) + " not in 0..3");
            }
            if (e.minLevel > e.maxLevel) {
                errors.push_back(ctx + ": minLevel " +
                    std::to_string(e.minLevel) + " > maxLevel " +
                    std::to_string(e.maxLevel));
            }
            if (e.recommendedLevel != 0 &&
                (e.recommendedLevel < e.minLevel ||
                 e.recommendedLevel > e.maxLevel)) {
                warnings.push_back(ctx + ": recommendedLevel " +
                    std::to_string(e.recommendedLevel) +
                    " outside [" + std::to_string(e.minLevel) +
                    ", " + std::to_string(e.maxLevel) + "]");
            }
            // Common group sizes: 5 (dungeon), 10/25 (raid),
            // 40 (vanilla raid). Other values are unusual but
            // not technically wrong.
            if (e.groupSize != 5 && e.groupSize != 10 &&
                e.groupSize != 25 && e.groupSize != 40) {
                warnings.push_back(ctx + ": groupSize " +
                    std::to_string(e.groupSize) +
                    " is unusual (5 / 10 / 25 / 40 are canonical)");
            }
            if (e.requiredRolesMask == 0) {
                errors.push_back(ctx +
                    ": requiredRolesMask=0 (no role requirement — "
                    "queue won't form a balanced group)");
            }
            if (!idsSeen.add(e.dungeonId)) errors.push_back(ctx + ": duplicate dungeonId");
        }
            return formatted("%zu dungeons, all dungeonIds unique, all level ranges valid", c.entries.size());
        });
}

} // namespace

bool handleLFGCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-lfg") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lfg-heroic") == 0 && i + 1 < argc) {
        outRc = handleGenHeroic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lfg-raid") == 0 && i + 1 < argc) {
        outRc = handleGenRaid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wlfg") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wlfg") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wlfg-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wlfg-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
