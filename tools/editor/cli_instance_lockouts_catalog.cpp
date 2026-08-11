#include "cli_instance_lockouts_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_instance_lockouts.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
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


void printGenSummary(const wowee::pipeline::WoweeInstanceLockout& c,
                     const std::string& base) {
    std::printf("Wrote %s.whld\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  lockouts : %zu\n", c.entries.size());
}

int handleGenRaidWeekly(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidWeeklyLockouts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".whld");
    auto c = wowee::pipeline::WoweeInstanceLockoutLoader::makeRaidWeekly(name);
    if (!saveOrError<wowee::pipeline::WoweeInstanceLockoutLoader>(c, base, "gen-hld", ".whld")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDungeonDaily(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DungeonDailyLockouts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".whld");
    auto c = wowee::pipeline::WoweeInstanceLockoutLoader::makeDungeonDaily(name);
    if (!saveOrError<wowee::pipeline::WoweeInstanceLockoutLoader>(c, base, "gen-hld-dungeon", ".whld")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWorldEvent(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WorldEventLockouts";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".whld");
    auto c = wowee::pipeline::WoweeInstanceLockoutLoader::makeWorldEvent(name);
    if (!saveOrError<wowee::pipeline::WoweeInstanceLockoutLoader>(c, base, "gen-hld-event", ".whld")) return 1;
    printGenSummary(c, base);
    return 0;
}

void formatInterval(uint32_t ms, char* buf, size_t bufSize) {
    if (ms == 0) {
        std::snprintf(buf, bufSize, "-");
    } else if (ms % 86400000u == 0) {
        std::snprintf(buf, bufSize, "%ud", ms / 86400000u);
    } else if (ms % 3600000u == 0) {
        std::snprintf(buf, bufSize, "%uh", ms / 3600000u);
    } else if (ms % 60000u == 0) {
        std::snprintf(buf, bufSize, "%um", ms / 60000u);
    } else {
        std::snprintf(buf, bufSize, "%ums", ms);
    }
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".whld");
    if (!wowee::pipeline::WoweeInstanceLockoutLoader::exists(base)) {
        std::fprintf(stderr, "WHLD not found: %s.whld\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeInstanceLockoutLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["whld"] = base + ".whld";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"lockoutId", e.lockoutId},
                {"name", e.name},
                {"description", e.description},
                {"mapId", e.mapId},
                {"difficultyId", e.difficultyId},
                {"resetIntervalMs", e.resetIntervalMs},
                {"maxBossKillsPerLockout", e.maxBossKillsPerLockout},
                {"bonusRolls", e.bonusRolls},
                {"raidLockoutKind", e.raidLockoutKind},
                {"raidLockoutKindName", wowee::pipeline::WoweeInstanceLockout::lockoutKindName(e.raidLockoutKind)},
                {"raidGroupSize", e.raidGroupSize},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WHLD: %s.whld\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  lockouts : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    map   diff   kind         interval    bosses  size  bonus  name\n");
    for (const auto& e : c.entries) {
        char intervalBuf[16];
        formatInterval(e.resetIntervalMs, intervalBuf, sizeof(intervalBuf));
        std::printf("  %4u   %4u  %4u   %-10s   %-9s   %3u     %3u   %3u    %s\n",
                    e.lockoutId, e.mapId, e.difficultyId,
                    wowee::pipeline::WoweeInstanceLockout::lockoutKindName(e.raidLockoutKind),
                    intervalBuf,
                    e.maxBossKillsPerLockout, e.raidGroupSize,
                    e.bonusRolls, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".whld");
    if (!wowee::pipeline::WoweeInstanceLockoutLoader::exists(base)) {
        std::fprintf(stderr,
            "export-whld-json: WHLD not found: %s.whld\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeInstanceLockoutLoader::load(base);
    if (outPath.empty()) outPath = base + ".whld.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["lockoutId"] = e.lockoutId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["mapId"] = e.mapId;
        je["difficultyId"] = e.difficultyId;
        je["resetIntervalMs"] = e.resetIntervalMs;
        je["maxBossKillsPerLockout"] = e.maxBossKillsPerLockout;
        je["bonusRolls"] = e.bonusRolls;
        je["raidLockoutKind"] = e.raidLockoutKind;
        je["raidLockoutKindName"] =
            wowee::pipeline::WoweeInstanceLockout::lockoutKindName(e.raidLockoutKind);
        je["raidGroupSize"] = e.raidGroupSize;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-whld-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  lockouts : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseLockoutKindToken(const nlohmann::json& jv,
                              uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeInstanceLockout::Custom)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "daily")        return wowee::pipeline::WoweeInstanceLockout::Daily;
        if (s == "weekly")       return wowee::pipeline::WoweeInstanceLockout::Weekly;
        if (s == "semi-weekly" ||
            s == "semiweekly")   return wowee::pipeline::WoweeInstanceLockout::SemiWeekly;
        if (s == "custom")       return wowee::pipeline::WoweeInstanceLockout::Custom;
    }
    return fallback;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-whld-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-whld-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeInstanceLockout c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeInstanceLockout::Entry e;
            if (je.contains("lockoutId"))      e.lockoutId = je["lockoutId"].get<uint32_t>();
            if (je.contains("name"))           e.name = je["name"].get<std::string>();
            if (je.contains("description"))    e.description = je["description"].get<std::string>();
            if (je.contains("mapId"))          e.mapId = je["mapId"].get<uint32_t>();
            if (je.contains("difficultyId"))   e.difficultyId = je["difficultyId"].get<uint32_t>();
            if (je.contains("resetIntervalMs")) e.resetIntervalMs = je["resetIntervalMs"].get<uint32_t>();
            if (je.contains("maxBossKillsPerLockout")) e.maxBossKillsPerLockout = je["maxBossKillsPerLockout"].get<uint8_t>();
            if (je.contains("bonusRolls"))     e.bonusRolls = je["bonusRolls"].get<uint8_t>();
            uint8_t kind = wowee::pipeline::WoweeInstanceLockout::Weekly;
            if (je.contains("raidLockoutKind"))
                kind = parseLockoutKindToken(je["raidLockoutKind"], kind);
            else if (je.contains("raidLockoutKindName"))
                kind = parseLockoutKindToken(je["raidLockoutKindName"], kind);
            e.raidLockoutKind = kind;
            if (je.contains("raidGroupSize")) e.raidGroupSize = je["raidGroupSize"].get<uint8_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".whld");
    outBase = cli::withoutExt(outBase, ".whld");
    if (!wowee::pipeline::WoweeInstanceLockoutLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-whld-json: failed to save %s.whld\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.whld\n", outBase.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  lockouts : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".whld");
    if (!wowee::pipeline::WoweeInstanceLockoutLoader::exists(base)) {
        return reportMissing("validate-whld", "WHLD", base, ".whld");
    }
    auto c = wowee::pipeline::WoweeInstanceLockoutLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.lockoutId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.lockoutId == 0)
            errors.push_back(ctx + ": lockoutId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.raidLockoutKind > wowee::pipeline::WoweeInstanceLockout::Custom) {
            errors.push_back(ctx + ": raidLockoutKind " +
                std::to_string(e.raidLockoutKind) + " not in 0..3");
        }
        if (e.resetIntervalMs == 0)
            errors.push_back(ctx +
                ": resetIntervalMs is 0 — lockout would never reset");
        // Standard sizes are 5/10/25/40 — anything else is a
        // server-custom raid size.
        if (e.raidGroupSize != 5 && e.raidGroupSize != 10 &&
            e.raidGroupSize != 25 && e.raidGroupSize != 40) {
            warnings.push_back(ctx +
                ": non-standard raidGroupSize " +
                std::to_string(e.raidGroupSize) +
                " (canonical sizes are 5/10/25/40)");
        }
        // Daily kind with non-daily interval is suspicious.
        if (e.raidLockoutKind == wowee::pipeline::WoweeInstanceLockout::Daily &&
            e.resetIntervalMs != wowee::pipeline::WoweeInstanceLockout::kDailyMs) {
            warnings.push_back(ctx +
                ": Daily kind with resetIntervalMs " +
                std::to_string(e.resetIntervalMs) +
                " — canonical Daily is 86400000ms (24h)");
        }
        // Weekly kind with non-weekly interval is suspicious.
        if (e.raidLockoutKind == wowee::pipeline::WoweeInstanceLockout::Weekly &&
            e.resetIntervalMs != wowee::pipeline::WoweeInstanceLockout::kWeeklyMs) {
            warnings.push_back(ctx +
                ": Weekly kind with resetIntervalMs " +
                std::to_string(e.resetIntervalMs) +
                " — canonical Weekly is 604800000ms (7d)");
        }
        if (e.maxBossKillsPerLockout == 0)
            warnings.push_back(ctx +
                ": maxBossKillsPerLockout=0 — instance grants no "
                "lockout-bound kills, every visit is fresh");
        for (uint32_t prev : idsSeen) {
            if (prev == e.lockoutId) {
                errors.push_back(ctx + ": duplicate lockoutId");
                break;
            }
        }
        idsSeen.push_back(e.lockoutId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("whld", base, errors, warnings);
    std::printf("validate-whld: %s.whld\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu lockouts, all lockoutIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleInstanceLockoutsCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-hld") == 0 && i + 1 < argc) {
        outRc = handleGenRaidWeekly(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-hld-dungeon") == 0 && i + 1 < argc) {
        outRc = handleGenDungeonDaily(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-hld-event") == 0 && i + 1 < argc) {
        outRc = handleGenWorldEvent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-whld") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-whld") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-whld-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-whld-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
