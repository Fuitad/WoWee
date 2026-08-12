#include "cli_mesh_io.hpp"
#include "cli_catalog_paths.hpp"

#include "pipeline/wowee_model.hpp"
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// stb_image impl lives in stb_image_impl.cpp (separate TU);
// stb_image_write impl lives in texture_exporter.cpp.
// We just need the function decls here.
#include "stb_image.h"
#include "stb_image_write.h"

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleDisplaceMesh(int& i, int argc, char** argv) {
    // Displaces each vertex along its current normal by the
    // heightmap brightness × scale. UVs determine where each
    // vertex samples the heightmap.
    //
    // Pairs naturally with --gen-mesh-grid: gen a flat grid,
    // then --displace-mesh with a noise PNG to create
    // procedural terrain. Or use it on a sphere to make a
    // bumpy planet.
    std::string womBase = argv[++i];
    std::string pngPath = argv[++i];
    float scale = 1.0f;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { scale = std::stof(argv[++i]); } catch (...) {}
    }
    if (!std::isfinite(scale)) scale = 1.0f;
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    namespace fs = std::filesystem;
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "displace-mesh: %s.wom does not exist\n", womBase.c_str());
        return 1;
    }
    int W, H, comp;
    uint8_t* data = stbi_load(pngPath.c_str(), &W, &H, &comp, 1);
    if (!data) {
        std::fprintf(stderr,
            "displace-mesh: cannot read %s (%s)\n",
            pngPath.c_str(), stbi_failure_reason());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "displace-mesh: failed to load %s.wom\n", womBase.c_str());
        stbi_image_free(data);
        return 1;
    }
    float minDelta = 1e30f, maxDelta = -1e30f;
    for (auto& v : wom.vertices) {
        // Sample the heightmap with bilinear filtering at
        // (u, v). Wrap repeating UVs.
        float u = v.texCoord.x - std::floor(v.texCoord.x);
        float vv = v.texCoord.y - std::floor(v.texCoord.y);
        float fx = u * (W - 1);
        float fy = vv * (H - 1);
        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, W - 1);
        int y1 = std::min(y0 + 1, H - 1);
        float tx = fx - x0;
        float ty = fy - y0;
        auto sample = [&](int x, int y) {
            return data[y * W + x] / 255.0f;
        };
        float a = sample(x0, y0);
        float b = sample(x1, y0);
        float c = sample(x0, y1);
        float d = sample(x1, y1);
        float ab = a + (b - a) * tx;
        float cd = c + (d - c) * tx;
        float h = ab + (cd - ab) * ty;
        float delta = h * scale;
        v.position += v.normal * delta;
        if (delta < minDelta) minDelta = delta;
        if (delta > maxDelta) maxDelta = delta;
    }
    stbi_image_free(data);
    // Recompute bounds; normals stay (they're now stale to
    // the displaced surface but the user can run --smooth-
    // mesh-normals if they want shading to follow the bumps).
    wom.boundMin = glm::vec3(1e30f);
    wom.boundMax = glm::vec3(-1e30f);
    for (const auto& v : wom.vertices) {
        wom.boundMin = glm::min(wom.boundMin, v.position);
        wom.boundMax = glm::max(wom.boundMax, v.position);
    }
    wom.boundRadius = glm::length(wom.boundMax - wom.boundMin) * 0.5f;
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "displace-mesh: failed to save %s.wom\n", womBase.c_str());
        return 1;
    }
    std::printf("Displaced %s.wom with %s\n",
                womBase.c_str(), pngPath.c_str());
    std::printf("  source PNG : %dx%d\n", W, H);
    std::printf("  scale      : %g\n", scale);
    std::printf("  vertices   : %zu touched\n", wom.vertices.size());
    std::printf("  delta      : %.3f to %.3f\n", minDelta, maxDelta);
    std::printf("  new bounds : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    std::printf("  hint       : run --smooth-mesh-normals so shading follows the bumps\n");
    return 0;
}

int handleGenMeshFromHeightmap(int& i, int argc, char** argv) {
    // Convert a grayscale PNG into a heightmap mesh. Each
    // pixel becomes one vertex; brightness becomes Y. The
    // mesh is centered on the XZ plane with X spanning
    // [-W*scaleXZ/2, +W*scaleXZ/2] and Z spanning the same
    // for H. Default scaleXZ=0.1 (so a 64×64 PNG covers a
    // 6.4×6.4 yard patch) and scaleY=2.0 (so full white
    // pixels rise 2 yards above black).
    //
    // Normals are computed from finite differences against
    // the height field - gives smooth shading across the
    // surface. Single batch covers all indices; one empty
    // texture slot for downstream binding via --add-
    // texture-to-mesh.
    std::string womBase = argv[++i];
    std::string pngPath = argv[++i];
    float scaleXZ = 0.1f;
    float scaleY = 2.0f;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { scaleXZ = std::stof(argv[++i]); } catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { scaleY = std::stof(argv[++i]); } catch (...) {}
    }
    if (scaleXZ <= 0 || !std::isfinite(scaleXZ) ||
        !std::isfinite(scaleY)) {
        std::fprintf(stderr,
            "gen-mesh-from-heightmap: scales must be finite, scaleXZ > 0\n");
        return 1;
    }
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    int W, H, comp;
    // Force 1-channel grayscale on read; stb downsamples
    // automatically.
    uint8_t* data = stbi_load(pngPath.c_str(), &W, &H, &comp, 1);
    if (!data) {
        std::fprintf(stderr,
            "gen-mesh-from-heightmap: cannot read %s (%s)\n",
            pngPath.c_str(), stbi_failure_reason());
        return 1;
    }
    if (W < 2 || H < 2) {
        std::fprintf(stderr,
            "gen-mesh-from-heightmap: image must be at least 2x2 (got %dx%d)\n",
            W, H);
        stbi_image_free(data);
        return 1;
    }
    // Capacity guard: a 1024x1024 PNG would be 1M verts /
    // ~6M tris - well past what makes sense for a single
    // WOM placeholder. Cap at 512×512 = 262K verts.
    if (W > 512 || H > 512) {
        std::fprintf(stderr,
            "gen-mesh-from-heightmap: image too large (%dx%d > 512x512)\n",
            W, H);
        stbi_image_free(data);
        return 1;
    }
    wowee::pipeline::WoweeModel wom;
    wom.name = std::filesystem::path(womBase).stem().string();
    wom.version = 3;
    float halfW = W * scaleXZ * 0.5f;
    float halfH = H * scaleXZ * 0.5f;
    auto sample = [&](int x, int y) {
        x = std::clamp(x, 0, W - 1);
        y = std::clamp(y, 0, H - 1);
        return data[y * W + x] / 255.0f * scaleY;
    };
    wom.vertices.reserve(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float h = sample(x, y);
            // Central-difference normal: (-dh/dx, 1, -dh/dz),
            // normalized.
            float dx = (sample(x + 1, y) - sample(x - 1, y)) /
                       (2.0f * scaleXZ);
            float dz = (sample(x, y + 1) - sample(x, y - 1)) /
                       (2.0f * scaleXZ);
            glm::vec3 n(-dx, 1.0f, -dz);
            n = glm::normalize(n);
            wowee::pipeline::WoweeModel::Vertex v;
            v.position = glm::vec3(x * scaleXZ - halfW,
                                    h,
                                    y * scaleXZ - halfH);
            v.normal = n;
            v.texCoord = glm::vec2(static_cast<float>(x) / (W - 1),
                                    static_cast<float>(y) / (H - 1));
            wom.vertices.push_back(v);
        }
    }
    wom.indices.reserve(static_cast<size_t>(W - 1) * (H - 1) * 6);
    for (int y = 0; y < H - 1; ++y) {
        for (int x = 0; x < W - 1; ++x) {
            uint32_t a = y * W + x;
            uint32_t b = a + 1;
            uint32_t c = a + W;
            uint32_t d = c + 1;
            wom.indices.push_back(a);
            wom.indices.push_back(c);
            wom.indices.push_back(b);
            wom.indices.push_back(b);
            wom.indices.push_back(c);
            wom.indices.push_back(d);
        }
    }
    stbi_image_free(data);
    // Bounds from vertex extents.
    wom.boundMin = glm::vec3(1e30f);
    wom.boundMax = glm::vec3(-1e30f);
    for (const auto& v : wom.vertices) {
        wom.boundMin = glm::min(wom.boundMin, v.position);
        wom.boundMax = glm::max(wom.boundMax, v.position);
    }
    wom.boundRadius = glm::length(wom.boundMax - wom.boundMin) * 0.5f;
    wowee::pipeline::WoweeModel::Batch b;
    b.indexStart = 0;
    b.indexCount = static_cast<uint32_t>(wom.indices.size());
    b.textureIndex = 0;
    b.blendMode = 0;
    b.flags = 0;
    wom.batches.push_back(b);
    wom.texturePaths.push_back("");
    std::filesystem::path womPath(womBase);
    ensureParentDirectory(womPath);
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "gen-mesh-from-heightmap: failed to save %s.wom\n",
            womBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wom from %s\n",
                womBase.c_str(), pngPath.c_str());
    std::printf("  source PNG : %dx%d\n", W, H);
    std::printf("  scaleXZ    : %g (mesh span %.2f × %.2f)\n",
                scaleXZ, W * scaleXZ, H * scaleXZ);
    std::printf("  scaleY     : %g (height range %.3f to %.3f)\n",
                scaleY, wom.boundMin.y, wom.boundMax.y);
    std::printf("  vertices   : %zu\n", wom.vertices.size());
    std::printf("  triangles  : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleExportMeshHeightmap(int& i, int argc, char** argv) {
    // Inverse of --gen-mesh-from-heightmap: extract a
    // grayscale PNG from a row-major W×H heightmap mesh.
    // The user supplies W and H since arbitrary meshes
    // aren't necessarily heightmap-shaped - taking the
    // dimensions explicitly avoids guessing wrong on a
    // mesh with vertex count W*H but a different layout.
    //
    // Y values are normalized to 0..255 using the mesh
    // bounds (Y_min → 0, Y_max → 255). Round-trips with
    // --gen-mesh-from-heightmap modulo the 1-byte
    // quantization step.
    std::string womBase = argv[++i];
    std::string outPath = argv[++i];
    int W = 0, H = 0;
    try {
        W = std::stoi(argv[++i]);
        H = std::stoi(argv[++i]);
    } catch (...) {
        std::fprintf(stderr,
            "export-mesh-heightmap: W and H must be integers\n");
        return 1;
    }
    if (W < 2 || H < 2 || W > 8192 || H > 8192) {
        std::fprintf(stderr,
            "export-mesh-heightmap: W and H must be 2..8192\n");
        return 1;
    }
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "export-mesh-heightmap: %s.wom does not exist\n",
            womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "export-mesh-heightmap: failed to load %s.wom\n",
            womBase.c_str());
        return 1;
    }
    size_t expected = static_cast<size_t>(W) * H;
    if (wom.vertices.size() < expected) {
        std::fprintf(stderr,
            "export-mesh-heightmap: %s.wom has %zu vertices, "
            "need at least %zu for %dx%d\n",
            womBase.c_str(), wom.vertices.size(), expected, W, H);
        return 1;
    }
    float yMin = wom.boundMin.y;
    float yMax = wom.boundMax.y;
    float range = yMax - yMin;
    std::vector<uint8_t> pixels(expected * 3, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            size_t idx = static_cast<size_t>(y) * W + x;
            float h = wom.vertices[idx].position.y;
            float t = (range > 1e-6f) ? (h - yMin) / range : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            uint8_t g = static_cast<uint8_t>(t * 255.0f + 0.5f);
            size_t i2 = idx * 3;
            pixels[i2 + 0] = g;
            pixels[i2 + 1] = g;
            pixels[i2 + 2] = g;
        }
    }
    if (!stbi_write_png(outPath.c_str(), W, H, 3,
                        pixels.data(), W * 3)) {
        std::fprintf(stderr,
            "export-mesh-heightmap: stbi_write_png failed for %s\n",
            outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s from %s.wom\n",
                outPath.c_str(), womBase.c_str());
    std::printf("  size       : %dx%d\n", W, H);
    std::printf("  height     : %.3f to %.3f (mapped to 0..255)\n",
                yMin, yMax);
    std::printf("  pixels     : %zu (W*H)\n", expected);
    return 0;
}


}  // namespace

bool handleMeshIO(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--displace-mesh") == 0 && i + 2 < argc) {
        outRc = handleDisplaceMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mesh-from-heightmap") == 0 && i + 2 < argc) {
        outRc = handleGenMeshFromHeightmap(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-mesh-heightmap") == 0 && i + 4 < argc) {
        outRc = handleExportMeshHeightmap(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
