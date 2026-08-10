#include "cli_spell_durations_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_durations.hpp"
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

bool saveOrError(const wowee::pipeline::WoweeSpellDuration& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeSpellDurationLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wsdr\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeSpellDuration& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsdr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterDurations";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsdr");
    auto c = wowee::pipeline::WoweeSpellDurationLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-sdr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBuffs(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "LongDurationBuffs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsdr");
    auto c = wowee::pipeline::WoweeSpellDurationLoader::makeBuffs(name);
    if (!saveOrError(c, base, "gen-sdr-buffs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDot(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DoTHoTDurations";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsdr");
    auto c = wowee::pipeline::WoweeSpellDurationLoader::makeDot(name);
    if (!saveOrError(c, base, "gen-sdr-dot")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsdr");
    if (!wowee::pipeline::WoweeSpellDurationLoader::exists(base)) {
        std::fprintf(stderr, "WSDR not found: %s.wsdr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellDurationLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsdr"] = base + ".wsdr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"durationId", e.durationId},
                {"name", e.name},
                {"description", e.description},
                {"durationKind", e.durationKind},
                {"durationKindName", wowee::pipeline::WoweeSpellDuration::durationKindName(e.durationKind)},
                {"baseDurationMs", e.baseDurationMs},
                {"perLevelMs", e.perLevelMs},
                {"maxDurationMs", e.maxDurationMs},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSDR: %s.wsdr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind             baseMs   perLvl     maxMs  color       name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-15s %8d %8d %9d  0x%08x  %s\n",
                    e.durationId,
                    wowee::pipeline::WoweeSpellDuration::durationKindName(e.durationKind),
                    e.baseDurationMs, e.perLevelMs,
                    e.maxDurationMs,
                    e.iconColorRGBA, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wsdr");
    if (!wowee::pipeline::WoweeSpellDurationLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wsdr-json: WSDR not found: %s.wsdr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellDurationLoader::load(base);
    if (outPath.empty()) outPath = base + ".wsdr.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["durationId"] = e.durationId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["durationKind"] = e.durationKind;
        je["durationKindName"] =
            wowee::pipeline::WoweeSpellDuration::durationKindName(e.durationKind);
        je["baseDurationMs"] = e.baseDurationMs;
        je["perLevelMs"] = e.perLevelMs;
        je["maxDurationMs"] = e.maxDurationMs;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wsdr-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseDurationKindToken(const nlohmann::json& jv,
                               uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellDuration::UntilDeath)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "instant")          return wowee::pipeline::WoweeSpellDuration::Instant;
        if (s == "timed")            return wowee::pipeline::WoweeSpellDuration::Timed;
        if (s == "tick" ||
            s == "tickbased")        return wowee::pipeline::WoweeSpellDuration::TickBased;
        if (s == "until-cancelled" ||
            s == "untilcancelled")   return wowee::pipeline::WoweeSpellDuration::UntilCancelled;
        if (s == "until-death" ||
            s == "untildeath")       return wowee::pipeline::WoweeSpellDuration::UntilDeath;
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
            "import-wsdr-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wsdr-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellDuration c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellDuration::Entry e;
            if (je.contains("durationId"))  e.durationId = je["durationId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeSpellDuration::Timed;
            if (je.contains("durationKind"))
                kind = parseDurationKindToken(je["durationKind"], kind);
            else if (je.contains("durationKindName"))
                kind = parseDurationKindToken(je["durationKindName"], kind);
            e.durationKind = kind;
            if (je.contains("baseDurationMs")) e.baseDurationMs = je["baseDurationMs"].get<int32_t>();
            if (je.contains("perLevelMs"))     e.perLevelMs = je["perLevelMs"].get<int32_t>();
            if (je.contains("maxDurationMs"))  e.maxDurationMs = je["maxDurationMs"].get<int32_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wsdr");
    outBase = cli::withoutExt(outBase, ".wsdr");
    if (!wowee::pipeline::WoweeSpellDurationLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsdr-json: failed to save %s.wsdr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsdr\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsdr");
    if (!wowee::pipeline::WoweeSpellDurationLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wsdr: WSDR not found: %s.wsdr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellDurationLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.durationId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.durationId == 0)
            errors.push_back(ctx + ": durationId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.durationKind > wowee::pipeline::WoweeSpellDuration::UntilDeath) {
            errors.push_back(ctx + ": durationKind " +
                std::to_string(e.durationKind) + " not in 0..4");
        }
        if (e.maxDurationMs < 0)
            errors.push_back(ctx + ": maxDurationMs < 0");
        if (e.perLevelMs < 0)
            warnings.push_back(ctx +
                ": perLevelMs < 0 — duration shrinks with "
                "level, double-check this is intentional");
        // Instant kind should have base == 0.
        if (e.durationKind == wowee::pipeline::WoweeSpellDuration::Instant &&
            e.baseDurationMs != 0) {
            warnings.push_back(ctx +
                ": Instant kind with baseDurationMs=" +
                std::to_string(e.baseDurationMs) +
                " — engine will track it as a timed aura");
        }
        // UntilCancelled / UntilDeath should signal "no
        // timer" via baseDurationMs<0; otherwise the engine
        // would tick down to expiry.
        if ((e.durationKind == wowee::pipeline::WoweeSpellDuration::UntilCancelled ||
             e.durationKind == wowee::pipeline::WoweeSpellDuration::UntilDeath) &&
            e.baseDurationMs >= 0) {
            warnings.push_back(ctx +
                ": permanent kind with non-negative "
                "baseDurationMs — engine treats this as timed; "
                "set baseDurationMs=-1 to flag as no-timer");
        }
        // Timed/TickBased should have base > 0.
        if ((e.durationKind == wowee::pipeline::WoweeSpellDuration::Timed ||
             e.durationKind == wowee::pipeline::WoweeSpellDuration::TickBased) &&
            e.baseDurationMs <= 0) {
            errors.push_back(ctx +
                ": Timed/TickBased kind requires "
                "baseDurationMs > 0");
        }
        // maxDurationMs<base is contradictory.
        if (e.maxDurationMs > 0 && e.baseDurationMs > e.maxDurationMs) {
            errors.push_back(ctx + ": baseDurationMs " +
                std::to_string(e.baseDurationMs) +
                " > maxDurationMs " +
                std::to_string(e.maxDurationMs));
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.durationId) {
                errors.push_back(ctx + ": duplicate durationId");
                break;
            }
        }
        idsSeen.push_back(e.durationId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wsdr", base, errors, warnings);
    std::printf("validate-wsdr: %s.wsdr\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu buckets, all durationIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellDurationsCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-sdr") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sdr-buffs") == 0 && i + 1 < argc) {
        outRc = handleGenBuffs(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sdr-dot") == 0 && i + 1 < argc) {
        outRc = handleGenDot(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsdr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsdr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsdr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsdr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
