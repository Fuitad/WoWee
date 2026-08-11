#include "cli_global_channels_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_global_channels.hpp"
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

const char* channelKindName(uint8_t k) {
    using G = wowee::pipeline::WoweeGlobalChannels;
    switch (k) {
        case G::Global:    return "global";
        case G::RealmZone: return "realmzone";
        case G::Faction:   return "faction";
        case G::Custom:    return "custom";
        default:           return "unknown";
    }
}

const char* accessKindName(uint8_t k) {
    using G = wowee::pipeline::WoweeGlobalChannels;
    switch (k) {
        case G::PublicJoin:     return "publicjoin";
        case G::InviteOnly:     return "inviteonly";
        case G::AutoJoinOnZone: return "autojoinonzone";
        case G::Moderated:      return "moderated";
        default:                return "unknown";
    }
}

int parseChannelKindToken(const std::string& s) {
    using G = wowee::pipeline::WoweeGlobalChannels;
    if (s == "global")    return G::Global;
    if (s == "realmzone") return G::RealmZone;
    if (s == "faction")   return G::Faction;
    if (s == "custom")    return G::Custom;
    return -1;
}

int parseAccessKindToken(const std::string& s) {
    using G = wowee::pipeline::WoweeGlobalChannels;
    if (s == "publicjoin")     return G::PublicJoin;
    if (s == "inviteonly")     return G::InviteOnly;
    if (s == "autojoinonzone") return G::AutoJoinOnZone;
    if (s == "moderated")      return G::Moderated;
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
                    "import-wgch-json: unknown %s token "
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


void printGenSummary(const wowee::pipeline::WoweeGlobalChannels& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgch\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  channels: %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardChatChannels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgch");
    auto c = wowee::pipeline::WoweeGlobalChannelsLoader::
        makeStandardChannels(name);
    if (!saveOrError<wowee::pipeline::WoweeGlobalChannelsLoader>(c, base, "gen-gch", ".wgch")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRoleplay(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RoleplayChannels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgch");
    auto c = wowee::pipeline::WoweeGlobalChannelsLoader::
        makeRoleplay(name);
    if (!saveOrError<wowee::pipeline::WoweeGlobalChannelsLoader>(c, base, "gen-gch-rp", ".wgch")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAdmin(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AdminChannels";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgch");
    auto c = wowee::pipeline::WoweeGlobalChannelsLoader::
        makeAdminChannels(name);
    if (!saveOrError<wowee::pipeline::WoweeGlobalChannelsLoader>(c, base, "gen-gch-admin", ".wgch")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgch");
    if (!wowee::pipeline::WoweeGlobalChannelsLoader::exists(base)) {
        std::fprintf(stderr, "WGCH not found: %s.wgch\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGlobalChannelsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgch"] = base + ".wgch";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"channelId", e.channelId},
                {"name", e.name},
                {"description", e.description},
                {"channelKind", e.channelKind},
                {"channelKindName", channelKindName(e.channelKind)},
                {"accessKind", e.accessKind},
                {"accessKindName", accessKindName(e.accessKind)},
                {"passwordRequired", e.passwordRequired != 0},
                {"levelMin", e.levelMin},
                {"maxMembers", e.maxMembers},
                {"topicSetByMods", e.topicSetByMods != 0},
                {"zoneDefaultMapId", e.zoneDefaultMapId},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGCH: %s.wgch\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  channels: %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind        access            pw  lvl  max     zone  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s  %-15s   %s   %3u  %5u  %5u   %s\n",
                    e.channelId, channelKindName(e.channelKind),
                    accessKindName(e.accessKind),
                    e.passwordRequired ? "Y" : "n",
                    e.levelMin, e.maxMembers,
                    e.zoneDefaultMapId, e.name.c_str());
    }
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgch");
    if (!wowee::pipeline::WoweeGlobalChannelsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wgch: WGCH not found: %s.wgch\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGlobalChannelsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    std::set<std::string> namesSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.channelId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.channelId == 0)
            errors.push_back(ctx + ": channelId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.channelKind > 3) {
            errors.push_back(ctx + ": channelKind " +
                std::to_string(e.channelKind) +
                " out of range (must be 0..3)");
        }
        if (e.accessKind > 3) {
            errors.push_back(ctx + ": accessKind " +
                std::to_string(e.accessKind) +
                " out of range (must be 0..3)");
        }
        // AutoJoinOnZone REQUIRES zoneDefaultMapId — else
        // the auto-join trigger never fires.
        using G = wowee::pipeline::WoweeGlobalChannels;
        if (e.accessKind == G::AutoJoinOnZone &&
            e.zoneDefaultMapId == 0) {
            errors.push_back(ctx +
                ": AutoJoinOnZone access kind with "
                "zoneDefaultMapId=0 — auto-join trigger "
                "would never fire (no zone bound to "
                "this channel)");
        }
        // Inverse: zoneDefaultMapId set with non-AutoJoin
        // kind is dead data — warn.
        if (e.zoneDefaultMapId != 0 &&
            e.accessKind != G::AutoJoinOnZone) {
            warnings.push_back(ctx +
                ": zoneDefaultMapId=" +
                std::to_string(e.zoneDefaultMapId) +
                " set but accessKind is not AutoJoinOn"
                "Zone — the field is ignored at runtime");
        }
        // Channel names must be unique — chat-window
        // dispatch identifies channels by name in /chat
        // commands.
        if (!e.name.empty() &&
            !namesSeen.insert(e.name).second) {
            errors.push_back(ctx +
                ": duplicate channel name '" + e.name +
                "' — /join command would route "
                "ambiguously");
        }
        if (!idsSeen.insert(e.channelId).second) {
            errors.push_back(ctx + ": duplicate channelId");
        }
    }
    return cli::reportValidation("wgch", base, jsonOut, errors, warnings,
                                 formatted("%zu channels, all channelIds "
                    "+ names unique, AutoJoinOnZone "
                    "channels have zoneDefaultMapId set", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wgch");
    if (out.empty()) out = base + ".wgch.json";
    if (!wowee::pipeline::WoweeGlobalChannelsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wgch-json: WGCH not found: %s.wgch\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGlobalChannelsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WGCH";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"channelId", e.channelId},
            {"name", e.name},
            {"description", e.description},
            {"channelKind", e.channelKind},
            {"channelKindName", channelKindName(e.channelKind)},
            {"accessKind", e.accessKind},
            {"accessKindName", accessKindName(e.accessKind)},
            {"passwordRequired", e.passwordRequired != 0},
            {"levelMin", e.levelMin},
            {"maxMembers", e.maxMembers},
            {"topicSetByMods", e.topicSetByMods != 0},
            {"zoneDefaultMapId", e.zoneDefaultMapId},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wgch-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu channels)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wgch");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wgch-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wgch-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeGlobalChannels c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wgch-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeGlobalChannels::Entry e;
        e.channelId = je.value("channelId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (!readEnumField(je, "channelKind", "channelKindName",
                            parseChannelKindToken, "channelKind",
                            e.channelId, e.channelKind)) return 1;
        if (!readEnumField(je, "accessKind", "accessKindName",
                            parseAccessKindToken, "accessKind",
                            e.channelId, e.accessKind)) return 1;
        e.passwordRequired = je.value("passwordRequired", false) ? 1 : 0;
        e.levelMin = static_cast<uint8_t>(je.value("levelMin", 0));
        e.maxMembers = static_cast<uint16_t>(je.value("maxMembers", 0));
        e.topicSetByMods = je.value("topicSetByMods", true) ? 1 : 0;
        e.zoneDefaultMapId = je.value("zoneDefaultMapId", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeGlobalChannelsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgch-json: failed to save %s.wgch\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgch (%zu channels)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleGlobalChannelsCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-gch") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gch-rp") == 0 && i + 1 < argc) {
        outRc = handleGenRoleplay(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gch-admin") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAdmin(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgch") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgch") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgch-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgch-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
