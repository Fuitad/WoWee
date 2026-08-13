#include "cli_bake.hpp"
#include "gltf_glb.hpp"
#include "cli_weld.hpp"

#include "pipeline/wowee_model.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "object_placer.hpp"
#include "zone_manifest.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleBakeZoneGlb(int& i, int argc, char** argv) {
    // Bake every WHM tile in a zone into ONE .glb so the whole
    // multi-tile zone opens in three.js / model-viewer with one
    // file. Each tile becomes its own mesh+node so they can be
    // toggled independently. v1: terrain only - object/WOB
    // instances are a follow-up that needs careful per-mesh
    // bufferView slicing.
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "bake-zone-glb: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "bake-zone-glb: failed to parse zone.json\n");
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/" + zm.mapName + ".glb";
    if (zm.tiles.empty()) {
        std::fprintf(stderr, "bake-zone-glb: zone has no tiles\n");
        return 1;
    }
    constexpr float kTileSize = 533.33333f;
    constexpr float kChunkSize = kTileSize / 16.0f;
    constexpr float kVertSpacing = kChunkSize / 8.0f;
    // Per-tile mesh metadata so we can create one node per tile
    // and slice its index range from the shared bufferView.
    struct TileMesh {
        int tx, ty;
        uint32_t vertOff, vertCount;
        uint32_t idxOff, idxCount;
    };
    std::vector<TileMesh> tileMeshes;
    std::vector<glm::vec3> positions;
    std::vector<uint32_t>  indices;
    int loadedTiles = 0;
    glm::vec3 bMin{1e30f}, bMax{-1e30f};
    for (const auto& [tx, ty] : zm.tiles) {
        std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                               std::to_string(tx) + "_" + std::to_string(ty);
        if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) {
            std::fprintf(stderr,
                "bake-zone-glb: tile (%d,%d) WHM/WOT missing - skipping\n",
                tx, ty);
            continue;
        }
        wowee::pipeline::ADTTerrain terrain;
        wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
        TileMesh tm{tx, ty, 0, 0, 0, 0};
        tm.vertOff = static_cast<uint32_t>(positions.size());
        tm.idxOff  = static_cast<uint32_t>(indices.size());
        // Same per-chunk outer-grid layout as --export-whm-glb,
        // but accumulated across all tiles so they share one
        // global vertex+index pool.
        for (int cx = 0; cx < 16; ++cx) {
            for (int cy = 0; cy < 16; ++cy) {
                const auto& chunk = terrain.getChunk(cx, cy);
                if (!chunk.heightMap.isLoaded()) continue;
                float chunkBaseX = (32.0f - terrain.coord.y) * kTileSize - cy * kChunkSize;
                float chunkBaseY = (32.0f - terrain.coord.x) * kTileSize - cx * kChunkSize;
                uint32_t chunkVertOff =
                    static_cast<uint32_t>(positions.size());
                for (int row = 0; row < 9; ++row) {
                    for (int col = 0; col < 9; ++col) {
                        glm::vec3 p{
                            chunkBaseX - row * kVertSpacing,
                            chunkBaseY - col * kVertSpacing,
                            chunk.position[2] +
                                chunk.heightMap.heights[row * 17 + col]
                        };
                        positions.push_back(p);
                        bMin = glm::min(bMin, p);
                        bMax = glm::max(bMax, p);
                    }
                }
                bool isHoleChunk = (chunk.holes != 0);
                for (int row = 0; row < 8; ++row) {
                    for (int col = 0; col < 8; ++col) {
                        if (isHoleChunk) {
                            int hx = col / 2, hy = row / 2;
                            if (chunk.holes & (1 << (hy * 4 + hx))) continue;
                        }
                        auto idx = [&](int r, int c) {
                            return chunkVertOff + r * 9 + c;
                        };
                        indices.push_back(idx(row, col));
                        indices.push_back(idx(row, col + 1));
                        indices.push_back(idx(row + 1, col + 1));
                        indices.push_back(idx(row, col));
                        indices.push_back(idx(row + 1, col + 1));
                        indices.push_back(idx(row + 1, col));
                    }
                }
            }
        }
        tm.vertCount = static_cast<uint32_t>(positions.size()) - tm.vertOff;
        tm.idxCount  = static_cast<uint32_t>(indices.size()) - tm.idxOff;
        if (tm.vertCount > 0 && tm.idxCount > 0) {
            tileMeshes.push_back(tm);
            loadedTiles++;
        }
    }
    if (loadedTiles == 0) {
        std::fprintf(stderr, "bake-zone-glb: no tiles loaded\n");
        return 1;
    }
    // Pack BIN chunk same way as --export-whm-glb (positions +
    // synthetic +Z normals + indices). Per-tile accessors slice
    // their index region via byteOffset.
    const auto packed = packTerrainBin(positions, indices);
    const std::vector<uint8_t>& bin = packed.bytes;
    const uint32_t totalV = static_cast<uint32_t>(positions.size());
    const uint32_t totalI = static_cast<uint32_t>(indices.size());
    const uint32_t posOff = packed.positionOffset;
    const uint32_t nrmOff = packed.normalOffset;
    const uint32_t idxOff = packed.indexOffset;
    const uint32_t binSize = static_cast<uint32_t>(bin.size());
    // Build glTF JSON. One mesh + one node per tile so they can
    // be toggled in viewers.
    nlohmann::json gj;
    gj["asset"] = {{"version", "2.0"},
                    {"generator", "wowee_editor --bake-zone-glb"}};
    gj["scene"] = 0;
    gj["buffers"] = nlohmann::json::array({nlohmann::json{
        {"byteLength", binSize}
    }});
    // Three shared bufferViews - pos, nrm, idx - sliced into
    // per-tile primitives via byteOffset on the index accessor.
    nlohmann::json bufferViews = nlohmann::json::array();
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", posOff},
                            {"byteLength", totalV * 12}, {"target", 34962}});
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", nrmOff},
                            {"byteLength", totalV * 12}, {"target", 34962}});
    bufferViews.push_back({{"buffer", 0}, {"byteOffset", idxOff},
                            {"byteLength", totalI * 4},  {"target", 34963}});
    gj["bufferViews"] = bufferViews;
    // Shared position+normal accessors (covering the full pool;
    // primitives reference them, the index accessor does the
    // per-tile slicing).
    nlohmann::json accessors = nlohmann::json::array();
    accessors.push_back({
        {"bufferView", 0}, {"componentType", 5126},
        {"count", totalV}, {"type", "VEC3"},
        {"min", {bMin.x, bMin.y, bMin.z}},
        {"max", {bMax.x, bMax.y, bMax.z}}
    });
    accessors.push_back({{"bufferView", 1}, {"componentType", 5126},
                          {"count", totalV}, {"type", "VEC3"}});
    // Per-tile mesh + node + indices accessor.
    nlohmann::json meshes = nlohmann::json::array();
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json sceneNodes = nlohmann::json::array();
    for (const auto& tm : tileMeshes) {
        uint32_t accIdx = static_cast<uint32_t>(accessors.size());
        accessors.push_back({
            {"bufferView", 2},
            {"byteOffset", tm.idxOff * 4},
            {"componentType", 5125},
            {"count", tm.idxCount},
            {"type", "SCALAR"}
        });
        uint32_t meshIdx = static_cast<uint32_t>(meshes.size());
        meshes.push_back({
            {"primitives", nlohmann::json::array({nlohmann::json{
                {"attributes", {{"POSITION", 0}, {"NORMAL", 1}}},
                {"indices", accIdx}, {"mode", 4}
            }})}
        });
        std::string nodeName = "tile_" + std::to_string(tm.tx) +
                               "_" + std::to_string(tm.ty);
        uint32_t nodeIdx = static_cast<uint32_t>(nodes.size());
        nodes.push_back({{"name", nodeName}, {"mesh", meshIdx}});
        sceneNodes.push_back(nodeIdx);
    }
    gj["accessors"] = accessors;
    gj["meshes"] = meshes;
    gj["nodes"] = nodes;
    gj["scenes"] = nlohmann::json::array({nlohmann::json{
        {"nodes", sceneNodes}
    }});
    // The container - header, chunks and the padding each needs - is shared
    // with the other exporters here.
    std::string glbError;
    if (!writeGlb(outPath, gj, bin, glbError)) {
        std::fprintf(stderr, "Failed to write GLB: %s\n", glbError.c_str());
        return 1;
    }
    std::printf("Baked %s -> %s\n", zoneDir.c_str(), outPath.c_str());
    std::printf("  %d tile(s), %u verts, %u tris, %zu meshes, %u-byte BIN\n",
                loadedTiles, totalV, totalI / 3,
                meshes.size(), static_cast<uint32_t>(bin.size()));
    return 0;
}

int handleBakeZoneStl(int& i, int argc, char** argv) {
    // STL counterpart to --bake-zone-glb. Designers can 3D-print a
    // miniature of an entire multi-tile zone in one slicer load -
    // useful for tabletop RPG props or a physical reference of a
    // playtest area.
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "bake-zone-stl: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "bake-zone-stl: failed to parse zone.json\n");
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/" + zm.mapName + ".stl";
    if (zm.tiles.empty()) {
        std::fprintf(stderr, "bake-zone-stl: zone has no tiles\n");
        return 1;
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr, "bake-zone-stl: cannot write %s\n", outPath.c_str());
        return 1;
    }
    constexpr float kTileSize = 533.33333f;
    constexpr float kChunkSize = kTileSize / 16.0f;
    constexpr float kVertSpacing = kChunkSize / 8.0f;
    // Solid name sanitized to alphanum + underscore.
    std::string solidName = zm.mapName;
    for (auto& c : solidName) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) c = '_';
    }
    if (solidName.empty()) solidName = "wowee_zone";
    out << "solid " << solidName << "\n";
    int loadedTiles = 0, holesSkipped = 0;
    uint64_t triCount = 0;
    // For each tile, generate the same 9x9 outer-grid mesh and
    // emit per-triangle facets directly (STL has no shared
    // vertex pool - each triangle stands alone). Compute face
    // normal from cross product (slicers use it for orientation).
    for (const auto& [tx, ty] : zm.tiles) {
        std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                               std::to_string(tx) + "_" + std::to_string(ty);
        if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) {
            std::fprintf(stderr,
                "bake-zone-stl: tile (%d, %d) WHM/WOT missing - skipping\n",
                tx, ty);
            continue;
        }
        wowee::pipeline::ADTTerrain terrain;
        wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
        loadedTiles++;
        for (int cx = 0; cx < 16; ++cx) {
            for (int cy = 0; cy < 16; ++cy) {
                const auto& chunk = terrain.getChunk(cx, cy);
                if (!chunk.heightMap.isLoaded()) continue;
                float chunkBaseX = (32.0f - terrain.coord.y) * kTileSize - cy * kChunkSize;
                float chunkBaseY = (32.0f - terrain.coord.x) * kTileSize - cx * kChunkSize;
                // Pre-compute the 9x9 vertex grid for this chunk.
                glm::vec3 V[9][9];
                for (int row = 0; row < 9; ++row) {
                    for (int col = 0; col < 9; ++col) {
                        V[row][col] = {
                            chunkBaseX - row * kVertSpacing,
                            chunkBaseY - col * kVertSpacing,
                            chunk.position[2] +
                                chunk.heightMap.heights[row * 17 + col]
                        };
                    }
                }
                bool isHoleChunk = (chunk.holes != 0);
                auto emitTri = [&](const glm::vec3& a,
                                   const glm::vec3& b,
                                   const glm::vec3& c) {
                    glm::vec3 e1 = b - a, e2 = c - a;
                    glm::vec3 n = glm::cross(e1, e2);
                    float len = glm::length(n);
                    if (len > 1e-12f) n /= len;
                    else n = {0, 0, 1};
                    out << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n"
                        << "    outer loop\n"
                        << "      vertex " << a.x << " " << a.y << " " << a.z << "\n"
                        << "      vertex " << b.x << " " << b.y << " " << b.z << "\n"
                        << "      vertex " << c.x << " " << c.y << " " << c.z << "\n"
                        << "    endloop\n"
                        << "  endfacet\n";
                    triCount++;
                };
                for (int row = 0; row < 8; ++row) {
                    for (int col = 0; col < 8; ++col) {
                        if (isHoleChunk) {
                            int hx = col / 2, hy = row / 2;
                            if (chunk.holes & (1 << (hy * 4 + hx))) {
                                holesSkipped++;
                                continue;
                            }
                        }
                        emitTri(V[row][col], V[row][col + 1], V[row + 1][col + 1]);
                        emitTri(V[row][col], V[row + 1][col + 1], V[row + 1][col]);
                    }
                }
            }
        }
    }
    out << "endsolid " << solidName << "\n";
    out.close();
    if (loadedTiles == 0) {
        std::fprintf(stderr, "bake-zone-stl: no tiles loaded\n");
        std::filesystem::remove(outPath);
        return 1;
    }
    std::printf("Baked %s -> %s\n", zoneDir.c_str(), outPath.c_str());
    std::printf("  %d tile(s), %llu facets, %d hole quads skipped\n",
                loadedTiles, static_cast<unsigned long long>(triCount),
                holesSkipped);
    return 0;
}

int handleBakeZoneObj(int& i, int argc, char** argv) {
    // OBJ companion to --bake-zone-glb / --bake-zone-stl. Same
    // multi-tile WHM aggregation, but as Wavefront OBJ - opens
    // directly in Blender / MeshLab / 3DS Max for hand-editing.
    // Each tile becomes its own 'g' block so designers can hide
    // tiles independently.
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "bake-zone-obj: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "bake-zone-obj: parse failed\n");
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/" + zm.mapName + ".obj";
    if (zm.tiles.empty()) {
        std::fprintf(stderr, "bake-zone-obj: zone has no tiles\n");
        return 1;
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr, "bake-zone-obj: cannot write %s\n", outPath.c_str());
        return 1;
    }
    constexpr float kTileSize = 533.33333f;
    constexpr float kChunkSize = kTileSize / 16.0f;
    constexpr float kVertSpacing = kChunkSize / 8.0f;
    out << "# Wavefront OBJ generated by wowee_editor --bake-zone-obj\n";
    out << "# Zone: " << zm.mapName << " (" << zm.tiles.size()
        << " tiles)\n";
    out << "o " << zm.mapName << "\n";
    // OBJ uses a single global vertex pool with per-tile g-blocks
    // and per-tile face index offsetting. We accumulate per-tile
    // vertex blocks first (so face indices know their offsets),
    // then per-tile face blocks at the end.
    // Layout: emit ALL verts first (organized by tile, in order),
    // then emit ALL face blocks. OBJ requires verts before faces
    // that reference them.
    int loadedTiles = 0;
    int totalVerts = 0;
    // Per-tile bookkeeping: vertex base index (1-based for OBJ)
    // and which faces reference it.
    struct TileMeta {
        int tx, ty;
        uint32_t vertBase;  // 1-based OBJ index of first vert
        uint32_t vertCount;
        std::vector<uint32_t> faceI0, faceI1, faceI2;  // local indices
    };
    std::vector<TileMeta> tiles;
    for (const auto& [tx, ty] : zm.tiles) {
        std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                               std::to_string(tx) + "_" + std::to_string(ty);
        if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) {
            std::fprintf(stderr,
                "bake-zone-obj: tile (%d, %d) WHM/WOT missing - skipping\n",
                tx, ty);
            continue;
        }
        wowee::pipeline::ADTTerrain terrain;
        wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
        TileMeta tm{tx, ty, static_cast<uint32_t>(totalVerts + 1), 0, {}, {}, {}};
        // Walk chunks; emit verts to file as we go (so we don't
        // hold a giant vector in memory). Track local indices for
        // face emission afterwards.
        uint32_t tileLocalIdx = 0;
        for (int cx = 0; cx < 16; ++cx) {
            for (int cy = 0; cy < 16; ++cy) {
                const auto& chunk = terrain.getChunk(cx, cy);
                if (!chunk.heightMap.isLoaded()) continue;
                float chunkBaseX = (32.0f - terrain.coord.y) * kTileSize - cy * kChunkSize;
                float chunkBaseY = (32.0f - terrain.coord.x) * kTileSize - cx * kChunkSize;
                uint32_t chunkBaseLocal = tileLocalIdx;
                for (int row = 0; row < 9; ++row) {
                    for (int col = 0; col < 9; ++col) {
                        float x = chunkBaseX - row * kVertSpacing;
                        float y = chunkBaseY - col * kVertSpacing;
                        float z = chunk.position[2] +
                                  chunk.heightMap.heights[row * 17 + col];
                        out << "v " << x << " " << y << " " << z << "\n";
                        tileLocalIdx++;
                    }
                }
                bool isHoleChunk = (chunk.holes != 0);
                for (int row = 0; row < 8; ++row) {
                    for (int col = 0; col < 8; ++col) {
                        if (isHoleChunk) {
                            int hx = col / 2, hy = row / 2;
                            if (chunk.holes & (1 << (hy * 4 + hx))) continue;
                        }
                        auto idx = [&](int r, int c) {
                            return chunkBaseLocal + r * 9 + c;
                        };
                        tm.faceI0.push_back(idx(row, col));
                        tm.faceI1.push_back(idx(row, col + 1));
                        tm.faceI2.push_back(idx(row + 1, col + 1));
                        tm.faceI0.push_back(idx(row, col));
                        tm.faceI1.push_back(idx(row + 1, col + 1));
                        tm.faceI2.push_back(idx(row + 1, col));
                    }
                }
            }
        }
        tm.vertCount = tileLocalIdx;
        totalVerts += tm.vertCount;
        if (tm.vertCount > 0) {
            tiles.push_back(std::move(tm));
            loadedTiles++;
        }
    }
    // Now emit per-tile face groups (after all verts are written).
    uint64_t totalFaces = 0;
    for (const auto& tm : tiles) {
        out << "g tile_" << tm.tx << "_" << tm.ty << "\n";
        for (size_t k = 0; k < tm.faceI0.size(); ++k) {
            uint32_t a = tm.faceI0[k] + tm.vertBase;
            uint32_t b = tm.faceI1[k] + tm.vertBase;
            uint32_t c = tm.faceI2[k] + tm.vertBase;
            out << "f " << a << " " << b << " " << c << "\n";
            totalFaces++;
        }
    }
    out.close();
    if (loadedTiles == 0) {
        std::fprintf(stderr, "bake-zone-obj: no tiles loaded\n");
        std::filesystem::remove(outPath);
        return 1;
    }
    std::printf("Baked %s -> %s\n", zoneDir.c_str(), outPath.c_str());
    std::printf("  %d tile(s), %d verts, %llu tris\n",
                loadedTiles, totalVerts,
                static_cast<unsigned long long>(totalFaces));
    return 0;
}

int handleBakeProjectObj(int& i, int argc, char** argv) {
    // Project-level OBJ bake: every zone in <projectDir> gets
    // emitted into one giant OBJ with one 'g zone_NAME' block
    // per zone. Useful for previewing an entire project's terrain
    // in MeshLab/Blender at once, or for printing the whole map.
    std::string projectDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "bake-project-obj: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = projectDir + "/project.obj";
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zoneDirs = wowee::editor::projectZoneDirs(projectDir);
    if (zoneDirs.empty()) {
        std::fprintf(stderr,
            "bake-project-obj: no zones found in %s\n",
            projectDir.c_str());
        return 1;
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "bake-project-obj: cannot write %s\n", outPath.c_str());
        return 1;
    }
    constexpr float kTileSize = 533.33333f;
    constexpr float kChunkSize = kTileSize / 16.0f;
    constexpr float kVertSpacing = kChunkSize / 8.0f;
    out << "# Wavefront OBJ generated by wowee_editor --bake-project-obj\n";
    out << "# Project: " << projectDir << " (" << zoneDirs.size() << " zones)\n";
    // Single global vertex pool. Per-zone we accumulate verts then
    // emit faces; same shape as --bake-zone-obj.
    int totalZones = 0, totalTiles = 0;
    int totalVerts = 0;
    uint64_t totalFaces = 0;
    struct Pending {
        std::string zoneName;
        uint32_t vertBase;  // 1-based OBJ index
        std::vector<uint32_t> faceI0, faceI1, faceI2;
    };
    std::vector<Pending> queues;
    for (const auto& zoneDir : zoneDirs) {
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) continue;
        Pending pq;
        pq.zoneName = zm.mapName;
        pq.vertBase = static_cast<uint32_t>(totalVerts + 1);
        int zoneTiles = 0;
        uint32_t zoneLocalIdx = 0;
        for (const auto& [tx, ty] : zm.tiles) {
            std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                                    std::to_string(tx) + "_" +
                                    std::to_string(ty);
            if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) continue;
            wowee::pipeline::ADTTerrain terrain;
            wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
            zoneTiles++;
            for (int cx = 0; cx < 16; ++cx) {
                for (int cy = 0; cy < 16; ++cy) {
                    const auto& chunk = terrain.getChunk(cx, cy);
                    if (!chunk.heightMap.isLoaded()) continue;
                    float chunkBaseX = (32.0f - terrain.coord.y) * kTileSize - cy * kChunkSize;
                    float chunkBaseY = (32.0f - terrain.coord.x) * kTileSize - cx * kChunkSize;
                    uint32_t chunkBaseLocal = zoneLocalIdx;
                    for (int row = 0; row < 9; ++row) {
                        for (int col = 0; col < 9; ++col) {
                            float x = chunkBaseX - row * kVertSpacing;
                            float y = chunkBaseY - col * kVertSpacing;
                            float z = chunk.position[2] +
                                      chunk.heightMap.heights[row * 17 + col];
                            out << "v " << x << " " << y << " " << z << "\n";
                            zoneLocalIdx++;
                        }
                    }
                    bool isHoleChunk = (chunk.holes != 0);
                    for (int row = 0; row < 8; ++row) {
                        for (int col = 0; col < 8; ++col) {
                            if (isHoleChunk) {
                                int hx = col / 2, hy = row / 2;
                                if (chunk.holes & (1 << (hy * 4 + hx))) continue;
                            }
                            auto idx = [&](int r, int c) {
                                return chunkBaseLocal + r * 9 + c;
                            };
                            pq.faceI0.push_back(idx(row, col));
                            pq.faceI1.push_back(idx(row, col + 1));
                            pq.faceI2.push_back(idx(row + 1, col + 1));
                            pq.faceI0.push_back(idx(row, col));
                            pq.faceI1.push_back(idx(row + 1, col + 1));
                            pq.faceI2.push_back(idx(row + 1, col));
                        }
                    }
                }
            }
        }
        if (zoneLocalIdx == 0) continue;
        totalVerts += zoneLocalIdx;
        totalTiles += zoneTiles;
        totalZones++;
        queues.push_back(std::move(pq));
    }
    // After all verts written, emit faces grouped by zone.
    for (const auto& pq : queues) {
        out << "g zone_" << pq.zoneName << "\n";
        for (size_t k = 0; k < pq.faceI0.size(); ++k) {
            out << "f " << (pq.faceI0[k] + pq.vertBase) << " "
                << (pq.faceI1[k] + pq.vertBase) << " "
                << (pq.faceI2[k] + pq.vertBase) << "\n";
            totalFaces++;
        }
    }
    out.close();
    std::printf("Baked %s -> %s\n", projectDir.c_str(), outPath.c_str());
    std::printf("  %d zone(s), %d tiles, %d verts, %llu tris\n",
                totalZones, totalTiles, totalVerts,
                static_cast<unsigned long long>(totalFaces));
    return 0;
}

int handleBakeProjectStlOrGlb(int& i, int argc, char** argv) {
    // STL + glTF project bakes share the per-zone walking logic
    // with --bake-project-obj. Only the output emission differs:
    //   STL → per-triangle 'facet normal'+'outer loop'+vertex×3
    //   GLB → packed BIN chunk + JSON describing per-zone meshes
    // Coords match across all three exporters so an .obj/.stl/
    // .glb of the same source line up spatially when overlaid.
    bool isStl = (std::strcmp(argv[i], "--bake-project-stl") == 0);
    const char* cmdName = isStl ? "bake-project-stl" : "bake-project-glb";
    std::string projectDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "%s: %s is not a directory\n", cmdName, projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) {
        outPath = projectDir + "/project." + (isStl ? "stl" : "glb");
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zoneDirs = wowee::editor::projectZoneDirs(projectDir);
    if (zoneDirs.empty()) {
        std::fprintf(stderr, "%s: no zones found\n", cmdName);
        return 1;
    }
    constexpr float kTileSize = 533.33333f;
    constexpr float kChunkSize = kTileSize / 16.0f;
    constexpr float kVertSpacing = kChunkSize / 8.0f;
    // Common pass: collect per-zone vertex+index pools. STL emits
    // per-triangle facets directly; GLB packs everything into BIN.
    struct ZonePool {
        std::string name;
        std::vector<glm::vec3> verts;
        std::vector<uint32_t> indices;
    };
    std::vector<ZonePool> zones;
    int totalZones = 0, totalTiles = 0;
    glm::vec3 bMin{1e30f}, bMax{-1e30f};
    for (const auto& zoneDir : zoneDirs) {
        wowee::editor::ZoneManifest zm;
        if (!zm.load(zoneDir + "/zone.json")) continue;
        ZonePool zp;
        zp.name = zm.mapName;
        int zoneTiles = 0;
        for (const auto& [tx, ty] : zm.tiles) {
            std::string tileBase = zoneDir + "/" + zm.mapName + "_" +
                                    std::to_string(tx) + "_" + std::to_string(ty);
            if (!wowee::pipeline::WoweeTerrainLoader::exists(tileBase)) continue;
            wowee::pipeline::ADTTerrain terrain;
            wowee::pipeline::WoweeTerrainLoader::load(tileBase, terrain);
            zoneTiles++;
            for (int cx = 0; cx < 16; ++cx) {
                for (int cy = 0; cy < 16; ++cy) {
                    const auto& chunk = terrain.getChunk(cx, cy);
                    if (!chunk.heightMap.isLoaded()) continue;
                    float chunkBaseX = (32.0f - terrain.coord.y) * kTileSize - cy * kChunkSize;
                    float chunkBaseY = (32.0f - terrain.coord.x) * kTileSize - cx * kChunkSize;
                    uint32_t chunkBase = static_cast<uint32_t>(zp.verts.size());
                    for (int row = 0; row < 9; ++row) {
                        for (int col = 0; col < 9; ++col) {
                            glm::vec3 p{
                                chunkBaseX - row * kVertSpacing,
                                chunkBaseY - col * kVertSpacing,
                                chunk.position[2] +
                                    chunk.heightMap.heights[row * 17 + col]
                            };
                            zp.verts.push_back(p);
                            bMin = glm::min(bMin, p);
                            bMax = glm::max(bMax, p);
                        }
                    }
                    bool isHoleChunk = (chunk.holes != 0);
                    for (int row = 0; row < 8; ++row) {
                        for (int col = 0; col < 8; ++col) {
                            if (isHoleChunk) {
                                int hx = col / 2, hy = row / 2;
                                if (chunk.holes & (1 << (hy * 4 + hx))) continue;
                            }
                            auto idx = [&](int r, int c) {
                                return chunkBase + r * 9 + c;
                            };
                            zp.indices.push_back(idx(row, col));
                            zp.indices.push_back(idx(row, col + 1));
                            zp.indices.push_back(idx(row + 1, col + 1));
                            zp.indices.push_back(idx(row, col));
                            zp.indices.push_back(idx(row + 1, col + 1));
                            zp.indices.push_back(idx(row + 1, col));
                        }
                    }
                }
            }
        }
        if (zp.verts.empty()) continue;
        totalTiles += zoneTiles;
        totalZones++;
        zones.push_back(std::move(zp));
    }
    if (zones.empty()) {
        std::fprintf(stderr, "%s: no loadable terrain found\n", cmdName);
        return 1;
    }
    if (isStl) {
        std::ofstream out(outPath);
        if (!out) {
            std::fprintf(stderr, "%s: cannot write %s\n", cmdName, outPath.c_str());
            return 1;
        }
        out << "solid wowee_project\n";
        uint64_t triCount = 0;
        for (const auto& zp : zones) {
            for (size_t k = 0; k + 2 < zp.indices.size(); k += 3) {
                const auto& v0 = zp.verts[zp.indices[k]];
                const auto& v1 = zp.verts[zp.indices[k + 1]];
                const auto& v2 = zp.verts[zp.indices[k + 2]];
                glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
                float len = glm::length(n);
                if (len > 1e-12f) n /= len; else n = {0, 0, 1};
                out << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n"
                    << "    outer loop\n"
                    << "      vertex " << v0.x << " " << v0.y << " " << v0.z << "\n"
                    << "      vertex " << v1.x << " " << v1.y << " " << v1.z << "\n"
                    << "      vertex " << v2.x << " " << v2.y << " " << v2.z << "\n"
                    << "    endloop\n"
                    << "  endfacet\n";
                triCount++;
            }
        }
        out << "endsolid wowee_project\n";
        out.close();
        std::printf("Baked %s -> %s\n", projectDir.c_str(), outPath.c_str());
        std::printf("  %d zone(s), %d tiles, %llu facets\n",
                    totalZones, totalTiles,
                    static_cast<unsigned long long>(triCount));
        return 0;
    }
    // GLB path: pack positions+normals+indices into one BIN chunk,
    // one mesh+node per zone with sliced index accessor.
    uint32_t totalV = 0, totalI = 0;
    for (const auto& zp : zones) {
        totalV += static_cast<uint32_t>(zp.verts.size());
        totalI += static_cast<uint32_t>(zp.indices.size());
    }
    const uint32_t posOff = 0;
    const uint32_t nrmOff = posOff + totalV * 12;
    const uint32_t idxOff = nrmOff + totalV * 12;
    const uint32_t binSize = idxOff + totalI * 4;
    std::vector<uint8_t> bin(binSize);
    uint32_t vCursor = 0, iCursor = 0;
    // Per-zone bookkeeping for accessor slicing.
    struct ZoneSlice { std::string name; uint32_t vOff, vCnt, iOff, iCnt; };
    std::vector<ZoneSlice> slices;
    for (const auto& zp : zones) {
        ZoneSlice s{zp.name, vCursor, static_cast<uint32_t>(zp.verts.size()),
                     iCursor, static_cast<uint32_t>(zp.indices.size())};
        for (const auto& v : zp.verts) {
            std::memcpy(&bin[posOff + vCursor * 12 + 0], &v.x, 4);
            std::memcpy(&bin[posOff + vCursor * 12 + 4], &v.y, 4);
            std::memcpy(&bin[posOff + vCursor * 12 + 8], &v.z, 4);
            float nx = 0, ny = 0, nz = 1;
            std::memcpy(&bin[nrmOff + vCursor * 12 + 0], &nx, 4);
            std::memcpy(&bin[nrmOff + vCursor * 12 + 4], &ny, 4);
            std::memcpy(&bin[nrmOff + vCursor * 12 + 8], &nz, 4);
            vCursor++;
        }
        // Offset zone indices by the global vertBase so they
        // resolve into the merged pool.
        for (uint32_t idx : zp.indices) {
            uint32_t global = idx + s.vOff;
            std::memcpy(&bin[idxOff + iCursor * 4], &global, 4);
            iCursor++;
        }
        slices.push_back(s);
    }
    nlohmann::json gj;
    gj["asset"] = {{"version", "2.0"},
                    {"generator", "wowee_editor --bake-project-glb"}};
    gj["scene"] = 0;
    gj["buffers"] = nlohmann::json::array({{{"byteLength", binSize}}});
    nlohmann::json bvs = nlohmann::json::array();
    bvs.push_back({{"buffer", 0}, {"byteOffset", posOff},
                    {"byteLength", totalV * 12}, {"target", 34962}});
    bvs.push_back({{"buffer", 0}, {"byteOffset", nrmOff},
                    {"byteLength", totalV * 12}, {"target", 34962}});
    bvs.push_back({{"buffer", 0}, {"byteOffset", idxOff},
                    {"byteLength", totalI * 4},  {"target", 34963}});
    gj["bufferViews"] = bvs;
    nlohmann::json accessors = nlohmann::json::array();
    accessors.push_back({{"bufferView", 0}, {"componentType", 5126},
                          {"count", totalV}, {"type", "VEC3"},
                          {"min", {bMin.x, bMin.y, bMin.z}},
                          {"max", {bMax.x, bMax.y, bMax.z}}});
    accessors.push_back({{"bufferView", 1}, {"componentType", 5126},
                          {"count", totalV}, {"type", "VEC3"}});
    nlohmann::json meshes = nlohmann::json::array();
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json sceneNodes = nlohmann::json::array();
    for (const auto& s : slices) {
        uint32_t accIdx = static_cast<uint32_t>(accessors.size());
        accessors.push_back({{"bufferView", 2},
                              {"byteOffset", s.iOff * 4},
                              {"componentType", 5125},
                              {"count", s.iCnt}, {"type", "SCALAR"}});
        uint32_t meshIdx = static_cast<uint32_t>(meshes.size());
        meshes.push_back({{"primitives", nlohmann::json::array({nlohmann::json{
            {"attributes", {{"POSITION", 0}, {"NORMAL", 1}}},
            {"indices", accIdx}, {"mode", 4}}})}});
        uint32_t nodeIdx = static_cast<uint32_t>(nodes.size());
        nodes.push_back({{"name", "zone_" + s.name}, {"mesh", meshIdx}});
        sceneNodes.push_back(nodeIdx);
    }
    gj["accessors"] = accessors;
    gj["meshes"] = meshes;
    gj["nodes"] = nodes;
    gj["scenes"] = nlohmann::json::array({{{"nodes", sceneNodes}}});
    std::string glbError;
    if (!writeGlb(outPath, gj, bin, glbError)) {
        std::fprintf(stderr, "%s: %s\n", cmdName, glbError.c_str());
        return 1;
    }
    std::printf("Baked %s -> %s\n", projectDir.c_str(), outPath.c_str());
    std::printf("  %d zone(s), %d tiles, %u verts, %u tris, %u-byte BIN\n",
                totalZones, totalTiles, totalV, totalI / 3, static_cast<uint32_t>(bin.size()));
    return 0;
}


}  // namespace

int handleBakeWomCollision(int& i, int argc, char** argv) {
    // Convert a single WOM into a WOC collision file. Optional
    // --weld <eps> first welds vertices that share a position so
    // adjacent per-face-shaded faces land in the same triangle
    // network for collision queries - without it, the WOC still
    // has the right triangles but they're authored independently
    // (which is fine for raycast/walkability but loses the edge
    // adjacency info that some physics queries want).
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    bool useWeld = false;
    float weldEps = 1e-5f;
    float steepAngle = 50.0f;
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        if (std::strcmp(argv[i + 1], "--weld") == 0 && i + 2 < argc) {
            useWeld = true;
            try { weldEps = std::stof(argv[i + 2]); } catch (...) {}
            i += 2;
        } else if (std::strcmp(argv[i + 1], "--steep") == 0 && i + 2 < argc) {
            try { steepAngle = std::stof(argv[i + 2]); } catch (...) {}
            i += 2;
        } else {
            break;
        }
    }
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom") {
        base = base.substr(0, base.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr,
            "bake-wom-collision: %s.wom does not exist\n", base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    if (!wom.isValid() || wom.indices.size() % 3 != 0) {
        std::fprintf(stderr,
            "bake-wom-collision: invalid WOM (no geometry or "
            "indices%%3 != 0)\n");
        return 1;
    }
    if (outPath.empty()) outPath = base + ".woc";
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    if (useWeld) {
        // Run cli_weld to map vertex i → canonical (lowest-index)
        // representative of its equivalence class, then compact
        // positions to one entry per unique class and renumber
        // indices accordingly. The collision mesh ends up properly
        // indexed so raycasts can share edges between faces.
        std::vector<glm::vec3> srcPositions;
        srcPositions.reserve(wom.vertices.size());
        for (const auto& vert : wom.vertices) srcPositions.push_back(vert.position);
        std::size_t uniq = 0;
        std::vector<uint32_t> canon = buildWeldMap(srcPositions, weldEps, uniq);
        // Build canon→compactedIndex remap as we walk vertices in order.
        std::vector<uint32_t> remap(wom.vertices.size(),
                                     std::numeric_limits<uint32_t>::max());
        positions.reserve(uniq);
        for (std::size_t v = 0; v < wom.vertices.size(); ++v) {
            uint32_t c = canon[v];
            if (remap[c] == std::numeric_limits<uint32_t>::max()) {
                remap[c] = static_cast<uint32_t>(positions.size());
                positions.push_back(srcPositions[c]);
            }
        }
        indices.reserve(wom.indices.size());
        for (uint32_t orig : wom.indices) indices.push_back(remap[canon[orig]]);
    } else {
        positions.reserve(wom.vertices.size());
        for (const auto& vert : wom.vertices) positions.push_back(vert.position);
        indices = wom.indices;
    }
    wowee::pipeline::WoweeCollision collision;
    glm::mat4 identity(1.0f);
    wowee::pipeline::WoweeCollisionBuilder::addMesh(
        collision, positions, indices, identity, 0, steepAngle);
    if (!wowee::pipeline::WoweeCollisionBuilder::save(collision, outPath)) {
        std::fprintf(stderr,
            "bake-wom-collision: failed to write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source     : %s.wom (%zu verts -> %zu)\n",
                base.c_str(), wom.vertices.size(), positions.size());
    std::printf("  triangles  : %zu (%zu walkable, %zu steep)\n",
                collision.triangles.size(),
                collision.walkableCount(),
                collision.steepCount());
    std::printf("  steep cut  : %.1f° from horizontal\n", steepAngle);
    if (useWeld) {
        std::printf("  weld eps   : %.6f\n", weldEps);
    }
    return 0;
}

int handleBakeWobCollision(int& i, int argc, char** argv) {
    // Convert a multi-group WOB into a single WOC collision file.
    // Each group's triangles are appended via WoweeCollisionBuilder
    // ::addMesh. Optional --weld <eps> is applied PER GROUP - groups
    // are intentionally separate (rooms with portals between them),
    // so welding across groups would fuse walls that should remain
    // distinct collision surfaces.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    bool useWeld = false;
    float weldEps = 1e-5f;
    float steepAngle = 50.0f;
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        if (std::strcmp(argv[i + 1], "--weld") == 0 && i + 2 < argc) {
            useWeld = true;
            try { weldEps = std::stof(argv[i + 2]); } catch (...) {}
            i += 2;
        } else if (std::strcmp(argv[i + 1], "--steep") == 0 && i + 2 < argc) {
            try { steepAngle = std::stof(argv[i + 2]); } catch (...) {}
            i += 2;
        } else {
            break;
        }
    }
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wob") {
        base = base.substr(0, base.size() - 4);
    }
    if (!wowee::pipeline::WoweeBuildingLoader::exists(base)) {
        std::fprintf(stderr,
            "bake-wob-collision: %s.wob does not exist\n", base.c_str());
        return 1;
    }
    auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
    if (!bld.isValid()) {
        std::fprintf(stderr,
            "bake-wob-collision: %s.wob has no groups\n", base.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = base + ".woc";
    wowee::pipeline::WoweeCollision collision;
    glm::mat4 identity(1.0f);
    std::size_t totalSrc = 0, totalUniq = 0;
    for (const auto& g : bld.groups) {
        if (g.indices.size() % 3 != 0) {
            std::fprintf(stderr,
                "bake-wob-collision: group '%s' has indices %% 3 != 0\n",
                g.name.c_str());
            return 1;
        }
        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
        if (useWeld) {
            std::vector<glm::vec3> srcPositions;
            srcPositions.reserve(g.vertices.size());
            for (const auto& v : g.vertices) srcPositions.push_back(v.position);
            std::size_t uniq = 0;
            std::vector<uint32_t> canon = buildWeldMap(srcPositions, weldEps, uniq);
            std::vector<uint32_t> remap(g.vertices.size(),
                                         std::numeric_limits<uint32_t>::max());
            positions.reserve(uniq);
            for (std::size_t v = 0; v < g.vertices.size(); ++v) {
                uint32_t c = canon[v];
                if (remap[c] == std::numeric_limits<uint32_t>::max()) {
                    remap[c] = static_cast<uint32_t>(positions.size());
                    positions.push_back(srcPositions[c]);
                }
            }
            indices.reserve(g.indices.size());
            for (uint32_t orig : g.indices) indices.push_back(remap[canon[orig]]);
        } else {
            positions.reserve(g.vertices.size());
            for (const auto& v : g.vertices) positions.push_back(v.position);
            indices = g.indices;
        }
        wowee::pipeline::WoweeCollisionBuilder::addMesh(
            collision, positions, indices, identity, 0, steepAngle);
        totalSrc += g.vertices.size();
        totalUniq += positions.size();
    }
    if (!wowee::pipeline::WoweeCollisionBuilder::save(collision, outPath)) {
        std::fprintf(stderr,
            "bake-wob-collision: failed to write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source     : %s.wob (%zu groups, %zu verts -> %zu)\n",
                base.c_str(), bld.groups.size(), totalSrc, totalUniq);
    std::printf("  triangles  : %zu (%zu walkable, %zu steep)\n",
                collision.triangles.size(),
                collision.walkableCount(),
                collision.steepCount());
    std::printf("  steep cut  : %.1f° from horizontal\n", steepAngle);
    if (useWeld) {
        std::printf("  weld eps   : %.6f (per group)\n", weldEps);
    }
    return 0;
}

int handleBakeZoneCollision(int& i, int argc, char** argv) {
    // Walk every .wom and .wob under <zoneDir>, weld each one
    // independently (per-mesh / per-WOB-group), and append its
    // triangles to a single WoweeCollision. Useful for shipping
    // a zone - one .woc file holds all object collision so the
    // server side has a single artifact to serve.
    //
    // Per-file weld preserves between-object boundaries: two
    // distinct WOMs sitting at the same world position keep
    // their topology separate even if their corner positions
    // happen to overlap.
    std::string root = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    bool useWeld = false;
    float weldEps = 1e-5f;
    float steepAngle = 50.0f;
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        if (std::strcmp(argv[i + 1], "--weld") == 0 && i + 2 < argc) {
            useWeld = true;
            try { weldEps = std::stof(argv[i + 2]); } catch (...) {}
            i += 2;
        } else if (std::strcmp(argv[i + 1], "--steep") == 0 && i + 2 < argc) {
            try { steepAngle = std::stof(argv[i + 2]); } catch (...) {}
            i += 2;
        } else {
            break;
        }
    }
    namespace fs = std::filesystem;
    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::fprintf(stderr,
            "bake-zone-collision: %s is not a directory\n", root.c_str());
        return 1;
    }
    if (outPath.empty()) {
        outPath = (fs::path(root) / "zone.woc").string();
    }
    wowee::pipeline::WoweeCollision collision;
    glm::mat4 identity(1.0f);
    auto weldOne = [&](std::vector<glm::vec3>& positions,
                       std::vector<uint32_t>& indices) {
        if (!useWeld) return;
        std::size_t uniq = 0;
        std::vector<uint32_t> canon = buildWeldMap(positions, weldEps, uniq);
        std::vector<glm::vec3> compacted;
        std::vector<uint32_t> remap(positions.size(),
                                     std::numeric_limits<uint32_t>::max());
        compacted.reserve(uniq);
        for (std::size_t v = 0; v < positions.size(); ++v) {
            uint32_t c = canon[v];
            if (remap[c] == std::numeric_limits<uint32_t>::max()) {
                remap[c] = static_cast<uint32_t>(compacted.size());
                compacted.push_back(positions[c]);
            }
        }
        for (uint32_t& idx : indices) idx = remap[canon[idx]];
        positions = std::move(compacted);
    };
    int wcount = 0, bcount = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension();
        if (ext == ".wom") {
            std::string base = e.path().string();
            base = base.substr(0, base.size() - 4);
            auto wom = wowee::pipeline::WoweeModelLoader::load(base);
            if (!wom.isValid() || wom.indices.size() % 3 != 0) continue;
            std::vector<glm::vec3> positions;
            positions.reserve(wom.vertices.size());
            for (const auto& v : wom.vertices) positions.push_back(v.position);
            std::vector<uint32_t> indices = wom.indices;
            weldOne(positions, indices);
            wowee::pipeline::WoweeCollisionBuilder::addMesh(
                collision, positions, indices, identity, 0, steepAngle);
            ++wcount;
        } else if (ext == ".wob") {
            std::string base = e.path().string();
            base = base.substr(0, base.size() - 4);
            auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
            if (!bld.isValid()) continue;
            for (const auto& g : bld.groups) {
                if (g.indices.size() % 3 != 0) continue;
                std::vector<glm::vec3> positions;
                positions.reserve(g.vertices.size());
                for (const auto& v : g.vertices) positions.push_back(v.position);
                std::vector<uint32_t> indices = g.indices;
                weldOne(positions, indices);
                wowee::pipeline::WoweeCollisionBuilder::addMesh(
                    collision, positions, indices, identity, 0, steepAngle);
            }
            ++bcount;
        }
    }
    if (collision.triangles.empty()) {
        std::fprintf(stderr,
            "bake-zone-collision: no .wom or .wob found under %s\n",
            root.c_str());
        return 1;
    }
    if (!wowee::pipeline::WoweeCollisionBuilder::save(collision, outPath)) {
        std::fprintf(stderr,
            "bake-zone-collision: failed to write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  scanned    : %d WOM + %d WOB under %s\n",
                wcount, bcount, root.c_str());
    std::printf("  triangles  : %zu (%zu walkable, %zu steep)\n",
                collision.triangles.size(),
                collision.walkableCount(),
                collision.steepCount());
    std::printf("  steep cut  : %.1f° from horizontal\n", steepAngle);
    if (useWeld) {
        std::printf("  weld eps   : %.6f (per file/group)\n", weldEps);
    }
    return 0;
}

bool handleBake(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--bake-zone-glb") == 0 && i + 1 < argc) {
        outRc = handleBakeZoneGlb(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bake-zone-stl") == 0 && i + 1 < argc) {
        outRc = handleBakeZoneStl(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bake-zone-obj") == 0 && i + 1 < argc) {
        outRc = handleBakeZoneObj(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bake-project-obj") == 0 && i + 1 < argc) {
        outRc = handleBakeProjectObj(i, argc, argv); return true;
    }
    if ((std::strcmp(argv[i], "--bake-project-stl") == 0 ||
         std::strcmp(argv[i], "--bake-project-glb") == 0) &&
        i + 1 < argc) {
        outRc = handleBakeProjectStlOrGlb(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bake-wom-collision") == 0 && i + 1 < argc) {
        outRc = handleBakeWomCollision(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bake-wob-collision") == 0 && i + 1 < argc) {
        outRc = handleBakeWobCollision(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bake-zone-collision") == 0 && i + 1 < argc) {
        outRc = handleBakeZoneCollision(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
