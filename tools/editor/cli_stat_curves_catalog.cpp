#include "cli_stat_curves_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_stat_curves.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeStatCurve& c,
                     const std::string& base) {
    std::printf("Wrote %s.wstm\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  curves  : %zu\n", c.entries.size());
}

int handleGenCrit(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CritCurves";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wstm");
    auto c = wowee::pipeline::WoweeStatCurveLoader::makeCrit(name);
    if (!saveOrError<wowee::pipeline::WoweeStatCurveLoader>(c, base, "gen-stm", ".wstm")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRegen(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RegenCurves";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wstm");
    auto c = wowee::pipeline::WoweeStatCurveLoader::makeRegen(name);
    if (!saveOrError<wowee::pipeline::WoweeStatCurveLoader>(c, base, "gen-stm-regen", ".wstm")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenArmor(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ArmorCurves";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wstm");
    auto c = wowee::pipeline::WoweeStatCurveLoader::makeArmor(name);
    if (!saveOrError<wowee::pipeline::WoweeStatCurveLoader>(c, base, "gen-stm-armor", ".wstm")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wstm");
    if (!wowee::pipeline::WoweeStatCurveLoader::exists(base)) {
        return reportMissing("WSTM", base, ".wstm");
    }
    auto c = wowee::pipeline::WoweeStatCurveLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wstm"] = base + ".wstm";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"curveId", e.curveId},
                {"name", e.name},
                {"description", e.description},
                {"curveKind", e.curveKind},
                {"curveKindName", wowee::pipeline::WoweeStatCurve::curveKindName(e.curveKind)},
                {"minLevel", e.minLevel},
                {"maxLevel", e.maxLevel},
                {"baseValue", e.baseValue},
                {"perLevelDelta", e.perLevelDelta},
                {"multiplier", e.multiplier},
                {"valueAtLevel80", c.resolveAtLevel(e.curveId, 80)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSTM: %s.wstm\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  curves  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind         minL  maxL    base      perLvl     mult    @lvl80    name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %-10s   %3u   %3u    %7.3f   %7.4f   %5.2f   %7.3f   %s\n",
                    e.curveId,
                    wowee::pipeline::WoweeStatCurve::curveKindName(e.curveKind),
                    e.minLevel, e.maxLevel,
                    e.baseValue, e.perLevelDelta, e.multiplier,
                    c.resolveAtLevel(e.curveId, 80),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wstm");
    if (!wowee::pipeline::WoweeStatCurveLoader::exists(base)) {
        return reportMissing("export-wstm-json", "WSTM", base, ".wstm");
    }
    auto c = wowee::pipeline::WoweeStatCurveLoader::load(base);
    if (outPath.empty()) outPath = base + ".wstm.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["curveId"] = e.curveId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["curveKind"] = e.curveKind;
        je["curveKindName"] =
            wowee::pipeline::WoweeStatCurve::curveKindName(e.curveKind);
        je["minLevel"] = e.minLevel;
        je["maxLevel"] = e.maxLevel;
        je["baseValue"] = e.baseValue;
        je["perLevelDelta"] = e.perLevelDelta;
        je["multiplier"] = e.multiplier;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wstm-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  curves  : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseCurveKindToken(const nlohmann::json& jv, uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeStatCurve::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "crit")       return wowee::pipeline::WoweeStatCurve::Crit;
        if (s == "hit")        return wowee::pipeline::WoweeStatCurve::Hit;
        if (s == "power")      return wowee::pipeline::WoweeStatCurve::Power;
        if (s == "regen")      return wowee::pipeline::WoweeStatCurve::Regen;
        if (s == "resist")     return wowee::pipeline::WoweeStatCurve::Resist;
        if (s == "mitigation") return wowee::pipeline::WoweeStatCurve::Mitigation;
        if (s == "misc")       return wowee::pipeline::WoweeStatCurve::Misc;
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
            "import-wstm-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wstm-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeStatCurve c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeStatCurve::Entry e;
            if (je.contains("curveId"))       e.curveId = je["curveId"].get<uint32_t>();
            if (je.contains("name"))          e.name = je["name"].get<std::string>();
            if (je.contains("description"))   e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeStatCurve::Misc;
            if (je.contains("curveKind"))
                kind = parseCurveKindToken(je["curveKind"], kind);
            else if (je.contains("curveKindName"))
                kind = parseCurveKindToken(je["curveKindName"], kind);
            e.curveKind = kind;
            if (je.contains("minLevel"))      e.minLevel = je["minLevel"].get<uint8_t>();
            if (je.contains("maxLevel"))      e.maxLevel = je["maxLevel"].get<uint8_t>();
            if (je.contains("baseValue"))     e.baseValue = je["baseValue"].get<float>();
            if (je.contains("perLevelDelta")) e.perLevelDelta = je["perLevelDelta"].get<float>();
            if (je.contains("multiplier"))    e.multiplier = je["multiplier"].get<float>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wstm");
    outBase = cli::withoutExt(outBase, ".wstm");
    if (!wowee::pipeline::WoweeStatCurveLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wstm-json: failed to save %s.wstm\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wstm\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  curves  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeStatCurveLoader>(
        i, argc, argv, "wstm", "WSTM",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.curveId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.curveId == 0)
                errors.push_back(ctx + ": curveId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.curveKind > wowee::pipeline::WoweeStatCurve::Misc) {
                errors.push_back(ctx + ": curveKind " +
                    std::to_string(e.curveKind) + " not in 0..6");
            }
            if (e.minLevel > e.maxLevel) {
                errors.push_back(ctx + ": minLevel " +
                    std::to_string(e.minLevel) +
                    " > maxLevel " + std::to_string(e.maxLevel) +
                    " — curve will never apply");
            }
            if (e.maxLevel > 80) {
                warnings.push_back(ctx +
                    ": maxLevel " + std::to_string(e.maxLevel) +
                    " > 80 — characters cap at 80 in WotLK");
            }
            if (e.multiplier == 0.0f)
                warnings.push_back(ctx +
                    ": multiplier=0 — curve always evaluates to 0");
            if (e.multiplier < 0.0f)
                warnings.push_back(ctx +
                    ": multiplier=" + std::to_string(e.multiplier) +
                    " (< 0) — inverts the curve, double-check this "
                    "is intentional");
            // Negative perLevelDelta is unusual — most stats
            // grow with level.
            if (e.perLevelDelta < 0.0f)
                warnings.push_back(ctx +
                    ": perLevelDelta=" + std::to_string(e.perLevelDelta) +
                    " (< 0) — curve shrinks with level, double-check");
            if (!idsSeen.add(e.curveId)) errors.push_back(ctx + ": duplicate curveId");
        }
            return formatted("%zu curves, all curveIds unique, all minLevel<=maxLevel", c.entries.size());
        });
}

} // namespace

bool handleStatCurvesCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-stm") == 0 && i + 1 < argc) {
        outRc = handleGenCrit(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-stm-regen") == 0 && i + 1 < argc) {
        outRc = handleGenRegen(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-stm-armor") == 0 && i + 1 < argc) {
        outRc = handleGenArmor(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wstm") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wstm") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wstm-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wstm-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
