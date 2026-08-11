#include "cli_combat_stats_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_combat_stats.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* classIdName(uint8_t c) {
    switch (c) {
        case 1:  return "Warrior";
        case 2:  return "Paladin";
        case 3:  return "Hunter";
        case 4:  return "Rogue";
        case 5:  return "Priest";
        case 7:  return "Shaman";
        case 8:  return "Mage";
        case 9:  return "Warlock";
        case 11: return "Druid";
        default: return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeCombatStats& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcst\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorBaseStats";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcst");
    auto c = wowee::pipeline::WoweeCombatStatsLoader::
        makeWarriorStats(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatStatsLoader>(c, base, "gen-cst-warrior", ".wcst")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageBaseStats";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcst");
    auto c = wowee::pipeline::WoweeCombatStatsLoader::
        makeMageStats(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatStatsLoader>(c, base, "gen-cst-mage", ".wcst")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenStarting(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StartingLevelStats";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcst");
    auto c = wowee::pipeline::WoweeCombatStatsLoader::
        makeStartingLevels(name);
    if (!saveOrError<wowee::pipeline::WoweeCombatStatsLoader>(c, base, "gen-cst-starting", ".wcst")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcst");
    if (!wowee::pipeline::WoweeCombatStatsLoader::exists(base)) {
        std::fprintf(stderr, "WCST not found: %s.wcst\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCombatStatsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcst"] = base + ".wcst";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"statId", e.statId},
                {"classId", e.classId},
                {"className", classIdName(e.classId)},
                {"level", e.level},
                {"baseHealth", e.baseHealth},
                {"baseMana", e.baseMana},
                {"baseStrength", e.baseStrength},
                {"baseAgility", e.baseAgility},
                {"baseStamina", e.baseStamina},
                {"baseIntellect", e.baseIntellect},
                {"baseSpirit", e.baseSpirit},
                {"baseArmor", e.baseArmor},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCST: %s.wcst\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   class    lvl    hp    mana  str agi sta int spi armor\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-8s  %3u  %5u  %5u  %3u %3u %3u %3u %3u  %5u\n",
                    e.statId, classIdName(e.classId),
                    e.level, e.baseHealth, e.baseMana,
                    e.baseStrength, e.baseAgility,
                    e.baseStamina, e.baseIntellect,
                    e.baseSpirit, e.baseArmor);
    }
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcst");
    if (!wowee::pipeline::WoweeCombatStatsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wcst: WCST not found: %s.wcst\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCombatStatsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    using Pair = std::pair<uint8_t, uint8_t>;
    std::set<Pair> classLevelPairs;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (statId=" + std::to_string(e.statId) +
                          " " + classIdName(e.classId) +
                          " L" + std::to_string(e.level) + ")";
        if (e.statId == 0)
            errors.push_back(ctx + ": statId is 0");
        if (e.classId == 0 || e.classId > 11) {
            errors.push_back(ctx + ": classId " +
                std::to_string(e.classId) +
                " out of vanilla range (1..11)");
        }
        if (e.classId == 6 || e.classId == 10) {
            warnings.push_back(ctx + ": classId " +
                std::to_string(e.classId) +
                " is unused in vanilla (DK/Monk gap)");
        }
        if (e.level == 0 || e.level > 60) {
            errors.push_back(ctx + ": level " +
                std::to_string(e.level) +
                " out of vanilla range (1..60)");
        }
        if (e.baseHealth == 0)
            errors.push_back(ctx +
                ": baseHealth is 0 (player would die "
                "instantly)");
        // Warrior(1) and Rogue(4) use Rage/Energy
        // respectively — baseMana > 0 for these
        // classes is wrong.
        if ((e.classId == 1 || e.classId == 4) &&
            e.baseMana > 0) {
            warnings.push_back(ctx +
                ": baseMana=" +
                std::to_string(e.baseMana) +
                " on Warrior/Rogue — these classes use "
                "Rage/Energy, not mana. Likely typo");
        }
        // (classId, level) MUST be unique — runtime
        // dispatch by this pair would tie.
        Pair p{e.classId, e.level};
        if (!classLevelPairs.insert(p).second) {
            errors.push_back(ctx +
                ": duplicate (classId=" +
                std::to_string(e.classId) +
                ", level=" + std::to_string(e.level) +
                ") — runtime stat-lookup tie");
        }
        if (!idsSeen.insert(e.statId).second) {
            errors.push_back(ctx + ": duplicate statId");
        }
    }
    // Monotonicity: per-class, when sorted by level,
    // no stat (HP/mana/Str/Agi/Sta/Int/Spi/armor)
    // should regress (decrease) as level increases.
    // Regression suggests a typo in the data table.
    std::map<uint8_t, std::vector<const wowee::pipeline::
        WoweeCombatStats::Entry*>> byClass;
    for (const auto& e : c.entries) byClass[e.classId].push_back(&e);
    for (auto& [classId, vec] : byClass) {
        std::sort(vec.begin(), vec.end(),
                  [](const wowee::pipeline::WoweeCombatStats::
                      Entry* a,
                      const wowee::pipeline::WoweeCombatStats::
                      Entry* b) {
                      return a->level < b->level;
                  });
        for (size_t k = 1; k < vec.size(); ++k) {
            const auto* prev = vec[k-1];
            const auto* cur = vec[k];
            auto chk = [&](const char* statName,
                            uint64_t prevV, uint64_t curV) {
                if (curV < prevV) {
                    warnings.push_back(
                        std::string("monotonicity: ") +
                        classIdName(classId) +
                        " " + statName +
                        " regresses from " +
                        std::to_string(prevV) +
                        " (L" + std::to_string(prev->level) +
                        ") to " +
                        std::to_string(curV) +
                        " (L" + std::to_string(cur->level) +
                        ") — likely typo");
                }
            };
            chk("baseHealth",   prev->baseHealth,   cur->baseHealth);
            chk("baseMana",     prev->baseMana,     cur->baseMana);
            chk("baseStrength", prev->baseStrength, cur->baseStrength);
            chk("baseAgility",  prev->baseAgility,  cur->baseAgility);
            chk("baseStamina",  prev->baseStamina,  cur->baseStamina);
            chk("baseIntellect",prev->baseIntellect,cur->baseIntellect);
            chk("baseSpirit",   prev->baseSpirit,   cur->baseSpirit);
            chk("baseArmor",    prev->baseArmor,    cur->baseArmor);
        }
    }
    return cli::reportValidation("wcst", base, jsonOut, errors, warnings,
                                 formatted("%zu entries, all statIds + "
                    "(classId,level) unique, classId 1..11, "
                    "level 1..60, no zero baseHealth, no "
                    "Warrior/Rogue mana, all stats "
                    "monotonic over level", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcst");
    if (out.empty()) out = base + ".wcst.json";
    if (!wowee::pipeline::WoweeCombatStatsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wcst-json: WCST not found: %s.wcst\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCombatStatsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCST";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"statId", e.statId},
            {"classId", e.classId},
            {"className", classIdName(e.classId)},
            {"level", e.level},
            {"baseHealth", e.baseHealth},
            {"baseMana", e.baseMana},
            {"baseStrength", e.baseStrength},
            {"baseAgility", e.baseAgility},
            {"baseStamina", e.baseStamina},
            {"baseIntellect", e.baseIntellect},
            {"baseSpirit", e.baseSpirit},
            {"baseArmor", e.baseArmor},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcst-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu entries)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcst");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcst-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcst-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCombatStats c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcst-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeCombatStats::Entry e;
        e.statId = je.value("statId", 0u);
        e.classId = static_cast<uint8_t>(je.value("classId", 0));
        e.level = static_cast<uint8_t>(je.value("level", 0));
        e.baseHealth = je.value("baseHealth", 0u);
        e.baseMana = je.value("baseMana", 0u);
        e.baseStrength = static_cast<uint16_t>(
            je.value("baseStrength", 0));
        e.baseAgility = static_cast<uint16_t>(
            je.value("baseAgility", 0));
        e.baseStamina = static_cast<uint16_t>(
            je.value("baseStamina", 0));
        e.baseIntellect = static_cast<uint16_t>(
            je.value("baseIntellect", 0));
        e.baseSpirit = static_cast<uint16_t>(
            je.value("baseSpirit", 0));
        e.baseArmor = je.value("baseArmor", 0u);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeCombatStatsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcst-json: failed to save %s.wcst\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcst (%zu entries)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleCombatStatsCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-cst-warrior") == 0 &&
        i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cst-mage") == 0 &&
        i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cst-starting") == 0 &&
        i + 1 < argc) {
        outRc = handleGenStarting(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcst") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcst") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcst-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcst-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
