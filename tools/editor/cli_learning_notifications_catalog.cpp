#include "cli_learning_notifications_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_learning_notifications.hpp"
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

const char* triggerKindName(uint8_t k) {
    using L = wowee::pipeline::WoweeLearningNotifications;
    switch (k) {
        case L::LevelReach:      return "levelreach";
        case L::FactionStanding: return "factionstanding";
        case L::ItemAcquired:    return "itemacquired";
        case L::QuestComplete:   return "questcomplete";
        case L::SpellLearned:    return "spelllearned";
        case L::ZoneEntered:     return "zoneentered";
        default:                 return "unknown";
    }
}

const char* channelKindName(uint8_t k) {
    using L = wowee::pipeline::WoweeLearningNotifications;
    switch (k) {
        case L::RaidWarning: return "raidwarning";
        case L::SystemMsg:   return "systemmsg";
        case L::Subtitle:    return "subtitle";
        case L::Tutorial:    return "tutorial";
        case L::MOTDAppend:  return "motdappend";
        default:             return "unknown";
    }
}

const char* factionFilterName(uint8_t f) {
    using L = wowee::pipeline::WoweeLearningNotifications;
    switch (f) {
        case L::AllianceOnly: return "alliance";
        case L::HordeOnly:    return "horde";
        case L::Both:         return "both";
        default:              return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeLearningNotifications& c,
                     const std::string& base) {
    std::printf("Wrote %s.wldn\n", base.c_str());
    std::printf("  catalog       : %s\n", c.name.c_str());
    std::printf("  notifications : %zu\n", c.entries.size());
}

int handleGenLevels(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "LevelMilestones";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wldn");
    auto c = wowee::pipeline::WoweeLearningNotificationsLoader::
        makeLevelMilestones(name);
    if (!saveOrError<wowee::pipeline::WoweeLearningNotificationsLoader>(c, base, "gen-ldn", ".wldn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAccount(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AccountUnlocks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wldn");
    auto c = wowee::pipeline::WoweeLearningNotificationsLoader::
        makeAccountUnlocks(name);
    if (!saveOrError<wowee::pipeline::WoweeLearningNotificationsLoader>(c, base, "gen-ldn-account", ".wldn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenReputation(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ReputationMilestones";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wldn");
    auto c = wowee::pipeline::WoweeLearningNotificationsLoader::
        makeReputation(name);
    if (!saveOrError<wowee::pipeline::WoweeLearningNotificationsLoader>(c, base, "gen-ldn-rep", ".wldn")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wldn");
    if (!wowee::pipeline::WoweeLearningNotificationsLoader::exists(
            base)) {
        std::fprintf(stderr, "WLDN not found: %s.wldn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLearningNotificationsLoader::load(
        base);
    if (jsonOut) {
        nlohmann::json j;
        j["wldn"] = base + ".wldn";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"notificationId", e.notificationId},
                {"name", e.name},
                {"description", e.description},
                {"messageText", e.messageText},
                {"triggerKind", e.triggerKind},
                {"triggerKindName", triggerKindName(e.triggerKind)},
                {"channelKind", e.channelKind},
                {"channelKindName", channelKindName(e.channelKind)},
                {"factionFilter", e.factionFilter},
                {"factionFilterName",
                    factionFilterName(e.factionFilter)},
                {"triggerValue", e.triggerValue},
                {"soundId", e.soundId},
                {"minTotalTimePlayed", e.minTotalTimePlayed},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLDN: %s.wldn\n", base.c_str());
    std::printf("  catalog       : %s\n", c.name.c_str());
    std::printf("  notifications : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   trigger          val    channel       faction   minPlayed   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-15s  %5d   %-12s  %-9s  %8u   %s\n",
                    e.notificationId,
                    triggerKindName(e.triggerKind),
                    e.triggerValue,
                    channelKindName(e.channelKind),
                    factionFilterName(e.factionFilter),
                    e.minTotalTimePlayed,
                    e.name.c_str());
    }
    return 0;
}

int parseTriggerKindToken(const std::string& s) {
    using L = wowee::pipeline::WoweeLearningNotifications;
    if (s == "levelreach")      return L::LevelReach;
    if (s == "factionstanding") return L::FactionStanding;
    if (s == "itemacquired")    return L::ItemAcquired;
    if (s == "questcomplete")   return L::QuestComplete;
    if (s == "spelllearned")    return L::SpellLearned;
    if (s == "zoneentered")     return L::ZoneEntered;
    return -1;
}

int parseChannelKindToken(const std::string& s) {
    using L = wowee::pipeline::WoweeLearningNotifications;
    if (s == "raidwarning") return L::RaidWarning;
    if (s == "systemmsg")   return L::SystemMsg;
    if (s == "subtitle")    return L::Subtitle;
    if (s == "tutorial")    return L::Tutorial;
    if (s == "motdappend")  return L::MOTDAppend;
    return -1;
}

int parseFactionFilterToken(const std::string& s) {
    using L = wowee::pipeline::WoweeLearningNotifications;
    if (s == "alliance") return L::AllianceOnly;
    if (s == "horde")    return L::HordeOnly;
    if (s == "both")     return L::Both;
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
                    "import-wldn-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wldn");
    if (out.empty()) out = base + ".wldn.json";
    if (!wowee::pipeline::WoweeLearningNotificationsLoader::exists(
            base)) {
        std::fprintf(stderr,
            "export-wldn-json: WLDN not found: %s.wldn\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLearningNotificationsLoader::load(
        base);
    nlohmann::json j;
    j["magic"] = "WLDN";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"notificationId", e.notificationId},
            {"name", e.name},
            {"description", e.description},
            {"messageText", e.messageText},
            {"triggerKind", e.triggerKind},
            {"triggerKindName", triggerKindName(e.triggerKind)},
            {"channelKind", e.channelKind},
            {"channelKindName", channelKindName(e.channelKind)},
            {"factionFilter", e.factionFilter},
            {"factionFilterName",
                factionFilterName(e.factionFilter)},
            {"triggerValue", e.triggerValue},
            {"soundId", e.soundId},
            {"minTotalTimePlayed", e.minTotalTimePlayed},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wldn-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu notifications)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wldn");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wldn-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wldn-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeLearningNotifications c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wldn-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeLearningNotifications::Entry e;
        e.notificationId = je.value("notificationId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.messageText = je.value("messageText", std::string{});
        if (!readEnumField(je, "triggerKind", "triggerKindName",
                            parseTriggerKindToken, "triggerKind",
                            e.notificationId,
                            e.triggerKind)) return 1;
        if (!readEnumField(je, "channelKind", "channelKindName",
                            parseChannelKindToken, "channelKind",
                            e.notificationId,
                            e.channelKind)) return 1;
        if (!readEnumField(je, "factionFilter",
                            "factionFilterName",
                            parseFactionFilterToken,
                            "factionFilter",
                            e.notificationId,
                            e.factionFilter)) return 1;
        e.triggerValue = je.value("triggerValue", 0);
        e.soundId = je.value("soundId", 0u);
        e.minTotalTimePlayed = je.value("minTotalTimePlayed", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeLearningNotificationsLoader::save(
            c, outBase)) {
        std::fprintf(stderr,
            "import-wldn-json: failed to save %s.wldn\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wldn (%zu notifications)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wldn");
    if (!wowee::pipeline::WoweeLearningNotificationsLoader::exists(
            base)) {
        std::fprintf(stderr,
            "validate-wldn: WLDN not found: %s.wldn\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLearningNotificationsLoader::load(
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
                          " (id=" + std::to_string(e.notificationId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.notificationId == 0)
            errors.push_back(ctx + ": notificationId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.messageText.empty())
            errors.push_back(ctx +
                ": messageText is empty — notification "
                "would deliver no payload");
        if (e.triggerKind > 5) {
            errors.push_back(ctx + ": triggerKind " +
                std::to_string(e.triggerKind) +
                " out of range (must be 0..5)");
        }
        if (e.channelKind > 4) {
            errors.push_back(ctx + ": channelKind " +
                std::to_string(e.channelKind) +
                " out of range (must be 0..4)");
        }
        if (e.factionFilter == 0 || e.factionFilter > 3) {
            errors.push_back(ctx + ": factionFilter " +
                std::to_string(e.factionFilter) +
                " out of range (must be 1=A / 2=H / 3=Both)");
        }
        // Per-trigger-kind validity of triggerValue.
        using L = wowee::pipeline::WoweeLearningNotifications;
        if (e.triggerKind == L::LevelReach) {
            if (e.triggerValue < 1 || e.triggerValue > 80) {
                warnings.push_back(ctx +
                    ": LevelReach triggerValue " +
                    std::to_string(e.triggerValue) +
                    " outside 1-80 range — current cap is "
                    "level 80 (WotLK)");
            }
        } else if (e.triggerKind == L::FactionStanding) {
            // Standing range: Hated=-42000, Exalted=42000.
            if (e.triggerValue < -42000 ||
                e.triggerValue > 42000) {
                errors.push_back(ctx +
                    ": FactionStanding triggerValue " +
                    std::to_string(e.triggerValue) +
                    " outside [-42000, 42000] valid range");
            }
        } else if (e.triggerKind == L::ItemAcquired ||
                   e.triggerKind == L::QuestComplete ||
                   e.triggerKind == L::SpellLearned ||
                   e.triggerKind == L::ZoneEntered) {
            if (e.triggerValue <= 0) {
                errors.push_back(ctx +
                    ": " + std::string(triggerKindName(
                        e.triggerKind)) +
                    " triggerValue " +
                    std::to_string(e.triggerValue) +
                    " <= 0 — must be a positive id");
            }
        }
        if (e.messageText.size() > 255) {
            warnings.push_back(ctx + ": messageText is " +
                std::to_string(e.messageText.size()) +
                " chars (>255) — server may truncate on "
                "delivery");
        }
        if (!idsSeen.insert(e.notificationId).second) {
            errors.push_back(ctx + ": duplicate notificationId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wldn", base, errors, warnings);
    std::printf("validate-wldn: %s.wldn\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu notifications, all "
                    "notificationIds unique, triggerValues "
                    "valid for kind\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleLearningNotificationsCatalog(int& i, int argc, char** argv,
                                          int& outRc) {
    if (std::strcmp(argv[i], "--gen-ldn") == 0 && i + 1 < argc) {
        outRc = handleGenLevels(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ldn-account") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAccount(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ldn-rep") == 0 && i + 1 < argc) {
        outRc = handleGenReputation(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wldn") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wldn") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wldn-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wldn-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
