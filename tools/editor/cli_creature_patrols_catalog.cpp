#include "cli_creature_patrols_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_creature_patrols.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCreaturePatrol& c,
                     const std::string& base) {
    size_t totalWp = 0;
    for (const auto& e : c.entries) totalWp += e.waypoints.size();
    std::printf("Wrote %s.wcmr\n", base.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  paths     : %zu\n", c.entries.size());
    std::printf("  waypoints : %zu (across all paths)\n", totalWp);
}

int handleGenPatrol(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PatrolPaths";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmr");
    auto c = wowee::pipeline::WoweeCreaturePatrolLoader::makePatrol(name);
    if (!saveOrError<wowee::pipeline::WoweeCreaturePatrolLoader>(c, base, "gen-cmr", ".wcmr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CityGuardRoutes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmr");
    auto c = wowee::pipeline::WoweeCreaturePatrolLoader::makeCity(name);
    if (!saveOrError<wowee::pipeline::WoweeCreaturePatrolLoader>(c, base, "gen-cmr-city", ".wcmr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBoss(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BossPatrols";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmr");
    auto c = wowee::pipeline::WoweeCreaturePatrolLoader::makeBoss(name);
    if (!saveOrError<wowee::pipeline::WoweeCreaturePatrolLoader>(c, base, "gen-cmr-boss", ".wcmr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcmr");
    if (!wowee::pipeline::WoweeCreaturePatrolLoader::exists(base)) {
        std::fprintf(stderr, "WCMR not found: %s.wcmr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreaturePatrolLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcmr"] = base + ".wcmr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json wpArr = nlohmann::json::array();
            for (const auto& w : e.waypoints) {
                wpArr.push_back({
                    {"x", w.x}, {"y", w.y}, {"z", w.z},
                    {"delayMs", w.delayMs},
                });
            }
            arr.push_back({
                {"pathId", e.pathId},
                {"name", e.name},
                {"description", e.description},
                {"creatureGuid", e.creatureGuid},
                {"pathKind", e.pathKind},
                {"pathKindName", wowee::pipeline::WoweeCreaturePatrol::pathKindName(e.pathKind)},
                {"moveType", e.moveType},
                {"moveTypeName", wowee::pipeline::WoweeCreaturePatrol::moveTypeName(e.moveType)},
                {"waypointCount", e.waypoints.size()},
                {"pathLengthYards", c.pathLengthYards(e.pathId)},
                {"waypoints", wpArr},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCMR: %s.wcmr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  paths   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    creatureGuid  kind       move    waypoints  pathLen(yd)  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %8u     %-8s   %-5s   %5zu      %8.1f    %s\n",
                    e.pathId, e.creatureGuid,
                    wowee::pipeline::WoweeCreaturePatrol::pathKindName(e.pathKind),
                    wowee::pipeline::WoweeCreaturePatrol::moveTypeName(e.moveType),
                    e.waypoints.size(),
                    c.pathLengthYards(e.pathId),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wcmr");
    if (!wowee::pipeline::WoweeCreaturePatrolLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wcmr-json: WCMR not found: %s.wcmr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreaturePatrolLoader::load(base);
    if (outPath.empty()) outPath = base + ".wcmr.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json wpArr = nlohmann::json::array();
        for (const auto& w : e.waypoints) {
            wpArr.push_back({
                {"x", w.x}, {"y", w.y}, {"z", w.z},
                {"delayMs", w.delayMs},
            });
        }
        nlohmann::json je;
        je["pathId"] = e.pathId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["creatureGuid"] = e.creatureGuid;
        je["pathKind"] = e.pathKind;
        je["pathKindName"] =
            wowee::pipeline::WoweeCreaturePatrol::pathKindName(e.pathKind);
        je["moveType"] = e.moveType;
        je["moveTypeName"] =
            wowee::pipeline::WoweeCreaturePatrol::moveTypeName(e.moveType);
        je["waypoints"] = wpArr;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wcmr-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  paths   : %zu\n", c.entries.size());
    return 0;
}

uint8_t parsePathKindToken(const nlohmann::json& jv, uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeCreaturePatrol::Random)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "loop")    return wowee::pipeline::WoweeCreaturePatrol::Loop;
        if (s == "one-shot" ||
            s == "oneshot") return wowee::pipeline::WoweeCreaturePatrol::OneShot;
        if (s == "reverse") return wowee::pipeline::WoweeCreaturePatrol::Reverse;
        if (s == "random")  return wowee::pipeline::WoweeCreaturePatrol::Random;
    }
    return fallback;
}

uint8_t parseMoveTypeToken(const nlohmann::json& jv, uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeCreaturePatrol::Swim)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "walk") return wowee::pipeline::WoweeCreaturePatrol::Walk;
        if (s == "run")  return wowee::pipeline::WoweeCreaturePatrol::Run;
        if (s == "fly")  return wowee::pipeline::WoweeCreaturePatrol::Fly;
        if (s == "swim") return wowee::pipeline::WoweeCreaturePatrol::Swim;
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
            "import-wcmr-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcmr-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCreaturePatrol c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCreaturePatrol::Entry e;
            if (je.contains("pathId"))       e.pathId = je["pathId"].get<uint32_t>();
            if (je.contains("name"))         e.name = je["name"].get<std::string>();
            if (je.contains("description"))  e.description = je["description"].get<std::string>();
            if (je.contains("creatureGuid")) e.creatureGuid = je["creatureGuid"].get<uint32_t>();
            uint8_t kind = wowee::pipeline::WoweeCreaturePatrol::Loop;
            if (je.contains("pathKind"))
                kind = parsePathKindToken(je["pathKind"], kind);
            else if (je.contains("pathKindName"))
                kind = parsePathKindToken(je["pathKindName"], kind);
            e.pathKind = kind;
            uint8_t move = wowee::pipeline::WoweeCreaturePatrol::Walk;
            if (je.contains("moveType"))
                move = parseMoveTypeToken(je["moveType"], move);
            else if (je.contains("moveTypeName"))
                move = parseMoveTypeToken(je["moveTypeName"], move);
            e.moveType = move;
            if (je.contains("waypoints") && je["waypoints"].is_array()) {
                for (const auto& wj : je["waypoints"]) {
                    wowee::pipeline::WoweeCreaturePatrol::Waypoint w;
                    if (wj.contains("x"))        w.x = wj["x"].get<float>();
                    if (wj.contains("y"))        w.y = wj["y"].get<float>();
                    if (wj.contains("z"))        w.z = wj["z"].get<float>();
                    if (wj.contains("delayMs"))  w.delayMs = wj["delayMs"].get<uint32_t>();
                    e.waypoints.push_back(w);
                }
            }
            if (je.contains("iconColorRGBA"))
                e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wcmr");
    outBase = cli::withoutExt(outBase, ".wcmr");
    if (!wowee::pipeline::WoweeCreaturePatrolLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcmr-json: failed to save %s.wcmr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcmr\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  paths   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcmr");
    if (!wowee::pipeline::WoweeCreaturePatrolLoader::exists(base)) {
        return reportMissing("validate-wcmr", "WCMR", base, ".wcmr");
    }
    auto c = wowee::pipeline::WoweeCreaturePatrolLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.pathId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.pathId == 0)
            errors.push_back(ctx + ": pathId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.creatureGuid == 0)
            errors.push_back(ctx +
                ": creatureGuid is 0 — path is unbound to any spawn");
        if (e.pathKind > wowee::pipeline::WoweeCreaturePatrol::Random) {
            errors.push_back(ctx + ": pathKind " +
                std::to_string(e.pathKind) + " not in 0..3");
        }
        if (e.moveType > wowee::pipeline::WoweeCreaturePatrol::Swim) {
            errors.push_back(ctx + ": moveType " +
                std::to_string(e.moveType) + " not in 0..3");
        }
        if (e.waypoints.empty())
            errors.push_back(ctx + ": no waypoints — path has nothing to walk");
        if (e.waypoints.size() == 1)
            warnings.push_back(ctx +
                ": only 1 waypoint — creature will idle in place");
        // Loop with fewer than 3 waypoints is degenerate
        // (back and forth between 2 points isn't a loop).
        if (e.pathKind == wowee::pipeline::WoweeCreaturePatrol::Loop &&
            e.waypoints.size() < 3) {
            warnings.push_back(ctx +
                ": Loop with " +
                std::to_string(e.waypoints.size()) +
                " waypoints — fewer than 3 makes Loop "
                "indistinguishable from Reverse");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.pathId) {
                errors.push_back(ctx + ": duplicate pathId");
                break;
            }
        }
        idsSeen.push_back(e.pathId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wcmr", base, errors, warnings);
    std::printf("validate-wcmr: %s.wcmr\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu paths, all pathIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleCreaturePatrolsCatalog(int& i, int argc, char** argv,
                                  int& outRc) {
    if (std::strcmp(argv[i], "--gen-cmr") == 0 && i + 1 < argc) {
        outRc = handleGenPatrol(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmr-city") == 0 && i + 1 < argc) {
        outRc = handleGenCity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmr-boss") == 0 && i + 1 < argc) {
        outRc = handleGenBoss(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcmr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcmr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcmr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcmr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
