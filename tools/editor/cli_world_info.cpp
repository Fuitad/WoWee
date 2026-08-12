#include "cli_world_info.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_weld.hpp"

#include "pipeline/wowee_building.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wowee_light.hpp"
#include "pipeline/wowee_weather.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoWob(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wob")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeBuildingLoader::exists(base)) {
        return reportMissing("WOB", base, ".wob");
    }
    auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
    size_t totalVerts = 0, totalIdx = 0, totalMats = 0;
    for (const auto& g : bld.groups) {
        totalVerts += g.vertices.size();
        totalIdx += g.indices.size();
        totalMats += g.materials.size();
    }
    if (jsonOut) {
        nlohmann::json j;
        j["wob"] = base + ".wob";
        j["name"] = bld.name;
        j["groups"] = bld.groups.size();
        j["portals"] = bld.portals.size();
        j["doodads"] = bld.doodads.size();
        j["boundRadius"] = bld.boundRadius;
        j["totalVerts"] = totalVerts;
        j["totalTris"] = totalIdx / 3;
        j["totalMats"] = totalMats;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOB: %s.wob\n", base.c_str());
    std::printf("  name        : %s\n", bld.name.c_str());
    std::printf("  groups      : %zu\n", bld.groups.size());
    std::printf("  portals     : %zu\n", bld.portals.size());
    std::printf("  doodads     : %zu\n", bld.doodads.size());
    std::printf("  boundRadius : %.2f\n", bld.boundRadius);
    std::printf("  total verts : %zu\n", totalVerts);
    std::printf("  total tris  : %zu\n", totalIdx / 3);
    std::printf("  total mats  : %zu (across all groups)\n", totalMats);
    return 0;
}

int handleInfoWobStats(int& i, int argc, char** argv) {
    // Geometric stats on a WOB building, per-group and aggregated
    // across all groups: triangle count, surface area, watertight
    // check via the same edge analysis as --info-mesh-stats. Pass
    // --weld <eps> to merge per-face vertex duplicates before edge
    // analysis (true topological closure check).
    std::string base = argv[++i];
    bool jsonOut = false;
    bool useWeld = false;
    float weldEps = 1e-5f;
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        if (std::strcmp(argv[i + 1], "--json") == 0) {
            jsonOut = true; ++i;
        } else if (std::strcmp(argv[i + 1], "--weld") == 0 && i + 2 < argc) {
            useWeld = true;
            try { weldEps = std::stof(argv[i + 2]); } catch (...) {}
            i += 2;
        } else {
            break;
        }
    }
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wob")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeBuildingLoader::exists(base)) {
        return reportMissing("WOB", base, ".wob");
    }
    auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
    struct GroupStats {
        std::string name;
        std::size_t tris = 0;
        std::size_t degenerate = 0;
        std::size_t uniquePositions = 0;
        std::size_t totalVerts = 0;
        std::size_t boundary = 0, manifold = 0, nonManifold = 0;
        bool watertight = false;
        double surfaceArea = 0.0;
    };
    std::vector<GroupStats> perGroup;
    perGroup.reserve(bld.groups.size());
    std::size_t aggBoundary = 0, aggManifold = 0, aggNonManifold = 0;
    std::size_t aggTris = 0, aggDegenerate = 0;
    double aggArea = 0.0;
    for (const auto& g : bld.groups) {
        GroupStats gs;
        gs.name = g.name;
        gs.totalVerts = g.vertices.size();
        if (g.indices.size() % 3 != 0) {
            std::fprintf(stderr,
                "info-wob-stats: group '%s' has indices %% 3 != 0\n",
                g.name.c_str());
            return 1;
        }
        gs.tris = g.indices.size() / 3;
        // Build canon[] for this group, optionally welding via the
        // shared cli_weld utility.
        std::vector<uint32_t> canon;
        if (useWeld) {
            std::vector<glm::vec3> positions;
            positions.reserve(g.vertices.size());
            for (const auto& v : g.vertices) positions.push_back(v.position);
            canon = buildWeldMap(positions, weldEps, gs.uniquePositions);
        } else {
            canon.resize(g.vertices.size());
            for (std::size_t v = 0; v < g.vertices.size(); ++v) {
                canon[v] = static_cast<uint32_t>(v);
            }
            gs.uniquePositions = g.vertices.size();
        }
        // Triangle area pass (also catches out-of-range indices).
        for (std::size_t t = 0; t < gs.tris; ++t) {
            uint32_t i0 = g.indices[t * 3 + 0];
            uint32_t i1 = g.indices[t * 3 + 1];
            uint32_t i2 = g.indices[t * 3 + 2];
            if (i0 >= g.vertices.size() ||
                i1 >= g.vertices.size() ||
                i2 >= g.vertices.size()) {
                std::fprintf(stderr,
                    "info-wob-stats: group '%s' has out-of-range index\n",
                    g.name.c_str());
                return 1;
            }
            glm::vec3 a = g.vertices[i0].position;
            glm::vec3 b = g.vertices[i1].position;
            glm::vec3 c = g.vertices[i2].position;
            double area = 0.5 * glm::length(glm::cross(b - a, c - a));
            if (area < 1e-12) ++gs.degenerate;
            gs.surfaceArea += area;
        }
        EdgeStats edges = classifyEdges(g.indices, canon);
        gs.boundary = edges.boundary;
        gs.manifold = edges.manifold;
        gs.nonManifold = edges.nonManifold;
        gs.watertight = edges.watertight();
        aggBoundary += gs.boundary;
        aggManifold += gs.manifold;
        aggNonManifold += gs.nonManifold;
        aggTris += gs.tris;
        aggDegenerate += gs.degenerate;
        aggArea += gs.surfaceArea;
        perGroup.push_back(std::move(gs));
    }
    if (jsonOut) {
        nlohmann::json j;
        j["wob"] = base + ".wob";
        j["welded"] = useWeld;
        if (useWeld) j["weldEps"] = weldEps;
        j["aggregate"] = {{"groups", perGroup.size()},
                           {"triangles", aggTris},
                           {"degenerateTriangles", aggDegenerate},
                           {"surfaceArea", aggArea},
                           {"boundary", aggBoundary},
                           {"manifold", aggManifold},
                           {"nonManifold", aggNonManifold}};
        nlohmann::json gs = nlohmann::json::array();
        for (const auto& g : perGroup) {
            gs.push_back({{"name", g.name},
                           {"triangles", g.tris},
                           {"degenerate", g.degenerate},
                           {"surfaceArea", g.surfaceArea},
                           {"uniquePositions", g.uniquePositions},
                           {"totalVerts", g.totalVerts},
                           {"boundary", g.boundary},
                           {"manifold", g.manifold},
                           {"nonManifold", g.nonManifold},
                           {"watertight", g.watertight}});
        }
        j["groups"] = gs;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOB stats: %s.wob\n", base.c_str());
    std::printf("  groups          : %zu\n", perGroup.size());
    std::printf("  total tris      : %zu (%zu degenerate)\n",
                aggTris, aggDegenerate);
    std::printf("  total area      : %.4f\n", aggArea);
    std::printf("  aggregate edges : %zu boundary, %zu manifold, %zu non-manifold\n",
                aggBoundary, aggManifold, aggNonManifold);
    if (useWeld) {
        std::printf("  weld eps        : %.6f\n", weldEps);
    }
    std::printf("\n  Per group:\n");
    std::printf("    idx  tris   area      verts→uniq  boundary  manifold  non-m  closed\n");
    for (std::size_t k = 0; k < perGroup.size(); ++k) {
        const auto& g = perGroup[k];
        std::printf("    %3zu  %5zu  %8.3f  %5zu→%-5zu  %8zu  %8zu  %5zu  %s\n",
                    k, g.tris, g.surfaceArea,
                    g.totalVerts, g.uniquePositions,
                    g.boundary, g.manifold, g.nonManifold,
                    g.watertight ? "YES" : "no");
    }
    return 0;
}

int handleInfoWot(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    // Accept "/path/file.wot", "/path/file.whm", or "/path/file"; the
    // loader pairs both extensions from the same base path.
    for (const char* ext : {".wot", ".whm"}) {
        if (base.size() >= 4 && base.substr(base.size() - 4) == ext) {
            base = base.substr(0, base.size() - 4);
            break;
        }
    }
    if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) {
        std::fprintf(stderr, "WOT/WHM not found at base: %s\n", base.c_str());
        return 1;
    }
    wowee::pipeline::ADTTerrain terrain;
    if (!wowee::pipeline::WoweeTerrainLoader::load(base, terrain)) {
        std::fprintf(stderr, "Failed to load WOT/WHM: %s\n", base.c_str());
        return 1;
    }
    int chunksWithHeights = 0, chunksWithLayers = 0, chunksWithWater = 0;
    float minH = 1e30f, maxH = -1e30f;
    for (int ci = 0; ci < 256; ci++) {
        const auto& c = terrain.chunks[ci];
        if (c.hasHeightMap()) {
            chunksWithHeights++;
            for (float h : c.heightMap.heights) {
                float total = c.position[2] + h;
                if (total < minH) minH = total;
                if (total > maxH) maxH = total;
            }
        }
        if (!c.layers.empty()) chunksWithLayers++;
        if (terrain.waterData[ci].hasWater()) chunksWithWater++;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["base"] = base;
        j["tileX"] = terrain.coord.x;
        j["tileY"] = terrain.coord.y;
        j["chunks"] = {{"withHeightmap", chunksWithHeights},
                        {"withLayers", chunksWithLayers},
                        {"withWater", chunksWithWater}};
        j["textures"] = terrain.textures.size();
        j["doodads"] = terrain.doodadPlacements.size();
        j["wmos"] = terrain.wmoPlacements.size();
        if (chunksWithHeights > 0) {
            j["heightMin"] = minH;
            j["heightMax"] = maxH;
        }
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOT/WHM: %s\n", base.c_str());
    std::printf("  tile         : (%d, %d)\n", terrain.coord.x, terrain.coord.y);
    std::printf("  chunks       : %d/256 with heightmap\n", chunksWithHeights);
    std::printf("  layers       : %d/256 chunks with texture layers\n", chunksWithLayers);
    std::printf("  water        : %d/256 chunks with water\n", chunksWithWater);
    std::printf("  textures     : %zu\n", terrain.textures.size());
    std::printf("  doodads      : %zu\n", terrain.doodadPlacements.size());
    std::printf("  WMOs         : %zu\n", terrain.wmoPlacements.size());
    if (chunksWithHeights > 0) {
        std::printf("  height range : [%.2f, %.2f]\n", minH, maxH);
    }
    return 0;
}

int handleInfoWoc(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".woc")
        path += ".woc";
    auto col = wowee::pipeline::WoweeCollisionBuilder::load(path);
    if (!col.isValid()) {
        std::fprintf(stderr, "WOC not found or invalid: %s\n", path.c_str());
        return 1;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["woc"] = path;
        j["tileX"] = col.tileX;
        j["tileY"] = col.tileY;
        j["triangles"] = col.triangles.size();
        j["walkable"] = col.walkableCount();
        j["steep"] = col.steepCount();
        j["boundsMin"] = {col.bounds.min.x, col.bounds.min.y, col.bounds.min.z};
        j["boundsMax"] = {col.bounds.max.x, col.bounds.max.y, col.bounds.max.z};
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOC: %s\n", path.c_str());
    std::printf("  tile        : (%u, %u)\n", col.tileX, col.tileY);
    std::printf("  triangles   : %zu\n", col.triangles.size());
    std::printf("  walkable    : %zu\n", col.walkableCount());
    std::printf("  steep       : %zu\n", col.steepCount());
    std::printf("  bounds.min  : (%.1f, %.1f, %.1f)\n",
                col.bounds.min.x, col.bounds.min.y, col.bounds.min.z);
    std::printf("  bounds.max  : (%.1f, %.1f, %.1f)\n",
                col.bounds.max.x, col.bounds.max.y, col.bounds.max.z);
    return 0;
}

int handleInfoWol(int& i, int argc, char** argv) {
    // Inspect a Wowee Open Light (.wol) file: zone name + per-
    // keyframe time-of-day + ambient/directional/fog colors and
    // fog distances.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) ++i;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wol")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeLightLoader::exists(base)) {
        return reportMissing("WOL", base, ".wol");
    }
    auto wol = wowee::pipeline::WoweeLightLoader::load(base);
    if (!wol.isValid()) {
        std::fprintf(stderr, "WOL parse failed: %s.wol\n", base.c_str());
        return 1;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["wol"] = base + ".wol";
        j["name"] = wol.name;
        j["keyframeCount"] = wol.keyframes.size();
        nlohmann::json kfs = nlohmann::json::array();
        for (const auto& kf : wol.keyframes) {
            kfs.push_back({
                {"timeOfDayMin", kf.timeOfDayMin},
                {"ambient", {kf.ambientColor.r, kf.ambientColor.g,
                              kf.ambientColor.b}},
                {"directional", {kf.directionalColor.r,
                                  kf.directionalColor.g,
                                  kf.directionalColor.b}},
                {"directionalDir", {kf.directionalDir.x,
                                     kf.directionalDir.y,
                                     kf.directionalDir.z}},
                {"fog", {kf.fogColor.r, kf.fogColor.g, kf.fogColor.b}},
                {"fogStart", kf.fogStart},
                {"fogEnd", kf.fogEnd},
            });
        }
        j["keyframes"] = kfs;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOL: %s.wol\n", base.c_str());
    std::printf("  zone       : %s\n", wol.name.c_str());
    std::printf("  keyframes  : %zu\n", wol.keyframes.size());
    for (std::size_t k = 0; k < wol.keyframes.size(); ++k) {
        const auto& kf = wol.keyframes[k];
        std::printf("  [%zu] %02u:%02u  ambient=(%.2f, %.2f, %.2f) "
                    "fog=(%.2f, %.2f, %.2f) [%.0f..%.0f]\n",
                    k,
                    kf.timeOfDayMin / 60, kf.timeOfDayMin % 60,
                    kf.ambientColor.r, kf.ambientColor.g, kf.ambientColor.b,
                    kf.fogColor.r, kf.fogColor.g, kf.fogColor.b,
                    kf.fogStart, kf.fogEnd);
    }
    return 0;
}

int handleValidateWol(int& i, int argc, char** argv) {
    // Walk every keyframe in a .wol and report structural problems:
    //   • times outside [0, 1440)
    //   • unsorted timeOfDayMin
    //   • duplicate timestamps
    //   • zero-area fog distances (fogEnd <= fogStart)
    //   • non-finite color components
    // Returns 0 PASS / 1 FAIL.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) ++i;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wol")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeLightLoader::exists(base)) {
        return reportMissing("WOL", base, ".wol");
    }
    auto wol = wowee::pipeline::WoweeLightLoader::load(base);
    std::vector<std::string> errors;
    if (wol.keyframes.empty()) {
        errors.push_back("no keyframes");
    }
    uint32_t prevTime = 0;
    bool first = true;
    auto checkColor = [&](const glm::vec3& c, const char* label, int idx) {
        for (int k = 0; k < 3; ++k) {
            float v = c[k];
            if (!std::isfinite(v)) {
                errors.push_back("kf " + std::to_string(idx) + " " +
                                  label + " channel " + std::to_string(k) +
                                  " is non-finite");
            }
        }
    };
    for (std::size_t k = 0; k < wol.keyframes.size(); ++k) {
        const auto& kf = wol.keyframes[k];
        if (kf.timeOfDayMin >= 1440) {
            errors.push_back("kf " + std::to_string(k) +
                              " time " + std::to_string(kf.timeOfDayMin) +
                              " >= 1440");
        }
        if (!first && kf.timeOfDayMin <= prevTime) {
            errors.push_back("kf " + std::to_string(k) +
                              " time " + std::to_string(kf.timeOfDayMin) +
                              " <= previous " + std::to_string(prevTime));
        }
        if (kf.fogEnd <= kf.fogStart) {
            errors.push_back("kf " + std::to_string(k) +
                              " fogEnd " + std::to_string(kf.fogEnd) +
                              " <= fogStart " +
                              std::to_string(kf.fogStart));
        }
        checkColor(kf.ambientColor, "ambient", static_cast<int>(k));
        checkColor(kf.directionalColor, "directional",
                   static_cast<int>(k));
        checkColor(kf.fogColor, "fog", static_cast<int>(k));
        prevTime = kf.timeOfDayMin;
        first = false;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["wol"] = base + ".wol";
        j["passed"] = errors.empty();
        j["errorCount"] = errors.size();
        j["errors"] = errors;
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    if (errors.empty()) {
        std::printf("WOL %s.wol PASSED - %zu keyframe(s) valid\n",
                    base.c_str(), wol.keyframes.size());
        return 0;
    }
    std::printf("WOL %s.wol FAILED - %zu error(s):\n",
                base.c_str(), errors.size());
    for (const auto& e : errors) std::printf("  - %s\n", e.c_str());
    return 1;
}

int handleInfoWolAt(int& i, int argc, char** argv) {
    // Sample the WOL's interpolated lighting state at a specific
    // time-of-day, given as HH:MM (24-hour) or as raw minutes.
    std::string base = argv[++i];
    if (i + 1 >= argc) {
        std::fprintf(stderr, "info-wol-at: missing time argument\n");
        return 1;
    }
    std::string timeStr = argv[++i];
    int timeMin = 0;
    auto colon = timeStr.find(':');
    if (colon != std::string::npos) {
        try {
            int hh = std::stoi(timeStr.substr(0, colon));
            int mm = std::stoi(timeStr.substr(colon + 1));
            timeMin = (hh * 60 + mm) % 1440;
        } catch (...) {
            std::fprintf(stderr, "info-wol-at: bad time %s (use HH:MM)\n",
                         timeStr.c_str());
            return 1;
        }
    } else {
        try { timeMin = std::stoi(timeStr) % 1440; } catch (...) {
            std::fprintf(stderr, "info-wol-at: bad time %s (use minutes)\n",
                         timeStr.c_str());
            return 1;
        }
    }
    if (timeMin < 0) timeMin += 1440;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wol")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeLightLoader::exists(base)) {
        return reportMissing("WOL", base, ".wol");
    }
    auto wol = wowee::pipeline::WoweeLightLoader::load(base);
    if (!wol.isValid()) {
        std::fprintf(stderr, "WOL parse failed: %s.wol\n", base.c_str());
        return 1;
    }
    auto kf = wowee::pipeline::WoweeLightLoader::sampleAtTime(
        wol, static_cast<uint32_t>(timeMin));
    std::printf("WOL %s.wol  sample at %02d:%02d\n",
                base.c_str(), timeMin / 60, timeMin % 60);
    std::printf("  ambient    : (%.3f, %.3f, %.3f)\n",
                kf.ambientColor.r, kf.ambientColor.g, kf.ambientColor.b);
    std::printf("  directional: (%.3f, %.3f, %.3f) dir (%.2f, %.2f, %.2f)\n",
                kf.directionalColor.r, kf.directionalColor.g,
                kf.directionalColor.b,
                kf.directionalDir.x, kf.directionalDir.y, kf.directionalDir.z);
    std::printf("  fog        : (%.3f, %.3f, %.3f) [%.1f..%.1f]\n",
                kf.fogColor.r, kf.fogColor.g, kf.fogColor.b,
                kf.fogStart, kf.fogEnd);
    return 0;
}

// Emit a .wol from a named preset. Used by all four
// --gen-light* convenience commands.
int emitLightPreset(const std::string& cmdName,
                    int& i, int argc, char** argv,
                    wowee::pipeline::WoweeLight (*maker)(const std::string&),
                    const char* presetDescription) {
    std::string base = argv[++i];
    std::string zoneName = "Default";
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        zoneName = argv[++i];
    }
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wol") {
        base = base.substr(0, base.size() - 4);
    }
    auto wol = maker(zoneName);
    if (!wowee::pipeline::WoweeLightLoader::save(wol, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wol\n",
                     cmdName.c_str(), base.c_str());
        return 1;
    }
    std::printf("Wrote %s.wol\n", base.c_str());
    std::printf("  zone       : %s\n", zoneName.c_str());
    std::printf("  preset     : %s (%zu keyframe%s)\n",
                presetDescription, wol.keyframes.size(),
                wol.keyframes.size() == 1 ? "" : "s");
    return 0;
}

int handleGenLight(int& i, int argc, char** argv) {
    return emitLightPreset(
        "gen-light", i, argc, argv,
        wowee::pipeline::WoweeLightLoader::makeDefaultDayNight,
        "midnight + dawn + noon + dusk");
}

int handleGenLightCave(int& i, int argc, char** argv) {
    return emitLightPreset(
        "gen-light-cave", i, argc, argv,
        wowee::pipeline::WoweeLightLoader::makeCave,
        "dim cool ambient + heavy short-range fog");
}

int handleGenLightDungeon(int& i, int argc, char** argv) {
    return emitLightPreset(
        "gen-light-dungeon", i, argc, argv,
        wowee::pipeline::WoweeLightLoader::makeDungeon,
        "warm torchlit ambient + medium fog");
}

int handleGenLightNight(int& i, int argc, char** argv) {
    return emitLightPreset(
        "gen-light-night", i, argc, argv,
        wowee::pipeline::WoweeLightLoader::makeNight,
        "moonlit directional + far fog");
}

int handleExportWolJson(int& i, int argc, char** argv) {
    // Export a binary .wol to a human-editable JSON sidecar.
    // Pairs with --import-wol-json for the round-trip authoring
    // workflow: export to JSON, hand-edit keyframe colors and
    // times, import back to .wol.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wol")
        base = base.substr(0, base.size() - 4);
    if (outPath.empty()) outPath = base + ".wol.json";
    if (!wowee::pipeline::WoweeLightLoader::exists(base)) {
        return reportMissing("WOL", base, ".wol");
    }
    auto wol = wowee::pipeline::WoweeLightLoader::load(base);
    if (!wol.isValid()) {
        std::fprintf(stderr, "WOL parse failed: %s.wol\n", base.c_str());
        return 1;
    }
    nlohmann::json j;
    j["name"] = wol.name;
    nlohmann::json kfs = nlohmann::json::array();
    for (const auto& kf : wol.keyframes) {
        kfs.push_back({
            {"timeOfDayMin", kf.timeOfDayMin},
            {"ambient",
                {kf.ambientColor.r, kf.ambientColor.g, kf.ambientColor.b}},
            {"directional",
                {kf.directionalColor.r, kf.directionalColor.g,
                 kf.directionalColor.b}},
            {"directionalDir",
                {kf.directionalDir.x, kf.directionalDir.y,
                 kf.directionalDir.z}},
            {"fog",
                {kf.fogColor.r, kf.fogColor.g, kf.fogColor.b}},
            {"fogStart", kf.fogStart},
            {"fogEnd", kf.fogEnd},
        });
    }
    j["keyframes"] = kfs;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wol-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    os << j.dump(2) << '\n';
    std::printf("Wrote %s (%zu keyframe%s)\n",
                outPath.c_str(), wol.keyframes.size(),
                wol.keyframes.size() == 1 ? "" : "s");
    return 0;
}

int handleImportWolJson(int& i, int argc, char** argv) {
    // Import a JSON sidecar back into binary .wol. Validates
    // structural correctness before saving - invalid JSON or
    // missing required fields fails out with a clear message.
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (i + 1 < argc && argv[i + 1][0] != '-') outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        // Strip ".wol.json" or ".json" tail.
        if (outBase.size() >= 9 &&
            outBase.substr(outBase.size() - 9) == ".wol.json") {
            outBase = outBase.substr(0, outBase.size() - 9);
        } else if (outBase.size() >= 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    if (outBase.size() >= 4 && outBase.substr(outBase.size() - 4) == ".wol") {
        outBase = outBase.substr(0, outBase.size() - 4);
    }
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wol-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { is >> j; } catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wol-json: parse error: %s\n", e.what());
        return 1;
    }
    wowee::pipeline::WoweeLight wol;
    try {
        wol.name = j.value("name", std::string("Imported"));
        for (const auto& jkf : j.at("keyframes")) {
            wowee::pipeline::WoweeLight::Keyframe kf;
            kf.timeOfDayMin = jkf.at("timeOfDayMin").get<uint32_t>();
            auto a = jkf.at("ambient");
            kf.ambientColor = {a[0], a[1], a[2]};
            auto d = jkf.at("directional");
            kf.directionalColor = {d[0], d[1], d[2]};
            auto dd = jkf.at("directionalDir");
            kf.directionalDir = {dd[0], dd[1], dd[2]};
            auto f = jkf.at("fog");
            kf.fogColor = {f[0], f[1], f[2]};
            kf.fogStart = jkf.at("fogStart").get<float>();
            kf.fogEnd = jkf.at("fogEnd").get<float>();
            wol.keyframes.push_back(kf);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wol-json: schema error: %s\n", e.what());
        return 1;
    }
    if (!wowee::pipeline::WoweeLightLoader::save(wol, outBase)) {
        std::fprintf(stderr,
            "import-wol-json: failed to save %s.wol\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wol (%zu keyframe%s, name=%s)\n",
                outBase.c_str(), wol.keyframes.size(),
                wol.keyframes.size() == 1 ? "" : "s",
                wol.name.c_str());
    return 0;
}

int handleValidateWow(int& i, int argc, char** argv) {
    // Walk every entry in a .wow and report structural problems:
    //   • unknown weather type id
    //   • intensity bounds out of [0, 1] or min > max
    //   • non-positive weight
    //   • duration bounds invalid (min > max, or = 0)
    //   • non-finite floats
    // Returns 0 PASS / 1 FAIL.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) ++i;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wow")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeWeatherLoader::exists(base)) {
        return reportMissing("WOW", base, ".wow");
    }
    auto wow = wowee::pipeline::WoweeWeatherLoader::load(base);
    std::vector<std::string> errors;
    if (wow.entries.empty()) errors.push_back("no entries");
    for (std::size_t k = 0; k < wow.entries.size(); ++k) {
        const auto& e = wow.entries[k];
        const std::string ks = std::to_string(k);
        if (e.weatherTypeId > wowee::pipeline::WoweeWeather::Blizzard) {
            errors.push_back("entry " + ks + " unknown typeId " +
                              std::to_string(e.weatherTypeId));
        }
        if (!std::isfinite(e.minIntensity) ||
            !std::isfinite(e.maxIntensity)) {
            errors.push_back("entry " + ks + " intensity not finite");
        }
        if (e.minIntensity < 0.0f || e.maxIntensity > 1.0f) {
            errors.push_back("entry " + ks + " intensity outside [0,1]");
        }
        if (e.minIntensity > e.maxIntensity) {
            errors.push_back("entry " + ks + " minIntensity > maxIntensity");
        }
        if (!std::isfinite(e.weight) || e.weight <= 0.0f) {
            errors.push_back("entry " + ks + " weight " +
                              std::to_string(e.weight) + " <= 0");
        }
        if (e.maxDurationSec == 0) {
            errors.push_back("entry " + ks + " maxDurationSec is 0");
        }
        if (e.minDurationSec > e.maxDurationSec) {
            errors.push_back("entry " + ks +
                              " minDurationSec > maxDurationSec");
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["wow"] = base + ".wow";
        j["passed"] = errors.empty();
        j["errorCount"] = errors.size();
        j["errors"] = errors;
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    if (errors.empty()) {
        std::printf("WOW %s.wow PASSED - %zu entry/entries valid\n",
                    base.c_str(), wow.entries.size());
        return 0;
    }
    std::printf("WOW %s.wow FAILED - %zu error(s):\n",
                base.c_str(), errors.size());
    for (const auto& e : errors) std::printf("  - %s\n", e.c_str());
    return 1;
}

int handleInfoWow(int& i, int argc, char** argv) {
    // Inspect a Wowee Open Weather (.wow) file: zone name +
    // per-entry weather type + intensity bounds + selection
    // weight + duration bounds.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) ++i;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wow")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeWeatherLoader::exists(base)) {
        return reportMissing("WOW", base, ".wow");
    }
    auto wow = wowee::pipeline::WoweeWeatherLoader::load(base);
    if (!wow.isValid()) {
        std::fprintf(stderr, "WOW parse failed: %s.wow\n", base.c_str());
        return 1;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["wow"] = base + ".wow";
        j["name"] = wow.name;
        j["entryCount"] = wow.entries.size();
        j["totalWeight"] = wow.totalWeight();
        nlohmann::json es = nlohmann::json::array();
        for (const auto& e : wow.entries) {
            es.push_back({
                {"type", wowee::pipeline::WoweeWeather::typeName(
                            e.weatherTypeId)},
                {"typeId", e.weatherTypeId},
                {"minIntensity", e.minIntensity},
                {"maxIntensity", e.maxIntensity},
                {"weight", e.weight},
                {"minDurationSec", e.minDurationSec},
                {"maxDurationSec", e.maxDurationSec},
            });
        }
        j["entries"] = es;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOW: %s.wow\n", base.c_str());
    std::printf("  zone       : %s\n", wow.name.c_str());
    std::printf("  entries    : %zu (totalWeight=%.2f)\n",
                wow.entries.size(), wow.totalWeight());
    for (std::size_t k = 0; k < wow.entries.size(); ++k) {
        const auto& e = wow.entries[k];
        std::printf("  [%zu] %-9s  intensity %.2f..%.2f  weight %.2f  "
                    "duration %u..%u s\n",
                    k,
                    wowee::pipeline::WoweeWeather::typeName(e.weatherTypeId),
                    e.minIntensity, e.maxIntensity, e.weight,
                    e.minDurationSec, e.maxDurationSec);
    }
    return 0;
}

int emitWeatherPreset(const std::string& cmdName,
                      int& i, int argc, char** argv,
                      wowee::pipeline::WoweeWeather (*maker)(const std::string&),
                      const char* presetDescription) {
    std::string base = argv[++i];
    std::string zoneName = "Default";
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        zoneName = argv[++i];
    }
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wow") {
        base = base.substr(0, base.size() - 4);
    }
    auto wow = maker(zoneName);
    if (!wowee::pipeline::WoweeWeatherLoader::save(wow, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wow\n",
                     cmdName.c_str(), base.c_str());
        return 1;
    }
    std::printf("Wrote %s.wow\n", base.c_str());
    std::printf("  zone       : %s\n", zoneName.c_str());
    std::printf("  preset     : %s (%zu entries)\n",
                presetDescription, wow.entries.size());
    return 0;
}

int handleGenWeatherTemperate(int& i, int argc, char** argv) {
    return emitWeatherPreset(
        "gen-weather-temperate", i, argc, argv,
        wowee::pipeline::WoweeWeatherLoader::makeTemperate,
        "clear-dominant + occasional rain + fog");
}

int handleGenWeatherArctic(int& i, int argc, char** argv) {
    return emitWeatherPreset(
        "gen-weather-arctic", i, argc, argv,
        wowee::pipeline::WoweeWeatherLoader::makeArctic,
        "snow-dominant + blizzard + fog");
}

int handleGenWeatherDesert(int& i, int argc, char** argv) {
    return emitWeatherPreset(
        "gen-weather-desert", i, argc, argv,
        wowee::pipeline::WoweeWeatherLoader::makeDesert,
        "clear-dominant + sandstorm");
}

int handleGenWeatherStormy(int& i, int argc, char** argv) {
    return emitWeatherPreset(
        "gen-weather-stormy", i, argc, argv,
        wowee::pipeline::WoweeWeatherLoader::makeStormy,
        "heavy rain + storm + occasional clear");
}

int handleExportWowJson(int& i, int argc, char** argv) {
    // Export a binary .wow to a human-editable JSON sidecar.
    // Pairs with --import-wow-json for the round-trip authoring
    // workflow on weather schedules.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wow")
        base = base.substr(0, base.size() - 4);
    if (outPath.empty()) outPath = base + ".wow.json";
    if (!wowee::pipeline::WoweeWeatherLoader::exists(base)) {
        return reportMissing("WOW", base, ".wow");
    }
    auto wow = wowee::pipeline::WoweeWeatherLoader::load(base);
    if (!wow.isValid()) {
        std::fprintf(stderr, "WOW parse failed: %s.wow\n", base.c_str());
        return 1;
    }
    nlohmann::json j;
    j["name"] = wow.name;
    nlohmann::json es = nlohmann::json::array();
    for (const auto& e : wow.entries) {
        es.push_back({
            {"type",
                wowee::pipeline::WoweeWeather::typeName(e.weatherTypeId)},
            {"typeId", e.weatherTypeId},
            {"minIntensity", e.minIntensity},
            {"maxIntensity", e.maxIntensity},
            {"weight", e.weight},
            {"minDurationSec", e.minDurationSec},
            {"maxDurationSec", e.maxDurationSec},
        });
    }
    j["entries"] = es;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wow-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    os << j.dump(2) << '\n';
    std::printf("Wrote %s (%zu entry/entries)\n",
                outPath.c_str(), wow.entries.size());
    return 0;
}

int handleImportWowJson(int& i, int argc, char** argv) {
    // Import a JSON sidecar back into binary .wow. The "type"
    // string field is human-friendly ("clear" / "rain" / etc.)
    // but typeId still wins if both are present, so users can
    // edit either. Schema mismatches fail with a clear message.
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (i + 1 < argc && argv[i + 1][0] != '-') outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        if (outBase.size() >= 9 &&
            outBase.substr(outBase.size() - 9) == ".wow.json") {
            outBase = outBase.substr(0, outBase.size() - 9);
        } else if (outBase.size() >= 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    if (outBase.size() >= 4 && outBase.substr(outBase.size() - 4) == ".wow") {
        outBase = outBase.substr(0, outBase.size() - 4);
    }
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wow-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { is >> j; } catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wow-json: parse error: %s\n", e.what());
        return 1;
    }
    auto typeFromName = [](const std::string& s) -> uint32_t {
        if (s == "clear")     return wowee::pipeline::WoweeWeather::Clear;
        if (s == "rain")      return wowee::pipeline::WoweeWeather::Rain;
        if (s == "snow")      return wowee::pipeline::WoweeWeather::Snow;
        if (s == "storm")     return wowee::pipeline::WoweeWeather::Storm;
        if (s == "sandstorm") return wowee::pipeline::WoweeWeather::Sandstorm;
        if (s == "fog")       return wowee::pipeline::WoweeWeather::Fog;
        if (s == "blizzard")  return wowee::pipeline::WoweeWeather::Blizzard;
        return wowee::pipeline::WoweeWeather::Clear;
    };
    wowee::pipeline::WoweeWeather wow;
    try {
        wow.name = j.value("name", std::string("Imported"));
        for (const auto& je : j.at("entries")) {
            wowee::pipeline::WoweeWeather::Entry e;
            if (je.contains("typeId")) {
                e.weatherTypeId = je.at("typeId").get<uint32_t>();
            } else if (je.contains("type")) {
                e.weatherTypeId = typeFromName(je.at("type").get<std::string>());
            }
            e.minIntensity = je.at("minIntensity").get<float>();
            e.maxIntensity = je.at("maxIntensity").get<float>();
            e.weight = je.at("weight").get<float>();
            e.minDurationSec = je.at("minDurationSec").get<uint32_t>();
            e.maxDurationSec = je.at("maxDurationSec").get<uint32_t>();
            wow.entries.push_back(e);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wow-json: schema error: %s\n", e.what());
        return 1;
    }
    if (!wowee::pipeline::WoweeWeatherLoader::save(wow, outBase)) {
        std::fprintf(stderr,
            "import-wow-json: failed to save %s.wow\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wow (%zu entry/entries, name=%s)\n",
                outBase.c_str(), wow.entries.size(), wow.name.c_str());
    return 0;
}

int handleGenZoneAtmosphere(int& i, int argc, char** argv) {
    // Convenience composite: drop both a default day/night WOL
    // and a temperate WOW into <zoneDir>/atmosphere.{wol,wow}.
    // Optional --preset <name> picks WOW + WOL preset pair:
    //   default  → makeDefaultDayNight + makeTemperate
    //   arctic   → makeNight            + makeArctic
    //   desert   → makeDefaultDayNight  + makeDesert
    //   stormy   → makeDefaultDayNight  + makeStormy
    //   cave     → makeCave             + makeTemperate (no rain UX, but kept for symmetry)
    std::string zoneDir = argv[++i];
    std::string zoneName = "Default";
    std::string preset = "default";
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        if (std::strcmp(argv[i + 1], "--name") == 0 && i + 2 < argc) {
            zoneName = argv[i + 2];
            i += 2;
        } else if (std::strcmp(argv[i + 1], "--preset") == 0 && i + 2 < argc) {
            preset = argv[i + 2];
            i += 2;
        } else {
            break;
        }
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(zoneDir, ec);
    if (ec) {
        std::fprintf(stderr,
            "gen-zone-atmosphere: cannot create %s: %s\n",
            zoneDir.c_str(), ec.message().c_str());
        return 1;
    }
    wowee::pipeline::WoweeLight wol;
    wowee::pipeline::WoweeWeather wow;
    if (preset == "arctic") {
        wol = wowee::pipeline::WoweeLightLoader::makeNight(zoneName);
        wow = wowee::pipeline::WoweeWeatherLoader::makeArctic(zoneName);
    } else if (preset == "desert") {
        wol = wowee::pipeline::WoweeLightLoader::makeDefaultDayNight(zoneName);
        wow = wowee::pipeline::WoweeWeatherLoader::makeDesert(zoneName);
    } else if (preset == "stormy") {
        wol = wowee::pipeline::WoweeLightLoader::makeDefaultDayNight(zoneName);
        wow = wowee::pipeline::WoweeWeatherLoader::makeStormy(zoneName);
    } else if (preset == "cave") {
        wol = wowee::pipeline::WoweeLightLoader::makeCave(zoneName);
        wow = wowee::pipeline::WoweeWeatherLoader::makeTemperate(zoneName);
    } else {
        wol = wowee::pipeline::WoweeLightLoader::makeDefaultDayNight(zoneName);
        wow = wowee::pipeline::WoweeWeatherLoader::makeTemperate(zoneName);
    }
    std::string wolBase = zoneDir + "/atmosphere";
    std::string wowBase = zoneDir + "/atmosphere";
    if (!wowee::pipeline::WoweeLightLoader::save(wol, wolBase)) {
        std::fprintf(stderr,
            "gen-zone-atmosphere: failed to save %s.wol\n", wolBase.c_str());
        return 1;
    }
    if (!wowee::pipeline::WoweeWeatherLoader::save(wow, wowBase)) {
        std::fprintf(stderr,
            "gen-zone-atmosphere: failed to save %s.wow\n", wowBase.c_str());
        return 1;
    }
    std::printf("Wrote zone atmosphere to %s/\n", zoneDir.c_str());
    std::printf("  zone       : %s\n", zoneName.c_str());
    std::printf("  preset     : %s\n", preset.c_str());
    std::printf("  atmosphere.wol : %zu light keyframe%s\n",
                wol.keyframes.size(), wol.keyframes.size() == 1 ? "" : "s");
    std::printf("  atmosphere.wow : %zu weather entry/entries\n",
                wow.entries.size());
    return 0;
}

}  // namespace

bool handleWorldInfo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-wob") == 0 && i + 1 < argc) {
        outRc = handleInfoWob(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wob-stats") == 0 && i + 1 < argc) {
        outRc = handleInfoWobStats(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wot") == 0 && i + 1 < argc) {
        outRc = handleInfoWot(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-woc") == 0 && i + 1 < argc) {
        outRc = handleInfoWoc(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wol") == 0 && i + 1 < argc) {
        outRc = handleInfoWol(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wol-at") == 0 && i + 2 < argc) {
        outRc = handleInfoWolAt(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wol") == 0 && i + 1 < argc) {
        outRc = handleValidateWol(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-light") == 0 && i + 1 < argc) {
        outRc = handleGenLight(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-light-cave") == 0 && i + 1 < argc) {
        outRc = handleGenLightCave(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-light-dungeon") == 0 && i + 1 < argc) {
        outRc = handleGenLightDungeon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-light-night") == 0 && i + 1 < argc) {
        outRc = handleGenLightNight(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wol-json") == 0 && i + 1 < argc) {
        outRc = handleExportWolJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wol-json") == 0 && i + 1 < argc) {
        outRc = handleImportWolJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wow") == 0 && i + 1 < argc) {
        outRc = handleInfoWow(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wow") == 0 && i + 1 < argc) {
        outRc = handleValidateWow(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-weather-temperate") == 0 && i + 1 < argc) {
        outRc = handleGenWeatherTemperate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-weather-arctic") == 0 && i + 1 < argc) {
        outRc = handleGenWeatherArctic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-weather-desert") == 0 && i + 1 < argc) {
        outRc = handleGenWeatherDesert(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-weather-stormy") == 0 && i + 1 < argc) {
        outRc = handleGenWeatherStormy(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-zone-atmosphere") == 0 && i + 1 < argc) {
        outRc = handleGenZoneAtmosphere(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wow-json") == 0 && i + 1 < argc) {
        outRc = handleExportWowJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wow-json") == 0 && i + 1 < argc) {
        outRc = handleImportWowJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
