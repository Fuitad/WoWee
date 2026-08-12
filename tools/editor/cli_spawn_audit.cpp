#include "cli_spawn_audit.hpp"
#include "cli_subprocess.hpp"

#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "zone_manifest.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleSnapZoneToGround(int& i, int argc, char** argv) {
    // Walk every creature + object in a zone and snap their Z
    // to the actual terrain height. Useful after terrain edits
    // or after --random-populate-zone if the spawn baseZ
    // doesn't match the carved terrain.
    //
    // Height lookup walks the loaded WHM tiles and finds the
    // chunk containing each spawn's (x, y), then uses the
    // chunk's average heightmap height + base.
    std::string zoneDir = argv[++i];
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "snap-zone-to-ground: %s has no zone.json\n",
            zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "snap-zone-to-ground: failed to parse %s\n",
            manifestPath.c_str());
        return 1;
    }
    // Load all tiles into a flat map keyed by (tx, ty).
    struct LoadedTile {
        wowee::pipeline::ADTTerrain terrain;
        int tx, ty;
    };
    std::vector<LoadedTile> tiles;
    for (const auto& [tx, ty] : zm.tiles) {
        std::string base = zoneDir + "/" + zm.mapName + "_" +
                            std::to_string(tx) + "_" + std::to_string(ty);
        if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) continue;
        LoadedTile lt;
        lt.tx = tx; lt.ty = ty;
        if (wowee::pipeline::WoweeTerrainLoader::load(base, lt.terrain)) {
            tiles.push_back(std::move(lt));
        }
    }
    if (tiles.empty()) {
        std::fprintf(stderr,
            "snap-zone-to-ground: no .whm tiles loaded\n");
        return 1;
    }
    // Compute terrain height at world (x, y) by finding the
    // chunk that contains it and averaging its heightmap. Each
    // chunk is 33.33y across; chunk position[1]=wowX origin,
    // [0]=wowY origin.
    constexpr float kChunkSize = 33.33333f;
    auto sampleHeight = [&](float wx, float wy) -> float {
        for (const auto& lt : tiles) {
            for (const auto& chunk : lt.terrain.chunks) {
                if (!chunk.heightMap.isLoaded()) continue;
                float cx0 = chunk.position[1];
                float cy0 = chunk.position[0];
                if (wx < cx0 || wx >= cx0 + kChunkSize) continue;
                if (wy < cy0 || wy >= cy0 + kChunkSize) continue;
                // Use average heightmap height to dodge the
                // need for full bilinear sampling. Good enough
                // for spawn placement; finer interpolation is
                // a future optimization.
                float sum = 0; int n = 0;
                for (float h : chunk.heightMap.heights) {
                    if (std::isfinite(h)) { sum += h; n++; }
                }
                if (n == 0) return chunk.position[2];
                return chunk.position[2] + sum / n;
            }
        }
        return zm.baseHeight;  // outside any loaded chunk
    };
    int snappedC = 0, snappedO = 0;
    // Creatures.
    wowee::editor::NpcSpawner spawner;
    std::string cpath = zoneDir + "/creatures.json";
    if (fs::exists(cpath) && spawner.loadFromFile(cpath)) {
        auto& spawns = spawner.getSpawns();
        for (auto& s : spawns) {
            s.position.z = sampleHeight(s.position.x, s.position.y);
            snappedC++;
        }
        if (snappedC > 0) spawner.saveToFile(cpath);
    }
    // Objects.
    wowee::editor::ObjectPlacer placer;
    std::string opath = zoneDir + "/objects.json";
    if (fs::exists(opath) && placer.loadFromFile(opath)) {
        auto& objs = placer.getObjects();
        for (auto& o : objs) {
            o.position.z = sampleHeight(o.position.x, o.position.y);
            snappedO++;
        }
        if (snappedO > 0) placer.saveToFile(opath);
    }
    std::printf("snap-zone-to-ground: %s\n", zoneDir.c_str());
    std::printf("  tiles loaded : %zu\n", tiles.size());
    std::printf("  creatures    : %d snapped\n", snappedC);
    std::printf("  objects      : %d snapped\n", snappedO);
    return 0;
}

int handleAuditZoneSpawns(int& i, int argc, char** argv) {
    // Non-destructive companion to --snap-zone-to-ground.
    // Loads the zone's terrain, walks every creature + object,
    // and flags any whose Z is more than <threshold> yards
    // off from the sampled terrain height. Useful for
    // surveying placement issues before deciding whether to
    // run --snap-zone-to-ground (which would silently rewrite
    // every spawn).
    std::string zoneDir = argv[++i];
    float threshold = 5.0f;
    if (i + 2 < argc && std::strcmp(argv[i + 1], "--threshold") == 0) {
        try { threshold = std::stof(argv[i + 2]); i += 2; }
        catch (...) {}
    }
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "audit-zone-spawns: %s has no zone.json\n",
            zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "audit-zone-spawns: failed to parse %s\n",
            manifestPath.c_str());
        return 1;
    }
    // Same chunk-average sampler as --snap-zone-to-ground.
    // Returning baseHeight when no chunk hits = "no terrain
    // data here", so flag those too via the threshold check.
    struct LoadedTile {
        wowee::pipeline::ADTTerrain terrain;
    };
    std::vector<LoadedTile> tiles;
    for (const auto& [tx, ty] : zm.tiles) {
        std::string base = zoneDir + "/" + zm.mapName + "_" +
                            std::to_string(tx) + "_" + std::to_string(ty);
        if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) continue;
        LoadedTile lt;
        if (wowee::pipeline::WoweeTerrainLoader::load(base, lt.terrain)) {
            tiles.push_back(std::move(lt));
        }
    }
    constexpr float kChunkSize = 33.33333f;
    auto sampleHeight = [&](float wx, float wy) -> float {
        for (const auto& lt : tiles) {
            for (const auto& chunk : lt.terrain.chunks) {
                if (!chunk.heightMap.isLoaded()) continue;
                float cx0 = chunk.position[1];
                float cy0 = chunk.position[0];
                if (wx < cx0 || wx >= cx0 + kChunkSize) continue;
                if (wy < cy0 || wy >= cy0 + kChunkSize) continue;
                float sum = 0; int n = 0;
                for (float h : chunk.heightMap.heights) {
                    if (std::isfinite(h)) { sum += h; n++; }
                }
                if (n == 0) return chunk.position[2];
                return chunk.position[2] + sum / n;
            }
        }
        return zm.baseHeight;
    };
    struct Issue { std::string kind; std::string name;
                   float spawnZ, terrainZ; };
    std::vector<Issue> issues;
    wowee::editor::NpcSpawner spawner;
    if (fs::exists(zoneDir + "/creatures.json") &&
        spawner.loadFromFile(zoneDir + "/creatures.json")) {
        for (const auto& s : spawner.getSpawns()) {
            float th = sampleHeight(s.position.x, s.position.y);
            if (std::fabs(s.position.z - th) > threshold) {
                issues.push_back({"creature", s.name,
                                  s.position.z, th});
            }
        }
    }
    wowee::editor::ObjectPlacer placer;
    if (fs::exists(zoneDir + "/objects.json") &&
        placer.loadFromFile(zoneDir + "/objects.json")) {
        for (const auto& o : placer.getObjects()) {
            float th = sampleHeight(o.position.x, o.position.y);
            if (std::fabs(o.position.z - th) > threshold) {
                issues.push_back({"object", o.path,
                                  o.position.z, th});
            }
        }
    }
    std::printf("audit-zone-spawns: %s\n", zoneDir.c_str());
    std::printf("  threshold    : %.1f yards\n", threshold);
    std::printf("  creatures    : %zu\n", spawner.spawnCount());
    std::printf("  objects      : %zu\n", placer.getObjects().size());
    std::printf("  issues       : %zu\n", issues.size());
    if (issues.empty()) {
        std::printf("\n  PASSED - every spawn is within %.1f y of the terrain\n",
                    threshold);
        return 0;
    }
    std::printf("\n  Flagged spawns (delta = spawnZ - terrainZ):\n");
    std::printf("  kind      delta    spawnZ   terrainZ  name\n");
    for (const auto& iss : issues) {
        float delta = iss.spawnZ - iss.terrainZ;
        std::printf("  %-8s  %+6.1f   %7.1f   %7.1f  %s\n",
                    iss.kind.c_str(), delta, iss.spawnZ,
                    iss.terrainZ,
                    iss.name.substr(0, 40).c_str());
    }
    std::printf("\n  Run --snap-zone-to-ground to fix in bulk.\n");
    return 1;
}

int handleListZoneSpawns(int& i, int argc, char** argv) {
    // Combined creature + object listing. Useful for a quick
    // "what's in this zone" survey without running both
    // --info-creatures and --info-objects separately.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "list-zone-spawns: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::NpcSpawner spawner;
    wowee::editor::ObjectPlacer placer;
    spawner.loadFromFile(zoneDir + "/creatures.json");
    placer.loadFromFile(zoneDir + "/objects.json");
    const auto& spawns = spawner.getSpawns();
    const auto& objs = placer.getObjects();
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["creatureCount"] = spawns.size();
        j["objectCount"] = objs.size();
        nlohmann::json carr = nlohmann::json::array();
        for (const auto& s : spawns) {
            carr.push_back({{"name", s.name},
                             {"level", s.level},
                             {"x", s.position.x},
                             {"y", s.position.y},
                             {"z", s.position.z},
                             {"hostile", s.hostile}});
        }
        j["creatures"] = carr;
        nlohmann::json oarr = nlohmann::json::array();
        for (const auto& o : objs) {
            oarr.push_back({{"path", o.path},
                             {"type", o.type == wowee::editor::PlaceableType::M2 ? "m2" : "wmo"},
                             {"x", o.position.x},
                             {"y", o.position.y},
                             {"z", o.position.z},
                             {"scale", o.scale}});
        }
        j["objects"] = oarr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone spawns: %s\n", zoneDir.c_str());
    std::printf("  creatures : %zu\n", spawns.size());
    std::printf("  objects   : %zu\n", objs.size());
    if (!spawns.empty()) {
        std::printf("\n  Creatures:\n");
        std::printf("    idx  lvl  hostile  x         y         z         name\n");
        for (size_t k = 0; k < spawns.size(); ++k) {
            const auto& s = spawns[k];
            std::printf("    %3zu  %3u  %-7s  %8.1f  %8.1f  %8.1f  %s\n",
                        k, s.level, s.hostile ? "yes" : "no",
                        s.position.x, s.position.y, s.position.z,
                        s.name.c_str());
        }
    }
    if (!objs.empty()) {
        std::printf("\n  Objects:\n");
        std::printf("    idx  type  scale  x         y         z         path\n");
        for (size_t k = 0; k < objs.size(); ++k) {
            const auto& o = objs[k];
            std::printf("    %3zu  %-4s  %5.2f  %8.1f  %8.1f  %8.1f  %s\n",
                        k,
                        o.type == wowee::editor::PlaceableType::M2 ? "m2" : "wmo",
                        o.scale,
                        o.position.x, o.position.y, o.position.z,
                        o.path.c_str());
        }
    }
    return 0;
}

int handleDiffZoneSpawns(int& i, int argc, char** argv) {
    // Compare two zones' creatures + objects. Matches by
    // (kind, name) - paired entries with mismatched positions
    // are reported as "moved" with the delta. Entries that
    // exist in only one zone are added/removed.
    //
    // Useful for "what did the new branch change vs main"
    // before merging, or for confirming a copy-zone-items
    // produced what was expected.
    std::string aDir = argv[++i];
    std::string bDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(aDir + "/zone.json")) {
        std::fprintf(stderr,
            "diff-zone-spawns: %s has no zone.json\n", aDir.c_str());
        return 1;
    }
    if (!fs::exists(bDir + "/zone.json")) {
        std::fprintf(stderr,
            "diff-zone-spawns: %s has no zone.json\n", bDir.c_str());
        return 1;
    }
    // Multiset key: kind/name. Position comes along so we can
    // report "moved" deltas when a name appears in both with
    // different XYZ.
    struct Entry { std::string kind, name; glm::vec3 pos; };
    auto load = [&](const std::string& dir) {
        std::vector<Entry> out;
        wowee::editor::NpcSpawner spawner;
        if (spawner.loadFromFile(dir + "/creatures.json")) {
            for (const auto& s : spawner.getSpawns()) {
                out.push_back({"creature", s.name, s.position});
            }
        }
        wowee::editor::ObjectPlacer placer;
        if (placer.loadFromFile(dir + "/objects.json")) {
            for (const auto& o : placer.getObjects()) {
                out.push_back({"object", o.path, o.position});
            }
        }
        return out;
    };
    auto av = load(aDir);
    auto bv = load(bDir);
    // Sort each side for stable key matching.
    auto cmp = [](const Entry& x, const Entry& y) {
        if (x.kind != y.kind) return x.kind < y.kind;
        return x.name < y.name;
    };
    std::sort(av.begin(), av.end(), cmp);
    std::sort(bv.begin(), bv.end(), cmp);
    int added = 0, removed = 0, moved = 0, same = 0;
    std::vector<std::string> diffs;
    // Two-pointer walk: equal keys → check position; A-only →
    // removed; B-only → added.
    size_t i_a = 0, i_b = 0;
    while (i_a < av.size() || i_b < bv.size()) {
        if (i_a < av.size() && i_b < bv.size() &&
            av[i_a].kind == bv[i_b].kind &&
            av[i_a].name == bv[i_b].name) {
            glm::vec3 d = bv[i_b].pos - av[i_a].pos;
            float dlen = glm::length(d);
            if (dlen > 0.5f) {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "  moved   %-9s %-30s by (%+.1f, %+.1f, %+.1f)",
                    av[i_a].kind.c_str(),
                    av[i_a].name.substr(0, 30).c_str(),
                    d.x, d.y, d.z);
                diffs.push_back(buf);
                moved++;
            } else {
                same++;
            }
            i_a++; i_b++;
        } else if (i_b == bv.size() ||
                   (i_a < av.size() && cmp(av[i_a], bv[i_b]))) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "  removed %-9s %s",
                av[i_a].kind.c_str(),
                av[i_a].name.substr(0, 60).c_str());
            diffs.push_back(buf);
            removed++;
            i_a++;
        } else {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "  added   %-9s %s",
                bv[i_b].kind.c_str(),
                bv[i_b].name.substr(0, 60).c_str());
            diffs.push_back(buf);
            added++;
            i_b++;
        }
    }
    std::printf("diff-zone-spawns: %s -> %s\n",
                aDir.c_str(), bDir.c_str());
    std::printf("  added   : %d\n", added);
    std::printf("  removed : %d\n", removed);
    std::printf("  moved   : %d (>0.5y)\n", moved);
    std::printf("  same    : %d\n", same);
    if (!diffs.empty()) {
        std::printf("\n");
        for (const auto& d : diffs) std::printf("%s\n", d.c_str());
    }
    return (added + removed + moved) == 0 ? 0 : 1;
}

int handleInfoSpawn(int& i, int argc, char** argv) {
    // Detailed view of one creature or object by index. The
    // list-zone-spawns table only shows headline fields; this
    // dumps every field including AI behavior, faction,
    // patrol path waypoints, etc.
    std::string zoneDir = argv[++i];
    std::string kind = argv[++i];
    int idx = -1;
    try { idx = std::stoi(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "info-spawn: <index> must be an integer\n");
        return 1;
    }
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    std::transform(kind.begin(), kind.end(), kind.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (kind == "creature") {
        wowee::editor::NpcSpawner spawner;
        if (!spawner.loadFromFile(zoneDir + "/creatures.json")) {
            std::fprintf(stderr,
                "info-spawn: %s has no creatures.json\n",
                zoneDir.c_str());
            return 1;
        }
        const auto& spawns = spawner.getSpawns();
        if (idx < 0 || static_cast<size_t>(idx) >= spawns.size()) {
            std::fprintf(stderr,
                "info-spawn: index %d out of range (have %zu)\n",
                idx, spawns.size());
            return 1;
        }
        const auto& s = spawns[idx];
        static const char* behaviors[] = {
            "Stationary", "Patrol", "Wander", "Scripted"
        };
        int bIdx = static_cast<int>(s.behavior);
        if (bIdx < 0 || bIdx > 3) bIdx = 0;
        if (jsonOut) {
            nlohmann::json j;
            j["zone"] = zoneDir;
            j["kind"] = "creature";
            j["index"] = idx;
            j["id"] = s.id;
            j["name"] = s.name;
            j["modelPath"] = s.modelPath;
            j["displayId"] = s.displayId;
            j["position"] = {s.position.x, s.position.y, s.position.z};
            j["orientation"] = s.orientation;
            j["level"] = s.level;
            j["health"] = s.health;
            j["mana"] = s.mana;
            j["faction"] = s.faction;
            j["scale"] = s.scale;
            j["behavior"] = behaviors[bIdx];
            j["wanderRadius"] = s.wanderRadius;
            j["aggroRadius"] = s.aggroRadius;
            j["leashRadius"] = s.leashRadius;
            j["respawnTimeMs"] = s.respawnTimeMs;
            j["hostile"] = s.hostile;
            j["questgiver"] = s.questgiver;
            j["vendor"] = s.vendor;
            j["trainer"] = s.trainer;
            j["patrolPathSize"] = s.patrolPath.size();
            std::printf("%s\n", j.dump(2).c_str());
            return 0;
        }
        std::printf("Creature spawn %d in %s\n", idx, zoneDir.c_str());
        std::printf("  id            : %u\n", s.id);
        std::printf("  name          : %s\n", s.name.c_str());
        std::printf("  modelPath     : %s\n",
                    s.modelPath.empty() ? "(template)" : s.modelPath.c_str());
        std::printf("  displayId     : %u\n", s.displayId);
        std::printf("  position      : (%.2f, %.2f, %.2f)\n",
                    s.position.x, s.position.y, s.position.z);
        std::printf("  orientation   : %.1f°\n", s.orientation);
        std::printf("  level         : %u\n", s.level);
        std::printf("  health/mana   : %u / %u\n", s.health, s.mana);
        std::printf("  faction       : %u\n", s.faction);
        std::printf("  scale         : %.2f\n", s.scale);
        std::printf("  behavior      : %s\n", behaviors[bIdx]);
        std::printf("  wander/aggro  : %.1f / %.1f y\n",
                    s.wanderRadius, s.aggroRadius);
        std::printf("  leash         : %.1f y\n", s.leashRadius);
        std::printf("  respawn       : %.0f s\n", s.respawnTimeMs / 1000.0f);
        std::printf("  flags         : %s%s%s%s\n",
                    s.hostile ? "hostile " : "",
                    s.questgiver ? "questgiver " : "",
                    s.vendor ? "vendor " : "",
                    s.trainer ? "trainer " : "");
        std::printf("  patrol path   : %zu waypoint(s)\n",
                    s.patrolPath.size());
        return 0;
    } else if (kind == "object") {
        wowee::editor::ObjectPlacer placer;
        if (!placer.loadFromFile(zoneDir + "/objects.json")) {
            std::fprintf(stderr,
                "info-spawn: %s has no objects.json\n",
                zoneDir.c_str());
            return 1;
        }
        const auto& objs = placer.getObjects();
        if (idx < 0 || static_cast<size_t>(idx) >= objs.size()) {
            std::fprintf(stderr,
                "info-spawn: index %d out of range (have %zu)\n",
                idx, objs.size());
            return 1;
        }
        const auto& o = objs[idx];
        if (jsonOut) {
            nlohmann::json j;
            j["zone"] = zoneDir;
            j["kind"] = "object";
            j["index"] = idx;
            j["uniqueId"] = o.uniqueId;
            j["path"] = o.path;
            j["type"] = o.type == wowee::editor::PlaceableType::M2 ? "m2" : "wmo";
            j["position"] = {o.position.x, o.position.y, o.position.z};
            j["rotation"] = {o.rotation.x, o.rotation.y, o.rotation.z};
            j["scale"] = o.scale;
            std::printf("%s\n", j.dump(2).c_str());
            return 0;
        }
        std::printf("Object spawn %d in %s\n", idx, zoneDir.c_str());
        std::printf("  uniqueId  : %u\n", o.uniqueId);
        std::printf("  path      : %s\n", o.path.c_str());
        std::printf("  type      : %s\n",
                    o.type == wowee::editor::PlaceableType::M2 ? "m2" : "wmo");
        std::printf("  position  : (%.2f, %.2f, %.2f)\n",
                    o.position.x, o.position.y, o.position.z);
        std::printf("  rotation  : (%.2f, %.2f, %.2f) rad\n",
                    o.rotation.x, o.rotation.y, o.rotation.z);
        std::printf("  scale     : %.2f\n", o.scale);
        return 0;
    }
    std::fprintf(stderr,
        "info-spawn: kind must be 'creature' or 'object' (got '%s')\n",
        kind.c_str());
    return 1;
}

int handleListProjectSpawns(int& i, int argc, char** argv) {
    // Project-wide companion to --list-zone-spawns. Combines
    // creatures + objects across every zone into one big
    // listing keyed by (zone, kind, name). Useful for project-
    // wide review and for piping into spreadsheets via --json.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "list-project-spawns: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    int totalCreat = 0, totalObj = 0;
    struct Row {
        std::string zone, kind, name;
        float x, y, z;
        std::string extra;
    };
    std::vector<Row> rows;
    for (const auto& zoneDir : zones) {
        std::string zname = fs::path(zoneDir).filename().string();
        wowee::editor::NpcSpawner spawner;
        if (spawner.loadFromFile(zoneDir + "/creatures.json")) {
            for (const auto& s : spawner.getSpawns()) {
                Row r;
                r.zone = zname;
                r.kind = "creature";
                r.name = s.name;
                r.x = s.position.x; r.y = s.position.y;
                r.z = s.position.z;
                r.extra = "lvl " + std::to_string(s.level);
                rows.push_back(r);
                totalCreat++;
            }
        }
        wowee::editor::ObjectPlacer placer;
        if (placer.loadFromFile(zoneDir + "/objects.json")) {
            for (const auto& o : placer.getObjects()) {
                Row r;
                r.zone = zname;
                r.kind = "object";
                r.name = o.path;
                r.x = o.position.x; r.y = o.position.y;
                r.z = o.position.z;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "scale %.2f", o.scale);
                r.extra = buf;
                rows.push_back(r);
                totalObj++;
            }
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zoneCount"] = zones.size();
        j["creatureCount"] = totalCreat;
        j["objectCount"] = totalObj;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({{"zone", r.zone},
                            {"kind", r.kind},
                            {"name", r.name},
                            {"x", r.x}, {"y", r.y}, {"z", r.z},
                            {"extra", r.extra}});
        }
        j["spawns"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project spawns: %s\n", projectDir.c_str());
    std::printf("  zones      : %zu\n", zones.size());
    std::printf("  creatures  : %d\n", totalCreat);
    std::printf("  objects    : %d\n", totalObj);
    if (rows.empty()) {
        std::printf("\n  *no spawns in any zone*\n");
        return 0;
    }
    std::printf("\n  zone                  kind      x         y         z         info       name\n");
    for (const auto& r : rows) {
        std::printf("  %-20s  %-8s  %8.1f  %8.1f  %8.1f  %-10s %s\n",
                    r.zone.substr(0, 20).c_str(),
                    r.kind.c_str(),
                    r.x, r.y, r.z,
                    r.extra.c_str(),
                    r.name.substr(0, 60).c_str());
    }
    return 0;
}

int handleAuditProjectSpawns(int& i, int argc, char** argv) {
    // Project-wide wrapper around --audit-zone-spawns. Spawns
    // the binary per-zone (only those with creatures.json or
    // objects.json), aggregates how many issues each zone has,
    // and exits 1 if any zone reports problems. CI-friendly
    // pre-release placement check.
    std::string projectDir = argv[++i];
    std::string thresholdArg;
    if (i + 2 < argc && std::strcmp(argv[i + 1], "--threshold") == 0) {
        thresholdArg = argv[i + 2];
        i += 2;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "audit-project-spawns: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        bool hasContent = fs::exists(entry.path() / "creatures.json") ||
                           fs::exists(entry.path() / "objects.json");
        if (!hasContent) continue;
        zones.push_back(entry.path().string());
    }
    std::sort(zones.begin(), zones.end());
    if (zones.empty()) {
        std::printf("audit-project-spawns: %s\n", projectDir.c_str());
        std::printf("  no zones with creatures.json or objects.json\n");
        return 0;
    }
    std::string self = argv[0];
    int passed = 0, failed = 0;
    std::printf("audit-project-spawns: %s\n", projectDir.c_str());
    std::printf("  zones to audit : %zu\n", zones.size());
    if (!thresholdArg.empty()) {
        std::printf("  threshold      : %s yards\n", thresholdArg.c_str());
    }
    std::printf("\n");
    for (const auto& zoneDir : zones) {
        std::printf("--- %s ---\n",
                    fs::path(zoneDir).filename().string().c_str());
        std::fflush(stdout);
        std::vector<std::string> args = {"--audit-zone-spawns", zoneDir};
        if (!thresholdArg.empty()) {
            args.push_back("--threshold");
            args.push_back(thresholdArg);
        }
        int rc = wowee::editor::cli::runChild(self, args);
        if (rc == 0) passed++;
        else failed++;
    }
    std::printf("\n--- summary ---\n");
    std::printf("  passed : %d\n", passed);
    std::printf("  failed : %d\n", failed);
    if (failed == 0) {
        std::printf("\n  ALL ZONES PASSED\n");
        return 0;
    }
    std::printf("\n  Run --snap-project-to-ground to fix in bulk.\n");
    return 1;
}

int handleSnapProjectToGround(int& i, int argc, char** argv) {
    // Orchestrator wrapper around --snap-zone-to-ground. Spawns
    // the binary per-zone (only zones with at least one of
    // creatures.json or objects.json since pure-terrain zones
    // have nothing to snap), aggregates a final summary.
    std::string projectDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "snap-project-to-ground: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        bool hasContent = fs::exists(entry.path() / "creatures.json") ||
                           fs::exists(entry.path() / "objects.json");
        if (!hasContent) continue;
        zones.push_back(entry.path().string());
    }
    std::sort(zones.begin(), zones.end());
    if (zones.empty()) {
        std::printf("snap-project-to-ground: %s\n", projectDir.c_str());
        std::printf("  no zones with creatures.json or objects.json\n");
        return 0;
    }
    std::string self = argv[0];
    int passed = 0, failed = 0;
    std::printf("snap-project-to-ground: %s\n", projectDir.c_str());
    std::printf("  zones to snap : %zu\n\n", zones.size());
    for (const auto& zoneDir : zones) {
        std::printf("--- %s ---\n",
                    fs::path(zoneDir).filename().string().c_str());
        std::fflush(stdout);
        int rc = wowee::editor::cli::runChild(self,
            {"--snap-zone-to-ground", zoneDir});
        if (rc == 0) passed++;
        else failed++;
    }
    std::printf("\n--- summary ---\n");
    std::printf("  zones snapped : %d\n", passed);
    std::printf("  failed        : %d\n", failed);
    return failed == 0 ? 0 : 1;
}


}  // namespace

bool handleSpawnAudit(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--snap-zone-to-ground") == 0 && i + 1 < argc) {
        outRc = handleSnapZoneToGround(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--audit-zone-spawns") == 0 && i + 1 < argc) {
        outRc = handleAuditZoneSpawns(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-zone-spawns") == 0 && i + 1 < argc) {
        outRc = handleListZoneSpawns(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--diff-zone-spawns") == 0 && i + 2 < argc) {
        outRc = handleDiffZoneSpawns(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-spawn") == 0 && i + 3 < argc) {
        outRc = handleInfoSpawn(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-project-spawns") == 0 && i + 1 < argc) {
        outRc = handleListProjectSpawns(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--audit-project-spawns") == 0 && i + 1 < argc) {
        outRc = handleAuditProjectSpawns(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--snap-project-to-ground") == 0 && i + 1 < argc) {
        outRc = handleSnapProjectToGround(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
