#include "cli_sky_params_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_sky_params.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeSkyParams& c,
                     const std::string& base) {
    std::printf("Wrote %s.wskp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  keyframes: %zu\n", c.entries.size());
}

int handleGenStormwind(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StormwindSkyDay";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wskp");
    auto c = wowee::pipeline::WoweeSkyParamsLoader::makeStormwindDay(name);
    if (!saveOrError<wowee::pipeline::WoweeSkyParamsLoader>(c, base, "gen-skp", ".wskp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenArctic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "NorthrendArcticSky";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wskp");
    auto c = wowee::pipeline::WoweeSkyParamsLoader::makeNorthrendArctic(name);
    if (!saveOrError<wowee::pipeline::WoweeSkyParamsLoader>(c, base, "gen-skp-arctic", ".wskp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHellfire(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "OutlandHellfireSky";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wskp");
    auto c = wowee::pipeline::WoweeSkyParamsLoader::makeOutlandHellfire(name);
    if (!saveOrError<wowee::pipeline::WoweeSkyParamsLoader>(c, base, "gen-skp-hellfire", ".wskp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wskp");
    if (!wowee::pipeline::WoweeSkyParamsLoader::exists(base)) {
        return reportMissing("WSKP", base, ".wskp");
    }
    auto c = wowee::pipeline::WoweeSkyParamsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wskp"] = base + ".wskp";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"skyId", e.skyId},
                {"name", e.name},
                {"description", e.description},
                {"mapId", e.mapId},
                {"areaId", e.areaId},
                {"timeOfDayHour", e.timeOfDayHour},
                {"zenithColor", e.zenithColor},
                {"horizonColor", e.horizonColor},
                {"sunColor", e.sunColor},
                {"sunAngleDeg", e.sunAngleDeg},
                {"fogStartYards", e.fogStartYards},
                {"fogEndYards", e.fogEndYards},
                {"cloudOpacity", e.cloudOpacity},
                {"cloudSpeedX10", e.cloudSpeedX10},
                {"cloudSpeedMph", e.cloudSpeedX10 / 10.0},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSKP: %s.wskp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  keyframes: %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   map   area   hr   sunAngle  fog(s..e)yd   cloud%%  windMph   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %4u   %4u   %2u    %5.1f    %4.0f..%4.0f    %3u%%   %4.1f      %s\n",
                    e.skyId, e.mapId, e.areaId,
                    e.timeOfDayHour, e.sunAngleDeg,
                    e.fogStartYards, e.fogEndYards,
                    (e.cloudOpacity * 100) / 255,
                    e.cloudSpeedX10 / 10.0,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wskp");
    if (out.empty()) out = base + ".wskp.json";
    if (!wowee::pipeline::WoweeSkyParamsLoader::exists(base)) {
        return reportMissing("export-wskp-json", "WSKP", base, ".wskp");
    }
    auto c = wowee::pipeline::WoweeSkyParamsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WSKP";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"skyId", e.skyId},
            {"name", e.name},
            {"description", e.description},
            {"mapId", e.mapId},
            {"areaId", e.areaId},
            {"timeOfDayHour", e.timeOfDayHour},
            {"zenithColor", e.zenithColor},
            {"horizonColor", e.horizonColor},
            {"sunColor", e.sunColor},
            {"sunAngleDeg", e.sunAngleDeg},
            {"fogStartYards", e.fogStartYards},
            {"fogEndYards", e.fogEndYards},
            {"cloudOpacity", e.cloudOpacity},
            {"cloudSpeedX10", e.cloudSpeedX10},
            {"cloudSpeedMph", e.cloudSpeedX10 / 10.0},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wskp-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu keyframes)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wskp");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wskp-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wskp-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSkyParams c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wskp-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeSkyParams::Entry e;
        e.skyId = je.value("skyId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.mapId = je.value("mapId", 0u);
        e.areaId = je.value("areaId", 0u);
        e.timeOfDayHour = static_cast<uint8_t>(
            je.value("timeOfDayHour", 12u));
        e.zenithColor = je.value("zenithColor", 0xFF000000u);
        e.horizonColor = je.value("horizonColor", 0xFF000000u);
        e.sunColor = je.value("sunColor", 0xFFFFFFFFu);
        e.sunAngleDeg = je.value("sunAngleDeg", 0.0f);
        e.fogStartYards = je.value("fogStartYards", 100.0f);
        e.fogEndYards = je.value("fogEndYards", 500.0f);
        e.cloudOpacity = static_cast<uint8_t>(
            je.value("cloudOpacity", 128u));
        // cloudSpeedX10 accepts raw int form (preferred,
        // 0..255 = 0..25.5 mph) OR cloudSpeedMph float
        // form (convenience for hand-edited JSON).
        if (je.contains("cloudSpeedX10") &&
            je["cloudSpeedX10"].is_number_integer()) {
            e.cloudSpeedX10 = static_cast<uint8_t>(
                je["cloudSpeedX10"].get<int>());
        } else if (je.contains("cloudSpeedMph") &&
                   je["cloudSpeedMph"].is_number()) {
            double mph = je["cloudSpeedMph"].get<double>();
            int x10 = static_cast<int>(mph * 10.0 + 0.5);
            if (x10 < 0) x10 = 0;
            if (x10 > 255) x10 = 255;
            e.cloudSpeedX10 = static_cast<uint8_t>(x10);
        }
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeSkyParamsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wskp-json: failed to save %s.wskp\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wskp (%zu keyframes)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wskp");
    if (!wowee::pipeline::WoweeSkyParamsLoader::exists(base)) {
        return reportMissing("validate-wskp", "WSKP", base, ".wskp");
    }
    auto c = wowee::pipeline::WoweeSkyParamsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-(mapId, areaId, timeOfDayHour) triple
    // uniqueness — two keyframes at same hour for the
    // same area would render in unstable order during
    // diurnal interpolation.
    std::set<uint64_t> tripleSeen;
    auto tripleKey = [](uint32_t mapId, uint32_t areaId,
                        uint8_t hour) {
        return (static_cast<uint64_t>(mapId) << 40) |
               (static_cast<uint64_t>(areaId) << 8) |
               hour;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.skyId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.skyId == 0)
            errors.push_back(ctx + ": skyId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.timeOfDayHour > 23) {
            errors.push_back(ctx + ": timeOfDayHour " +
                std::to_string(e.timeOfDayHour) +
                " > 23 — must be 0..23");
        }
        if (e.sunAngleDeg < 0.0f || e.sunAngleDeg > 360.0f) {
            warnings.push_back(ctx + ": sunAngleDeg " +
                std::to_string(e.sunAngleDeg) +
                " outside [0, 360] — renderer wraps "
                "modulo but values outside the canonical "
                "range suggest authoring confusion");
        }
        if (e.fogStartYards >= e.fogEndYards) {
            errors.push_back(ctx + ": fogStartYards " +
                std::to_string(e.fogStartYards) +
                " >= fogEndYards " +
                std::to_string(e.fogEndYards) +
                " — fog falloff would be inverted or "
                "zero-thickness");
        }
        if (e.fogStartYards < 0.0f || e.fogEndYards < 0.0f) {
            errors.push_back(ctx +
                ": negative fog distance — fog distances "
                "must be non-negative");
        }
        // Triple uniqueness: same area + same hour
        // duplication would tie at runtime.
        uint64_t key = tripleKey(e.mapId, e.areaId,
                                  e.timeOfDayHour);
        if (!tripleSeen.insert(key).second) {
            errors.push_back(ctx +
                ": (mapId=" + std::to_string(e.mapId) +
                ", areaId=" + std::to_string(e.areaId) +
                ", hour=" +
                std::to_string(e.timeOfDayHour) +
                ") triple already bound by another sky "
                "entry — diurnal interpolation would tie "
                "non-deterministically");
        }
        if (!idsSeen.insert(e.skyId).second) {
            errors.push_back(ctx + ": duplicate skyId");
        }
    }
    return cli::reportValidation("wskp", base, jsonOut, errors, warnings,
                                 formatted("%zu keyframes, all skyIds + "
                    "(map,area,hour) triples unique", c.entries.size()));
}

} // namespace

bool handleSkyParamsCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-skp") == 0 && i + 1 < argc) {
        outRc = handleGenStormwind(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-skp-arctic") == 0 &&
        i + 1 < argc) {
        outRc = handleGenArctic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-skp-hellfire") == 0 &&
        i + 1 < argc) {
        outRc = handleGenHellfire(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wskp") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wskp") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wskp-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wskp-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
