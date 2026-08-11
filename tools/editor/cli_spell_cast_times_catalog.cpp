#include "cli_spell_cast_times_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_cast_times.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeSpellCastTime& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsct\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCastTimes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsct");
    auto c = wowee::pipeline::WoweeSpellCastTimeLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellCastTimeLoader>(c, base, "gen-sct", ".wsct")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenChannel(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ChannelCastTimes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsct");
    auto c = wowee::pipeline::WoweeSpellCastTimeLoader::makeChannel(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellCastTimeLoader>(c, base, "gen-sct-channel", ".wsct")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRamp(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "LevelScaledCastTimes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wsct");
    auto c = wowee::pipeline::WoweeSpellCastTimeLoader::makeRamp(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellCastTimeLoader>(c, base, "gen-sct-ramp", ".wsct")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsct");
    if (!wowee::pipeline::WoweeSpellCastTimeLoader::exists(base)) {
        return reportMissing("WSCT", base, ".wsct");
    }
    auto c = wowee::pipeline::WoweeSpellCastTimeLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsct"] = base + ".wsct";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"castTimeId", e.castTimeId},
                {"name", e.name},
                {"description", e.description},
                {"castKind", e.castKind},
                {"castKindName", wowee::pipeline::WoweeSpellCastTime::castKindName(e.castKind)},
                {"baseCastMs", e.baseCastMs},
                {"perLevelMs", e.perLevelMs},
                {"minCastMs", e.minCastMs},
                {"maxCastMs", e.maxCastMs},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSCT: %s.wsct\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind       baseMs perLvl  minMs   maxMs   color       name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-8s   %6d %6d %6d %7d  0x%08x  %s\n",
                    e.castTimeId,
                    wowee::pipeline::WoweeSpellCastTime::castKindName(e.castKind),
                    e.baseCastMs, e.perLevelMs,
                    e.minCastMs, e.maxCastMs,
                    e.iconColorRGBA, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wsct");
    if (!wowee::pipeline::WoweeSpellCastTimeLoader::exists(base)) {
        return reportMissing("export-wsct-json", "WSCT", base, ".wsct");
    }
    auto c = wowee::pipeline::WoweeSpellCastTimeLoader::load(base);
    if (outPath.empty()) outPath = base + ".wsct.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["castTimeId"] = e.castTimeId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["castKind"] = e.castKind;
        je["castKindName"] =
            wowee::pipeline::WoweeSpellCastTime::castKindName(e.castKind);
        je["baseCastMs"] = e.baseCastMs;
        je["perLevelMs"] = e.perLevelMs;
        je["minCastMs"] = e.minCastMs;
        je["maxCastMs"] = e.maxCastMs;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wsct-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseCastKindToken(const nlohmann::json& jv,
                           uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellCastTime::ChargeCast)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "instant")     return wowee::pipeline::WoweeSpellCastTime::Instant;
        if (s == "cast")        return wowee::pipeline::WoweeSpellCastTime::Cast;
        if (s == "channel")     return wowee::pipeline::WoweeSpellCastTime::Channel;
        if (s == "delayed" ||
            s == "delayedcast") return wowee::pipeline::WoweeSpellCastTime::DelayedCast;
        if (s == "charge" ||
            s == "chargecast")  return wowee::pipeline::WoweeSpellCastTime::ChargeCast;
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
            "import-wsct-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wsct-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellCastTime c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellCastTime::Entry e;
            if (je.contains("castTimeId"))  e.castTimeId = je["castTimeId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeSpellCastTime::Cast;
            if (je.contains("castKind"))
                kind = parseCastKindToken(je["castKind"], kind);
            else if (je.contains("castKindName"))
                kind = parseCastKindToken(je["castKindName"], kind);
            e.castKind = kind;
            if (je.contains("baseCastMs"))   e.baseCastMs = je["baseCastMs"].get<int32_t>();
            if (je.contains("perLevelMs"))   e.perLevelMs = je["perLevelMs"].get<int32_t>();
            if (je.contains("minCastMs"))    e.minCastMs = je["minCastMs"].get<int32_t>();
            if (je.contains("maxCastMs"))    e.maxCastMs = je["maxCastMs"].get<int32_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wsct");
    outBase = cli::withoutExt(outBase, ".wsct");
    if (!wowee::pipeline::WoweeSpellCastTimeLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsct-json: failed to save %s.wsct\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsct\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeSpellCastTimeLoader>(
        i, argc, argv, "wsct", "WSCT",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.castTimeId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.castTimeId == 0)
                errors.push_back(ctx + ": castTimeId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.castKind > wowee::pipeline::WoweeSpellCastTime::ChargeCast) {
                errors.push_back(ctx + ": castKind " +
                    std::to_string(e.castKind) + " not in 0..4");
            }
            if (e.baseCastMs < 0)
                errors.push_back(ctx + ": baseCastMs < 0");
            if (e.perLevelMs < 0)
                warnings.push_back(ctx +
                    ": perLevelMs < 0 — cast time shrinks with "
                    "level, double-check this is intentional");
            if (e.maxCastMs > 0 && e.minCastMs > e.maxCastMs) {
                errors.push_back(ctx + ": minCastMs " +
                    std::to_string(e.minCastMs) +
                    " > maxCastMs " + std::to_string(e.maxCastMs));
            }
            // Instant kind should have base == 0 — otherwise the
            // engine would still display a cast bar.
            if (e.castKind == wowee::pipeline::WoweeSpellCastTime::Instant &&
                e.baseCastMs != 0) {
                warnings.push_back(ctx +
                    ": Instant kind with baseCastMs=" +
                    std::to_string(e.baseCastMs) +
                    " — engine will draw a cast bar (use Cast "
                    "kind if that's intended)");
            }
            // Channel kind should have base > 0 — otherwise it
            // would tick once and immediately end.
            if (e.castKind == wowee::pipeline::WoweeSpellCastTime::Channel &&
                e.baseCastMs <= 0) {
                errors.push_back(ctx +
                    ": Channel kind requires baseCastMs > 0 "
                    "(channel duration)");
            }
            if (!idsSeen.add(e.castTimeId)) errors.push_back(ctx + ": duplicate castTimeId");
        }
            return formatted("%zu buckets, all castTimeIds unique, all min<=max", c.entries.size());
        });
}

} // namespace

bool handleSpellCastTimesCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-sct") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sct-channel") == 0 && i + 1 < argc) {
        outRc = handleGenChannel(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sct-ramp") == 0 && i + 1 < argc) {
        outRc = handleGenRamp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsct") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsct") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsct-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsct-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
