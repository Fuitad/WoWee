#include "cli_mesh_info.hpp"
#include "zone_manifest.hpp"
#include "cli_weld.hpp"

#include "pipeline/wowee_model.hpp"

#include "cli_paths.hpp"
#include "wom_mesh_stats.hpp"
#include "pipeline/wowee_building.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoZoneModelsTotal(int& i, int argc, char** argv) {
    // Aggregate WOM/WOB stats across every model in a zone.
    // Useful for capacity planning ('how many bones across all
    // my creatures?') and perf budgeting ('total triangles
    // per frame if all loaded?').
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr,
            "info-zone-models-total: %s does not exist\n", zoneDir.c_str());
        return 1;
    }
    int womCount = 0, wobCount = 0;
    uint64_t womVerts = 0, womIndices = 0;
    uint64_t womBones = 0, womAnims = 0, womBatches = 0;
    uint64_t wobGroups = 0, wobVerts = 0, wobIndices = 0;
    uint64_t wobDoodads = 0, wobPortals = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::string base = e.path().string();
        if (base.size() > ext.size())
            base = base.substr(0, base.size() - ext.size());
        if (ext == ".wom") {
            womCount++;
            auto wom = wowee::pipeline::WoweeModelLoader::load(base);
            womVerts += wom.vertices.size();
            womIndices += wom.indices.size();
            womBones += wom.bones.size();
            womAnims += wom.animations.size();
            womBatches += wom.batches.size();
        } else if (ext == ".wob") {
            wobCount++;
            auto wob = wowee::pipeline::WoweeBuildingLoader::load(base);
            wobGroups += wob.groups.size();
            for (const auto& g : wob.groups) {
                wobVerts += g.vertices.size();
                wobIndices += g.indices.size();
            }
            wobDoodads += wob.doodads.size();
            wobPortals += wob.portals.size();
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["wom"] = {{"count", womCount},
                     {"vertices", womVerts},
                     {"indices", womIndices},
                     {"triangles", womIndices / 3},
                     {"bones", womBones},
                     {"animations", womAnims},
                     {"batches", womBatches}};
        j["wob"] = {{"count", wobCount},
                     {"groups", wobGroups},
                     {"vertices", wobVerts},
                     {"indices", wobIndices},
                     {"triangles", wobIndices / 3},
                     {"doodads", wobDoodads},
                     {"portals", wobPortals}};
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone models total: %s\n", zoneDir.c_str());
    std::printf("\n  WOM (open M2):\n");
    std::printf("    files     : %d\n", womCount);
    std::printf("    vertices  : %llu\n", static_cast<unsigned long long>(womVerts));
    std::printf("    triangles : %llu\n", static_cast<unsigned long long>(womIndices / 3));
    std::printf("    bones     : %llu\n", static_cast<unsigned long long>(womBones));
    std::printf("    anims     : %llu\n", static_cast<unsigned long long>(womAnims));
    std::printf("    batches   : %llu\n", static_cast<unsigned long long>(womBatches));
    std::printf("\n  WOB (open WMO):\n");
    std::printf("    files     : %d\n", wobCount);
    std::printf("    groups    : %llu\n", static_cast<unsigned long long>(wobGroups));
    std::printf("    vertices  : %llu\n", static_cast<unsigned long long>(wobVerts));
    std::printf("    triangles : %llu\n", static_cast<unsigned long long>(wobIndices / 3));
    std::printf("    doodads   : %llu\n", static_cast<unsigned long long>(wobDoodads));
    std::printf("    portals   : %llu\n", static_cast<unsigned long long>(wobPortals));
    std::printf("\n  Combined  :\n");
    std::printf("    vertices  : %llu\n", static_cast<unsigned long long>(womVerts + wobVerts));
    std::printf("    triangles : %llu\n", static_cast<unsigned long long>((womIndices + wobIndices) / 3));
    return 0;
}

int handleListZoneMeshesDetail(int& i, int argc, char** argv) {
    // Per-mesh breakdown of every .wom file in <zoneDir>,
    // sorted by triangle count descending so the heaviest
    // meshes float to the top. Complements
    // --list-zone-meshes (per-zone summary) by surfacing
    // individual mesh metrics - useful for spotting
    // outliers ("which mesh is using 80% of my triangle
    // budget?") and for content audits.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr,
            "list-zone-meshes-detail: %s does not exist\n", zoneDir.c_str());
        return 1;
    }
    struct Row : WomMeshStats {
        std::string path;
        uint64_t bytes;
    };
    std::vector<Row> rows;
    for (const auto& found : findFilesByExtension(zoneDir, ".wom")) {
        const auto wom = wowee::pipeline::WoweeModelLoader::load(found.base);
        Row r;
        r.path = found.relative;
        r.bytes = found.bytes;
        setMeshStats(r, wom);
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.tris > b.tris; });
    uint64_t totVerts = 0, totTris = 0, totBones = 0, totBytes = 0;
    for (const auto& r : rows) {
        totVerts += r.verts; totTris += r.tris;
        totBones += r.bones; totBytes += r.bytes;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["meshCount"] = rows.size();
        j["totals"] = {{"vertices", totVerts},
                        {"triangles", totTris},
                        {"bones", totBones},
                        {"bytes", totBytes}};
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({{"path", r.path},
                            {"version", r.version},
                            {"vertices", r.verts},
                            {"triangles", r.tris},
                            {"bones", r.bones},
                            {"batches", r.batches},
                            {"textures", r.textures},
                            {"bytes", r.bytes}});
        }
        j["meshes"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Zone meshes: %s\n", zoneDir.c_str());
    std::printf("  meshes : %zu\n", rows.size());
    std::printf("  totals : %llu verts, %llu tris, %llu bones, %.1f KB\n",
                static_cast<unsigned long long>(totVerts),
                static_cast<unsigned long long>(totTris),
                static_cast<unsigned long long>(totBones),
                totBytes / 1024.0);
    if (rows.empty()) {
        std::printf("\n  *no .wom files in this zone*\n");
        return 0;
    }
    std::printf("\n  v   verts    tris   bones  batch  tex    bytes  path\n");
    for (const auto& r : rows) {
        std::printf("  v%u %6zu  %6zu  %5zu  %5zu  %3zu  %7llu  %s\n",
                    r.version, r.verts, r.tris, r.bones,
                    r.batches, r.textures,
                    static_cast<unsigned long long>(r.bytes),
                    r.path.c_str());
    }
    return 0;
}

int handleInfoMesh(int& i, int argc, char** argv) {
    // Single-mesh detail view aggregating bounds, version,
    // batches, bones, animations, and texture slots into one
    // report. Composite of what --info-batches / --info-bones
    // / --info-batches show separately. Useful authoring
    // command: pass a WOM and see everything about it without
    // running three sub-commands.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom") {
        base = base.substr(0, base.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr,
            "info-mesh: %s.wom does not exist\n", base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "info-mesh: failed to load %s.wom\n", base.c_str());
        return 1;
    }
    // Per-batch material summary.
    static const char* blendNames[] = {
        "opaque", "alpha-test", "alpha", "additive", "?", "?", "?", "?"
    };
    if (jsonOut) {
        nlohmann::json j;
        j["base"] = base;
        j["name"] = wom.name;
        j["version"] = wom.version;
        j["bounds"] = {{"min", {wom.boundMin.x, wom.boundMin.y, wom.boundMin.z}},
                        {"max", {wom.boundMax.x, wom.boundMax.y, wom.boundMax.z}},
                        {"radius", wom.boundRadius}};
        j["counts"] = {{"vertices", wom.vertices.size()},
                        {"indices", wom.indices.size()},
                        {"triangles", wom.indices.size() / 3},
                        {"bones", wom.bones.size()},
                        {"animations", wom.animations.size()},
                        {"batches", wom.batches.size()},
                        {"textures", wom.texturePaths.size()}};
        nlohmann::json bs = nlohmann::json::array();
        for (const auto& b : wom.batches) {
            std::string tex;
            if (b.textureIndex < wom.texturePaths.size())
                tex = wom.texturePaths[b.textureIndex];
            bs.push_back({{"indexStart", b.indexStart},
                           {"indexCount", b.indexCount},
                           {"triangles", b.indexCount / 3},
                           {"textureIndex", b.textureIndex},
                           {"texture", tex},
                           {"blendMode", b.blendMode},
                           {"flags", b.flags}});
        }
        j["batchDetail"] = bs;
        j["texturePaths"] = wom.texturePaths;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Mesh: %s.wom\n", base.c_str());
    std::printf("  name       : %s\n", wom.name.c_str());
    std::printf("  version    : v%u\n", wom.version);
    std::printf("\n  Counts:\n");
    std::printf("    vertices  : %zu\n", wom.vertices.size());
    std::printf("    triangles : %zu\n", wom.indices.size() / 3);
    std::printf("    bones     : %zu\n", wom.bones.size());
    std::printf("    anims     : %zu\n", wom.animations.size());
    std::printf("    batches   : %zu\n", wom.batches.size());
    std::printf("    textures  : %zu\n", wom.texturePaths.size());
    std::printf("\n  Bounds:\n");
    std::printf("    min       : (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z);
    std::printf("    max       : (%.3f, %.3f, %.3f)\n",
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    std::printf("    radius    : %.3f\n", wom.boundRadius);
    if (!wom.batches.empty()) {
        std::printf("\n  Batches:\n");
        std::printf("    idx  iStart  iCount  tris   blend       texture\n");
        for (size_t k = 0; k < wom.batches.size(); ++k) {
            const auto& b = wom.batches[k];
            std::string tex = "<oob>";
            if (b.textureIndex < wom.texturePaths.size())
                tex = wom.texturePaths[b.textureIndex];
            if (tex.empty()) tex = "(empty)";
            int blend = b.blendMode < 8 ? b.blendMode : 0;
            std::printf("    %3zu  %6u  %6u  %4u   %-10s  %s\n",
                        k, b.indexStart, b.indexCount,
                        b.indexCount / 3, blendNames[blend],
                        tex.c_str());
        }
    }
    if (!wom.texturePaths.empty()) {
        std::printf("\n  Texture slots:\n");
        for (size_t k = 0; k < wom.texturePaths.size(); ++k) {
            std::printf("    [%zu] %s\n", k,
                        wom.texturePaths[k].empty()
                        ? "(empty placeholder)"
                        : wom.texturePaths[k].c_str());
        }
    }
    return 0;
}


int handleInfoMeshStats(int& i, int argc, char** argv) {
    // Geometric statistics on a WOM mesh: total surface area,
    // triangle area distribution (min/max/mean/median), edge
    // count, and a watertight check (is every edge shared by
    // exactly 2 triangles?). Watertightness is what collision
    // baking and physics need; the histogram catches degenerate
    // (zero-area) and outsized triangles that would otherwise
    // hide inside a mesh.
    //
    // Procedural primitives use per-face vertex layouts (each box
    // face has its own 4 vertices for flat shading), so they will
    // always read as "not watertight" at the index level even when
    // visually closed. Pass --weld <eps> to first map vertices that
    // share a position (within eps) to a canonical representative
    // before edge analysis - this measures topological closure of
    // the actual surface, which is what collision baking checks.
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
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom") {
        base = base.substr(0, base.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr,
            "info-mesh-stats: %s.wom does not exist\n", base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "info-mesh-stats: failed to load %s.wom\n", base.c_str());
        return 1;
    }
    if (wom.indices.size() % 3 != 0) {
        std::fprintf(stderr,
            "info-mesh-stats: index count %zu not divisible by 3\n",
            wom.indices.size());
        return 1;
    }
    const std::size_t triCount = wom.indices.size() / 3;
    std::vector<double> areas;
    areas.reserve(triCount);
    double totalArea = 0.0;
    std::size_t degenerate = 0;
    // Optional vertex weld: hash positions to a quantized grid at
    // 1/weldEps resolution, then map every vertex to the lowest
    // index that shares its grid cell. Used as the canonical id
    // when keying edges, so adjacent triangles whose corner
    // vertices happen to share a position (per-face shading
    // emitting duplicates) get unified.
    std::vector<uint32_t> canon;
    std::size_t uniquePositions = 0;
    if (useWeld) {
        std::vector<glm::vec3> positions;
        positions.reserve(wom.vertices.size());
        for (const auto& v : wom.vertices) positions.push_back(v.position);
        canon = buildWeldMap(positions, weldEps, uniquePositions);
    } else {
        canon.resize(wom.vertices.size());
        for (std::size_t v = 0; v < wom.vertices.size(); ++v) {
            canon[v] = static_cast<uint32_t>(v);
        }
        uniquePositions = wom.vertices.size();
    }
    // First pass: triangle areas + range checks (no edge work).
    for (std::size_t t = 0; t < triCount; ++t) {
        uint32_t i0 = wom.indices[t * 3 + 0];
        uint32_t i1 = wom.indices[t * 3 + 1];
        uint32_t i2 = wom.indices[t * 3 + 2];
        if (i0 >= wom.vertices.size() ||
            i1 >= wom.vertices.size() ||
            i2 >= wom.vertices.size()) {
            std::fprintf(stderr,
                "info-mesh-stats: out-of-range index in triangle %zu\n", t);
            return 1;
        }
        glm::vec3 a = wom.vertices[i0].position;
        glm::vec3 b = wom.vertices[i1].position;
        glm::vec3 c = wom.vertices[i2].position;
        double area = 0.5 * glm::length(glm::cross(b - a, c - a));
        if (area < 1e-12) ++degenerate;
        areas.push_back(area);
        totalArea += area;
    }
    double minArea = areas.empty() ? 0.0 :
                     *std::min_element(areas.begin(), areas.end());
    double maxArea = areas.empty() ? 0.0 :
                     *std::max_element(areas.begin(), areas.end());
    double meanArea = areas.empty() ? 0.0 : totalArea / areas.size();
    double medianArea = 0.0;
    if (!areas.empty()) {
        std::vector<double> sortedAreas(areas);
        std::nth_element(sortedAreas.begin(),
                         sortedAreas.begin() + sortedAreas.size() / 2,
                         sortedAreas.end());
        medianArea = sortedAreas[sortedAreas.size() / 2];
    }
    // Edge analysis via shared cli_weld utility. Skipped for huge
    // meshes (>2M tris) since the underlying unordered_map would
    // balloon.
    const bool runEdgeAnalysis = (triCount <= 2'000'000);
    EdgeStats edges;
    if (runEdgeAnalysis) {
        edges = classifyEdges(wom.indices, canon);
    }
    bool watertight = runEdgeAnalysis && edges.watertight();
    glm::vec3 dim = wom.boundMax - wom.boundMin;
    double bboxVol = double(dim.x) * dim.y * dim.z;
    if (jsonOut) {
        nlohmann::json j;
        j["base"] = base;
        j["triangles"] = triCount;
        j["surfaceArea"] = totalArea;
        j["bboxVolume"] = bboxVol;
        j["areas"] = {{"min", minArea}, {"max", maxArea},
                       {"mean", meanArea}, {"median", medianArea}};
        j["degenerateTriangles"] = degenerate;
        j["welded"] = useWeld;
        if (useWeld) {
            j["weldEps"] = weldEps;
            j["uniquePositions"] = uniquePositions;
            j["totalVertices"] = wom.vertices.size();
        }
        if (runEdgeAnalysis) {
            j["edges"] = {{"total", edges.total},
                           {"boundary", edges.boundary},
                           {"manifold", edges.manifold},
                           {"nonManifold", edges.nonManifold}};
            j["watertight"] = watertight;
        }
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Mesh stats: %s.wom\n", base.c_str());
    std::printf("  triangles      : %zu (%zu degenerate)\n",
                triCount, degenerate);
    std::printf("  surface area   : %.4f\n", totalArea);
    std::printf("  bbox volume    : %.4f (%.3f x %.3f x %.3f)\n",
                bboxVol, dim.x, dim.y, dim.z);
    std::printf("  triangle area  : min %.6f / max %.6f / mean %.6f / median %.6f\n",
                minArea, maxArea, meanArea, medianArea);
    if (useWeld) {
        std::printf("  welded         : %zu unique positions / %zu vertices "
                    "(eps %.6f)\n",
                    uniquePositions, wom.vertices.size(), weldEps);
    }
    if (runEdgeAnalysis) {
        std::printf("  edges          : %zu total\n", edges.total);
        std::printf("    boundary     : %zu (open seams)\n", edges.boundary);
        std::printf("    manifold     : %zu (shared by 2 tris)\n", edges.manifold);
        std::printf("    non-manifold : %zu (shared by 3+ tris)\n",
                    edges.nonManifold);
        std::printf("  watertight     : %s%s\n", watertight ? "YES" : "NO",
                    useWeld ? " (after weld)" : "");
    } else {
        std::printf("  edges          : (skipped, too many triangles)\n");
    }
    return 0;
}

int handleInfoMeshStorageBudget(int& i, int argc, char** argv) {
    // Estimated bytes-per-category breakdown for a WOM.
    // Numbers are based on the in-memory struct sizes, not
    // the actual on-disk encoding (which has framing
    // overhead) - but the relative shares are accurate and
    // help users decide where shrinking efforts pay off.
    //
    // For example: a heightmap mesh's bytes are dominated by
    // vertices, so reducing vertex count is the lever to
    // pull. A skeletal mesh's animation keyframes can dwarf
    // the geometry itself - surfacing that lets the user
    // know to consider --strip-mesh --anims.
    std::string base = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom") {
        base = base.substr(0, base.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr,
            "info-mesh-storage-budget: %s.wom does not exist\n",
            base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "info-mesh-storage-budget: failed to load %s.wom\n",
            base.c_str());
        return 1;
    }
    // Per-category byte estimates. Vertex is 12+12+8+4+4=40
    // bytes (pos/normal/uv/4 weights/4 indices). Index is
    // 4 bytes. Bone is 4+2+12+4=22 bytes. Batch is 4+4+4+2+
    // 2=16. Animation keyframe is 4+12+16+12=44 bytes.
    // Texture path is summed length plus a small per-string
    // overhead.
    uint64_t vertBytes = wom.vertices.size() * 40;
    uint64_t idxBytes = wom.indices.size() * 4;
    uint64_t boneBytes = wom.bones.size() * 22;
    uint64_t batchBytes = wom.batches.size() * 16;
    uint64_t animBytes = 0;
    size_t totalKeyframes = 0;
    for (const auto& a : wom.animations) {
        animBytes += 12;  // id + duration + movingSpeed
        for (const auto& bone : a.boneKeyframes) {
            animBytes += bone.size() * 44;
            totalKeyframes += bone.size();
        }
    }
    uint64_t texBytes = 0;
    for (const auto& t : wom.texturePaths) texBytes += t.size() + 8;
    namespace fs = std::filesystem;
    uint64_t actualBytes = fs::file_size(base + ".wom");
    uint64_t estBytes = vertBytes + idxBytes + boneBytes +
                         batchBytes + animBytes + texBytes;
    struct Row { const char* name; uint64_t bytes; };
    std::vector<Row> rows = {
        {"vertices  ", vertBytes},
        {"indices   ", idxBytes},
        {"bones     ", boneBytes},
        {"animations", animBytes},
        {"batches   ", batchBytes},
        {"textures  ", texBytes},
    };
    if (jsonOut) {
        nlohmann::json j;
        j["base"] = base;
        j["fileBytes"] = actualBytes;
        j["estimatedBytes"] = estBytes;
        j["categories"] = nlohmann::json::object();
        for (const auto& r : rows) {
            double share = estBytes > 0
                           ? 100.0 * r.bytes / estBytes : 0.0;
            j["categories"][r.name] = {{"bytes", r.bytes},
                                        {"share", share}};
        }
        j["counts"] = {{"vertices", wom.vertices.size()},
                        {"indices", wom.indices.size()},
                        {"bones", wom.bones.size()},
                        {"animations", wom.animations.size()},
                        {"keyframes", totalKeyframes},
                        {"batches", wom.batches.size()},
                        {"textures", wom.texturePaths.size()}};
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Mesh storage budget: %s.wom\n", base.c_str());
    std::printf("  on-disk    : %llu bytes (%.1f KB)\n",
                static_cast<unsigned long long>(actualBytes),
                actualBytes / 1024.0);
    std::printf("  estimated  : %llu bytes (sum of in-memory parts)\n",
                static_cast<unsigned long long>(estBytes));
    std::printf("\n  Per-category (estimated):\n");
    for (const auto& r : rows) {
        if (r.bytes == 0) continue;
        double share = estBytes > 0
                       ? 100.0 * r.bytes / estBytes : 0.0;
        std::printf("    %s : %10llu bytes  (%5.1f%%)\n",
                    r.name,
                    static_cast<unsigned long long>(r.bytes),
                    share);
    }
    std::printf("\n  Tips:\n");
    if (animBytes > vertBytes && wom.animations.size() > 0) {
        std::printf("    - animations dominate; --strip-mesh "
                    "--anims would save %.1f KB\n",
                    animBytes / 1024.0);
    }
    if (boneBytes > vertBytes / 2 && wom.bones.size() > 0) {
        std::printf("    - bones non-trivial; consider "
                    "--strip-mesh --bones for static placement\n");
    }
    if (vertBytes > estBytes / 2) {
        std::printf("    - vertices dominate; check if a "
                    "lower-poly variant works for placement\n");
    }
    return 0;
}

int handleInfoProjectModelsTotal(int& i, int argc, char** argv) {
    // Multi-zone aggregate. Walks every zone in <projectDir>,
    // sums the same WOM/WOB metrics --info-zone-models-total
    // emits, and prints a per-zone breakdown table followed
    // by project-wide totals. Useful for capacity planning
    // across an entire content project.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-models-total: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    struct ZRow {
        std::string name;
        int womCount = 0, wobCount = 0;
        uint64_t womVerts = 0, womIndices = 0, womBones = 0;
        uint64_t womAnims = 0, womBatches = 0;
        uint64_t wobGroups = 0, wobVerts = 0, wobIndices = 0;
        uint64_t wobDoodads = 0, wobPortals = 0;
    };
    std::vector<ZRow> rows;
    ZRow tot;
    tot.name = "TOTAL";
    for (const auto& zoneDir : zones) {
        ZRow r;
        r.name = fs::path(zoneDir).filename().string();
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::string base = e.path().string();
            if (base.size() > ext.size())
                base = base.substr(0, base.size() - ext.size());
            if (ext == ".wom") {
                r.womCount++;
                auto wom = wowee::pipeline::WoweeModelLoader::load(base);
                r.womVerts += wom.vertices.size();
                r.womIndices += wom.indices.size();
                r.womBones += wom.bones.size();
                r.womAnims += wom.animations.size();
                r.womBatches += wom.batches.size();
            } else if (ext == ".wob") {
                r.wobCount++;
                auto wob = wowee::pipeline::WoweeBuildingLoader::load(base);
                r.wobGroups += wob.groups.size();
                for (const auto& g : wob.groups) {
                    r.wobVerts += g.vertices.size();
                    r.wobIndices += g.indices.size();
                }
                r.wobDoodads += wob.doodads.size();
                r.wobPortals += wob.portals.size();
            }
        }
        tot.womCount += r.womCount;
        tot.wobCount += r.wobCount;
        tot.womVerts += r.womVerts;
        tot.womIndices += r.womIndices;
        tot.womBones += r.womBones;
        tot.womAnims += r.womAnims;
        tot.womBatches += r.womBatches;
        tot.wobGroups += r.wobGroups;
        tot.wobVerts += r.wobVerts;
        tot.wobIndices += r.wobIndices;
        tot.wobDoodads += r.wobDoodads;
        tot.wobPortals += r.wobPortals;
        rows.push_back(r);
    }
    if (jsonOut) {
        nlohmann::json j;
        j["project"] = projectDir;
        j["zones"] = nlohmann::json::array();
        auto rowJson = [](const ZRow& r) {
            nlohmann::json z;
            z["name"] = r.name;
            z["wom"] = {{"count", r.womCount},
                         {"vertices", r.womVerts},
                         {"indices", r.womIndices},
                         {"triangles", r.womIndices / 3},
                         {"bones", r.womBones},
                         {"animations", r.womAnims},
                         {"batches", r.womBatches}};
            z["wob"] = {{"count", r.wobCount},
                         {"groups", r.wobGroups},
                         {"vertices", r.wobVerts},
                         {"indices", r.wobIndices},
                         {"triangles", r.wobIndices / 3},
                         {"doodads", r.wobDoodads},
                         {"portals", r.wobPortals}};
            return z;
        };
        for (const auto& r : rows) j["zones"].push_back(rowJson(r));
        j["total"] = rowJson(tot);
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Project models total: %s\n", projectDir.c_str());
    std::printf("  zones : %zu\n\n", zones.size());
    std::printf("  zone                  WOMs  WOMtri  bones  WOBs  WOBtri  doodads\n");
    for (const auto& r : rows) {
        std::printf("  %-20s %5d %7llu %6llu %5d %7llu %8llu\n",
                    r.name.substr(0, 20).c_str(),
                    r.womCount,
                    static_cast<unsigned long long>(r.womIndices / 3),
                    static_cast<unsigned long long>(r.womBones),
                    r.wobCount,
                    static_cast<unsigned long long>(r.wobIndices / 3),
                    static_cast<unsigned long long>(r.wobDoodads));
    }
    std::printf("  %-20s %5d %7llu %6llu %5d %7llu %8llu\n",
                tot.name.c_str(),
                tot.womCount,
                static_cast<unsigned long long>(tot.womIndices / 3),
                static_cast<unsigned long long>(tot.womBones),
                tot.wobCount,
                static_cast<unsigned long long>(tot.wobIndices / 3),
                static_cast<unsigned long long>(tot.wobDoodads));
    std::printf("\n  Combined verts/tris (WOM+WOB): %llu / %llu\n",
                static_cast<unsigned long long>(tot.womVerts + tot.wobVerts),
                static_cast<unsigned long long>((tot.womIndices + tot.wobIndices) / 3));
    return 0;
}


}  // namespace

bool handleMeshInfo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone-models-total") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneModelsTotal(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-zone-meshes-detail") == 0 && i + 1 < argc) {
        outRc = handleListZoneMeshesDetail(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-mesh") == 0 && i + 1 < argc) {
        outRc = handleInfoMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-mesh-storage-budget") == 0 && i + 1 < argc) {
        outRc = handleInfoMeshStorageBudget(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-mesh-stats") == 0 && i + 1 < argc) {
        outRc = handleInfoMeshStats(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-models-total") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectModelsTotal(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
