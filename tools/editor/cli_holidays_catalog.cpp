#include "cli_holidays_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_holidays.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeHoliday& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeHolidayLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.whol\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeHoliday& c,
                     const std::string& base) {
    std::printf("Wrote %s.whol\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  holidays : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterHolidays";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".whol");
    auto c = wowee::pipeline::WoweeHolidayLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-holidays")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWeekly(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeeklyHolidays";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".whol");
    auto c = wowee::pipeline::WoweeHolidayLoader::makeWeekly(name);
    if (!saveOrError(c, base, "gen-holidays-weekly")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenSpecial(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SpecialHolidays";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".whol");
    auto c = wowee::pipeline::WoweeHolidayLoader::makeSpecial(name);
    if (!saveOrError(c, base, "gen-holidays-special")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".whol");
    if (!wowee::pipeline::WoweeHolidayLoader::exists(base)) {
        std::fprintf(stderr, "WHOL not found: %s.whol\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeHolidayLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["whol"] = base + ".whol";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"holidayId", e.holidayId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"holidayKind", e.holidayKind},
                {"holidayKindName", wowee::pipeline::WoweeHoliday::holidayKindName(e.holidayKind)},
                {"recurrence", e.recurrence},
                {"recurrenceName", wowee::pipeline::WoweeHoliday::recurrenceName(e.recurrence)},
                {"startMonth", e.startMonth},
                {"startDay", e.startDay},
                {"durationHours", e.durationHours},
                {"holidayQuestId", e.holidayQuestId},
                {"bossCreatureId", e.bossCreatureId},
                {"itemRewardId", e.itemRewardId},
                {"areaIdGate", e.areaIdGate},
                {"mapIdGate", e.mapIdGate},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WHOL: %s.whol\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  holidays : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind         recur     start    dur(h)  quest  boss   item   name\n");
    for (const auto& e : c.entries) {
        char start[16];
        std::snprintf(start, sizeof(start), "%02u/%02u",
                       e.startMonth, e.startDay);
        std::printf("  %4u   %-9s   %-8s   %-7s   %5u  %5u  %5u  %5u  %s\n",
                    e.holidayId,
                    wowee::pipeline::WoweeHoliday::holidayKindName(e.holidayKind),
                    wowee::pipeline::WoweeHoliday::recurrenceName(e.recurrence),
                    start, e.durationHours,
                    e.holidayQuestId, e.bossCreatureId,
                    e.itemRewardId, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each holiday emits all 13 scalar fields
    // plus dual int + name forms for holidayKind and
    // recurrence so hand-edits can use either representation.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".whol");
    if (outPath.empty()) outPath = base + ".whol.json";
    if (!wowee::pipeline::WoweeHolidayLoader::exists(base)) {
        std::fprintf(stderr,
            "export-whol-json: WHOL not found: %s.whol\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeHolidayLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"holidayId", e.holidayId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"holidayKind", e.holidayKind},
            {"holidayKindName", wowee::pipeline::WoweeHoliday::holidayKindName(e.holidayKind)},
            {"recurrence", e.recurrence},
            {"recurrenceName", wowee::pipeline::WoweeHoliday::recurrenceName(e.recurrence)},
            {"startMonth", e.startMonth},
            {"startDay", e.startDay},
            {"durationHours", e.durationHours},
            {"holidayQuestId", e.holidayQuestId},
            {"bossCreatureId", e.bossCreatureId},
            {"itemRewardId", e.itemRewardId},
            {"areaIdGate", e.areaIdGate},
            {"mapIdGate", e.mapIdGate},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-whol-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.whol\n", base.c_str());
    std::printf("  holidays : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".whol.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".whol");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-whol-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-whol-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "seasonal")  return wowee::pipeline::WoweeHoliday::Seasonal;
        if (s == "weekly")    return wowee::pipeline::WoweeHoliday::Weekly;
        if (s == "daily")     return wowee::pipeline::WoweeHoliday::Daily;
        if (s == "world-pvp") return wowee::pipeline::WoweeHoliday::WorldPvp;
        if (s == "one-shot")  return wowee::pipeline::WoweeHoliday::OneShot;
        if (s == "special")   return wowee::pipeline::WoweeHoliday::Special;
        return wowee::pipeline::WoweeHoliday::Seasonal;
    };
    auto recurFromName = [](const std::string& s) -> uint8_t {
        if (s == "annual")   return wowee::pipeline::WoweeHoliday::Annual;
        if (s == "monthly")  return wowee::pipeline::WoweeHoliday::Monthly;
        if (s == "weekly")   return wowee::pipeline::WoweeHoliday::WeeklyRecur;
        if (s == "one-time") return wowee::pipeline::WoweeHoliday::OneTime;
        return wowee::pipeline::WoweeHoliday::Annual;
    };
    wowee::pipeline::WoweeHoliday c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeHoliday::Entry e;
            e.holidayId = je.value("holidayId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("holidayKind") &&
                je["holidayKind"].is_number_integer()) {
                e.holidayKind = static_cast<uint8_t>(
                    je["holidayKind"].get<int>());
            } else if (je.contains("holidayKindName") &&
                       je["holidayKindName"].is_string()) {
                e.holidayKind = kindFromName(
                    je["holidayKindName"].get<std::string>());
            }
            if (je.contains("recurrence") &&
                je["recurrence"].is_number_integer()) {
                e.recurrence = static_cast<uint8_t>(
                    je["recurrence"].get<int>());
            } else if (je.contains("recurrenceName") &&
                       je["recurrenceName"].is_string()) {
                e.recurrence = recurFromName(
                    je["recurrenceName"].get<std::string>());
            }
            e.startMonth = static_cast<uint8_t>(je.value("startMonth", 0));
            e.startDay = static_cast<uint8_t>(je.value("startDay", 0));
            e.durationHours = static_cast<uint16_t>(
                je.value("durationHours", 168));
            e.holidayQuestId = je.value("holidayQuestId", 0u);
            e.bossCreatureId = je.value("bossCreatureId", 0u);
            e.itemRewardId = je.value("itemRewardId", 0u);
            e.areaIdGate = je.value("areaIdGate", 0u);
            e.mapIdGate = je.value("mapIdGate", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeHolidayLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-whol-json: failed to save %s.whol\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.whol\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  holidays : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".whol");
    if (!wowee::pipeline::WoweeHolidayLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-whol: WHOL not found: %s.whol\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeHolidayLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.holidayId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.holidayId == 0)
            errors.push_back(ctx + ": holidayId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.holidayKind > wowee::pipeline::WoweeHoliday::Special) {
            errors.push_back(ctx + ": holidayKind " +
                std::to_string(e.holidayKind) + " not in 0..5");
        }
        if (e.recurrence > wowee::pipeline::WoweeHoliday::OneTime) {
            errors.push_back(ctx + ": recurrence " +
                std::to_string(e.recurrence) + " not in 0..3");
        }
        if (e.durationHours == 0) {
            errors.push_back(ctx +
                ": durationHours=0 (holiday window has no length)");
        }
        // Annual / Monthly / OneTime require a real calendar
        // start. WeeklyRecur is exempt — it triggers based on
        // weekday rather than fixed date.
        if (e.recurrence != wowee::pipeline::WoweeHoliday::WeeklyRecur) {
            if (e.startMonth == 0 || e.startMonth > 12) {
                errors.push_back(ctx + ": startMonth " +
                    std::to_string(e.startMonth) +
                    " not in 1..12 (required for non-weekly recurrence)");
            }
            if (e.startDay == 0 || e.startDay > 31) {
                errors.push_back(ctx + ": startDay " +
                    std::to_string(e.startDay) + " not in 1..31");
            }
        }
        // Holidays with no quest, no boss, AND no reward have
        // no in-game presence beyond a calendar entry — useful
        // for simple banner-only events but worth flagging.
        if (e.holidayQuestId == 0 && e.bossCreatureId == 0 &&
            e.itemRewardId == 0) {
            warnings.push_back(ctx +
                ": no quest, boss, or reward — calendar-only event");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.holidayId) {
                errors.push_back(ctx + ": duplicate holidayId");
                break;
            }
        }
        idsSeen.push_back(e.holidayId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("whol", base, errors, warnings);
    std::printf("validate-whol: %s.whol\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu holidays, all holidayIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleHolidaysCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-holidays") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-holidays-weekly") == 0 && i + 1 < argc) {
        outRc = handleGenWeekly(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-holidays-special") == 0 && i + 1 < argc) {
        outRc = handleGenSpecial(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-whol") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-whol") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-whol-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-whol-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
