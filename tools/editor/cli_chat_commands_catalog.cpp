#include "cli_chat_commands_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_chat_commands.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
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

const char* securityLevelName(uint8_t s) {
    using W = wowee::pipeline::WoweeChatCommands;
    switch (s) {
        case W::Player:     return "player";
        case W::Helper:     return "helper";
        case W::Moderator:  return "moderator";
        case W::GameMaster: return "gamemaster";
        case W::Admin:      return "admin";
        default:            return "?";
    }
}

const char* categoryName(uint8_t c) {
    using W = wowee::pipeline::WoweeChatCommands;
    switch (c) {
        case W::Info:          return "info";
        case W::Movement:      return "movement";
        case W::Communication: return "communication";
        case W::AdminCmd:      return "admincmd";
        case W::Debug:         return "debug";
        default:               return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeChatCommands& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcmd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  commands: %zu\n", c.entries.size());
}

int handleGenBasic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BasicChatCommands";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmd");
    auto c = wowee::pipeline::WoweeChatCommandsLoader::
        makeBasicCommands(name);
    if (!saveOrError<wowee::pipeline::WoweeChatCommandsLoader>(c, base, "gen-cmd-basic", ".wcmd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMovement(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MovementChatCommands";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmd");
    auto c = wowee::pipeline::WoweeChatCommandsLoader::
        makeMovementCommands(name);
    if (!saveOrError<wowee::pipeline::WoweeChatCommandsLoader>(c, base, "gen-cmd-movement", ".wcmd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAdmin(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AdminChatCommands";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmd");
    auto c = wowee::pipeline::WoweeChatCommandsLoader::
        makeAdminCommands(name);
    if (!saveOrError<wowee::pipeline::WoweeChatCommandsLoader>(c, base, "gen-cmd-admin", ".wcmd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcmd");
    if (!wowee::pipeline::WoweeChatCommandsLoader::exists(base)) {
        std::fprintf(stderr, "WCMD not found: %s.wcmd\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeChatCommandsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcmd"] = base + ".wcmd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"cmdId", e.cmdId},
                {"command", e.command},
                {"minSecurityLevel", e.minSecurityLevel},
                {"minSecurityLevelName",
                    securityLevelName(e.minSecurityLevel)},
                {"category", e.category},
                {"categoryName",
                    categoryName(e.category)},
                {"isHidden", e.isHidden != 0},
                {"throttleMs", e.throttleMs},
                {"argSchema", e.argSchema},
                {"helpText", e.helpText},
                {"aliases", e.aliases},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCMD: %s.wcmd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  commands: %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  /command          security    category        hide  throttleMs  aliases  argSchema\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  /%-15s  %-10s  %-13s   %s   %8u   %5zu   %s\n",
                    e.cmdId, e.command.c_str(),
                    securityLevelName(e.minSecurityLevel),
                    categoryName(e.category),
                    e.isHidden ? "Y" : "n",
                    e.throttleMs,
                    e.aliases.size(),
                    e.argSchema.c_str());
    }
    return 0;
}

int parseSecurityLevelToken(const std::string& s) {
    using W = wowee::pipeline::WoweeChatCommands;
    if (s == "player")     return W::Player;
    if (s == "helper")     return W::Helper;
    if (s == "moderator")  return W::Moderator;
    if (s == "gamemaster") return W::GameMaster;
    if (s == "admin")      return W::Admin;
    return -1;
}

int parseCategoryToken(const std::string& s) {
    using W = wowee::pipeline::WoweeChatCommands;
    if (s == "info")          return W::Info;
    if (s == "movement")      return W::Movement;
    if (s == "communication") return W::Communication;
    if (s == "admincmd")      return W::AdminCmd;
    if (s == "debug")         return W::Debug;
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
                    "import-wcmd-json: unknown %s token "
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

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcmd");
    if (!wowee::pipeline::WoweeChatCommandsLoader::exists(base)) {
        return reportMissing("validate-wcmd", "WCMD", base, ".wcmd");
    }
    auto c = wowee::pipeline::WoweeChatCommandsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-name uniqueness: tracks ALL command names
    // (canonical + aliases) since chat parser
    // dispatches uniformly.
    std::set<std::string> allNamesSeen;
    auto addName = [&](const std::string& nm,
                        const std::string& ctx,
                        const std::string& source) {
        if (nm.empty()) return;
        if (!allNamesSeen.insert(nm).second) {
            errors.push_back(ctx +
                ": " + source + " '" + nm +
                "' collides with another command name "
                "or alias — chat parser would dispatch "
                "ambiguously");
        }
        // Lowercase check: chat parser is case-
        // insensitive but storing canonical lowercase
        // is the convention.
        for (char ch : nm) {
            if (ch >= 'A' && ch <= 'Z') {
                warnings.push_back(ctx +
                    ": " + source + " '" + nm +
                    "' contains uppercase — convention "
                    "is canonical lowercase (chat parser "
                    "is case-insensitive)");
                break;
            }
        }
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (cmdId=" + std::to_string(e.cmdId);
        if (!e.command.empty())
            ctx += " /" + e.command;
        ctx += ")";
        if (e.cmdId == 0)
            errors.push_back(ctx + ": cmdId is 0");
        if (e.command.empty())
            errors.push_back(ctx +
                ": command name is empty");
        if (e.minSecurityLevel > 4) {
            errors.push_back(ctx +
                ": minSecurityLevel " +
                std::to_string(e.minSecurityLevel) +
                " out of range (0..4)");
        }
        if (e.category > 4) {
            errors.push_back(ctx + ": category " +
                std::to_string(e.category) +
                " out of range (0..4)");
        }
        if (e.helpText.empty()) {
            warnings.push_back(ctx +
                ": helpText is empty — /help would "
                "show this command without "
                "description");
        }
        // Throttle > 60s is almost certainly a typo
        // (units mismatch — milliseconds vs seconds).
        if (e.throttleMs > 60000) {
            warnings.push_back(ctx +
                ": throttleMs=" +
                std::to_string(e.throttleMs) +
                " exceeds 60000ms (60s) — verify "
                "intentional or check units (ms vs s "
                "typo)");
        }
        // Admin-category command at Player security
        // level is a security hole — warn.
        using W = wowee::pipeline::WoweeChatCommands;
        if (e.category == W::AdminCmd &&
            e.minSecurityLevel <= W::Helper) {
            warnings.push_back(ctx +
                ": Admin category command at security "
                "level " +
                std::to_string(e.minSecurityLevel) +
                " (Player/Helper) — likely security "
                "misconfiguration; admin commands "
                "usually require GameMaster+");
        }
        // Per-name uniqueness check (canonical +
        // aliases share same flat namespace).
        addName(e.command, ctx, "command");
        for (const auto& a : e.aliases) {
            addName(a, ctx, "alias");
            // Self-alias is meaningless (canonical
            // already matches).
            if (a == e.command) {
                warnings.push_back(ctx +
                    ": alias '" + a + "' equals "
                    "canonical command name — "
                    "redundant entry");
            }
        }
        if (!idsSeen.insert(e.cmdId).second) {
            errors.push_back(ctx + ": duplicate cmdId");
        }
    }
    return cli::reportValidation("wcmd", base, jsonOut, errors, warnings,
                                 formatted("%zu commands, all cmdIds + "
                    "names + aliases unique across flat "
                    "namespace, security 0..4, category "
                    "0..4, all command/alias names "
                    "lowercase", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcmd");
    if (out.empty()) out = base + ".wcmd.json";
    if (!wowee::pipeline::WoweeChatCommandsLoader::exists(base)) {
        return reportMissing("export-wcmd-json", "WCMD", base, ".wcmd");
    }
    auto c = wowee::pipeline::WoweeChatCommandsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCMD";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"cmdId", e.cmdId},
            {"command", e.command},
            {"minSecurityLevel", e.minSecurityLevel},
            {"minSecurityLevelName",
                securityLevelName(e.minSecurityLevel)},
            {"category", e.category},
            {"categoryName", categoryName(e.category)},
            {"isHidden", e.isHidden != 0},
            {"throttleMs", e.throttleMs},
            {"argSchema", e.argSchema},
            {"helpText", e.helpText},
            {"aliases", e.aliases},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcmd-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu commands)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcmd");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcmd-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcmd-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeChatCommands c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcmd-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeChatCommands::Entry e;
        e.cmdId = je.value("cmdId", 0u);
        e.command = je.value("command", std::string{});
        if (!readEnumField(je, "minSecurityLevel",
                            "minSecurityLevelName",
                            parseSecurityLevelToken,
                            "minSecurityLevel", e.cmdId,
                            e.minSecurityLevel)) return 1;
        if (!readEnumField(je, "category", "categoryName",
                            parseCategoryToken, "category",
                            e.cmdId, e.category)) return 1;
        e.isHidden = je.value("isHidden", false) ? 1 : 0;
        e.throttleMs = je.value("throttleMs", 0u);
        e.argSchema = je.value("argSchema", std::string{});
        e.helpText = je.value("helpText", std::string{});
        if (je.contains("aliases") &&
            je["aliases"].is_array()) {
            for (const auto& a : je["aliases"]) {
                if (a.is_string()) {
                    e.aliases.push_back(a.get<std::string>());
                }
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeChatCommandsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcmd-json: failed to save %s.wcmd\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcmd (%zu commands)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleChatCommandsCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-cmd-basic") == 0 &&
        i + 1 < argc) {
        outRc = handleGenBasic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmd-movement") == 0 &&
        i + 1 < argc) {
        outRc = handleGenMovement(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmd-admin") == 0 &&
        i + 1 < argc) {
        outRc = handleGenAdmin(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcmd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcmd") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcmd-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcmd-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
