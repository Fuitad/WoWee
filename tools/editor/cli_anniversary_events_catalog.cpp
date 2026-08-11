#include "cli_anniversary_events_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_anniversary_events.hpp"
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

const char* eventKindName(uint8_t k) {
    using A = wowee::pipeline::WoweeAnniversaryEvents;
    switch (k) {
        case A::Holiday:           return "holiday";
        case A::Anniversary:       return "anniversary";
        case A::DoubleXP:          return "doublexp";
        case A::DoubleHonor:       return "doublehonor";
        case A::PetBattleWeekend:  return "petbattle";
        case A::BattlegroundBonus: return "bgbonus";
        case A::SeasonalQuest:     return "seasonalquest";
        case A::Misc:              return "misc";
        default:                   return "unknown";
    }
}

const char* recurrenceKindName(uint8_t k) {
    using A = wowee::pipeline::WoweeAnniversaryEvents;
    switch (k) {
        case A::Yearly:  return "yearly";
        case A::Monthly: return "monthly";
        case A::Weekly:  return "weekly";
        case A::OneOff:  return "oneoff";
        default:         return "unknown";
    }
}

const char* weekdayName(uint8_t d) {
    static const char* kDays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    return d <= 6 ? kDays[d] : "?";
}


void printGenSummary(const wowee::pipeline::WoweeAnniversaryEvents& c,
                     const std::string& base) {
    std::printf("Wrote %s.wanv\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  events  : %zu\n", c.entries.size());
}

int handleGenHolidays(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardHolidays";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wanv");
    auto c = wowee::pipeline::WoweeAnniversaryEventsLoader::
        makeStandardHolidays(name);
    if (!saveOrError<wowee::pipeline::WoweeAnniversaryEventsLoader>(c, base, "gen-anv", ".wanv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBonus(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeeklyBonusEvents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wanv");
    auto c = wowee::pipeline::WoweeAnniversaryEventsLoader::
        makeBonusEvents(name);
    if (!saveOrError<wowee::pipeline::WoweeAnniversaryEventsLoader>(c, base, "gen-anv-bonus", ".wanv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAnniversary(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "GameLaunchAnniversaries";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wanv");
    auto c = wowee::pipeline::WoweeAnniversaryEventsLoader::
        makeAnniversary(name);
    if (!saveOrError<wowee::pipeline::WoweeAnniversaryEventsLoader>(c, base, "gen-anv-launch", ".wanv")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wanv");
    if (!wowee::pipeline::WoweeAnniversaryEventsLoader::exists(
            base)) {
        std::fprintf(stderr, "WANV not found: %s.wanv\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAnniversaryEventsLoader::load(
        base);
    if (jsonOut) {
        nlohmann::json j;
        j["wanv"] = base + ".wanv";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"eventId", e.eventId},
                {"name", e.name},
                {"description", e.description},
                {"eventKind", e.eventKind},
                {"eventKindName", eventKindName(e.eventKind)},
                {"recurrenceKind", e.recurrenceKind},
                {"recurrenceKindName",
                    recurrenceKindName(e.recurrenceKind)},
                {"startMonth", e.startMonth},
                {"startDay", e.startDay},
                {"durationDays", e.durationDays},
                {"payloadSpellId", e.payloadSpellId},
                {"payloadItemId", e.payloadItemId},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WANV: %s.wanv\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  events  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind         recurrence   schedule          dur(d)  spell  item   name\n");
    for (const auto& e : c.entries) {
        char schedule[32];
        using A = wowee::pipeline::WoweeAnniversaryEvents;
        if (e.recurrenceKind == A::Weekly) {
            std::snprintf(schedule, sizeof(schedule),
                          "%-3s every week", weekdayName(e.startDay));
        } else if (e.recurrenceKind == A::Monthly) {
            std::snprintf(schedule, sizeof(schedule),
                          "Day %u every month", e.startDay);
        } else {
            std::snprintf(schedule, sizeof(schedule),
                          "%02u/%02u %s", e.startMonth, e.startDay,
                          recurrenceKindName(e.recurrenceKind));
        }
        std::printf("  %4u   %-10s   %-10s   %-15s   %4u   %5u  %5u  %s\n",
                    e.eventId, eventKindName(e.eventKind),
                    recurrenceKindName(e.recurrenceKind),
                    schedule, e.durationDays,
                    e.payloadSpellId, e.payloadItemId,
                    e.name.c_str());
    }
    return 0;
}

int parseEventKindToken(const std::string& s) {
    using A = wowee::pipeline::WoweeAnniversaryEvents;
    if (s == "holiday")       return A::Holiday;
    if (s == "anniversary")   return A::Anniversary;
    if (s == "doublexp")      return A::DoubleXP;
    if (s == "doublehonor")   return A::DoubleHonor;
    if (s == "petbattle")     return A::PetBattleWeekend;
    if (s == "bgbonus")       return A::BattlegroundBonus;
    if (s == "seasonalquest") return A::SeasonalQuest;
    if (s == "misc")          return A::Misc;
    return -1;
}

int parseRecurrenceKindToken(const std::string& s) {
    using A = wowee::pipeline::WoweeAnniversaryEvents;
    if (s == "yearly")  return A::Yearly;
    if (s == "monthly") return A::Monthly;
    if (s == "weekly")  return A::Weekly;
    if (s == "oneoff")  return A::OneOff;
    return -1;
}

template <typename ParseFn>
bool readEnumField(const nlohmann::json& je,
                    const char* intKey,
                    const char* nameKey,
                    ParseFn parseFn,
                    const char* label,
                    uint32_t entryId,
                    uint8_t& outValue) {
    if (je.contains(intKey)) {
        const auto& v = je[intKey];
        if (v.is_string()) {
            int parsed = parseFn(v.get<std::string>());
            if (parsed < 0) {
                std::fprintf(stderr,
                    "import-wanv-json: unknown %s token "
                    "'%s' on entry id=%u\n",
                    label, v.get<std::string>().c_str(),
                    entryId);
                return false;
            }
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
        if (v.is_number_integer()) {
            outValue = static_cast<uint8_t>(v.get<int>());
            return true;
        }
    }
    if (je.contains(nameKey) && je[nameKey].is_string()) {
        int parsed = parseFn(je[nameKey].get<std::string>());
        if (parsed >= 0) {
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
    }
    return true;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wanv");
    if (out.empty()) out = base + ".wanv.json";
    if (!wowee::pipeline::WoweeAnniversaryEventsLoader::exists(
            base)) {
        std::fprintf(stderr,
            "export-wanv-json: WANV not found: %s.wanv\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAnniversaryEventsLoader::load(
        base);
    nlohmann::json j;
    j["magic"] = "WANV";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"eventId", e.eventId},
            {"name", e.name},
            {"description", e.description},
            {"eventKind", e.eventKind},
            {"eventKindName", eventKindName(e.eventKind)},
            {"recurrenceKind", e.recurrenceKind},
            {"recurrenceKindName",
                recurrenceKindName(e.recurrenceKind)},
            {"startMonth", e.startMonth},
            {"startDay", e.startDay},
            {"durationDays", e.durationDays},
            {"payloadSpellId", e.payloadSpellId},
            {"payloadItemId", e.payloadItemId},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wanv-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu events)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wanv");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wanv-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wanv-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeAnniversaryEvents c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wanv-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeAnniversaryEvents::Entry e;
        e.eventId = je.value("eventId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (!readEnumField(je, "eventKind", "eventKindName",
                            parseEventKindToken, "eventKind",
                            e.eventId, e.eventKind)) return 1;
        if (!readEnumField(je, "recurrenceKind",
                            "recurrenceKindName",
                            parseRecurrenceKindToken,
                            "recurrenceKind",
                            e.eventId,
                            e.recurrenceKind)) return 1;
        e.startMonth = static_cast<uint8_t>(
            je.value("startMonth", 1u));
        e.startDay = static_cast<uint8_t>(
            je.value("startDay", 1u));
        e.durationDays = static_cast<uint16_t>(
            je.value("durationDays", 7u));
        e.payloadSpellId = je.value("payloadSpellId", 0u);
        e.payloadItemId = je.value("payloadItemId", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeAnniversaryEventsLoader::save(
            c, outBase)) {
        std::fprintf(stderr,
            "import-wanv-json: failed to save %s.wanv\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wanv (%zu events)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wanv");
    if (!wowee::pipeline::WoweeAnniversaryEventsLoader::exists(
            base)) {
        std::fprintf(stderr,
            "validate-wanv: WANV not found: %s.wanv\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeAnniversaryEventsLoader::load(
        base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.eventId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.eventId == 0)
            errors.push_back(ctx + ": eventId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.eventKind > 6 && e.eventKind != 255) {
            errors.push_back(ctx + ": eventKind " +
                std::to_string(e.eventKind) +
                " out of range (must be 0..6 or 255 Misc)");
        }
        if (e.recurrenceKind > 3) {
            errors.push_back(ctx + ": recurrenceKind " +
                std::to_string(e.recurrenceKind) +
                " out of range (must be 0..3)");
        }
        if (e.durationDays == 0) {
            errors.push_back(ctx +
                ": durationDays is 0 — event would never "
                "have an active window");
        }
        // Per-recurrence schedule validity: Yearly /
        // Monthly / OneOff need valid month + day; Weekly
        // ignores month + uses day as weekday 0..6.
        using A = wowee::pipeline::WoweeAnniversaryEvents;
        if (e.recurrenceKind == A::Weekly) {
            if (e.startDay > 6) {
                errors.push_back(ctx +
                    ": Weekly recurrence with startDay " +
                    std::to_string(e.startDay) +
                    " > 6 — must be 0 (Sun) through 6 (Sat)");
            }
            if (e.durationDays > 7) {
                warnings.push_back(ctx +
                    ": Weekly recurrence with "
                    "durationDays > 7 — event would "
                    "overlap with itself across week "
                    "boundaries");
            }
        } else {
            if (e.startMonth < 1 || e.startMonth > 12) {
                errors.push_back(ctx +
                    ": startMonth " +
                    std::to_string(e.startMonth) +
                    " out of range (must be 1..12 for "
                    "Yearly / Monthly / OneOff)");
            }
            if (e.startDay < 1 || e.startDay > 31) {
                errors.push_back(ctx + ": startDay " +
                    std::to_string(e.startDay) +
                    " out of range (must be 1..31)");
            }
            // Calendar sanity: Feb has 28-29 days, etc.
            // The validator doesn't try to be a full
            // calendar — just catches the obvious "Feb 30"
            // type errors.
            if (e.startMonth == 2 && e.startDay > 29) {
                errors.push_back(ctx +
                    ": startDay " + std::to_string(e.startDay) +
                    " for February — must be 1..29 (28 in "
                    "non-leap years; the schedule rolls "
                    "over to Mar 1 in those cases)");
            }
            if ((e.startMonth == 4 || e.startMonth == 6 ||
                 e.startMonth == 9 || e.startMonth == 11) &&
                e.startDay > 30) {
                errors.push_back(ctx +
                    ": startDay 31 for month " +
                    std::to_string(e.startMonth) +
                    " — that month only has 30 days");
            }
        }
        if (!idsSeen.insert(e.eventId).second) {
            errors.push_back(ctx + ": duplicate eventId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wanv", base, errors, warnings);
    std::printf("validate-wanv: %s.wanv\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu events, all eventIds "
                    "unique, calendar dates valid\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleAnniversaryEventsCatalog(int& i, int argc, char** argv,
                                     int& outRc) {
    if (std::strcmp(argv[i], "--gen-anv") == 0 && i + 1 < argc) {
        outRc = handleGenHolidays(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-anv-bonus") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBonus(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-anv-launch") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAnniversary(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wanv") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wanv") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wanv-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wanv-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
