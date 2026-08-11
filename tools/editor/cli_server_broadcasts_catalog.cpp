#include "cli_server_broadcasts_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_server_broadcasts.hpp"
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

const char* channelKindName(uint8_t k) {
    using S = wowee::pipeline::WoweeServerBroadcasts;
    switch (k) {
        case S::Login:         return "login";
        case S::SystemChannel: return "system";
        case S::RaidWarning:   return "raidwarning";
        case S::MOTD:          return "motd";
        case S::HelpTip:       return "helptip";
        default:               return "unknown";
    }
}

const char* factionFilterName(uint8_t f) {
    using S = wowee::pipeline::WoweeServerBroadcasts;
    switch (f) {
        case S::AllianceOnly: return "alliance";
        case S::HordeOnly:    return "horde";
        case S::Both:         return "both";
        default:              return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeServerBroadcasts& c,
                     const std::string& base) {
    std::printf("Wrote %s.wscb\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  broadcasts : %zu\n", c.entries.size());
}

int handleGenMotd(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ServerMOTD";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscb");
    auto c = wowee::pipeline::WoweeServerBroadcastsLoader::makeMotd(name);
    if (!saveOrError<wowee::pipeline::WoweeServerBroadcastsLoader>(c, base, "gen-scb", ".wscb")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMaintenance(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MaintenanceWarnings";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscb");
    auto c = wowee::pipeline::WoweeServerBroadcastsLoader::makeMaintenance(name);
    if (!saveOrError<wowee::pipeline::WoweeServerBroadcastsLoader>(c, base, "gen-scb-maintenance", ".wscb")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHelpTips(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HelpChannelTips";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscb");
    auto c = wowee::pipeline::WoweeServerBroadcastsLoader::makeHelpTips(name);
    if (!saveOrError<wowee::pipeline::WoweeServerBroadcastsLoader>(c, base, "gen-scb-helptips", ".wscb")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wscb");
    if (!wowee::pipeline::WoweeServerBroadcastsLoader::exists(base)) {
        std::fprintf(stderr, "WSCB not found: %s.wscb\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeServerBroadcastsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wscb"] = base + ".wscb";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"broadcastId", e.broadcastId},
                {"name", e.name},
                {"description", e.description},
                {"messageText", e.messageText},
                {"intervalSeconds", e.intervalSeconds},
                {"channelKind", e.channelKind},
                {"channelKindName", channelKindName(e.channelKind)},
                {"factionFilter", e.factionFilter},
                {"factionFilterName",
                    factionFilterName(e.factionFilter)},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSCB: %s.wscb\n", base.c_str());
    std::printf("  catalog    : %s\n", c.name.c_str());
    std::printf("  broadcasts : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   channel       faction   interval(s)  lvls    name\n");
    for (const auto& e : c.entries) {
        char lvls[16];
        std::snprintf(lvls, sizeof(lvls), "%u-%u",
                      e.minLevel, e.maxLevel);
        std::printf("  %4u  %-12s  %-9s  %10u  %-6s  %s\n",
                    e.broadcastId,
                    channelKindName(e.channelKind),
                    factionFilterName(e.factionFilter),
                    e.intervalSeconds, lvls, e.name.c_str());
    }
    return 0;
}

// Token parser for channelKind. Returns -1 if unknown.
int parseChannelKindToken(const std::string& s) {
    using S = wowee::pipeline::WoweeServerBroadcasts;
    if (s == "login")       return S::Login;
    if (s == "system")      return S::SystemChannel;
    if (s == "raidwarning") return S::RaidWarning;
    if (s == "motd")        return S::MOTD;
    if (s == "helptip")     return S::HelpTip;
    return -1;
}

// Token parser for factionFilter. Returns -1 if unknown.
int parseFactionFilterToken(const std::string& s) {
    using S = wowee::pipeline::WoweeServerBroadcasts;
    if (s == "alliance") return S::AllianceOnly;
    if (s == "horde")    return S::HordeOnly;
    if (s == "both")     return S::Both;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wscb");
    if (out.empty()) out = base + ".wscb.json";
    if (!wowee::pipeline::WoweeServerBroadcastsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wscb-json: WSCB not found: %s.wscb\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeServerBroadcastsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WSCB";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"broadcastId", e.broadcastId},
            {"name", e.name},
            {"description", e.description},
            {"messageText", e.messageText},
            {"intervalSeconds", e.intervalSeconds},
            {"channelKind", e.channelKind},
            {"channelKindName", channelKindName(e.channelKind)},
            {"factionFilter", e.factionFilter},
            {"factionFilterName",
                factionFilterName(e.factionFilter)},
            {"minLevel", e.minLevel},
            {"maxLevel", e.maxLevel},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wscb-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu broadcasts)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wscb");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wscb-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wscb-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeServerBroadcasts c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wscb-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeServerBroadcasts::Entry e;
        e.broadcastId = je.value("broadcastId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.messageText = je.value("messageText", std::string{});
        e.intervalSeconds = je.value("intervalSeconds", 0u);
        // channelKind: int OR token string.
        if (je.contains("channelKind")) {
            const auto& ck = je["channelKind"];
            if (ck.is_string()) {
                int parsed = parseChannelKindToken(ck.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wscb-json: unknown channelKind "
                        "token '%s' on entry id=%u\n",
                        ck.get<std::string>().c_str(), e.broadcastId);
                    return 1;
                }
                e.channelKind = static_cast<uint8_t>(parsed);
            } else if (ck.is_number_integer()) {
                e.channelKind = static_cast<uint8_t>(ck.get<int>());
            }
        } else if (je.contains("channelKindName") &&
                   je["channelKindName"].is_string()) {
            int parsed = parseChannelKindToken(
                je["channelKindName"].get<std::string>());
            if (parsed >= 0)
                e.channelKind = static_cast<uint8_t>(parsed);
        }
        // factionFilter: int OR token string.
        if (je.contains("factionFilter")) {
            const auto& ff = je["factionFilter"];
            if (ff.is_string()) {
                int parsed = parseFactionFilterToken(
                    ff.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wscb-json: unknown factionFilter "
                        "token '%s' on entry id=%u\n",
                        ff.get<std::string>().c_str(),
                        e.broadcastId);
                    return 1;
                }
                e.factionFilter = static_cast<uint8_t>(parsed);
            } else if (ff.is_number_integer()) {
                e.factionFilter = static_cast<uint8_t>(ff.get<int>());
            }
        } else if (je.contains("factionFilterName") &&
                   je["factionFilterName"].is_string()) {
            int parsed = parseFactionFilterToken(
                je["factionFilterName"].get<std::string>());
            if (parsed >= 0)
                e.factionFilter = static_cast<uint8_t>(parsed);
        }
        e.minLevel = static_cast<uint8_t>(je.value("minLevel", 0u));
        e.maxLevel = static_cast<uint8_t>(je.value("maxLevel", 0u));
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeServerBroadcastsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wscb-json: failed to save %s.wscb\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wscb (%zu broadcasts)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wscb");
    if (!wowee::pipeline::WoweeServerBroadcastsLoader::exists(base)) {
        return reportMissing("validate-wscb", "WSCB", base, ".wscb");
    }
    auto c = wowee::pipeline::WoweeServerBroadcastsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.broadcastId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.broadcastId == 0)
            errors.push_back(ctx + ": broadcastId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.messageText.empty())
            errors.push_back(ctx + ": messageText is empty "
                "— broadcast would deliver no payload");
        if (e.factionFilter == 0 || e.factionFilter > 3) {
            errors.push_back(ctx + ": factionFilter " +
                std::to_string(e.factionFilter) +
                " out of range (must be 1=A / 2=H / 3=Both)");
        }
        if (e.channelKind > 4) {
            errors.push_back(ctx + ": channelKind " +
                std::to_string(e.channelKind) +
                " out of range (must be 0..4)");
        }
        if (e.minLevel > 0 && e.maxLevel > 0 &&
            e.minLevel > e.maxLevel) {
            errors.push_back(ctx + ": minLevel " +
                std::to_string(e.minLevel) +
                " > maxLevel " + std::to_string(e.maxLevel));
        }
        // Periodic broadcasts (interval>0) make sense
        // mainly on SystemChannel and HelpTip. Login/MOTD
        // with interval>0 is a configuration mistake —
        // those fire on session enter, not on a timer.
        using S = wowee::pipeline::WoweeServerBroadcasts;
        if (e.intervalSeconds > 0 &&
            (e.channelKind == S::Login ||
             e.channelKind == S::MOTD)) {
            warnings.push_back(ctx +
                ": intervalSeconds=" +
                std::to_string(e.intervalSeconds) +
                " on " + channelKindName(e.channelKind) +
                " channel — login/MOTD fire on session "
                "enter, not on a timer; interval likely "
                "ignored");
        }
        // Very short intervals would spam players. Warn
        // below 60 seconds; reject below 10 seconds.
        if (e.intervalSeconds > 0 && e.intervalSeconds < 10) {
            errors.push_back(ctx + ": intervalSeconds " +
                std::to_string(e.intervalSeconds) +
                " < 10 — would spam players faster than "
                "they can read");
        } else if (e.intervalSeconds > 0 &&
                   e.intervalSeconds < 60) {
            warnings.push_back(ctx + ": intervalSeconds " +
                std::to_string(e.intervalSeconds) +
                " < 60 — broadcast fires more than once "
                "per minute; verify if intentional");
        }
        // Message length sanity: WoW chat message buffer
        // is ~255 chars; over that, server may truncate.
        if (e.messageText.size() > 255) {
            warnings.push_back(ctx + ": messageText is " +
                std::to_string(e.messageText.size()) +
                " chars (>255) — server may truncate on "
                "delivery");
        }
        if (!idsSeen.add(e.broadcastId)) errors.push_back(ctx + ": duplicate broadcastId");
    }
    return cli::reportValidation("wscb", base, jsonOut, errors, warnings,
                                 formatted("%zu broadcasts, all ids unique", c.entries.size()));
}

} // namespace

bool handleServerBroadcastsCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-scb") == 0 && i + 1 < argc) {
        outRc = handleGenMotd(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-scb-maintenance") == 0 &&
        i + 1 < argc) {
        outRc = handleGenMaintenance(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-scb-helptips") == 0 &&
        i + 1 < argc) {
        outRc = handleGenHelpTips(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wscb") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wscb") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wscb-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wscb-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
