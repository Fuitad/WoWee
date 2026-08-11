#include "cli_loot_modes_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_loot_modes.hpp"
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

const char* modeKindName(uint8_t k) {
    using L = wowee::pipeline::WoweeLootModes;
    switch (k) {
        case L::FreeForAll:      return "freeforall";
        case L::RoundRobin:      return "roundrobin";
        case L::MasterLoot:      return "masterloot";
        case L::NeedBeforeGreed: return "needbeforegreed";
        case L::Personal:        return "personal";
        case L::Disenchant:      return "disenchant";
        default:                 return "unknown";
    }
}

const char* qualityName(uint8_t q) {
    switch (q) {
        case 0: return "Poor";
        case 1: return "Common";
        case 2: return "Uncommon";
        case 3: return "Rare";
        case 4: return "Epic";
        case 5: return "Legendary";
        case 6: return "Artifact";
        case 7: return "Heirloom";
        default: return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeLootModes& c,
                     const std::string& base) {
    std::printf("Wrote %s.wlma\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  modes   : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardLootModes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlma");
    auto c = wowee::pipeline::WoweeLootModesLoader::makeStandard(name);
    if (!saveOrError<wowee::pipeline::WoweeLootModesLoader>(c, base, "gen-lma", ".wlma")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidLootPolicies";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlma");
    auto c = wowee::pipeline::WoweeLootModesLoader::makeRaidPolicies(name);
    if (!saveOrError<wowee::pipeline::WoweeLootModesLoader>(c, base, "gen-lma-raid", ".wlma")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAFK(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AFKPreventionLootModes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlma");
    auto c = wowee::pipeline::WoweeLootModesLoader::makeAFKPrevention(name);
    if (!saveOrError<wowee::pipeline::WoweeLootModesLoader>(c, base, "gen-lma-afk", ".wlma")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlma");
    if (!wowee::pipeline::WoweeLootModesLoader::exists(base)) {
        std::fprintf(stderr, "WLMA not found: %s.wlma\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLootModesLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wlma"] = base + ".wlma";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"modeId", e.modeId},
                {"name", e.name},
                {"description", e.description},
                {"modeKind", e.modeKind},
                {"modeKindName", modeKindName(e.modeKind)},
                {"thresholdQuality", e.thresholdQuality},
                {"thresholdQualityName",
                    qualityName(e.thresholdQuality)},
                {"masterLooterRequired",
                    e.masterLooterRequired != 0},
                {"idleSkipSec", e.idleSkipSec},
                {"timeoutFallbackKind", e.timeoutFallbackKind},
                {"timeoutFallbackKindName",
                    modeKindName(e.timeoutFallbackKind)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLMA: %s.wlma\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  modes   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind             threshold     ML   idleSkip  fallback         name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-15s   %-10s    %s    %3us     %-15s   %s\n",
                    e.modeId, modeKindName(e.modeKind),
                    qualityName(e.thresholdQuality),
                    e.masterLooterRequired ? "Y" : "n",
                    e.idleSkipSec,
                    modeKindName(e.timeoutFallbackKind),
                    e.name.c_str());
    }
    return 0;
}

int parseModeKindToken(const std::string& s) {
    using L = wowee::pipeline::WoweeLootModes;
    if (s == "freeforall")      return L::FreeForAll;
    if (s == "roundrobin")      return L::RoundRobin;
    if (s == "masterloot")      return L::MasterLoot;
    if (s == "needbeforegreed") return L::NeedBeforeGreed;
    if (s == "personal")        return L::Personal;
    if (s == "disenchant")      return L::Disenchant;
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
                    "import-wlma-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wlma");
    if (out.empty()) out = base + ".wlma.json";
    if (!wowee::pipeline::WoweeLootModesLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wlma-json: WLMA not found: %s.wlma\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLootModesLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WLMA";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"modeId", e.modeId},
            {"name", e.name},
            {"description", e.description},
            {"modeKind", e.modeKind},
            {"modeKindName", modeKindName(e.modeKind)},
            {"thresholdQuality", e.thresholdQuality},
            {"thresholdQualityName",
                qualityName(e.thresholdQuality)},
            {"masterLooterRequired",
                e.masterLooterRequired != 0},
            {"idleSkipSec", e.idleSkipSec},
            {"timeoutFallbackKind", e.timeoutFallbackKind},
            {"timeoutFallbackKindName",
                modeKindName(e.timeoutFallbackKind)},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wlma-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu modes)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wlma");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wlma-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wlma-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeLootModes c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wlma-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeLootModes::Entry e;
        e.modeId = je.value("modeId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (!readEnumField(je, "modeKind", "modeKindName",
                            parseModeKindToken, "modeKind",
                            e.modeId, e.modeKind)) return 1;
        e.thresholdQuality = static_cast<uint8_t>(
            je.value("thresholdQuality", 2u));
        if (je.contains("masterLooterRequired")) {
            const auto& v = je["masterLooterRequired"];
            if (v.is_boolean())
                e.masterLooterRequired = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.masterLooterRequired = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        e.idleSkipSec = static_cast<uint8_t>(
            je.value("idleSkipSec", 0u));
        if (!readEnumField(je, "timeoutFallbackKind",
                            "timeoutFallbackKindName",
                            parseModeKindToken,
                            "timeoutFallbackKind",
                            e.modeId,
                            e.timeoutFallbackKind)) return 1;
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeLootModesLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wlma-json: failed to save %s.wlma\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wlma (%zu modes)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlma");
    if (!wowee::pipeline::WoweeLootModesLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wlma: WLMA not found: %s.wlma\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLootModesLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.modeId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.modeId == 0)
            errors.push_back(ctx + ": modeId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.modeKind > 5) {
            errors.push_back(ctx + ": modeKind " +
                std::to_string(e.modeKind) +
                " out of range (must be 0..5)");
        }
        if (e.thresholdQuality > 7) {
            errors.push_back(ctx + ": thresholdQuality " +
                std::to_string(e.thresholdQuality) +
                " out of range (must be 0..7 — Poor "
                "through Heirloom)");
        }
        if (e.timeoutFallbackKind > 5) {
            errors.push_back(ctx + ": timeoutFallbackKind " +
                std::to_string(e.timeoutFallbackKind) +
                " out of range (must be 0..5)");
        }
        // Per-kind validity: MasterLoot kind REQUIRES
        // masterLooterRequired=1 (else the policy is
        // self-contradicting — saying use Master Loot
        // without requiring a master looter).
        using L = wowee::pipeline::WoweeLootModes;
        if (e.modeKind == L::MasterLoot &&
            e.masterLooterRequired == 0) {
            errors.push_back(ctx +
                ": MasterLoot kind with master"
                "LooterRequired=0 — policy contradicts "
                "itself (Master Loot mode without "
                "requiring a master looter)");
        }
        // Personal kind doesn't use master-looter
        // semantics — flag as warning if set.
        if (e.modeKind == L::Personal &&
            e.masterLooterRequired != 0) {
            warnings.push_back(ctx +
                ": Personal kind with master"
                "LooterRequired=1 — Personal Loot "
                "doesn't use master-looter semantics; "
                "the flag is meaningless");
        }
        // Fallback to self is meaningful only for kinds
        // with a "leader" concept that can disconnect
        // (MasterLoot). Other kinds (FFA, Personal,
        // RoundRobin, NBG) have no leader-timeout
        // scenario so the fallback field is unused —
        // setting it to self is the conventional default
        // and not a bug.
        if (e.modeKind == L::MasterLoot &&
            e.timeoutFallbackKind == e.modeKind) {
            warnings.push_back(ctx +
                ": MasterLoot timeoutFallbackKind equals "
                "modeKind — if the master looter "
                "disconnects, the fallback would just "
                "wait for them to reconnect (no policy "
                "change). Common alternatives: Need"
                "BeforeGreed for democratic recovery, "
                "FreeForAll for fast unblocking.");
        }
        if (!idsSeen.insert(e.modeId).second) {
            errors.push_back(ctx + ": duplicate modeId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wlma", base, errors, warnings);
    std::printf("validate-wlma: %s.wlma\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu modes, all modeIds unique, "
                    "per-kind constraints satisfied\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleLootModesCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-lma") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lma-raid") == 0 && i + 1 < argc) {
        outRc = handleGenRaid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lma-afk") == 0 && i + 1 < argc) {
        outRc = handleGenAFK(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wlma") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wlma") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wlma-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wlma-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
