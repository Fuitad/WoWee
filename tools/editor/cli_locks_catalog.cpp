#include "cli_locks_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_locks.hpp"
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

void appendLockFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeLock::DestructOnOpen) s += "destruct ";
    if (flags & wowee::pipeline::WoweeLock::RespawnOnKey)   s += "respawn ";
    if (flags & wowee::pipeline::WoweeLock::TrapOnFail)     s += "trap ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}


void printGenSummary(const wowee::pipeline::WoweeLock& c,
                     const std::string& base) {
    std::printf("Wrote %s.wlck\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  locks   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterLocks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlck");
    auto c = wowee::pipeline::WoweeLockLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeLockLoader>(c, base, "gen-locks", ".wlck")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDungeon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DungeonLocks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlck");
    auto c = wowee::pipeline::WoweeLockLoader::makeDungeon(name);
    if (!saveOrError<wowee::pipeline::WoweeLockLoader>(c, base, "gen-locks-dungeon", ".wlck")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenProfessions(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ProfessionLocks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlck");
    auto c = wowee::pipeline::WoweeLockLoader::makeProfessions(name);
    if (!saveOrError<wowee::pipeline::WoweeLockLoader>(c, base, "gen-locks-professions", ".wlck")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlck");
    if (!wowee::pipeline::WoweeLockLoader::exists(base)) {
        return reportMissing("WLCK", base, ".wlck");
    }
    auto c = wowee::pipeline::WoweeLockLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wlck"] = base + ".wlck";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendLockFlagsStr(fs, e.flags);
            nlohmann::json je;
            je["lockId"] = e.lockId;
            je["name"] = e.name;
            je["flags"] = e.flags;
            je["flagsStr"] = fs;
            nlohmann::json chans = nlohmann::json::array();
            for (int k = 0; k < wowee::pipeline::WoweeLock::kChannelSlots; ++k) {
                const auto& ch = e.channels[k];
                chans.push_back({
                    {"slot", k},
                    {"kind", ch.kind},
                    {"kindName", wowee::pipeline::WoweeLock::channelKindName(ch.kind)},
                    {"skillRequired", ch.skillRequired},
                    {"targetId", ch.targetId},
                });
            }
            je["channels"] = chans;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLCK: %s.wlck\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  locks   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::string fs;
        appendLockFlagsStr(fs, e.flags);
        std::printf("\n  lockId=%u  flags=%s  %s\n",
                    e.lockId, fs.c_str(), e.name.c_str());
        for (int k = 0; k < wowee::pipeline::WoweeLock::kChannelSlots; ++k) {
            const auto& ch = e.channels[k];
            if (ch.kind == wowee::pipeline::WoweeLock::ChannelNone) continue;
            std::printf("    slot %d : %-9s  target=%u  skillReq=%u\n",
                        k,
                        wowee::pipeline::WoweeLock::channelKindName(ch.kind),
                        ch.targetId, ch.skillRequired);
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each lock emits scalar fields plus the
    // 5 fixed channel slots; channel.kind emits dual int +
    // name forms.
    return cli::exportCatalogJson<wowee::pipeline::WoweeLockLoader>(
        i, argc, argv, "wlck", "WLCK", "locks  ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendLockFlagsStr(fs, e.flags);
            nlohmann::json je;
            je["lockId"] = e.lockId;
            je["name"] = e.name;
            je["flags"] = e.flags;
            nlohmann::json fa = nlohmann::json::array();
            if (e.flags & wowee::pipeline::WoweeLock::DestructOnOpen) fa.push_back("destruct");
            if (e.flags & wowee::pipeline::WoweeLock::RespawnOnKey)   fa.push_back("respawn");
            if (e.flags & wowee::pipeline::WoweeLock::TrapOnFail)     fa.push_back("trap");
            je["flagsList"] = fa;
            nlohmann::json chans = nlohmann::json::array();
            for (int k = 0; k < wowee::pipeline::WoweeLock::kChannelSlots; ++k) {
                const auto& ch = e.channels[k];
                chans.push_back({
                    {"slot", k},
                    {"kind", ch.kind},
                    {"kindName", wowee::pipeline::WoweeLock::channelKindName(ch.kind)},
                    {"skillRequired", ch.skillRequired},
                    {"targetId", ch.targetId},
                });
            }
            je["channels"] = chans;
            arr.push_back(je);
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wlck");
    outBase = cli::withoutExt(outBase, ".wlck");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wlck-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wlck-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "-" || s.empty()) return wowee::pipeline::WoweeLock::ChannelNone;
        if (s == "item")           return wowee::pipeline::WoweeLock::ChannelItem;
        if (s == "lockpick")       return wowee::pipeline::WoweeLock::ChannelLockpick;
        if (s == "spell")          return wowee::pipeline::WoweeLock::ChannelSpell;
        if (s == "damage")         return wowee::pipeline::WoweeLock::ChannelDamage;
        return wowee::pipeline::WoweeLock::ChannelNone;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "destruct") return wowee::pipeline::WoweeLock::DestructOnOpen;
        if (s == "respawn")  return wowee::pipeline::WoweeLock::RespawnOnKey;
        if (s == "trap")     return wowee::pipeline::WoweeLock::TrapOnFail;
        return 0;
    };
    wowee::pipeline::WoweeLock c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeLock::Entry e;
            e.lockId = je.value("lockId", 0u);
            e.name = je.value("name", std::string{});
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= flagFromName(f.get<std::string>());
                }
            }
            if (je.contains("channels") && je["channels"].is_array()) {
                int slotIdx = 0;
                for (const auto& jc : je["channels"]) {
                    if (slotIdx >= wowee::pipeline::WoweeLock::kChannelSlots) break;
                    auto& ch = e.channels[slotIdx];
                    if (jc.contains("kind") && jc["kind"].is_number_integer()) {
                        ch.kind = static_cast<uint8_t>(jc["kind"].get<int>());
                    } else if (jc.contains("kindName") && jc["kindName"].is_string()) {
                        ch.kind = kindFromName(jc["kindName"].get<std::string>());
                    }
                    ch.skillRequired = static_cast<uint16_t>(jc.value("skillRequired", 0));
                    ch.targetId = jc.value("targetId", 0u);
                    ++slotIdx;
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeLockLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wlck-json: failed to save %s.wlck\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wlck\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  locks  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeLockLoader>(
        i, argc, argv, "wlck", "WLCK",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        idsSeen.reserve(c.entries.size());
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.lockId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.lockId == 0) {
                errors.push_back(ctx + ": lockId is 0");
            }
            // At least one channel must be active or the lock can
            // never be opened.
            bool anyActive = false;
            for (int ci = 0; ci < wowee::pipeline::WoweeLock::kChannelSlots; ++ci) {
                const auto& ch = e.channels[ci];
                if (ch.kind != wowee::pipeline::WoweeLock::ChannelNone) {
                    anyActive = true;
                }
                if (ch.kind > wowee::pipeline::WoweeLock::ChannelDamage) {
                    errors.push_back(ctx + " slot " + std::to_string(ci) +
                        ": kind " + std::to_string(ch.kind) +
                        " not in known range 0..4");
                }
                // Item / Spell / Lockpick channels need a non-zero
                // targetId; Damage channels don't.
                if ((ch.kind == wowee::pipeline::WoweeLock::ChannelItem ||
                     ch.kind == wowee::pipeline::WoweeLock::ChannelSpell ||
                     ch.kind == wowee::pipeline::WoweeLock::ChannelLockpick) &&
                    ch.targetId == 0) {
                    errors.push_back(ctx + " slot " + std::to_string(ci) +
                        ": kind requires non-zero targetId");
                }
                // skillRequired only meaningful for Lockpick - flag
                // odd usage on other kinds.
                if (ch.kind != wowee::pipeline::WoweeLock::ChannelLockpick &&
                    ch.kind != wowee::pipeline::WoweeLock::ChannelNone &&
                    ch.skillRequired != 0) {
                    warnings.push_back(ctx + " slot " + std::to_string(ci) +
                        ": skillRequired set on non-Lockpick channel (ignored at runtime)");
                }
            }
            if (!anyActive) {
                errors.push_back(ctx + ": all 5 channels are None (lock can never open)");
            }
            if (!idsSeen.add(e.lockId)) errors.push_back(ctx + ": duplicate lockId");
        }
            return formatted("%zu locks, all lockIds unique", c.entries.size());
        });
}

} // namespace

bool handleLocksCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-locks") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-locks-dungeon") == 0 && i + 1 < argc) {
        outRc = handleGenDungeon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-locks-professions") == 0 && i + 1 < argc) {
        outRc = handleGenProfessions(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wlck") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wlck") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wlck-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wlck-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
