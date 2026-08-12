#include "cli_format_validate.hpp"
#include "zone_manifest.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_subprocess.hpp"

#include "pipeline/wowee_model.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "content_pack.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

std::vector<std::string> validateWomErrors(
        const wowee::pipeline::WoweeModel& wom) {
    std::vector<std::string> errors;
    if (wom.version < 1 || wom.version > 3) {
        errors.push_back("version " + std::to_string(wom.version) +
                         " outside [1,3]");
    }
    if (!wom.isValid()) errors.push_back("empty geometry (no verts/indices)");
    if (wom.indices.size() % 3 != 0) {
        errors.push_back("indices.size()=" + std::to_string(wom.indices.size()) +
                         " not divisible by 3");
    }
    int oobIdx = 0;
    for (uint32_t idx : wom.indices) {
        if (idx >= wom.vertices.size()) {
            if (++oobIdx <= 3) {
                errors.push_back("index " + std::to_string(idx) +
                                 " >= vertexCount " +
                                 std::to_string(wom.vertices.size()));
            }
        }
    }
    if (oobIdx > 3) {
        errors.push_back("... and " + std::to_string(oobIdx - 3) +
                         " more out-of-range indices");
    }
    for (size_t b = 0; b < wom.bones.size(); ++b) {
        int16_t p = wom.bones[b].parentBone;
        if (p == -1) continue;
        if (p < 0 || p >= static_cast<int16_t>(wom.bones.size())) {
            errors.push_back("bone " + std::to_string(b) +
                             " parent=" + std::to_string(p) +
                             " out of range");
        } else if (p >= static_cast<int16_t>(b)) {
            errors.push_back("bone " + std::to_string(b) +
                             " parent=" + std::to_string(p) +
                             " not strictly less (DAG order)");
        }
    }
    int oobVB = 0;
    for (size_t v = 0; v < wom.vertices.size() && !wom.bones.empty(); ++v) {
        const auto& vert = wom.vertices[v];
        for (int k = 0; k < 4; ++k) {
            if (vert.boneWeights[k] == 0) continue;
            if (vert.boneIndices[k] >= wom.bones.size()) {
                if (++oobVB <= 3) {
                    errors.push_back("vertex " + std::to_string(v) +
                                     " boneIndex[" + std::to_string(k) +
                                     "]=" + std::to_string(vert.boneIndices[k]) +
                                     " >= boneCount " +
                                     std::to_string(wom.bones.size()));
                }
            }
        }
    }
    if (oobVB > 3) {
        errors.push_back("... and " + std::to_string(oobVB - 3) +
                         " more out-of-range vertex bone refs");
    }
    for (size_t a = 0; a < wom.animations.size(); ++a) {
        const auto& anim = wom.animations[a];
        if (!anim.boneKeyframes.empty() &&
            anim.boneKeyframes.size() != wom.bones.size()) {
            errors.push_back("animation " + std::to_string(a) +
                             " boneKeyframes.size()=" +
                             std::to_string(anim.boneKeyframes.size()) +
                             " != boneCount " +
                             std::to_string(wom.bones.size()));
        }
    }
    for (size_t b = 0; b < wom.batches.size(); ++b) {
        const auto& batch = wom.batches[b];
        uint64_t end = uint64_t(batch.indexStart) + batch.indexCount;
        if (end > wom.indices.size()) {
            errors.push_back("batch " + std::to_string(b) +
                             " indexStart+Count=" + std::to_string(end) +
                             " > indexCount " +
                             std::to_string(wom.indices.size()));
        }
        if (batch.indexCount % 3 != 0) {
            errors.push_back("batch " + std::to_string(b) +
                             " indexCount=" + std::to_string(batch.indexCount) +
                             " not divisible by 3");
        }
        if (!wom.texturePaths.empty() &&
            batch.textureIndex >= wom.texturePaths.size()) {
            errors.push_back("batch " + std::to_string(b) +
                             " textureIndex=" + std::to_string(batch.textureIndex) +
                             " >= textureCount " +
                             std::to_string(wom.texturePaths.size()));
        }
    }
    if (wom.boundMin.x > wom.boundMax.x ||
        wom.boundMin.y > wom.boundMax.y ||
        wom.boundMin.z > wom.boundMax.z) {
        errors.push_back("boundMin > boundMax on at least one axis");
    }
    if (wom.boundRadius < 0.0f) {
        errors.push_back("boundRadius=" + std::to_string(wom.boundRadius) +
                         " is negative");
    }
    return errors;
}

std::vector<std::string> validateWobErrors(
        const wowee::pipeline::WoweeBuilding& bld) {
    std::vector<std::string> errors;
    if (!bld.isValid()) errors.push_back("empty building (no groups)");
    int badMatTexCount = 0;
    for (size_t g = 0; g < bld.groups.size(); ++g) {
        const auto& grp = bld.groups[g];
        if (grp.indices.size() % 3 != 0) {
            errors.push_back("group " + std::to_string(g) +
                             " indices.size()=" + std::to_string(grp.indices.size()) +
                             " not divisible by 3");
        }
        int oobIdx = 0;
        for (uint32_t idx : grp.indices) {
            if (idx >= grp.vertices.size()) ++oobIdx;
        }
        if (oobIdx > 0) {
            errors.push_back("group " + std::to_string(g) + " has " +
                             std::to_string(oobIdx) +
                             " indices out of range (vertCount=" +
                             std::to_string(grp.vertices.size()) + ")");
        }
        for (size_t m = 0; m < grp.materials.size(); ++m) {
            if (grp.materials[m].texturePath.empty()) {
                badMatTexCount++;
                if (badMatTexCount <= 3) {
                    errors.push_back("group " + std::to_string(g) +
                                     " material " + std::to_string(m) +
                                     " has empty texturePath");
                }
            }
        }
        if (grp.boundMin.x > grp.boundMax.x ||
            grp.boundMin.y > grp.boundMax.y ||
            grp.boundMin.z > grp.boundMax.z) {
            errors.push_back("group " + std::to_string(g) +
                             " boundMin > boundMax on at least one axis");
        }
    }
    if (badMatTexCount > 3) {
        errors.push_back("... and " + std::to_string(badMatTexCount - 3) +
                         " more empty material textures");
    }
    int badPortal = 0;
    for (size_t p = 0; p < bld.portals.size(); ++p) {
        const auto& portal = bld.portals[p];
        auto inRange = [&](int g) {
            return g == -1 ||
                   (g >= 0 && g < static_cast<int>(bld.groups.size()));
        };
        if (!inRange(portal.groupA) || !inRange(portal.groupB)) {
            if (++badPortal <= 3) {
                errors.push_back("portal " + std::to_string(p) +
                                 " refs out-of-range groups (" +
                                 std::to_string(portal.groupA) + ", " +
                                 std::to_string(portal.groupB) + ")");
            }
        }
        if (portal.vertices.size() < 3) {
            if (++badPortal <= 3) {
                errors.push_back("portal " + std::to_string(p) +
                                 " has only " +
                                 std::to_string(portal.vertices.size()) +
                                 " verts (need >= 3 for a polygon)");
            }
        }
    }
    if (badPortal > 3) {
        errors.push_back("... and " + std::to_string(badPortal - 3) +
                         " more bad portal entries");
    }
    int badDoodad = 0;
    for (size_t d = 0; d < bld.doodads.size(); ++d) {
        const auto& doodad = bld.doodads[d];
        if (doodad.modelPath.empty()) {
            if (++badDoodad <= 3) {
                errors.push_back("doodad " + std::to_string(d) +
                                 " has empty modelPath");
            }
        }
        if (!std::isfinite(doodad.scale) || doodad.scale <= 0.0f) {
            if (++badDoodad <= 3) {
                errors.push_back("doodad " + std::to_string(d) +
                                 " has non-positive scale " +
                                 std::to_string(doodad.scale));
            }
        }
    }
    if (badDoodad > 3) {
        errors.push_back("... and " + std::to_string(badDoodad - 3) +
                         " more bad doodad entries");
    }
    if (bld.boundRadius < 0.0f) {
        errors.push_back("boundRadius=" + std::to_string(bld.boundRadius) +
                         " is negative");
    }
    return errors;
}

std::vector<std::string> validateWocErrors(
        const wowee::pipeline::WoweeCollision& woc) {
    std::vector<std::string> errors;
    if (!woc.isValid()) errors.push_back("empty collision (no triangles)");
    if (woc.tileX >= 64 || woc.tileY >= 64) {
        errors.push_back("tile coords out of WoW grid: (" +
                         std::to_string(woc.tileX) + ", " +
                         std::to_string(woc.tileY) + ") - must be < 64");
    }
    int nanTris = 0, degenerate = 0, badFlags = 0;
    auto isFiniteVec = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    constexpr uint8_t kKnownFlags = 0x0F;  // walkable|water|steep|indoor
    for (size_t t = 0; t < woc.triangles.size(); ++t) {
        const auto& tri = woc.triangles[t];
        if (!isFiniteVec(tri.v0) || !isFiniteVec(tri.v1) || !isFiniteVec(tri.v2)) {
            if (++nanTris <= 3) {
                errors.push_back("triangle " + std::to_string(t) +
                                 " has non-finite vertex coord");
            }
        }
        if (tri.v0 == tri.v1 || tri.v1 == tri.v2 || tri.v0 == tri.v2) {
            if (++degenerate <= 3) {
                errors.push_back("triangle " + std::to_string(t) +
                                 " is degenerate (two vertices identical)");
            }
        }
        if (tri.flags & ~kKnownFlags) {
            if (++badFlags <= 3) {
                errors.push_back("triangle " + std::to_string(t) +
                                 " has unknown flag bits 0x" +
                                 [&]{ char b[8]; std::snprintf(b,sizeof b,"%02X",tri.flags); return std::string(b); }());
            }
        }
    }
    if (nanTris > 3) errors.push_back("... and " + std::to_string(nanTris - 3) +
                                       " more non-finite triangles");
    if (degenerate > 3) errors.push_back("... and " + std::to_string(degenerate - 3) +
                                          " more degenerate triangles");
    if (badFlags > 3) errors.push_back("... and " + std::to_string(badFlags - 3) +
                                        " more triangles with unknown flag bits");
    if (woc.bounds.min.x > woc.bounds.max.x ||
        woc.bounds.min.y > woc.bounds.max.y ||
        woc.bounds.min.z > woc.bounds.max.z) {
        errors.push_back("bounds.min > bounds.max on at least one axis");
    }
    return errors;
}

std::vector<std::string> validateWhmErrors(
        const wowee::pipeline::ADTTerrain& terrain) {
    std::vector<std::string> errors;
    if (!terrain.isLoaded()) {
        errors.push_back("terrain not loaded");
        return errors;
    }
    if (terrain.coord.x < 0 || terrain.coord.x >= 64 ||
        terrain.coord.y < 0 || terrain.coord.y >= 64) {
        errors.push_back("tile coord out of WoW grid: (" +
                         std::to_string(terrain.coord.x) + ", " +
                         std::to_string(terrain.coord.y) + ")");
    }
    int nanHeightChunks = 0, nanPosChunks = 0;
    int loadedChunks = 0;
    float minH = 1e30f, maxH = -1e30f;
    for (size_t c = 0; c < 256; ++c) {
        const auto& chunk = terrain.chunks[c];
        if (!chunk.heightMap.isLoaded()) continue;
        loadedChunks++;
        if (!std::isfinite(chunk.position[0]) ||
            !std::isfinite(chunk.position[1]) ||
            !std::isfinite(chunk.position[2])) {
            if (++nanPosChunks <= 3) {
                errors.push_back("chunk " + std::to_string(c) +
                                 " has non-finite position");
            }
        }
        bool chunkHasBadHeight = false;
        for (float h : chunk.heightMap.heights) {
            if (!std::isfinite(h)) {
                chunkHasBadHeight = true;
            } else {
                if (h < minH) minH = h;
                if (h > maxH) maxH = h;
            }
        }
        if (chunkHasBadHeight) {
            if (++nanHeightChunks <= 3) {
                errors.push_back("chunk " + std::to_string(c) +
                                 " contains non-finite heights");
            }
        }
    }
    if (nanHeightChunks > 3) {
        errors.push_back("... and " + std::to_string(nanHeightChunks - 3) +
                         " more chunks with non-finite heights");
    }
    if (nanPosChunks > 3) {
        errors.push_back("... and " + std::to_string(nanPosChunks - 3) +
                         " more chunks with non-finite positions");
    }
    if (loadedChunks == 0) {
        errors.push_back("no chunks loaded (heightmap empty)");
    }
    // Heights outside the WoW world envelope often signal a units-confusion
    // bug - most maps stay in [-3000, 3000]. Warn-class, not fail.
    if (loadedChunks > 0 && (minH < -10000.0f || maxH > 10000.0f)) {
        errors.push_back("height range [" + std::to_string(minH) +
                         ", " + std::to_string(maxH) +
                         "] is outside reasonable WoW envelope");
    }
    int badPlacements = 0;
    for (size_t p = 0; p < terrain.doodadPlacements.size(); ++p) {
        const auto& d = terrain.doodadPlacements[p];
        if (!std::isfinite(d.position[0]) ||
            !std::isfinite(d.position[1]) ||
            !std::isfinite(d.position[2])) {
            if (++badPlacements <= 3) {
                errors.push_back("doodad placement " + std::to_string(p) +
                                 " has non-finite position");
            }
        }
        if (d.scale == 0) {
            if (++badPlacements <= 3) {
                errors.push_back("doodad placement " + std::to_string(p) +
                                 " has scale=0");
            }
        }
        if (!terrain.doodadNames.empty() && d.nameId >= terrain.doodadNames.size()) {
            if (++badPlacements <= 3) {
                errors.push_back("doodad placement " + std::to_string(p) +
                                 " nameId=" + std::to_string(d.nameId) +
                                 " >= doodadNames " +
                                 std::to_string(terrain.doodadNames.size()));
            }
        }
    }
    for (size_t p = 0; p < terrain.wmoPlacements.size(); ++p) {
        const auto& w = terrain.wmoPlacements[p];
        if (!std::isfinite(w.position[0]) ||
            !std::isfinite(w.position[1]) ||
            !std::isfinite(w.position[2])) {
            if (++badPlacements <= 3) {
                errors.push_back("wmo placement " + std::to_string(p) +
                                 " has non-finite position");
            }
        }
        if (!terrain.wmoNames.empty() && w.nameId >= terrain.wmoNames.size()) {
            if (++badPlacements <= 3) {
                errors.push_back("wmo placement " + std::to_string(p) +
                                 " nameId=" + std::to_string(w.nameId) +
                                 " >= wmoNames " +
                                 std::to_string(terrain.wmoNames.size()));
            }
        }
    }
    if (badPlacements > 3) {
        errors.push_back("... and " + std::to_string(badPlacements - 3) +
                         " more bad placement entries");
    }
    return errors;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string zoneDir = argv[++i];
    // Optional --json after the dir for machine-readable output
    // (matches --info-extract --json).
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    auto v = wowee::editor::ContentPacker::validateZone(zoneDir);
    int score = v.openFormatScore();
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["score"] = score;
        j["maxScore"] = 7;
        j["formats"] = v.summary();
        auto fmt = [&](const char* name, bool present, int count,
                        bool valid = true, int invalid = 0) {
            nlohmann::json f;
            f["present"] = present;
            f["count"] = count;
            f["valid"] = valid;
            if (invalid > 0) f["invalid"] = invalid;
            j[name] = f;
        };
        fmt("wot", v.hasWot, v.wotCount);
        fmt("whm", v.hasWhm, v.whmCount, v.whmValid);
        fmt("wom", v.hasWom, v.womCount, v.womValid, v.womInvalidCount);
        fmt("wob", v.hasWob, v.wobCount, v.wobValid, v.wobInvalidCount);
        fmt("woc", v.hasWoc, v.wocCount, v.wocValid, v.wocInvalidCount);
        fmt("png", v.hasPng, v.pngCount);
        j["zoneJson"]   = v.hasZoneJson;
        j["creatures"]  = v.hasCreatures;
        j["quests"]     = v.hasQuests;
        j["objects"]    = v.hasObjects;
        std::printf("%s\n", j.dump(2).c_str());
        return score == 7 ? 0 : 1;
    }
    std::printf("Zone: %s\n", zoneDir.c_str());
    std::printf("Open format score: %d/7\n", score);
    std::printf("Formats: %s\n", v.summary().c_str());
    std::printf("Files present:\n");
    std::printf("  WOT  (terrain meta)   : %s (%d)\n",
                v.hasWot ? "yes" : "no", v.wotCount);
    std::printf("  WHM  (heightmap)      : %s (%d)%s\n",
                v.hasWhm ? "yes" : "no", v.whmCount,
                v.hasWhm && !v.whmValid ? " (BAD MAGIC)" : "");
    std::printf("  WOM  (models)         : %s (%d)%s\n",
                v.hasWom ? "yes" : "no", v.womCount,
                v.womInvalidCount > 0 ?
                    (" (" + std::to_string(v.womInvalidCount) + " invalid)").c_str() : "");
    std::printf("  WOB  (buildings)      : %s (%d)%s\n",
                v.hasWob ? "yes" : "no", v.wobCount,
                v.wobInvalidCount > 0 ?
                    (" (" + std::to_string(v.wobInvalidCount) + " invalid)").c_str() : "");
    std::printf("  WOC  (collision)      : %s (%d)%s\n",
                v.hasWoc ? "yes" : "no", v.wocCount,
                v.wocInvalidCount > 0 ?
                    (" (" + std::to_string(v.wocInvalidCount) + " invalid)").c_str() : "");
    std::printf("  PNG  (textures)       : %s (%d)\n",
                v.hasPng ? "yes" : "no", v.pngCount);
    std::printf("  zone.json             : %s\n", v.hasZoneJson ? "yes" : "no");
    std::printf("  creatures.json        : %s\n", v.hasCreatures ? "yes" : "no");
    std::printf("  quests.json           : %s\n", v.hasQuests ? "yes" : "no");
    std::printf("  objects.json          : %s\n", v.hasObjects ? "yes" : "no");
    return score == 7 ? 0 : 1;
}

int handleValidateWom(int& i, int argc, char** argv) {
    // Deep consistency check on a single WOM. The loader is
    // deliberately lenient (it accepts older/partial files), so
    // silent corruption can survive load. This walks every cross-
    // reference and reports anything out of range.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        return reportMissing("WOM", base, ".wom");
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    auto errors = validateWomErrors(wom);
    if (jsonOut) {
        nlohmann::json j;
        j["wom"] = base + ".wom";
        j["version"] = wom.version;
        j["errorCount"] = errors.size();
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("WOM: %s.wom (v%u)\n", base.c_str(), wom.version);
    if (errors.empty()) {
        std::printf("  PASSED - %zu verts, %zu indices, %zu bones, %zu anims, %zu batches\n",
                    wom.vertices.size(), wom.indices.size(),
                    wom.bones.size(), wom.animations.size(),
                    wom.batches.size());
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleValidateWob(int& i, int argc, char** argv) {
    // Deep consistency check on a single WOB. Like --validate-wom
    // but covering buildings: per-group index/material refs, portal
    // group references, doodad scales, and bounds.
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
    auto errors = validateWobErrors(bld);
    if (jsonOut) {
        nlohmann::json j;
        j["wob"] = base + ".wob";
        j["name"] = bld.name;
        j["groups"] = bld.groups.size();
        j["portals"] = bld.portals.size();
        j["doodads"] = bld.doodads.size();
        j["errorCount"] = errors.size();
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("WOB: %s.wob\n", base.c_str());
    std::printf("  name      : %s\n", bld.name.c_str());
    if (errors.empty()) {
        std::printf("  PASSED - %zu groups, %zu portals, %zu doodads\n",
                    bld.groups.size(), bld.portals.size(), bld.doodads.size());
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleValidateWoc(int& i, int argc, char** argv) {
    // Deep check on a WOC collision mesh - finite vertex coords,
    // non-degenerate triangles, valid flag bits, sane bounds.
    // Catches corruption that breaks movement queries silently.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "WOC not found: %s\n", path.c_str());
        return 1;
    }
    auto woc = wowee::pipeline::WoweeCollisionBuilder::load(path);
    auto errors = validateWocErrors(woc);
    if (jsonOut) {
        nlohmann::json j;
        j["woc"] = path;
        j["triangles"] = woc.triangles.size();
        j["walkable"] = woc.walkableCount();
        j["steep"] = woc.steepCount();
        j["tile"] = {woc.tileX, woc.tileY};
        j["errorCount"] = errors.size();
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("WOC: %s\n", path.c_str());
    std::printf("  tile      : (%u, %u)\n", woc.tileX, woc.tileY);
    if (errors.empty()) {
        std::printf("  PASSED - %zu triangles (%zu walkable, %zu steep)\n",
                    woc.triangles.size(),
                    woc.walkableCount(), woc.steepCount());
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleValidateWhm(int& i, int argc, char** argv) {
    // Deep check on a WHM/WOT terrain pair - finite heights,
    // chunks present, placements within name-table bounds.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    for (const char* ext : {".wot", ".whm"}) {
        if (base.size() >= 4 && base.substr(base.size() - 4) == ext) {
            base = base.substr(0, base.size() - 4);
            break;
        }
    }
    if (!wowee::pipeline::WoweeTerrainLoader::exists(base)) {
        std::fprintf(stderr, "WHM/WOT not found: %s.{whm,wot}\n", base.c_str());
        return 1;
    }
    wowee::pipeline::ADTTerrain terrain;
    wowee::pipeline::WoweeTerrainLoader::load(base, terrain);
    auto errors = validateWhmErrors(terrain);
    if (jsonOut) {
        nlohmann::json j;
        j["whm"] = base + ".whm";
        j["wot"] = base + ".wot";
        j["coord"] = {terrain.coord.x, terrain.coord.y};
        j["doodadPlacements"] = terrain.doodadPlacements.size();
        j["wmoPlacements"] = terrain.wmoPlacements.size();
        int loadedChunks = 0;
        for (const auto& c : terrain.chunks) if (c.heightMap.isLoaded()) loadedChunks++;
        j["loadedChunks"] = loadedChunks;
        j["errorCount"] = errors.size();
        j["errors"] = errors;
        j["passed"] = errors.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return errors.empty() ? 0 : 1;
    }
    std::printf("WHM/WOT: %s.{whm,wot}\n", base.c_str());
    std::printf("  tile      : (%d, %d)\n", terrain.coord.x, terrain.coord.y);
    if (errors.empty()) {
        int loaded = 0;
        for (const auto& c : terrain.chunks) if (c.heightMap.isLoaded()) loaded++;
        std::printf("  PASSED - %d/256 chunks, %zu doodad + %zu wmo placements\n",
                    loaded, terrain.doodadPlacements.size(),
                    terrain.wmoPlacements.size());
        return 0;
    }
    std::printf("  FAILED - %zu error(s):\n", errors.size());
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleValidateAll(int& i, int argc, char** argv) {
    // CI gate: walk a directory, run every per-format validator on
    // every matching file. Aggregate counts for fast triage; per-
    // file errors are listed (capped at 20) so the user knows which
    // file to drill into with --validate-{wom,wob,woc,whm}.
    std::string root = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(root)) {
        std::fprintf(stderr, "validate-all: not found: %s\n", root.c_str());
        return 1;
    }
    int womTotal = 0, womFail = 0, wobTotal = 0, wobFail = 0;
    int wocTotal = 0, wocFail = 0, whmTotal = 0, whmFail = 0;
    int totalErrors = 0;
    std::vector<std::pair<std::string, std::vector<std::string>>> failures;
    auto recordFailure = [&](const std::string& path,
                              const std::vector<std::string>& errs) {
        totalErrors += errs.size();
        if (failures.size() < 20) failures.push_back({path, errs});
    };
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::string base = entry.path().string();
        base = base.substr(0, base.size() - ext.size());
        if (ext == ".wom") {
            womTotal++;
            auto wom = wowee::pipeline::WoweeModelLoader::load(base);
            auto errs = validateWomErrors(wom);
            if (!errs.empty()) { womFail++; recordFailure(entry.path().string(), errs); }
        } else if (ext == ".wob") {
            wobTotal++;
            auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
            auto errs = validateWobErrors(bld);
            if (!errs.empty()) { wobFail++; recordFailure(entry.path().string(), errs); }
        } else if (ext == ".woc") {
            wocTotal++;
            auto woc = wowee::pipeline::WoweeCollisionBuilder::load(entry.path().string());
            auto errs = validateWocErrors(woc);
            if (!errs.empty()) { wocFail++; recordFailure(entry.path().string(), errs); }
        } else if (ext == ".whm") {
            // Only validate via the .whm half - .wot is its sidecar
            // and gets pulled in by load(base).
            whmTotal++;
            wowee::pipeline::ADTTerrain terrain;
            wowee::pipeline::WoweeTerrainLoader::load(base, terrain);
            auto errs = validateWhmErrors(terrain);
            if (!errs.empty()) { whmFail++; recordFailure(entry.path().string(), errs); }
        }
    }
    int allPassed = (womFail == 0 && wobFail == 0 &&
                     wocFail == 0 && whmFail == 0);
    int totalFiles = womTotal + wobTotal + wocTotal + whmTotal;
    if (jsonOut) {
        nlohmann::json j;
        j["root"] = root;
        j["wom"] = {{"total", womTotal}, {"failed", womFail}};
        j["wob"] = {{"total", wobTotal}, {"failed", wobFail}};
        j["woc"] = {{"total", wocTotal}, {"failed", wocFail}};
        j["whm"] = {{"total", whmTotal}, {"failed", whmFail}};
        j["totalErrors"] = totalErrors;
        j["passed"] = bool(allPassed);
        nlohmann::json failArr = nlohmann::json::array();
        for (const auto& [path, errs] : failures) {
            failArr.push_back({{"file", path}, {"errors", errs}});
        }
        j["failures"] = failArr;
        std::printf("%s\n", j.dump(2).c_str());
        return allPassed ? 0 : 1;
    }
    std::printf("validate-all: %s\n", root.c_str());
    std::printf("  WOM: %d total, %d failed\n", womTotal, womFail);
    std::printf("  WOB: %d total, %d failed\n", wobTotal, wobFail);
    std::printf("  WOC: %d total, %d failed\n", wocTotal, wocFail);
    std::printf("  WHM: %d total, %d failed\n", whmTotal, whmFail);
    if (allPassed) {
        std::printf("  PASSED - all %d file(s) clean\n", totalFiles);
        return 0;
    }
    std::printf("  FAILED - %d total error(s) across %zu file(s):\n",
                totalErrors, failures.size());
    for (const auto& [path, errs] : failures) {
        std::printf("  %s:\n", path.c_str());
        for (const auto& e : errs) std::printf("    - %s\n", e.c_str());
    }
    return 1;
}

int handleValidateProject(int& i, int argc, char** argv) {
    // Project-level validate. Walks every zone in <projectDir>
    // and runs the per-format validators (same as --validate-all).
    // Aggregates pass/fail counts; exits 1 if any zone has any
    // validation errors. Designed for CI gates before --pack-wcp.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "validate-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    // Per-zone pass/fail with file-level breakdown.
    struct ZoneResult { std::string name; int totalFiles, failedFiles, totalErrors; };
    std::vector<ZoneResult> results;
    int projectFailedZones = 0;
    for (const auto& zoneDir : zones) {
        ZoneResult r{zoneDir, 0, 0, 0};
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::string base = entry.path().string();
            base = base.substr(0, base.size() - ext.size());
            std::vector<std::string> errs;
            if (ext == ".wom") {
                r.totalFiles++;
                auto wom = wowee::pipeline::WoweeModelLoader::load(base);
                errs = validateWomErrors(wom);
            } else if (ext == ".wob") {
                r.totalFiles++;
                auto wob = wowee::pipeline::WoweeBuildingLoader::load(base);
                errs = validateWobErrors(wob);
            } else if (ext == ".woc") {
                r.totalFiles++;
                auto woc = wowee::pipeline::WoweeCollisionBuilder::load(entry.path().string());
                errs = validateWocErrors(woc);
            } else if (ext == ".whm") {
                r.totalFiles++;
                wowee::pipeline::ADTTerrain terrain;
                wowee::pipeline::WoweeTerrainLoader::load(base, terrain);
                errs = validateWhmErrors(terrain);
            }
            if (!errs.empty()) {
                r.failedFiles++;
                r.totalErrors += static_cast<int>(errs.size());
            }
        }
        if (r.failedFiles > 0) projectFailedZones++;
        results.push_back(r);
    }
    int allPassed = (projectFailedZones == 0);
    if (jsonOut) {
        nlohmann::json j;
        j["projectDir"] = projectDir;
        j["totalZones"] = zones.size();
        j["failedZones"] = projectFailedZones;
        j["passed"] = bool(allPassed);
        nlohmann::json zarr = nlohmann::json::array();
        for (const auto& r : results) {
            zarr.push_back({
                {"zone", r.name},
                {"totalFiles", r.totalFiles},
                {"failedFiles", r.failedFiles},
                {"totalErrors", r.totalErrors}
            });
        }
        j["zones"] = zarr;
        std::printf("%s\n", j.dump(2).c_str());
        return allPassed ? 0 : 1;
    }
    std::printf("validate-project: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu (%d failed)\n",
                zones.size(), projectFailedZones);
    std::printf("\n  zone                       files  failed  errors  status\n");
    for (const auto& r : results) {
        std::string shortName = fs::path(r.name).filename().string();
        std::printf("  %-26s  %5d  %6d  %6d  %s\n",
                    shortName.substr(0, 26).c_str(),
                    r.totalFiles, r.failedFiles, r.totalErrors,
                    r.failedFiles == 0 ? "PASS" : "FAIL");
    }
    if (allPassed) {
        std::printf("\n  ALL ZONES PASSED\n");
        return 0;
    }
    std::printf("\n  %d zone(s) failed validation\n", projectFailedZones);
    return 1;
}

int handleValidateProjectOpenOnly(int& i, int argc, char** argv) {
    // Release gate. Walks every file in <projectDir> and exits
    // 1 if any proprietary Blizzard asset is present (.m2, .skin,
    // .wmo, .blp, .dbc). Designed for CI to enforce a
    // "no-proprietary-assets" release condition once a project
    // has fully migrated to the open WOM/WOB/PNG/JSON formats.
    std::string projectDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "validate-project-open-only: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // Standard set of proprietary extensions. Mirrors the
    // "(proprietary)" categories used by --info-project-bytes.
    static const std::set<std::string> propExt = {
        ".m2", ".skin", ".wmo", ".blp", ".dbc",
    };
    std::map<std::string, int> byExt;
    std::vector<std::string> hits;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(projectDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (!propExt.count(ext)) continue;
        byExt[ext]++;
        std::string rel = fs::relative(e.path(), projectDir, ec).string();
        if (ec) rel = e.path().string();
        hits.push_back(rel);
    }
    std::sort(hits.begin(), hits.end());
    std::printf("validate-project-open-only: %s\n", projectDir.c_str());
    if (hits.empty()) {
        std::printf("  PASSED - no proprietary Blizzard assets present\n");
        return 0;
    }
    std::printf("  FAILED - %zu proprietary file(s) remain\n", hits.size());
    std::printf("\n  Per-extension:\n");
    for (const auto& [ext, count] : byExt) {
        std::printf("    %-6s : %d\n", ext.c_str(), count);
    }
    std::printf("\n  Files (sorted):\n");
    // Cap the file list at 50 entries so a wholly unmigrated
    // project doesn't fill the user's terminal.
    size_t shown = 0;
    for (const auto& h : hits) {
        if (shown >= 50) {
            std::printf("    ... and %zu more\n", hits.size() - shown);
            break;
        }
        std::printf("    - %s\n", h.c_str());
        shown++;
    }
    return 1;
}

int handleAuditProject(int& i, int argc, char** argv) {
    // Composite CI gate. Re-invokes the binary to run the four
    // most important per-project checks back-to-back and rolls
    // their exit codes into a single PASS/FAIL verdict. Emits
    // a one-line summary for each sub-check plus the final
    // overall result. Designed to be the only command CI needs
    // to run before --pack-wcp.
    //
    // Sub-checks (ordered cheapest→most expensive so a fast
    // failure surfaces before the slow ones run):
    //   1. validate-project        (per-format integrity)
    //   2. validate-project-open-only (no proprietary leaks)
    //   3. validate-project-items  (items.json schema)
    //   4. check-project-refs      (every model/NPC ref resolves)
    //   5. check-project-content   (sane field values)
    //   6. audit-project-spawns    (spawn Z near terrain)
    std::string projectDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "audit-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // Use the binary's own path so the audit works from any cwd.
    std::string self = argv[0];
    // Quote both to survive paths with spaces; redirect each
    // sub-check's stdout to a separate temp file so the final
    // verdict isn't drowned in their output.
    auto runStep = [&](const std::string& flag) -> int {
        // Suppress child stdout/stderr so the audit's own report stays
        // readable; users can rerun the individual sub-check for full
        // output if needed.
        return wowee::editor::cli::runChild(self,
            {flag, projectDir}, /*quiet=*/true);
    };
    struct Step { const char* name; const char* flag; int rc; };
    std::vector<Step> steps = {
        {"format validation       ", "--validate-project",            0},
        {"open-only release gate  ", "--validate-project-open-only",  0},
        {"items schema            ", "--validate-project-items",      0},
        {"reference integrity     ", "--check-project-refs",          0},
        {"content field sanity    ", "--check-project-content",       0},
        {"spawn placement         ", "--audit-project-spawns",        0},
    };
    int totalFailed = 0;
    std::printf("audit-project: %s\n\n", projectDir.c_str());
    for (auto& s : steps) {
        s.rc = runStep(s.flag);
        bool pass = (s.rc == 0);
        std::printf("  [%s] %s  (%s, rc=%d)\n",
                    pass ? "PASS" : "FAIL",
                    s.name, s.flag, s.rc);
        if (!pass) totalFailed++;
    }
    std::printf("\n");
    if (totalFailed == 0) {
        std::printf("OVERALL: PASS - project is release-ready\n");
        return 0;
    }
    std::printf("OVERALL: FAIL - %d sub-check(s) failed\n", totalFailed);
    std::printf("  rerun a failing sub-check directly for detailed output\n");
    return 1;
}

int handleBenchAuditProject(int& i, [[maybe_unused]] int argc, char** argv) {
    // Time each --audit-project sub-step end-to-end so users
    // can see where the slow checks are. Useful for tuning a
    // CI pipeline: drop the slowest check from a fast-feedback
    // pre-commit hook, run the full audit on push.
    std::string projectDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "bench-audit-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    std::string self = argv[0];
    struct Step { const char* name; const char* flag; double ms; int rc; };
    std::vector<Step> steps = {
        {"format validation       ", "--validate-project",            0, 0},
        {"open-only release gate  ", "--validate-project-open-only",  0, 0},
        {"items schema            ", "--validate-project-items",      0, 0},
        {"reference integrity     ", "--check-project-refs",          0, 0},
        {"content field sanity    ", "--check-project-content",       0, 0},
        {"spawn placement         ", "--audit-project-spawns",        0, 0},
    };
    double totalMs = 0;
    for (auto& s : steps) {
        auto t0 = std::chrono::steady_clock::now();
        s.rc = wowee::editor::cli::runChild(self,
            {s.flag, projectDir}, /*quiet=*/true);
        auto t1 = std::chrono::steady_clock::now();
        s.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalMs += s.ms;
    }
    std::printf("bench-audit-project: %s\n", projectDir.c_str());
    std::printf("  total : %.1f ms (%.2f s)\n", totalMs, totalMs / 1000.0);
    std::printf("\n  step                       wall-clock    share   status\n");
    for (const auto& s : steps) {
        double share = totalMs > 0 ? 100.0 * s.ms / totalMs : 0.0;
        std::printf("  %s   %9.1f ms   %5.1f%%   %s (rc=%d)\n",
                    s.name, s.ms, share,
                    s.rc == 0 ? "ok" : "FAIL", s.rc);
    }
    return 0;
}

int handleBenchValidateProject(int& i, int argc, char** argv) {
    // Time --validate-project per zone. Reports avg/min/max
    // latency so users can spot zones that are unusually slow
    // to validate (huge WHM/WOC pairs, lots of WOM batches).
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "bench-validate-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    // Per-zone timing pass - same validator walk as
    // --validate-project but timing each zone separately.
    struct Timing { std::string name; double ms; int files; };
    std::vector<Timing> timings;
    double totalMs = 0;
    for (const auto& zoneDir : zones) {
        auto t0 = std::chrono::steady_clock::now();
        int files = 0;
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::string base = entry.path().string();
            base = base.substr(0, base.size() - ext.size());
            if (ext == ".wom") {
                files++;
                auto wom = wowee::pipeline::WoweeModelLoader::load(base);
                (void)validateWomErrors(wom);
            } else if (ext == ".wob") {
                files++;
                auto wob = wowee::pipeline::WoweeBuildingLoader::load(base);
                (void)validateWobErrors(wob);
            } else if (ext == ".woc") {
                files++;
                auto woc = wowee::pipeline::WoweeCollisionBuilder::load(entry.path().string());
                (void)validateWocErrors(woc);
            } else if (ext == ".whm") {
                files++;
                wowee::pipeline::ADTTerrain terrain;
                wowee::pipeline::WoweeTerrainLoader::load(base, terrain);
                (void)validateWhmErrors(terrain);
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalMs += ms;
        timings.push_back({fs::path(zoneDir).filename().string(), ms, files});
    }
    // Compute aggregate stats.
    double avgMs = !timings.empty() ? totalMs / timings.size() : 0.0;
    double minMs = 1e30, maxMs = 0;
    std::string slowestZone;
    for (const auto& t : timings) {
        if (t.ms < minMs) minMs = t.ms;
        if (t.ms > maxMs) { maxMs = t.ms; slowestZone = t.name; }
    }
    if (timings.empty()) { minMs = 0; maxMs = 0; }
    if (jsonOut) {
        nlohmann::json j;
        j["projectDir"] = projectDir;
        j["totalMs"] = totalMs;
        j["zoneCount"] = timings.size();
        j["avgMs"] = avgMs;
        j["minMs"] = minMs;
        j["maxMs"] = maxMs;
        j["slowestZone"] = slowestZone;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : timings) {
            arr.push_back({{"zone", t.name}, {"ms", t.ms},
                            {"files", t.files}});
        }
        j["perZone"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Bench validate: %s\n", projectDir.c_str());
    std::printf("  zones    : %zu\n", timings.size());
    std::printf("  total    : %.2f ms\n", totalMs);
    std::printf("  per zone : avg=%.2f min=%.2f max=%.2f ms\n",
                avgMs, minMs, maxMs);
    if (!slowestZone.empty()) {
        std::printf("  slowest  : %s (%.2f ms)\n",
                    slowestZone.c_str(), maxMs);
    }
    std::printf("\n  Per-zone timings:\n");
    std::printf("    zone                       ms       files  ms/file\n");
    for (const auto& t : timings) {
        double mspf = t.files > 0 ? t.ms / t.files : 0.0;
        std::printf("    %-26s %7.2f  %5d   %6.3f\n",
                    t.name.substr(0, 26).c_str(), t.ms, t.files, mspf);
    }
    return 0;
}


}  // namespace

bool handleFormatValidate(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--validate") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wom") == 0 && i + 1 < argc) {
        outRc = handleValidateWom(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wob") == 0 && i + 1 < argc) {
        outRc = handleValidateWob(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-woc") == 0 && i + 1 < argc) {
        outRc = handleValidateWoc(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-whm") == 0 && i + 1 < argc) {
        outRc = handleValidateWhm(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-all") == 0 && i + 1 < argc) {
        outRc = handleValidateAll(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-project") == 0 && i + 1 < argc) {
        outRc = handleValidateProject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-project-open-only") == 0 && i + 1 < argc) {
        outRc = handleValidateProjectOpenOnly(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--audit-project") == 0 && i + 1 < argc) {
        outRc = handleAuditProject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bench-audit-project") == 0 && i + 1 < argc) {
        outRc = handleBenchAuditProject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bench-validate-project") == 0 && i + 1 < argc) {
        outRc = handleBenchValidateProject(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
