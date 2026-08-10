#include "cli_events_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_events.hpp"
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

std::string stripWseaExt(std::string base) {
    stripExt(base, ".wsea");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeEvent& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeEventLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wsea\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeEvent& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsea\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  events  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterEvents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWseaExt(base);
    auto c = wowee::pipeline::WoweeEventLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-events")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenYearly(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "YearlyEvents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWseaExt(base);
    auto c = wowee::pipeline::WoweeEventLoader::makeYearly(name);
    if (!saveOrError(c, base, "gen-events-yearly")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBonusWeekends(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BonusWeekends";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWseaExt(base);
    auto c = wowee::pipeline::WoweeEventLoader::makeBonusWeekends(name);
    if (!saveOrError(c, base, "gen-events-weekends")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWseaExt(base);
    if (!wowee::pipeline::WoweeEventLoader::exists(base)) {
        std::fprintf(stderr, "WSEA not found: %s.wsea\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeEventLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsea"] = base + ".wsea";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"eventId", e.eventId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"announceMessage", e.announceMessage},
                {"startDate", e.startDate},
                {"duration_seconds", e.duration_seconds},
                {"recurrenceDays", e.recurrenceDays},
                {"holidayKind", e.holidayKind},
                {"holidayKindName", wowee::pipeline::WoweeEvent::holidayKindName(e.holidayKind)},
                {"factionGroup", e.factionGroup},
                {"factionGroupName", wowee::pipeline::WoweeEvent::factionGroupName(e.factionGroup)},
                {"bonusXpPercent", e.bonusXpPercent},
                {"tokenIdReward", e.tokenIdReward},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSEA: %s.wsea\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  events  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind         duration  recur   bonus%%  token   name\n");
    for (const auto& e : c.entries) {
        uint32_t durDays = e.duration_seconds / (24 * 3600);
        uint32_t durHours = (e.duration_seconds % (24 * 3600)) / 3600;
        std::printf("  %4u   %-11s  %2ud %2uh    %4u    %3u     %4u    %s\n",
                    e.eventId,
                    wowee::pipeline::WoweeEvent::holidayKindName(e.holidayKind),
                    durDays, durHours,
                    e.recurrenceDays, e.bonusXpPercent,
                    e.tokenIdReward, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each event emits all 11 scalar fields
    // plus dual int + name forms for holidayKind and
    // factionGroup.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = stripWseaExt(base);
    if (outPath.empty()) outPath = base + ".wsea.json";
    if (!wowee::pipeline::WoweeEventLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsea-json: WSEA not found: %s.wsea\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeEventLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"eventId", e.eventId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"announceMessage", e.announceMessage},
            {"startDate", e.startDate},
            {"duration_seconds", e.duration_seconds},
            {"recurrenceDays", e.recurrenceDays},
            {"holidayKind", e.holidayKind},
            {"holidayKindName", wowee::pipeline::WoweeEvent::holidayKindName(e.holidayKind)},
            {"factionGroup", e.factionGroup},
            {"factionGroupName", wowee::pipeline::WoweeEvent::factionGroupName(e.factionGroup)},
            {"bonusXpPercent", e.bonusXpPercent},
            {"tokenIdReward", e.tokenIdReward},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wsea-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wsea\n", base.c_str());
    std::printf("  events : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wsea.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = stripWseaExt(outBase);
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wsea-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wsea-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "combat")      return wowee::pipeline::WoweeEvent::Combat;
        if (s == "collection")  return wowee::pipeline::WoweeEvent::Collection;
        if (s == "racial")      return wowee::pipeline::WoweeEvent::Racial;
        if (s == "anniversary") return wowee::pipeline::WoweeEvent::Anniversary;
        if (s == "fishing")     return wowee::pipeline::WoweeEvent::Fishing;
        if (s == "cosmetic")    return wowee::pipeline::WoweeEvent::Cosmetic;
        if (s == "world-event") return wowee::pipeline::WoweeEvent::WorldEvent;
        return wowee::pipeline::WoweeEvent::Combat;
    };
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "both")     return wowee::pipeline::WoweeEvent::FactionBoth;
        if (s == "alliance") return wowee::pipeline::WoweeEvent::FactionAlliance;
        if (s == "horde")    return wowee::pipeline::WoweeEvent::FactionHorde;
        return wowee::pipeline::WoweeEvent::FactionBoth;
    };
    wowee::pipeline::WoweeEvent c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeEvent::Entry e;
            e.eventId = je.value("eventId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            e.announceMessage = je.value("announceMessage", std::string{});
            e.startDate = je.value("startDate", static_cast<uint64_t>(0));
            e.duration_seconds = je.value("duration_seconds", 0u);
            e.recurrenceDays = static_cast<uint16_t>(
                je.value("recurrenceDays", 0));
            if (je.contains("holidayKind") && je["holidayKind"].is_number_integer()) {
                e.holidayKind = static_cast<uint8_t>(je["holidayKind"].get<int>());
            } else if (je.contains("holidayKindName") &&
                       je["holidayKindName"].is_string()) {
                e.holidayKind = kindFromName(je["holidayKindName"].get<std::string>());
            }
            if (je.contains("factionGroup") && je["factionGroup"].is_number_integer()) {
                e.factionGroup = static_cast<uint8_t>(je["factionGroup"].get<int>());
            } else if (je.contains("factionGroupName") &&
                       je["factionGroupName"].is_string()) {
                e.factionGroup = factionFromName(je["factionGroupName"].get<std::string>());
            }
            e.bonusXpPercent = static_cast<uint8_t>(
                je.value("bonusXpPercent", 0));
            e.tokenIdReward = je.value("tokenIdReward", 0u);
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeEventLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsea-json: failed to save %s.wsea\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsea\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  events : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWseaExt(base);
    if (!wowee::pipeline::WoweeEventLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsea: WSEA not found: %s.wsea\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeEventLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.eventId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.eventId == 0) errors.push_back(ctx + ": eventId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.holidayKind > wowee::pipeline::WoweeEvent::WorldEvent) {
            errors.push_back(ctx + ": holidayKind " +
                std::to_string(e.holidayKind) + " not in 0..6");
        }
        if (e.factionGroup > wowee::pipeline::WoweeEvent::FactionHorde) {
            errors.push_back(ctx + ": factionGroup " +
                std::to_string(e.factionGroup) + " not in 0..2");
        }
        if (e.duration_seconds == 0) {
            errors.push_back(ctx + ": duration_seconds is 0 (event never runs)");
        }
        if (e.recurrenceDays > 0 &&
            e.duration_seconds > e.recurrenceDays * 24u * 3600u) {
            errors.push_back(ctx +
                ": duration exceeds recurrence period (events would overlap)");
        }
        if (e.bonusXpPercent > 200) {
            warnings.push_back(ctx +
                ": bonusXpPercent > 200 (very high — verify intentional)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.eventId) {
                errors.push_back(ctx + ": duplicate eventId");
                break;
            }
        }
        idsSeen.push_back(e.eventId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsea", base, errors, warnings);
    std::printf("validate-wsea: %s.wsea\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu events, all eventIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleEventsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-events") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-events-yearly") == 0 && i + 1 < argc) {
        outRc = handleGenYearly(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-events-weekends") == 0 && i + 1 < argc) {
        outRc = handleGenBonusWeekends(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsea") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsea") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsea-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsea-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
