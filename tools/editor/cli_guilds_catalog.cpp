#include "cli_guilds_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_guilds.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeGuild& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgld\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  guilds  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterGuilds";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgld");
    auto c = wowee::pipeline::WoweeGuildLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeGuildLoader>(c, base, "gen-guilds", ".wgld")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFull(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FullGuild";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgld");
    auto c = wowee::pipeline::WoweeGuildLoader::makeFull(name);
    if (!saveOrError<wowee::pipeline::WoweeGuildLoader>(c, base, "gen-guilds-full", ".wgld")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFactionPair(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FactionPair";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgld");
    auto c = wowee::pipeline::WoweeGuildLoader::makeFactionPair(name);
    if (!saveOrError<wowee::pipeline::WoweeGuildLoader>(c, base, "gen-guilds-pair", ".wgld")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgld");
    if (!wowee::pipeline::WoweeGuildLoader::exists(base)) {
        std::fprintf(stderr, "WGLD not found: %s.wgld\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGuildLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgld"] = base + ".wgld";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json je;
            je["guildId"] = e.guildId;
            je["name"] = e.name;
            je["leaderName"] = e.leaderName;
            je["motd"] = e.motd;
            je["info"] = e.info;
            je["creationDate"] = e.creationDate;
            je["experience"] = e.experience;
            je["level"] = e.level;
            je["factionId"] = e.factionId;
            je["factionName"] = wowee::pipeline::WoweeGuild::factionName(e.factionId);
            je["bankCopper"] = e.bankCopper;
            je["emblem"] = e.emblem;
            nlohmann::json ranks = nlohmann::json::array();
            for (const auto& r : e.ranks) {
                ranks.push_back({
                    {"rankIndex", r.rankIndex},
                    {"name", r.name},
                    {"permissionsMask", r.permissionsMask},
                    {"moneyPerDayCopper", r.moneyPerDayCopper},
                });
            }
            je["ranks"] = ranks;
            nlohmann::json members = nlohmann::json::array();
            for (const auto& m : e.members) {
                members.push_back({
                    {"characterName", m.characterName},
                    {"rankIndex", m.rankIndex},
                    {"joinedDate", m.joinedDate},
                    {"publicNote", m.publicNote},
                    {"officerNote", m.officerNote},
                });
            }
            je["members"] = members;
            nlohmann::json tabs = nlohmann::json::array();
            for (const auto& t : e.bankTabs) {
                tabs.push_back({
                    {"tabIndex", t.tabIndex},
                    {"name", t.name},
                    {"iconPath", t.iconPath},
                    {"depositPermissionMask", t.depositPermissionMask},
                    {"withdrawPermissionMask", t.withdrawPermissionMask},
                    {"viewPermissionMask", t.viewPermissionMask},
                });
            }
            je["bankTabs"] = tabs;
            nlohmann::json perks = nlohmann::json::array();
            for (const auto& p : e.perks) {
                perks.push_back({
                    {"perkId", p.perkId},
                    {"name", p.name},
                    {"spellId", p.spellId},
                    {"requiredGuildLevel", p.requiredGuildLevel},
                });
            }
            je["perks"] = perks;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGLD: %s.wgld\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  guilds  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::printf("\n  guildId=%u  faction=%s  level=%u  bank=%uc\n",
                    e.guildId,
                    wowee::pipeline::WoweeGuild::factionName(e.factionId),
                    e.level, e.bankCopper);
        std::printf("    name      : %s\n", e.name.c_str());
        std::printf("    leader    : %s\n", e.leaderName.c_str());
        if (!e.motd.empty()) {
            std::printf("    motd      : %s\n", e.motd.c_str());
        }
        std::printf("    ranks=%zu  members=%zu  tabs=%zu  perks=%zu\n",
                    e.ranks.size(), e.members.size(),
                    e.bankTabs.size(), e.perks.size());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each guild emits header scalars plus the
    // ranks / members / bankTabs / perks sub-arrays.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wgld");
    if (outPath.empty()) outPath = base + ".wgld.json";
    if (!wowee::pipeline::WoweeGuildLoader::exists(base)) {
        return reportMissing("export-wgld-json", "WGLD", base, ".wgld");
    }
    auto c = wowee::pipeline::WoweeGuildLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["guildId"] = e.guildId;
        je["name"] = e.name;
        je["leaderName"] = e.leaderName;
        je["motd"] = e.motd;
        je["info"] = e.info;
        je["creationDate"] = e.creationDate;
        je["experience"] = e.experience;
        je["level"] = e.level;
        je["factionId"] = e.factionId;
        je["factionName"] = wowee::pipeline::WoweeGuild::factionName(e.factionId);
        je["bankCopper"] = e.bankCopper;
        je["emblem"] = e.emblem;
        nlohmann::json ranks = nlohmann::json::array();
        for (const auto& r : e.ranks) {
            ranks.push_back({
                {"rankIndex", r.rankIndex},
                {"name", r.name},
                {"permissionsMask", r.permissionsMask},
                {"moneyPerDayCopper", r.moneyPerDayCopper},
            });
        }
        je["ranks"] = ranks;
        nlohmann::json members = nlohmann::json::array();
        for (const auto& m : e.members) {
            members.push_back({
                {"characterName", m.characterName},
                {"rankIndex", m.rankIndex},
                {"joinedDate", m.joinedDate},
                {"publicNote", m.publicNote},
                {"officerNote", m.officerNote},
            });
        }
        je["members"] = members;
        nlohmann::json tabs = nlohmann::json::array();
        for (const auto& t : e.bankTabs) {
            tabs.push_back({
                {"tabIndex", t.tabIndex},
                {"name", t.name},
                {"iconPath", t.iconPath},
                {"depositPermissionMask", t.depositPermissionMask},
                {"withdrawPermissionMask", t.withdrawPermissionMask},
                {"viewPermissionMask", t.viewPermissionMask},
            });
        }
        je["bankTabs"] = tabs;
        nlohmann::json perks = nlohmann::json::array();
        for (const auto& p : e.perks) {
            perks.push_back({
                {"perkId", p.perkId},
                {"name", p.name},
                {"spellId", p.spellId},
                {"requiredGuildLevel", p.requiredGuildLevel},
            });
        }
        je["perks"] = perks;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wgld-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wgld\n", base.c_str());
    std::printf("  guilds : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wgld");
    outBase = cli::withoutExt(outBase, ".wgld");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wgld-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wgld-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "alliance") return wowee::pipeline::WoweeGuild::Alliance;
        if (s == "horde")    return wowee::pipeline::WoweeGuild::Horde;
        return wowee::pipeline::WoweeGuild::Alliance;
    };
    wowee::pipeline::WoweeGuild c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeGuild::Entry e;
            e.guildId = je.value("guildId", 0u);
            e.name = je.value("name", std::string{});
            e.leaderName = je.value("leaderName", std::string{});
            e.motd = je.value("motd", std::string{});
            e.info = je.value("info", std::string{});
            e.creationDate = je.value("creationDate",
                static_cast<uint64_t>(0));
            e.experience = je.value("experience",
                static_cast<uint64_t>(0));
            e.level = static_cast<uint16_t>(je.value("level", 1));
            if (je.contains("factionId") && je["factionId"].is_number_integer()) {
                e.factionId = static_cast<uint8_t>(je["factionId"].get<int>());
            } else if (je.contains("factionName") &&
                       je["factionName"].is_string()) {
                e.factionId = factionFromName(je["factionName"].get<std::string>());
            }
            e.bankCopper = je.value("bankCopper", 0u);
            e.emblem = je.value("emblem", 0u);
            if (je.contains("ranks") && je["ranks"].is_array()) {
                for (const auto& jr : je["ranks"]) {
                    wowee::pipeline::WoweeGuild::Rank r;
                    r.rankIndex = static_cast<uint8_t>(jr.value("rankIndex", 0));
                    r.name = jr.value("name", std::string{});
                    r.permissionsMask = jr.value("permissionsMask", 0u);
                    r.moneyPerDayCopper = jr.value("moneyPerDayCopper", 0u);
                    e.ranks.push_back(r);
                }
            }
            if (je.contains("members") && je["members"].is_array()) {
                for (const auto& jm : je["members"]) {
                    wowee::pipeline::WoweeGuild::Member m;
                    m.characterName = jm.value("characterName", std::string{});
                    m.rankIndex = static_cast<uint8_t>(jm.value("rankIndex", 0));
                    m.joinedDate = jm.value("joinedDate",
                        static_cast<uint64_t>(0));
                    m.publicNote = jm.value("publicNote", std::string{});
                    m.officerNote = jm.value("officerNote", std::string{});
                    e.members.push_back(m);
                }
            }
            if (je.contains("bankTabs") && je["bankTabs"].is_array()) {
                for (const auto& jt : je["bankTabs"]) {
                    wowee::pipeline::WoweeGuild::BankTab t;
                    t.tabIndex = static_cast<uint8_t>(jt.value("tabIndex", 0));
                    t.name = jt.value("name", std::string{});
                    t.iconPath = jt.value("iconPath", std::string{});
                    t.depositPermissionMask = jt.value("depositPermissionMask", 0u);
                    t.withdrawPermissionMask = jt.value("withdrawPermissionMask", 0u);
                    t.viewPermissionMask = jt.value("viewPermissionMask", 0u);
                    e.bankTabs.push_back(t);
                }
            }
            if (je.contains("perks") && je["perks"].is_array()) {
                for (const auto& jp : je["perks"]) {
                    wowee::pipeline::WoweeGuild::Perk p;
                    p.perkId = jp.value("perkId", 0u);
                    p.name = jp.value("name", std::string{});
                    p.spellId = jp.value("spellId", 0u);
                    p.requiredGuildLevel = static_cast<uint16_t>(
                        jp.value("requiredGuildLevel", 1));
                    e.perks.push_back(p);
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeGuildLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgld-json: failed to save %s.wgld\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgld\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  guilds : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgld");
    if (!wowee::pipeline::WoweeGuildLoader::exists(base)) {
        return reportMissing("validate-wgld", "WGLD", base, ".wgld");
    }
    auto c = wowee::pipeline::WoweeGuildLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "guild " + std::to_string(k) +
                          " (id=" + std::to_string(e.guildId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.guildId == 0) errors.push_back(ctx + ": guildId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.leaderName.empty()) {
            errors.push_back(ctx + ": leaderName is empty");
        }
        if (e.factionId > wowee::pipeline::WoweeGuild::Horde) {
            errors.push_back(ctx + ": factionId " +
                std::to_string(e.factionId) + " not in 0..1");
        }
        if (e.ranks.empty()) {
            errors.push_back(ctx + ": no ranks (cannot have any members)");
        }
        // Validate that each member's rankIndex resolves.
        uint8_t maxRankIdx = 0;
        for (const auto& r : e.ranks) {
            if (r.rankIndex > maxRankIdx) maxRankIdx = r.rankIndex;
        }
        for (size_t mi = 0; mi < e.members.size(); ++mi) {
            const auto& m = e.members[mi];
            if (m.rankIndex > maxRankIdx) {
                errors.push_back(ctx + " member " + std::to_string(mi) +
                    " (" + m.characterName + "): rankIndex " +
                    std::to_string(m.rankIndex) +
                    " exceeds highest defined rank " +
                    std::to_string(maxRankIdx));
            }
            if (m.characterName.empty()) {
                errors.push_back(ctx + " member " + std::to_string(mi) +
                    ": characterName is empty");
            }
        }
        // Bank tab indices should be unique.
        std::vector<uint8_t> tabIdxSeen;
        for (const auto& t : e.bankTabs) {
            for (uint8_t prev : tabIdxSeen) {
                if (prev == t.tabIndex) {
                    errors.push_back(ctx +
                        ": duplicate bank tabIndex " +
                        std::to_string(t.tabIndex));
                    break;
                }
            }
            tabIdxSeen.push_back(t.tabIndex);
        }
        // Perks should reference a non-zero spellId.
        for (size_t pi = 0; pi < e.perks.size(); ++pi) {
            const auto& p = e.perks[pi];
            if (p.spellId == 0) {
                warnings.push_back(ctx + " perk " + std::to_string(pi) +
                    " (" + p.name + "): spellId is 0 (perk does nothing)");
            }
        }
        if (!idsSeen.add(e.guildId)) errors.push_back(ctx + ": duplicate guildId");
    }
    return cli::reportValidation("wgld", base, jsonOut, errors, warnings,
                                 formatted("%zu guilds, all guildIds unique", c.entries.size()));
}

} // namespace

bool handleGuildsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-guilds") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-guilds-full") == 0 && i + 1 < argc) {
        outRc = handleGenFull(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-guilds-pair") == 0 && i + 1 < argc) {
        outRc = handleGenFactionPair(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgld") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgld") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgld-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgld-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
