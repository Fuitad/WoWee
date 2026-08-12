#include "cli_world_io.hpp"
#include "gltf_glb.hpp"
#include "cli_catalog_paths.hpp"

#include "pipeline/wowee_building.hpp"

#include "obj_parse.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleExportWobGlb(int& i, int argc, char** argv) {
    // glTF 2.0 binary export for WOB. Same purpose as --export-glb
    // for WOM but adapted for buildings: each WOB group becomes
    // one primitive in a single mesh, sharing one big vertex
    // pool concatenated from per-group vertex arrays.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wob")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeBuildingLoader::exists(base)) {
        return reportMissing("WOB", base, ".wob");
    }
    if (outPath.empty()) outPath = base + ".glb";
    auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
    if (!bld.isValid()) {
        std::fprintf(stderr, "WOB has no groups: %s.wob\n", base.c_str());
        return 1;
    }
    // Total counts + per-group offsets needed before allocating
    // the BIN buffer. Index buffer is uint32 so groups can each
    // index into the global pool by offset.
    uint32_t totalV = 0, totalI = 0;
    std::vector<uint32_t> groupVertOff(bld.groups.size(), 0);
    std::vector<uint32_t> groupIdxOff(bld.groups.size(), 0);
    for (size_t g = 0; g < bld.groups.size(); ++g) {
        groupVertOff[g] = totalV;
        groupIdxOff[g] = totalI;
        totalV += static_cast<uint32_t>(bld.groups[g].vertices.size());
        totalI += static_cast<uint32_t>(bld.groups[g].indices.size());
    }
    if (totalV == 0 || totalI == 0) {
        std::fprintf(stderr, "WOB has no vertex data\n");
        return 1;
    }
    const uint32_t posOff = 0;
    const uint32_t nrmOff = posOff + totalV * 12;
    const uint32_t uvOff  = nrmOff + totalV * 12;
    const uint32_t idxOff = uvOff  + totalV * 8;
    const uint32_t binSize = idxOff + totalI * 4;
    std::vector<uint8_t> bin(binSize);
    // Pack per-group geometry into the global pool. Indices get
    // offset by the group's starting vertex index so they
    // continue to reference the right vertices in the merged pool.
    uint32_t vCursor = 0, iCursor = 0;
    glm::vec3 bMin{1e30f}, bMax{-1e30f};
    for (size_t g = 0; g < bld.groups.size(); ++g) {
        const auto& grp = bld.groups[g];
        for (const auto& v : grp.vertices) {
            std::memcpy(&bin[posOff + vCursor * 12 + 0], &v.position.x, 4);
            std::memcpy(&bin[posOff + vCursor * 12 + 4], &v.position.y, 4);
            std::memcpy(&bin[posOff + vCursor * 12 + 8], &v.position.z, 4);
            std::memcpy(&bin[nrmOff + vCursor * 12 + 0], &v.normal.x, 4);
            std::memcpy(&bin[nrmOff + vCursor * 12 + 4], &v.normal.y, 4);
            std::memcpy(&bin[nrmOff + vCursor * 12 + 8], &v.normal.z, 4);
            std::memcpy(&bin[uvOff  + vCursor * 8  + 0], &v.texCoord.x, 4);
            std::memcpy(&bin[uvOff  + vCursor * 8  + 4], &v.texCoord.y, 4);
            bMin = glm::min(bMin, v.position);
            bMax = glm::max(bMax, v.position);
            vCursor++;
        }
        // Offset indices by group's vertex base so merged pool
        // indexing still works. uint32 indices, written LE.
        for (uint32_t idx : grp.indices) {
            uint32_t off = idx + groupVertOff[g];
            std::memcpy(&bin[idxOff + iCursor * 4], &off, 4);
            iCursor++;
        }
    }
    // Build glTF JSON.
    nlohmann::json gj;
    gj["asset"] = {{"version", "2.0"},
                    {"generator", "wowee_editor --export-wob-glb"}};
    gj["scene"] = 0;
    gj["scenes"] = nlohmann::json::array({nlohmann::json{{"nodes", {0}}}});
    gj["nodes"] = nlohmann::json::array({nlohmann::json{
        {"name", bld.name.empty() ? "WoweeBuilding" : bld.name},
        {"mesh", 0}
    }});
    gj["buffers"] = nlohmann::json::array({nlohmann::json{
        {"byteLength", binSize}
    }});
    nlohmann::json bufferViews = nlohmann::json::array();
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", posOff},
                            {"byteLength", totalV * 12}, {"target", 34962}});
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", nrmOff},
                            {"byteLength", totalV * 12}, {"target", 34962}});
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", uvOff},
                            {"byteLength", totalV * 8},  {"target", 34962}});
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", idxOff},
                            {"byteLength", totalI * 4},  {"target", 34963}});
    gj["bufferViews"] = bufferViews;
    nlohmann::json accessors = nlohmann::json::array();
    accessors.push_back({
        {"bufferView", 0}, {"componentType", 5126},
        {"count", totalV}, {"type", "VEC3"},
        {"min", {bMin.x, bMin.y, bMin.z}},
        {"max", {bMax.x, bMax.y, bMax.z}}
    });
    accessors.push_back({{"bufferView", 1}, {"componentType", 5126},
                          {"count", totalV}, {"type", "VEC3"}});
    accessors.push_back({{"bufferView", 2}, {"componentType", 5126},
                          {"count", totalV}, {"type", "VEC2"}});
    // Per-group primitives - each gets its own indices accessor
    // sliced from the shared index bufferView via byteOffset.
    nlohmann::json primitives = nlohmann::json::array();
    for (size_t g = 0; g < bld.groups.size(); ++g) {
        uint32_t accIdx = static_cast<uint32_t>(accessors.size());
        accessors.push_back({
            {"bufferView", 3},
            {"byteOffset", groupIdxOff[g] * 4},
            {"componentType", 5125},
            {"count", bld.groups[g].indices.size()},
            {"type", "SCALAR"}
        });
        primitives.push_back({
            {"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
            {"indices", accIdx},
            {"mode", 4}
        });
    }
    gj["accessors"] = accessors;
    gj["meshes"] = nlohmann::json::array({nlohmann::json{
        {"primitives", primitives}
    }});
    // The container - header, chunks and the padding each needs - is shared
    // with the other exporters here.
    std::string glbError;
    if (!writeGlb(outPath, gj, bin, glbError)) {
        std::fprintf(stderr, "Failed to write GLB: %s\n", glbError.c_str());
        return 1;
    }
    std::printf("Exported %s.wob -> %s\n", base.c_str(), outPath.c_str());
    std::printf("  %zu groups -> %zu primitives, %u verts, %u tris, %u-byte BIN\n",
                bld.groups.size(), primitives.size(),
                totalV, totalI / 3, static_cast<uint32_t>(bin.size()));
    return 0;
}

int handleExportWhmGlb(int& i, int argc, char** argv) {
    // glTF 2.0 binary export for WHM/WOT terrain. Mirrors
    // --export-whm-obj's mesh layout (9x9 outer grid per chunk
    // → 8x8 quads → 2 tris each), but ships as a single .glb
    // viewable in any modern web 3D tool. Per-chunk primitives
    // so designers can hide individual chunks in three.js.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
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
    if (outPath.empty()) outPath = base + ".glb";
    wowee::pipeline::ADTTerrain terrain;
    wowee::pipeline::WoweeTerrainLoader::load(base, terrain);
    // Same coord constants as --export-whm-obj so the .glb and
    // .obj of the same source align spatially.
    constexpr float kTileSize = 533.33333f;
    constexpr float kChunkSize = kTileSize / 16.0f;
    constexpr float kVertSpacing = kChunkSize / 8.0f;
    // Walk the 16x16 chunk grid, build per-chunk vertex + index
    // arrays. Hole bits respected (cave-entrance quads dropped).
    struct ChunkMesh { uint32_t vertOff, vertCount, idxOff, idxCount; };
    std::vector<ChunkMesh> chunkMeshes;
    std::vector<glm::vec3> positions;  // packed sequentially
    std::vector<uint32_t>  indices;
    int loadedChunks = 0;
    glm::vec3 bMin{1e30f}, bMax{-1e30f};
    for (int cx = 0; cx < 16; ++cx) {
        for (int cy = 0; cy < 16; ++cy) {
            const auto& chunk = terrain.getChunk(cx, cy);
            if (!chunk.heightMap.isLoaded()) continue;
            loadedChunks++;
            ChunkMesh cm{};
            cm.vertOff = static_cast<uint32_t>(positions.size());
            cm.idxOff  = static_cast<uint32_t>(indices.size());
            float chunkBaseX = (32.0f - terrain.coord.y) * kTileSize - cy * kChunkSize;
            float chunkBaseY = (32.0f - terrain.coord.x) * kTileSize - cx * kChunkSize;
            // 9x9 outer verts (skip 8x8 inner fan-center verts).
            for (int row = 0; row < 9; ++row) {
                for (int col = 0; col < 9; ++col) {
                    glm::vec3 p{
                        chunkBaseX - row * kVertSpacing,
                        chunkBaseY - col * kVertSpacing,
                        chunk.position[2] + chunk.heightMap.heights[row * 17 + col]
                    };
                    positions.push_back(p);
                    bMin = glm::min(bMin, p);
                    bMax = glm::max(bMax, p);
                }
            }
            cm.vertCount = 81;
            bool isHoleChunk = (chunk.holes != 0);
            auto idx = [&](int r, int c) { return cm.vertOff + r * 9 + c; };
            for (int row = 0; row < 8; ++row) {
                for (int col = 0; col < 8; ++col) {
                    if (isHoleChunk) {
                        int hx = col / 2, hy = row / 2;
                        if (chunk.holes & (1 << (hy * 4 + hx))) continue;
                    }
                    indices.push_back(idx(row, col));
                    indices.push_back(idx(row, col + 1));
                    indices.push_back(idx(row + 1, col + 1));
                    indices.push_back(idx(row, col));
                    indices.push_back(idx(row + 1, col + 1));
                    indices.push_back(idx(row + 1, col));
                }
            }
            cm.idxCount = static_cast<uint32_t>(indices.size()) - cm.idxOff;
            chunkMeshes.push_back(cm);
        }
    }
    if (loadedChunks == 0) {
        std::fprintf(stderr, "WHM has no loaded chunks\n");
        return 1;
    }
    // Synthesize normals as +Z (terrain is Z-up). Real per-vertex
    // normals would need a smoothing pass across chunk boundaries
    // - skip for v1, viewers can compute their own from positions.
    const auto packed = packTerrainBin(positions, indices);
    const std::vector<uint8_t>& bin = packed.bytes;
    const uint32_t totalV = static_cast<uint32_t>(positions.size());
    const uint32_t totalI = static_cast<uint32_t>(indices.size());
    const uint32_t posOff = packed.positionOffset;
    const uint32_t nrmOff = packed.normalOffset;
    const uint32_t idxOff = packed.indexOffset;
    const uint32_t binSize = static_cast<uint32_t>(bin.size());
    // Build glTF JSON.
    nlohmann::json gj;
    gj["asset"] = {{"version", "2.0"},
                    {"generator", "wowee_editor --export-whm-glb"}};
    gj["scene"] = 0;
    gj["scenes"] = nlohmann::json::array({nlohmann::json{{"nodes", {0}}}});
    std::string nodeName = "WoweeTerrain_" + std::to_string(terrain.coord.x) +
                            "_" + std::to_string(terrain.coord.y);
    gj["nodes"] = nlohmann::json::array({nlohmann::json{
        {"name", nodeName}, {"mesh", 0}
    }});
    gj["buffers"] = nlohmann::json::array({nlohmann::json{
        {"byteLength", binSize}
    }});
    nlohmann::json bufferViews = nlohmann::json::array();
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", posOff},
                            {"byteLength", totalV * 12}, {"target", 34962}});
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", nrmOff},
                            {"byteLength", totalV * 12}, {"target", 34962}});
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", idxOff},
                            {"byteLength", totalI * 4},  {"target", 34963}});
    gj["bufferViews"] = bufferViews;
    nlohmann::json accessors = nlohmann::json::array();
    accessors.push_back({
        {"bufferView", 0}, {"componentType", 5126},
        {"count", totalV}, {"type", "VEC3"},
        {"min", {bMin.x, bMin.y, bMin.z}},
        {"max", {bMax.x, bMax.y, bMax.z}}
    });
    accessors.push_back({{"bufferView", 1}, {"componentType", 5126},
                          {"count", totalV}, {"type", "VEC3"}});
    // Per-chunk primitive - sliced from shared index bufferView.
    nlohmann::json primitives = nlohmann::json::array();
    for (const auto& cm : chunkMeshes) {
        if (cm.idxCount == 0) continue;  // all-hole chunk
        uint32_t accIdx = static_cast<uint32_t>(accessors.size());
        accessors.push_back({
            {"bufferView", 2},
            {"byteOffset", cm.idxOff * 4},
            {"componentType", 5125},
            {"count", cm.idxCount},
            {"type", "SCALAR"}
        });
        primitives.push_back({
            {"attributes", {{"POSITION", 0}, {"NORMAL", 1}}},
            {"indices", accIdx},
            {"mode", 4}
        });
    }
    gj["accessors"] = accessors;
    gj["meshes"] = nlohmann::json::array({nlohmann::json{
        {"primitives", primitives}
    }});
    // The container - header, chunks and the padding each needs - is shared
    // with the other exporters here.
    std::string glbError;
    if (!writeGlb(outPath, gj, bin, glbError)) {
        std::fprintf(stderr, "Failed to write GLB: %s\n", glbError.c_str());
        return 1;
    }
    std::printf("Exported %s.whm -> %s\n", base.c_str(), outPath.c_str());
    std::printf("  %d chunks loaded, %u verts, %u tris, %zu primitives, %u-byte BIN\n",
                loadedChunks, totalV, totalI / 3, primitives.size(), static_cast<uint32_t>(bin.size()));
    return 0;
}

int handleExportWobObj(int& i, int argc, char** argv) {
    // WOB is the WMO replacement; like --export-obj for WOM, this
    // bridges WOB into the universal-3D-tool ecosystem. Each WOB
    // group becomes one OBJ 'g' block, preserving the room/floor
    // structure for downstream selection in Blender/MeshLab.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wob")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeBuildingLoader::exists(base)) {
        return reportMissing("WOB", base, ".wob");
    }
    if (outPath.empty()) outPath = base + ".obj";
    auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
    if (!bld.isValid()) {
        std::fprintf(stderr, "WOB has no groups to export: %s.wob\n", base.c_str());
        return 1;
    }
    std::ofstream obj(outPath);
    if (!obj) {
        std::fprintf(stderr, "Failed to open output file: %s\n", outPath.c_str());
        return 1;
    }
    // Total verts/tris across all groups for the header.
    size_t totalV = 0, totalI = 0;
    for (const auto& g : bld.groups) {
        totalV += g.vertices.size();
        totalI += g.indices.size();
    }
    obj << "# Wavefront OBJ generated by wowee_editor --export-wob-obj\n";
    obj << "# Source: " << base << ".wob\n";
    obj << "# Groups: " << bld.groups.size()
        << " Verts: " << totalV
        << " Tris: " << totalI / 3
        << " Portals: " << bld.portals.size()
        << " Doodads: " << bld.doodads.size() << "\n\n";
    obj << "o " << (bld.name.empty() ? "WoweeBuilding" : bld.name) << "\n";
    // OBJ uses a single global vertex pool, so we offset each group's
    // local indices by the running total of verts written so far.
    uint32_t vertOffset = 0;
    for (size_t g = 0; g < bld.groups.size(); ++g) {
        const auto& grp = bld.groups[g];
        if (grp.vertices.empty()) continue;
        for (const auto& v : grp.vertices) {
            obj << "v "  << v.position.x << " "
                          << v.position.y << " "
                          << v.position.z << "\n";
        }
        for (const auto& v : grp.vertices) {
            obj << "vt " << v.texCoord.x << " "
                          << (1.0f - v.texCoord.y) << "\n";
        }
        for (const auto& v : grp.vertices) {
            obj << "vn " << v.normal.x << " "
                          << v.normal.y << " "
                          << v.normal.z << "\n";
        }
        std::string groupName = grp.name.empty()
            ? "group_" + std::to_string(g)
            : grp.name;
        if (grp.isOutdoor) groupName += "_outdoor";
        obj << "g " << groupName << "\n";
        for (size_t k = 0; k + 2 < grp.indices.size(); k += 3) {
            uint32_t i0 = grp.indices[k]     + 1 + vertOffset;
            uint32_t i1 = grp.indices[k + 1] + 1 + vertOffset;
            uint32_t i2 = grp.indices[k + 2] + 1 + vertOffset;
            obj << "f "
                << i0 << "/" << i0 << "/" << i0 << " "
                << i1 << "/" << i1 << "/" << i1 << " "
                << i2 << "/" << i2 << "/" << i2 << "\n";
        }
        vertOffset += static_cast<uint32_t>(grp.vertices.size());
    }
    // Doodad placements as a separate informational block - emit
    // each as a comment line so OBJ stays valid but the data is
    // recoverable for tools that want to re-create the placements.
    if (!bld.doodads.empty()) {
        obj << "\n# Doodad placements (model, position, rotation, scale):\n";
        for (const auto& d : bld.doodads) {
            obj << "# doodad " << d.modelPath
                << " pos " << d.position.x << "," << d.position.y << "," << d.position.z
                << " rot " << d.rotation.x << "," << d.rotation.y << "," << d.rotation.z
                << " scale " << d.scale << "\n";
        }
    }
    obj.close();
    std::printf("Exported %s.wob -> %s\n", base.c_str(), outPath.c_str());
    std::printf("  %zu groups, %zu verts, %zu tris, %zu doodad placements\n",
                bld.groups.size(), totalV, totalI / 3,
                bld.doodads.size());
    return 0;
}

int handleImportWobObj(int& i, int argc, char** argv) {
    // Round-trip companion to --export-wob-obj. Each OBJ 'g' block
    // becomes one WoweeBuilding::Group; geometry under that group
    // is deduped into the group's local vertex array. Faces
    // before any 'g' directive land in a default 'imported' group.
    // Doodad placements written as # comment lines by --export-wob-obj
    // ARE recognized and re-instanced into bld.doodads.
    std::string objPath = argv[++i];
    std::string wobBase;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        wobBase = argv[++i];
    }
    if (!std::filesystem::exists(objPath)) {
        std::fprintf(stderr, "OBJ not found: %s\n", objPath.c_str());
        return 1;
    }
    if (wobBase.empty()) {
        wobBase = objPath;
        if (wobBase.size() >= 4 &&
            wobBase.substr(wobBase.size() - 4) == ".obj") {
            wobBase = wobBase.substr(0, wobBase.size() - 4);
        }
    }
    std::ifstream in(objPath);
    if (!in) {
        std::fprintf(stderr, "Failed to open OBJ: %s\n", objPath.c_str());
        return 1;
    }
    wowee::pipeline::WoweeBuilding bld;
    const ObjDocument obj = parseObj(in);

    // The doodad placements --export-wob-obj wrote as comments, read back so
    // the round-trip keeps every prop in the building.
    for (const std::string& comment : obj.comments) {
        if (comment.rfind("# doodad ", 0) != 0) continue;
        std::istringstream ss(comment);
        std::string hash, doodadKw, modelPath, posKw, posStr, rotKw, rotStr,
                    scaleKw;
        float scale = 1.0f;
        ss >> hash >> doodadKw >> modelPath >> posKw >> posStr
           >> rotKw >> rotStr >> scaleKw >> scale;
        const auto parse3 = [](const std::string& s, glm::vec3& out) {
            return std::sscanf(s.c_str(), "%f,%f,%f", &out.x, &out.y, &out.z) == 3;
        };
        wowee::pipeline::WoweeBuilding::DoodadPlacement d;
        d.modelPath = modelPath;
        if (parse3(posStr, d.position) && parse3(rotStr, d.rotation) &&
            std::isfinite(scale) && scale > 0.0f) {
            d.scale = scale;
            bld.doodads.push_back(d);
        }
    }

    // One WOB group per `g` block, and one vertex pool per group: the dedupe
    // table is per-group because a group's indices are local to its own
    // buffer. Groups are created only when a face lands in them, so a `g` with
    // no geometry does not become an empty group.
    std::vector<int> groupIndexFor(obj.groupNames.size(), -1);
    std::vector<std::unordered_map<std::string, uint32_t>> groupDedupe(
        obj.groupNames.size());

    const auto ensureGroup = [&](size_t objGroup) -> int {
        if (groupIndexFor[objGroup] >= 0) return groupIndexFor[objGroup];
        wowee::pipeline::WoweeBuilding::Group g;
        // An unnamed group is one the file never declared: everything before
        // the first `g`, which is the whole file for an OBJ without groups.
        g.name = obj.groupNames[objGroup].empty() ? std::string("imported")
                                                  : obj.groupNames[objGroup];
        if (g.name.size() >= 8 &&
            g.name.substr(g.name.size() - 8) == "_outdoor") {
            g.name = g.name.substr(0, g.name.size() - 8);
            g.isOutdoor = true;
        }
        bld.groups.push_back(g);
        groupIndexFor[objGroup] = static_cast<int>(bld.groups.size()) - 1;
        return groupIndexFor[objGroup];
    };

    const auto vertexFor = [&](size_t objGroup, const ObjCorner& corner) -> uint32_t {
        const int gi = ensureGroup(objGroup);
        const std::string key = std::to_string(corner.position) + "/" +
                                std::to_string(corner.texcoord) + "/" +
                                std::to_string(corner.normal);
        auto& dedupe = groupDedupe[objGroup];
        const auto it = dedupe.find(key);
        if (it != dedupe.end()) return it->second;

        wowee::pipeline::WoweeBuilding::Vertex vert;
        vert.position = obj.positions[static_cast<size_t>(corner.position)];
        if (corner.texcoord >= 0) {
            vert.texCoord = obj.texcoords[static_cast<size_t>(corner.texcoord)];
            // Reverse the V-flip from --export-wob-obj.
            vert.texCoord.y = 1.0f - vert.texCoord.y;
        } else {
            vert.texCoord = {0, 0};
        }
        vert.normal = corner.normal >= 0
            ? obj.normals[static_cast<size_t>(corner.normal)]
            : glm::vec3(0, 0, 1);
        vert.color = {1, 1, 1, 1};
        auto& grp = bld.groups[static_cast<size_t>(gi)];
        const uint32_t newIdx = static_cast<uint32_t>(grp.vertices.size());
        grp.vertices.push_back(vert);
        dedupe[key] = newIdx;
        return newIdx;
    };

    for (const ObjFace& face : obj.faces) {
        const int gi = ensureGroup(face.group);
        for (size_t k = 1; k + 1 < face.cornerCount; ++k) {
            const uint32_t a = vertexFor(face.group, obj.corners[face.firstCorner]);
            const uint32_t b = vertexFor(face.group, obj.corners[face.firstCorner + k]);
            const uint32_t c =
                vertexFor(face.group, obj.corners[face.firstCorner + k + 1]);
            auto& grp = bld.groups[static_cast<size_t>(gi)];
            grp.indices.push_back(a);
            grp.indices.push_back(b);
            grp.indices.push_back(c);
        }
    }
    const size_t badFaces = obj.malformedFaces;
    const size_t triangulatedNgons = obj.ngonFaces;
    const std::string& objectName = obj.objectName;

    // Compute per-group bounds + global building bound.
    if (bld.groups.empty()) {
        std::fprintf(stderr, "import-wob-obj: no geometry found in %s\n",
                     objPath.c_str());
        return 1;
    }
    glm::vec3 bMin{1e30f}, bMax{-1e30f};
    for (auto& grp : bld.groups) {
        if (grp.vertices.empty()) continue;
        grp.boundMin = grp.vertices[0].position;
        grp.boundMax = grp.boundMin;
        for (const auto& v : grp.vertices) {
            grp.boundMin = glm::min(grp.boundMin, v.position);
            grp.boundMax = glm::max(grp.boundMax, v.position);
        }
        bMin = glm::min(bMin, grp.boundMin);
        bMax = glm::max(bMax, grp.boundMax);
    }
    glm::vec3 center = (bMin + bMax) * 0.5f;
    float r2 = 0;
    for (const auto& grp : bld.groups) {
        for (const auto& v : grp.vertices) {
            glm::vec3 d = v.position - center;
            r2 = std::max(r2, glm::dot(d, d));
        }
    }
    bld.boundRadius = std::sqrt(r2);
    bld.name = objectName.empty()
        ? std::filesystem::path(objPath).stem().string()
        : objectName;
    if (!wowee::pipeline::WoweeBuildingLoader::save(bld, wobBase)) {
        std::fprintf(stderr, "import-wob-obj: failed to write %s.wob\n",
                     wobBase.c_str());
        return 1;
    }
    size_t totalV = 0, totalI = 0;
    for (const auto& g : bld.groups) {
        totalV += g.vertices.size();
        totalI += g.indices.size();
    }
    std::printf("Imported %s -> %s.wob\n", objPath.c_str(), wobBase.c_str());
    std::printf("  %zu groups, %zu verts, %zu tris, %zu doodad placements\n",
                bld.groups.size(), totalV, totalI / 3, bld.doodads.size());
    if (triangulatedNgons > 0) {
        std::printf("  fan-triangulated %zu n-gon(s)\n", triangulatedNgons);
    }
    if (badFaces > 0) {
        std::printf("  warning: skipped %zu malformed face(s)\n", badFaces);
    }
    return 0;
}

int handleExportWocObj(int& i, int argc, char** argv) {
    // Visualize a WOC collision mesh in any 3D tool. Each
    // walkability class becomes its own OBJ group (walkable /
    // steep / water / indoor) so designers can hide categories
    // independently in Blender to debug 'why can the player
    // walk here?' or 'why can't they walk there?'.
    std::string path = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "WOC not found: %s\n", path.c_str());
        return 1;
    }
    if (outPath.empty()) {
        outPath = path;
        if (outPath.size() >= 4 &&
            outPath.substr(outPath.size() - 4) == ".woc") {
            outPath = outPath.substr(0, outPath.size() - 4);
        }
        outPath += ".obj";
    }
    auto woc = wowee::pipeline::WoweeCollisionBuilder::load(path);
    if (!woc.isValid()) {
        std::fprintf(stderr, "WOC has no triangles: %s\n", path.c_str());
        return 1;
    }
    std::ofstream obj(outPath);
    if (!obj) {
        std::fprintf(stderr, "Failed to open output file: %s\n", outPath.c_str());
        return 1;
    }
    // Bucket triangles by flag combination so the OBJ can split
    // them into named groups. Flag bits: walkable=0x01, water=0x02,
    // steep=0x04, indoor=0x08 (per WoweeCollision::Triangle).
    // Triangles can have multiple flags set so a per-flag group
    // would over-count; instead we bucket by exact flag value.
    std::unordered_map<uint8_t, std::vector<size_t>> byFlag;
    for (size_t t = 0; t < woc.triangles.size(); ++t) {
        byFlag[woc.triangles[t].flags].push_back(t);
    }
    obj << "# Wavefront OBJ generated by wowee_editor --export-woc-obj\n";
    obj << "# Source: " << path << "\n";
    obj << "# Triangles: " << woc.triangles.size()
        << " (walkable=" << woc.walkableCount()
        << " steep=" << woc.steepCount() << ")\n";
    obj << "# Tile: (" << woc.tileX << ", " << woc.tileY << ")\n\n";
    obj << "o WoweeCollision\n";
    // Emit ALL vertices first (3 per triangle, no dedupe - the
    // collision mesh has triangle-soup topology where shared
    // verts often have different flags, so deduping would
    // actually merge categories).
    for (const auto& tri : woc.triangles) {
        obj << "v " << tri.v0.x << " " << tri.v0.y << " " << tri.v0.z << "\n";
        obj << "v " << tri.v1.x << " " << tri.v1.y << " " << tri.v1.z << "\n";
        obj << "v " << tri.v2.x << " " << tri.v2.y << " " << tri.v2.z << "\n";
    }
    // Emit faces grouped by flag class. OBJ index of triangle t
    // vertex k is (t * 3 + k + 1) - 1-based, three verts per tri.
    auto flagName = [](uint8_t f) {
        if (f == 0) return std::string("nonwalkable");
        std::string s;
        if (f & 0x01) s += "walkable";
        if (f & 0x02) { if (!s.empty()) s += "_"; s += "water"; }
        if (f & 0x04) { if (!s.empty()) s += "_"; s += "steep"; }
        if (f & 0x08) { if (!s.empty()) s += "_"; s += "indoor"; }
        if (s.empty()) s = "flag" + std::to_string(int(f));
        return s;
    };
    for (const auto& [flag, tris] : byFlag) {
        obj << "g " << flagName(flag) << "\n";
        for (size_t t : tris) {
            uint32_t base = static_cast<uint32_t>(t * 3 + 1);
            obj << "f " << base << " " << (base + 1) << " " << (base + 2) << "\n";
        }
    }
    obj.close();
    std::printf("Exported %s -> %s\n", path.c_str(), outPath.c_str());
    std::printf("  %zu triangles in %zu flag class(es), tile (%u, %u)\n",
                woc.triangles.size(), byFlag.size(), woc.tileX, woc.tileY);
    return 0;
}

int handleExportWhmObj(int& i, int argc, char** argv) {
    // Convert a WHM/WOT terrain pair to OBJ for visualization in
    // Blender / MeshLab. Emits the 9x9 outer vertex grid per
    // chunk (skipping the 8x8 inner verts the engine uses for
    // 4-tri fans) - that's the canonical 'heightmap as mesh'
    // view, 256 chunks × 81 verts = 20736 verts, 32768 tris.
    // Geometry mirrors WoweeCollisionBuilder's outer-grid layout
    // exactly so the OBJ aligns with the corresponding WOC.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
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
    if (outPath.empty()) outPath = base + ".obj";
    wowee::pipeline::ADTTerrain terrain;
    wowee::pipeline::WoweeTerrainLoader::load(base, terrain);
    std::ofstream obj(outPath);
    if (!obj) {
        std::fprintf(stderr, "Failed to open output file: %s\n", outPath.c_str());
        return 1;
    }
    // Tile + chunk constants - must match WoweeCollisionBuilder so
    // exports of the same source align in space when overlaid.
    constexpr float kTileSize = 533.33333f;
    constexpr float kChunkSize = kTileSize / 16.0f;
    constexpr float kVertSpacing = kChunkSize / 8.0f;
    obj << "# Wavefront OBJ generated by wowee_editor --export-whm-obj\n";
    obj << "# Source: " << base << ".whm\n";
    obj << "# Tile coord: (" << terrain.coord.x << ", " << terrain.coord.y << ")\n";
    obj << "# Layout: 9x9 outer vertex grid per chunk, 8x8 quads -> 2 tris each\n\n";
    obj << "o WoweeTerrain_" << terrain.coord.x << "_" << terrain.coord.y << "\n";
    int loadedChunks = 0;
    uint32_t vertOffset = 0;
    for (int cx = 0; cx < 16; ++cx) {
        for (int cy = 0; cy < 16; ++cy) {
            const auto& chunk = terrain.getChunk(cx, cy);
            if (!chunk.heightMap.isLoaded()) continue;
            loadedChunks++;
            // Same XY origin formula as collision builder so
            // overlaid OBJ exports line up exactly.
            float chunkBaseX = (32.0f - terrain.coord.y) * kTileSize - cy * kChunkSize;
            float chunkBaseY = (32.0f - terrain.coord.x) * kTileSize - cx * kChunkSize;
            // Emit 9x9 outer verts. Layout: heights[row*17 + col]
            // for col in [0,8] (the inner 8 verts at col 9..16
            // are skipped - they're the quad-center verts).
            for (int row = 0; row < 9; ++row) {
                for (int col = 0; col < 9; ++col) {
                    float x = chunkBaseX - row * kVertSpacing;
                    float y = chunkBaseY - col * kVertSpacing;
                    float z = chunk.position[2] +
                              chunk.heightMap.heights[row * 17 + col];
                    obj << "v " << x << " " << y << " " << z << "\n";
                }
            }
            // Per-vertex UV: just the row/col in 0..1 - Blender
            // can use this to slap a checker texture for scale.
            for (int row = 0; row < 9; ++row) {
                for (int col = 0; col < 9; ++col) {
                    obj << "vt " << (col / 8.0f) << " "
                                  << (row / 8.0f) << "\n";
                }
            }
            // 8x8 quads - two tris each, respecting hole bits so
            // cave-entrance quads correctly disappear from the mesh.
            bool isHoleChunk = (chunk.holes != 0);
            obj << "g chunk_" << cx << "_" << cy << "\n";
            auto idx = [&](int r, int c) {
                return vertOffset + r * 9 + c + 1;  // 1-based
            };
            for (int row = 0; row < 8; ++row) {
                for (int col = 0; col < 8; ++col) {
                    if (isHoleChunk) {
                        int hx = col / 2, hy = row / 2;
                        if (chunk.holes & (1 << (hy * 4 + hx))) continue;
                    }
                    uint32_t i00 = idx(row, col);
                    uint32_t i10 = idx(row, col + 1);
                    uint32_t i01 = idx(row + 1, col);
                    uint32_t i11 = idx(row + 1, col + 1);
                    obj << "f " << i00 << "/" << i00 << " "
                        << i10 << "/" << i10 << " "
                        << i11 << "/" << i11 << "\n";
                    obj << "f " << i00 << "/" << i00 << " "
                        << i11 << "/" << i11 << " "
                        << i01 << "/" << i01 << "\n";
                }
            }
            vertOffset += 81;  // 9x9 verts per chunk
        }
    }
    obj.close();
    // Estimated tri count: chunks × 128 (8x8 quads × 2 tris).
    // Holes reduce this but counting exactly would mean walking
    // the bitmask again - the rough estimate is the user-visible
    // useful number anyway.
    std::printf("Exported %s.whm -> %s\n", base.c_str(), outPath.c_str());
    std::printf("  %d chunks loaded, ~%d verts, ~%d tris\n",
                loadedChunks, loadedChunks * 81, loadedChunks * 128);
    return 0;
}


}  // namespace

bool handleWorldIo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--export-wob-glb") == 0 && i + 1 < argc) {
        outRc = handleExportWobGlb(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-whm-glb") == 0 && i + 1 < argc) {
        outRc = handleExportWhmGlb(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wob-obj") == 0 && i + 1 < argc) {
        outRc = handleExportWobObj(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wob-obj") == 0 && i + 1 < argc) {
        outRc = handleImportWobObj(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-woc-obj") == 0 && i + 1 < argc) {
        outRc = handleExportWocObj(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-whm-obj") == 0 && i + 1 < argc) {
        outRc = handleExportWhmObj(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
