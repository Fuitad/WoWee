#include "cli_taxi_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_taxi.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
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

std::string stripWtaxExt(std::string base) {
    stripExt(base, ".wtax");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeTaxi& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeTaxiLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wtax\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

uint32_t totalWaypoints(const wowee::pipeline::WoweeTaxi& c) {
    uint32_t n = 0;
    for (const auto& p : c.paths) n += static_cast<uint32_t>(p.waypoints.size());
    return n;
}

void printGenSummary(const wowee::pipeline::WoweeTaxi& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtax\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  nodes   : %zu\n", c.nodes.size());
    std::printf("  paths   : %zu (%u waypoints total)\n",
                c.paths.size(), totalWaypoints(c));
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterTaxi";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWtaxExt(base);
    auto c = wowee::pipeline::WoweeTaxiLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-taxi")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRegion(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RegionTaxi";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWtaxExt(base);
    auto c = wowee::pipeline::WoweeTaxiLoader::makeRegion(name);
    if (!saveOrError(c, base, "gen-taxi-region")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenContinent(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ContinentTaxi";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWtaxExt(base);
    auto c = wowee::pipeline::WoweeTaxiLoader::makeContinent(name);
    if (!saveOrError(c, base, "gen-taxi-continent")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWtaxExt(base);
    if (!wowee::pipeline::WoweeTaxiLoader::exists(base)) {
        std::fprintf(stderr, "WTAX not found: %s.wtax\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTaxiLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtax"] = base + ".wtax";
        j["name"] = c.name;
        j["nodeCount"] = c.nodes.size();
        j["pathCount"] = c.paths.size();
        j["totalWaypoints"] = totalWaypoints(c);
        nlohmann::json na = nlohmann::json::array();
        for (const auto& n : c.nodes) {
            na.push_back({
                {"nodeId", n.nodeId},
                {"mapId", n.mapId},
                {"name", n.name},
                {"iconPath", n.iconPath},
                {"position", {n.position.x, n.position.y, n.position.z}},
                {"factionAlliance", n.factionAlliance},
                {"factionHorde", n.factionHorde},
            });
        }
        j["nodes"] = na;
        nlohmann::json pa = nlohmann::json::array();
        for (const auto& p : c.paths) {
            nlohmann::json wpa = nlohmann::json::array();
            for (const auto& w : p.waypoints) {
                wpa.push_back({
                    {"position", {w.position.x, w.position.y, w.position.z}},
                    {"delaySec", w.delaySec},
                });
            }
            pa.push_back({
                {"pathId", p.pathId},
                {"fromNodeId", p.fromNodeId},
                {"toNodeId", p.toNodeId},
                {"moneyCostCopper", p.moneyCostCopper},
                {"waypoints", wpa},
            });
        }
        j["paths"] = pa;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTAX: %s.wtax\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  nodes   : %zu\n", c.nodes.size());
    std::printf("  paths   : %zu (%u waypoints total)\n",
                c.paths.size(), totalWaypoints(c));
    if (!c.nodes.empty()) {
        std::printf("\n  Nodes:\n");
        std::printf("    id   map  pos (x, y, z)              name\n");
        for (const auto& n : c.nodes) {
            std::printf("  %4u  %3u  (%7.1f,%6.1f,%7.1f)  %s\n",
                        n.nodeId, n.mapId,
                        n.position.x, n.position.y, n.position.z,
                        n.name.c_str());
        }
    }
    if (!c.paths.empty()) {
        std::printf("\n  Paths:\n");
        std::printf("    id    from -> to    cost      waypoints\n");
        for (const auto& p : c.paths) {
            std::printf("  %4u   %4u -> %-4u  %5uc    %zu\n",
                        p.pathId, p.fromNodeId, p.toNodeId,
                        p.moneyCostCopper, p.waypoints.size());
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Two top-level arrays (nodes / paths)
    // mirroring the binary layout. Vec3 fields become
    // 3-element JSON arrays.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = stripWtaxExt(base);
    if (outPath.empty()) outPath = base + ".wtax.json";
    if (!wowee::pipeline::WoweeTaxiLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtax-json: WTAX not found: %s.wtax\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTaxiLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json na = nlohmann::json::array();
    for (const auto& n : c.nodes) {
        na.push_back({
            {"nodeId", n.nodeId},
            {"mapId", n.mapId},
            {"name", n.name},
            {"iconPath", n.iconPath},
            {"position", {n.position.x, n.position.y, n.position.z}},
            {"factionAlliance", n.factionAlliance},
            {"factionHorde", n.factionHorde},
        });
    }
    j["nodes"] = na;
    nlohmann::json pa = nlohmann::json::array();
    for (const auto& p : c.paths) {
        nlohmann::json wpa = nlohmann::json::array();
        for (const auto& w : p.waypoints) {
            wpa.push_back({
                {"position", {w.position.x, w.position.y, w.position.z}},
                {"delaySec", w.delaySec},
            });
        }
        pa.push_back({
            {"pathId", p.pathId},
            {"fromNodeId", p.fromNodeId},
            {"toNodeId", p.toNodeId},
            {"moneyCostCopper", p.moneyCostCopper},
            {"waypoints", wpa},
        });
    }
    j["paths"] = pa;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wtax-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wtax\n", base.c_str());
    std::printf("  nodes  : %zu  paths : %zu\n",
                c.nodes.size(), c.paths.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wtax.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = stripWtaxExt(outBase);
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wtax-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wtax-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto readVec3 = [](const nlohmann::json& jv, glm::vec3& v) {
        if (jv.is_array() && jv.size() >= 3) {
            v.x = jv[0].get<float>();
            v.y = jv[1].get<float>();
            v.z = jv[2].get<float>();
        }
    };
    wowee::pipeline::WoweeTaxi c;
    c.name = j.value("name", std::string{});
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& jn : j["nodes"]) {
            wowee::pipeline::WoweeTaxi::Node n;
            n.nodeId = jn.value("nodeId", 0u);
            n.mapId = jn.value("mapId", 0u);
            n.name = jn.value("name", std::string{});
            n.iconPath = jn.value("iconPath", std::string{});
            if (jn.contains("position")) readVec3(jn["position"], n.position);
            n.factionAlliance = jn.value("factionAlliance", 0u);
            n.factionHorde = jn.value("factionHorde", 0u);
            c.nodes.push_back(n);
        }
    }
    if (j.contains("paths") && j["paths"].is_array()) {
        for (const auto& jp : j["paths"]) {
            wowee::pipeline::WoweeTaxi::Path p;
            p.pathId = jp.value("pathId", 0u);
            p.fromNodeId = jp.value("fromNodeId", 0u);
            p.toNodeId = jp.value("toNodeId", 0u);
            p.moneyCostCopper = jp.value("moneyCostCopper", 0u);
            if (jp.contains("waypoints") && jp["waypoints"].is_array()) {
                for (const auto& jw : jp["waypoints"]) {
                    wowee::pipeline::WoweeTaxi::Waypoint w;
                    if (jw.contains("position")) readVec3(jw["position"], w.position);
                    w.delaySec = jw.value("delaySec", 0.0f);
                    p.waypoints.push_back(w);
                }
            }
            c.paths.push_back(p);
        }
    }
    if (!wowee::pipeline::WoweeTaxiLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtax-json: failed to save %s.wtax\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtax\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  nodes  : %zu  paths : %zu\n",
                c.nodes.size(), c.paths.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWtaxExt(base);
    if (!wowee::pipeline::WoweeTaxiLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtax: WTAX not found: %s.wtax\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTaxiLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.nodes.empty()) {
        warnings.push_back("catalog has zero nodes");
    }
    std::vector<uint32_t> nodeIdsSeen;
    for (size_t k = 0; k < c.nodes.size(); ++k) {
        const auto& n = c.nodes[k];
        std::string ctx = "node " + std::to_string(k) +
                          " (id=" + std::to_string(n.nodeId);
        if (!n.name.empty()) ctx += " " + n.name;
        ctx += ")";
        if (n.nodeId == 0) {
            errors.push_back(ctx + ": nodeId is 0");
        }
        if (n.name.empty()) {
            errors.push_back(ctx + ": name is empty");
        }
        if (!std::isfinite(n.position.x) ||
            !std::isfinite(n.position.y) ||
            !std::isfinite(n.position.z)) {
            errors.push_back(ctx + ": position not finite");
        }
        for (uint32_t prev : nodeIdsSeen) {
            if (prev == n.nodeId) {
                errors.push_back(ctx + ": duplicate nodeId");
                break;
            }
        }
        nodeIdsSeen.push_back(n.nodeId);
    }
    std::vector<uint32_t> pathIdsSeen;
    for (size_t k = 0; k < c.paths.size(); ++k) {
        const auto& p = c.paths[k];
        std::string ctx = "path " + std::to_string(k) +
                          " (id=" + std::to_string(p.pathId) + ")";
        if (p.pathId == 0) {
            errors.push_back(ctx + ": pathId is 0");
        }
        if (p.fromNodeId == p.toNodeId) {
            errors.push_back(ctx +
                ": fromNodeId == toNodeId (path goes nowhere)");
        }
        if (!c.findNode(p.fromNodeId)) {
            errors.push_back(ctx + ": fromNodeId " +
                std::to_string(p.fromNodeId) + " does not exist");
        }
        if (!c.findNode(p.toNodeId)) {
            errors.push_back(ctx + ": toNodeId " +
                std::to_string(p.toNodeId) + " does not exist");
        }
        if (p.waypoints.empty()) {
            warnings.push_back(ctx +
                ": no waypoints (gryphon teleports instantly)");
        }
        for (size_t wi = 0; wi < p.waypoints.size(); ++wi) {
            const auto& w = p.waypoints[wi];
            if (!std::isfinite(w.position.x) ||
                !std::isfinite(w.position.y) ||
                !std::isfinite(w.position.z) ||
                !std::isfinite(w.delaySec)) {
                errors.push_back(ctx + " waypoint " +
                    std::to_string(wi) + ": position/delay not finite");
            }
            if (w.delaySec < 0) {
                errors.push_back(ctx + " waypoint " +
                    std::to_string(wi) + ": delaySec is negative");
            }
        }
        for (uint32_t prev : pathIdsSeen) {
            if (prev == p.pathId) {
                errors.push_back(ctx + ": duplicate pathId");
                break;
            }
        }
        pathIdsSeen.push_back(p.pathId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wtax", base, errors, warnings);
    std::printf("validate-wtax: %s.wtax\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu nodes, %zu paths, %u waypoints, all IDs unique\n",
                    c.nodes.size(), c.paths.size(), totalWaypoints(c));
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleTaxiCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-taxi") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-taxi-region") == 0 && i + 1 < argc) {
        outRc = handleGenRegion(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-taxi-continent") == 0 && i + 1 < argc) {
        outRc = handleGenContinent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtax") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtax") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtax-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtax-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
