#include "cli_mesh_edit.hpp"
#include "cli_catalog_paths.hpp"

#include "pipeline/wowee_model.hpp"

#include "wom_model_bounds.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleAddTextureToMesh(int& i, int argc, char** argv) {
    // Manual companion to --gen-mesh-textured. Binds an
    // existing PNG to a WOM by appending it to texturePaths
    // (or reusing the slot if already present) and pointing
    // the chosen batch at it.
    //
    // The PNG path stored in the WOM is just the leaf - the
    // runtime resolves textures relative to the model's own
    // directory, so the user is responsible for placing the
    // PNG next to the WOM.
    std::string womBase = argv[++i];
    std::string pngPath = argv[++i];
    int batchIdx = 0;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { batchIdx = std::stoi(argv[++i]); }
        catch (...) {
            std::fprintf(stderr,
                "add-texture-to-mesh: batchIdx must be an integer\n");
            return 1;
        }
    }
    // Strip .wom if user passed a full filename.
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    namespace fs = std::filesystem;
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "add-texture-to-mesh: %s.wom does not exist\n",
            womBase.c_str());
        return 1;
    }
    if (!fs::exists(pngPath)) {
        std::fprintf(stderr,
            "add-texture-to-mesh: png '%s' does not exist\n",
            pngPath.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "add-texture-to-mesh: failed to load %s.wom\n",
            womBase.c_str());
        return 1;
    }
    if (wom.batches.empty()) {
        std::fprintf(stderr,
            "add-texture-to-mesh: %s.wom has no batches "
            "(run --migrate-wom to upgrade WOM1/WOM2 first)\n",
            womBase.c_str());
        return 1;
    }
    if (batchIdx < 0 ||
        static_cast<size_t>(batchIdx) >= wom.batches.size()) {
        std::fprintf(stderr,
            "add-texture-to-mesh: batchIdx %d out of range "
            "(have %zu batches)\n",
            batchIdx, wom.batches.size());
        return 1;
    }
    std::string pngLeaf = fs::path(pngPath).filename().string();
    // Reuse texture slot if the leaf is already in the table;
    // otherwise append a new slot at the end.
    uint32_t texIdx = static_cast<uint32_t>(wom.texturePaths.size());
    for (size_t k = 0; k < wom.texturePaths.size(); ++k) {
        if (wom.texturePaths[k] == pngLeaf) {
            texIdx = static_cast<uint32_t>(k);
            break;
        }
    }
    if (texIdx == wom.texturePaths.size()) {
        wom.texturePaths.push_back(pngLeaf);
    }
    wom.batches[batchIdx].textureIndex = texIdx;
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "add-texture-to-mesh: failed to re-save %s.wom\n",
            womBase.c_str());
        return 1;
    }
    std::printf("Bound %s -> %s.wom batch %d (texture slot %u)\n",
                pngLeaf.c_str(), womBase.c_str(),
                batchIdx, texIdx);
    std::printf("  total texture slots : %zu\n", wom.texturePaths.size());
    // Warn if the PNG isn't sitting next to the WOM - the
    // runtime resolves leaf paths relative to the WOM dir.
    std::string womDir = fs::path(womBase).parent_path().string();
    if (womDir.empty()) womDir = ".";
    std::string expected = womDir + "/" + pngLeaf;
    if (!fs::exists(expected)) {
        std::printf("  NOTE: %s does not exist next to the WOM\n",
                    expected.c_str());
        std::printf("        copy or move %s -> %s before shipping\n",
                    pngPath.c_str(), expected.c_str());
    }
    return 0;
}

int handleScaleMesh(int& i, int argc, char** argv) {
    // Uniformly scale a WOM in place. Multiplies every
    // vertex position, every bone pivot, and the bounds by
    // <factor>. Normals are unchanged (uniform scale
    // preserves direction). Useful for "I imported this OBJ
    // but it's the wrong size" cleanup.
    std::string womBase = argv[++i];
    float factor = 1.0f;
    try { factor = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "scale-mesh: <factor> must be a number\n");
        return 1;
    }
    if (factor <= 0.0f || !std::isfinite(factor)) {
        std::fprintf(stderr,
            "scale-mesh: factor must be positive and finite (got %g)\n",
            factor);
        return 1;
    }
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "scale-mesh: %s.wom does not exist\n", womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "scale-mesh: failed to load %s.wom\n", womBase.c_str());
        return 1;
    }
    for (auto& v : wom.vertices) v.position *= factor;
    for (auto& b : wom.bones) b.pivot *= factor;
    // Animation translations also scale; rotation/scale
    // tracks are dimensionless.
    for (auto& a : wom.animations) {
        for (auto& bone : a.boneKeyframes) {
            for (auto& kf : bone) kf.translation *= factor;
        }
    }
    wom.boundMin *= factor;
    wom.boundMax *= factor;
    wom.boundRadius *= factor;
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "scale-mesh: failed to save %s.wom\n", womBase.c_str());
        return 1;
    }
    std::printf("Scaled %s.wom by %g\n", womBase.c_str(), factor);
    std::printf("  new bounds : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    std::printf("  new radius : %.3f\n", wom.boundRadius);
    return 0;
}

int handleTranslateMesh(int& i, int argc, char** argv) {
    // Offset every vertex (and bones / anim translations /
    // bounds) by (dx, dy, dz). Useful for re-centering a
    // mesh whose origin was wrong on import, or for shifting
    // a procedural primitive that isn't centered the way
    // you want.
    std::string womBase = argv[++i];
    float dx = 0, dy = 0, dz = 0;
    try {
        dx = std::stof(argv[++i]);
        dy = std::stof(argv[++i]);
        dz = std::stof(argv[++i]);
    } catch (...) {
        std::fprintf(stderr,
            "translate-mesh: dx/dy/dz must be numbers\n");
        return 1;
    }
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) {
        std::fprintf(stderr,
            "translate-mesh: offsets must be finite\n");
        return 1;
    }
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "translate-mesh: %s.wom does not exist\n", womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "translate-mesh: failed to load %s.wom\n", womBase.c_str());
        return 1;
    }
    glm::vec3 d(dx, dy, dz);
    for (auto& v : wom.vertices) v.position += d;
    for (auto& b : wom.bones) b.pivot += d;
    // Bone-relative animation translations don't shift with
    // the model - only the bone pivots do, since translations
    // are in bone-local space. Leave anim keyframes alone.
    wom.boundMin += d;
    wom.boundMax += d;
    // Radius is unchanged (translation is rigid, doesn't
    // change extent).
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "translate-mesh: failed to save %s.wom\n", womBase.c_str());
        return 1;
    }
    std::printf("Translated %s.wom by (%g, %g, %g)\n",
                womBase.c_str(), dx, dy, dz);
    std::printf("  new bounds : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    return 0;
}

int handleStripMesh(int& i, int argc, char** argv) {
    // Drop bones and/or animations from a WOM in place. Use
    // case: a model imported with full skeleton + anims that
    // will only ever be placed as static decoration - there's
    // no point shipping the bone data, and stripping it can
    // shrink the file substantially.
    //
    // Default (no flags) is a no-op so the user explicitly
    // opts in to destruction. --bones drops bones (and
    // therefore animations, since they reference bones).
    // --anims drops only animations. --all is shorthand for
    // both.
    std::string womBase = argv[++i];
    bool dropBones = false, dropAnims = false;
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        std::string flag = argv[++i];
        if (flag == "--bones") { dropBones = true; }
        else if (flag == "--anims") { dropAnims = true; }
        else if (flag == "--all") { dropBones = true; dropAnims = true; }
        else {
            std::fprintf(stderr,
                "strip-mesh: unknown flag '%s'\n", flag.c_str());
            return 1;
        }
    }
    if (!dropBones && !dropAnims) {
        std::fprintf(stderr,
            "strip-mesh: no --bones / --anims / --all specified - nothing to do\n");
        return 1;
    }
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    namespace fs = std::filesystem;
    std::string fullPath = womBase + ".wom";
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "strip-mesh: %s.wom does not exist\n", womBase.c_str());
        return 1;
    }
    uint64_t bytesBefore = fs::file_size(fullPath);
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "strip-mesh: failed to load %s.wom\n", womBase.c_str());
        return 1;
    }
    size_t bonesBefore = wom.bones.size();
    size_t animsBefore = wom.animations.size();
    if (dropBones) {
        wom.bones.clear();
        // Bones implies anims (anims reference bones).
        wom.animations.clear();
        // Reset per-vertex skinning to identity-on-bone-0 so
        // a renderer that expects the field doesn't read
        // stale indices.
        for (auto& v : wom.vertices) {
            v.boneWeights[0] = 255;
            v.boneWeights[1] = 0;
            v.boneWeights[2] = 0;
            v.boneWeights[3] = 0;
            v.boneIndices[0] = 0;
            v.boneIndices[1] = 0;
            v.boneIndices[2] = 0;
            v.boneIndices[3] = 0;
        }
    } else if (dropAnims) {
        wom.animations.clear();
    }
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "strip-mesh: failed to save %s.wom\n", womBase.c_str());
        return 1;
    }
    uint64_t bytesAfter = fs::file_size(fullPath);
    std::printf("Stripped %s.wom\n", womBase.c_str());
    std::printf("  bones      : %zu -> %zu\n", bonesBefore, wom.bones.size());
    std::printf("  animations : %zu -> %zu\n", animsBefore, wom.animations.size());
    std::printf("  bytes      : %llu -> %llu (%+lld)\n",
                static_cast<unsigned long long>(bytesBefore),
                static_cast<unsigned long long>(bytesAfter),
                static_cast<long long>(bytesAfter) -
                  static_cast<long long>(bytesBefore));
    return 0;
}

int handleRotateMesh(int& i, int argc, char** argv) {
    // Rotate every vertex position and normal around the
    // chosen axis (x, y, or z) by <degrees>. Bone pivots
    // also rotate so the skeleton stays in sync. Bounds are
    // recomputed from rotated positions (axis-aligned bbox
    // grows during rotation).
    std::string womBase = argv[++i];
    std::string axisStr = argv[++i];
    float degrees = 0.0f;
    try { degrees = std::stof(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "rotate-mesh: <degrees> must be a number\n");
        return 1;
    }
    if (!std::isfinite(degrees)) {
        std::fprintf(stderr,
            "rotate-mesh: degrees must be finite\n");
        return 1;
    }
    std::transform(axisStr.begin(), axisStr.end(), axisStr.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    int axis = -1;
    if (axisStr == "x") axis = 0;
    else if (axisStr == "y") axis = 1;
    else if (axisStr == "z") axis = 2;
    else {
        std::fprintf(stderr,
            "rotate-mesh: axis must be x, y, or z (got '%s')\n",
            axisStr.c_str());
        return 1;
    }
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "rotate-mesh: %s.wom does not exist\n", womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "rotate-mesh: failed to load %s.wom\n", womBase.c_str());
        return 1;
    }
    float rad = degrees * 3.14159265358979f / 180.0f;
    float cs = std::cos(rad), sn = std::sin(rad);
    // Rotation around each axis: standard right-hand rule.
    auto rot = [axis, cs, sn](glm::vec3 v) -> glm::vec3 {
        if (axis == 0) {
            return glm::vec3(v.x,
                              cs * v.y - sn * v.z,
                              sn * v.y + cs * v.z);
        }
        if (axis == 1) {
            return glm::vec3( cs * v.x + sn * v.z,
                              v.y,
                             -sn * v.x + cs * v.z);
        }
        return glm::vec3(cs * v.x - sn * v.y,
                          sn * v.x + cs * v.y,
                          v.z);
    };
    for (auto& v : wom.vertices) {
        v.position = rot(v.position);
        v.normal = rot(v.normal);
    }
    for (auto& b : wom.bones) {
        b.pivot = rot(b.pivot);
    }
    // Recompute bounds from rotated vertices (axis-aligned
    // bbox can only grow under rotation, so reuse the loop).
    setModelBounds(wom);
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "rotate-mesh: failed to save %s.wom\n", womBase.c_str());
        return 1;
    }
    std::printf("Rotated %s.wom by %g° around %s\n",
                womBase.c_str(), degrees, axisStr.c_str());
    std::printf("  new bounds : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    return 0;
}

int handleCenterMesh(int& i, int argc, char** argv) {
    // Translate the mesh so the bounds center lands at the
    // origin. Convenience for "this mesh's pivot is in some
    // weird corner - make it center-pivoted." Doesn't change
    // shape, just shifts.
    std::string womBase = argv[++i];
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "center-mesh: %s.wom does not exist\n", womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "center-mesh: failed to load %s.wom\n", womBase.c_str());
        return 1;
    }
    glm::vec3 center = (wom.boundMin + wom.boundMax) * 0.5f;
    for (auto& v : wom.vertices) v.position -= center;
    for (auto& b : wom.bones) b.pivot -= center;
    wom.boundMin -= center;
    wom.boundMax -= center;
    // Radius is preserved (pure translation).
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "center-mesh: failed to save %s.wom\n", womBase.c_str());
        return 1;
    }
    std::printf("Centered %s.wom (shifted by %g, %g, %g)\n",
                womBase.c_str(), -center.x, -center.y, -center.z);
    std::printf("  new bounds : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    return 0;
}

int handleFlipMeshNormals(int& i, int argc, char** argv) {
    // Invert every vertex normal. Use case: an OBJ imported
    // with flipped winding renders inside-out - flipping the
    // normals makes shading correct without re-winding the
    // index buffer (which would also need batch-aware care).
    // Also useful for skybox-like meshes where the "outside"
    // texture should appear when looking from inside.
    std::string womBase = argv[++i];
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "flip-mesh-normals: %s.wom does not exist\n",
            womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "flip-mesh-normals: failed to load %s.wom\n",
            womBase.c_str());
        return 1;
    }
    for (auto& v : wom.vertices) v.normal = -v.normal;
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "flip-mesh-normals: failed to save %s.wom\n",
            womBase.c_str());
        return 1;
    }
    std::printf("Flipped normals on %s.wom (%zu vertices)\n",
                womBase.c_str(), wom.vertices.size());
    return 0;
}

int handleMirrorMesh(int& i, int argc, char** argv) {
    // Mirror every vertex + normal across the chosen axis.
    // Negating just one position component reverses face
    // winding (the triangle's signed area flips), so we
    // also swap the second and third index of every triangle
    // to keep front-faces facing forward and lighting
    // correct. Bone pivots mirror too.
    //
    // Useful for "I have a left arm, mirror it for the right
    // arm" content reuse. The output is byte-stable
    // independent of execution order.
    std::string womBase = argv[++i];
    std::string axisStr = argv[++i];
    std::transform(axisStr.begin(), axisStr.end(), axisStr.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    int axis = -1;
    if (axisStr == "x") axis = 0;
    else if (axisStr == "y") axis = 1;
    else if (axisStr == "z") axis = 2;
    else {
        std::fprintf(stderr,
            "mirror-mesh: axis must be x, y, or z (got '%s')\n",
            axisStr.c_str());
        return 1;
    }
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "mirror-mesh: %s.wom does not exist\n", womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "mirror-mesh: failed to load %s.wom\n", womBase.c_str());
        return 1;
    }
    for (auto& v : wom.vertices) {
        v.position[axis] = -v.position[axis];
        v.normal[axis] = -v.normal[axis];
    }
    for (auto& b : wom.bones) {
        b.pivot[axis] = -b.pivot[axis];
    }
    // Flip winding: swap idx[1] and idx[2] of every triangle.
    // Indices are stored as a flat list of triangle triples.
    for (size_t k = 0; k + 2 < wom.indices.size(); k += 3) {
        std::swap(wom.indices[k + 1], wom.indices[k + 2]);
    }
    // Bounds: the mirrored extent on this axis is just the
    // negation of the previous extent - recompute from
    // vertices to be safe.
    setModelBounds(wom);
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "mirror-mesh: failed to save %s.wom\n", womBase.c_str());
        return 1;
    }
    std::printf("Mirrored %s.wom across %s axis\n",
                womBase.c_str(), axisStr.c_str());
    std::printf("  vertices touched : %zu\n", wom.vertices.size());
    std::printf("  triangles flipped: %zu\n", wom.indices.size() / 3);
    std::printf("  new bounds       : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    return 0;
}

int handleSmoothMeshNormals(int& i, int argc, char** argv) {
    // Recompute per-vertex normals as the area-weighted
    // average of incident face normals. Useful when:
    //   - Imported geometry has no normals (--import-obj
    //     leaves them zero or face-flat).
    //   - Custom transforms have desynced normals from the
    //     positions (e.g., user post-processed the WOM
    //     externally).
    //   - Faceted-by-construction meshes (cube, stairs) need
    //     a smooth re-shade for stylistic reasons.
    //
    // The cross-product magnitude is twice the triangle area,
    // which weights large faces more - bigger triangles
    // contribute more to the local surface direction.
    std::string womBase = argv[++i];
    if (womBase.size() >= 4 &&
        womBase.substr(womBase.size() - 4) == ".wom") {
        womBase = womBase.substr(0, womBase.size() - 4);
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(womBase)) {
        std::fprintf(stderr,
            "smooth-mesh-normals: %s.wom does not exist\n",
            womBase.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "smooth-mesh-normals: failed to load %s.wom\n",
            womBase.c_str());
        return 1;
    }
    // Reset vertex normals to zero so the accumulator sums
    // cleanly.
    for (auto& v : wom.vertices) v.normal = glm::vec3(0);
    for (size_t k = 0; k + 2 < wom.indices.size(); k += 3) {
        uint32_t i0 = wom.indices[k];
        uint32_t i1 = wom.indices[k + 1];
        uint32_t i2 = wom.indices[k + 2];
        if (i0 >= wom.vertices.size() ||
            i1 >= wom.vertices.size() ||
            i2 >= wom.vertices.size()) continue;
        glm::vec3 p0 = wom.vertices[i0].position;
        glm::vec3 p1 = wom.vertices[i1].position;
        glm::vec3 p2 = wom.vertices[i2].position;
        // Cross product magnitude == 2 * triangle area, used
        // as the weight.
        glm::vec3 faceN = glm::cross(p1 - p0, p2 - p0);
        wom.vertices[i0].normal += faceN;
        wom.vertices[i1].normal += faceN;
        wom.vertices[i2].normal += faceN;
    }
    int normalized = 0, degenerate = 0;
    for (auto& v : wom.vertices) {
        float len = glm::length(v.normal);
        if (len > 1e-6f) {
            v.normal /= len;
            normalized++;
        } else {
            // Vertex unreferenced or sum cancelled - fall
            // back to "up" rather than leaving zero so the
            // shader doesn't get a dark NaN spot.
            v.normal = glm::vec3(0, 1, 0);
            degenerate++;
        }
    }
    if (!wowee::pipeline::WoweeModelLoader::save(wom, womBase)) {
        std::fprintf(stderr,
            "smooth-mesh-normals: failed to save %s.wom\n",
            womBase.c_str());
        return 1;
    }
    std::printf("Smoothed normals on %s.wom\n", womBase.c_str());
    std::printf("  vertices touched : %zu\n", wom.vertices.size());
    std::printf("  triangles read   : %zu\n", wom.indices.size() / 3);
    std::printf("  normalized       : %d\n", normalized);
    if (degenerate > 0) {
        std::printf("  degenerate       : %d (set to (0,1,0))\n",
                    degenerate);
    }
    return 0;
}

int handleMergeMeshes(int& i, int argc, char** argv) {
    // Combine two WOMs into one. The second mesh's indices
    // are offset by the first mesh's vertex count, and its
    // batches are appended with their indexStart shifted by
    // the first mesh's index count and their textureIndex
    // shifted by the first mesh's texture-slot count.
    //
    // Bones/animations are NOT merged - that requires
    // skeleton retargeting which is beyond a simple
    // concatenation. If either input has bones, the merged
    // output is treated as static (bones cleared, weights
    // reset to identity-on-bone-0) so renderers don't read
    // mismatched indices.
    std::string aBase = argv[++i];
    std::string bBase = argv[++i];
    std::string outBase = argv[++i];
    auto stripExt = [](std::string p) {
        if (p.size() >= 4 && p.substr(p.size() - 4) == ".wom") {
            return p.substr(0, p.size() - 4);
        }
        return p;
    };
    aBase = stripExt(aBase);
    bBase = stripExt(bBase);
    outBase = stripExt(outBase);
    if (!wowee::pipeline::WoweeModelLoader::exists(aBase)) {
        std::fprintf(stderr,
            "merge-meshes: %s.wom does not exist\n", aBase.c_str());
        return 1;
    }
    if (!wowee::pipeline::WoweeModelLoader::exists(bBase)) {
        std::fprintf(stderr,
            "merge-meshes: %s.wom does not exist\n", bBase.c_str());
        return 1;
    }
    auto a = wowee::pipeline::WoweeModelLoader::load(aBase);
    auto b = wowee::pipeline::WoweeModelLoader::load(bBase);
    if (!a.isValid() || !b.isValid()) {
        std::fprintf(stderr,
            "merge-meshes: failed to load one of the inputs\n");
        return 1;
    }
    wowee::pipeline::WoweeModel out;
    out.name = std::filesystem::path(outBase).stem().string();
    out.version = 3;
    out.vertices = a.vertices;
    out.vertices.insert(out.vertices.end(),
                         b.vertices.begin(), b.vertices.end());
    out.indices = a.indices;
    uint32_t indexOffset = static_cast<uint32_t>(a.vertices.size());
    for (uint32_t idx : b.indices) {
        out.indices.push_back(idx + indexOffset);
    }
    out.texturePaths = a.texturePaths;
    uint32_t textureOffset = static_cast<uint32_t>(a.texturePaths.size());
    for (const auto& t : b.texturePaths) {
        out.texturePaths.push_back(t);
    }
    // Promote single-batch / no-batch inputs into proper
    // batches so the merged output is well-formed v3.
    auto ensureBatch = [](const wowee::pipeline::WoweeModel& m) {
        std::vector<wowee::pipeline::WoweeModel::Batch> bs = m.batches;
        if (bs.empty()) {
            wowee::pipeline::WoweeModel::Batch only;
            only.indexStart = 0;
            only.indexCount = static_cast<uint32_t>(m.indices.size());
            only.textureIndex = 0;
            only.blendMode = 0;
            only.flags = 0;
            bs.push_back(only);
        }
        return bs;
    };
    auto aBatches = ensureBatch(a);
    auto bBatches = ensureBatch(b);
    for (const auto& bt : aBatches) out.batches.push_back(bt);
    uint32_t indexStartOffset = static_cast<uint32_t>(a.indices.size());
    for (auto bt : bBatches) {
        bt.indexStart += indexStartOffset;
        bt.textureIndex += textureOffset;
        out.batches.push_back(bt);
    }
    // Static-only output (see header comment).
    for (auto& v : out.vertices) {
        v.boneWeights[0] = 255;
        v.boneWeights[1] = 0;
        v.boneWeights[2] = 0;
        v.boneWeights[3] = 0;
        v.boneIndices[0] = 0;
        v.boneIndices[1] = 0;
        v.boneIndices[2] = 0;
        v.boneIndices[3] = 0;
    }
    // Bounds: union of inputs.
    out.boundMin = glm::min(a.boundMin, b.boundMin);
    out.boundMax = glm::max(a.boundMax, b.boundMax);
    setModelBounds(out);
    std::filesystem::path outPath(outBase);
    ensureParentDirectory(outPath);
    if (!wowee::pipeline::WoweeModelLoader::save(out, outBase)) {
        std::fprintf(stderr,
            "merge-meshes: failed to save %s.wom\n", outBase.c_str());
        return 1;
    }
    std::printf("Merged %s.wom + %s.wom -> %s.wom\n",
                aBase.c_str(), bBase.c_str(), outBase.c_str());
    std::printf("  vertices : %zu = %zu + %zu\n",
                out.vertices.size(),
                a.vertices.size(), b.vertices.size());
    std::printf("  indices  : %zu = %zu + %zu\n",
                out.indices.size(),
                a.indices.size(), b.indices.size());
    std::printf("  batches  : %zu = %zu + %zu\n",
                out.batches.size(),
                aBatches.size(), bBatches.size());
    std::printf("  textures : %zu = %zu + %zu\n",
                out.texturePaths.size(),
                a.texturePaths.size(), b.texturePaths.size());
    std::printf("  bounds   : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                out.boundMin.x, out.boundMin.y, out.boundMin.z,
                out.boundMax.x, out.boundMax.y, out.boundMax.z);
    return 0;
}


}  // namespace

bool handleMeshEdit(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--add-texture-to-mesh") == 0 && i + 2 < argc) {
        outRc = handleAddTextureToMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--scale-mesh") == 0 && i + 2 < argc) {
        outRc = handleScaleMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--translate-mesh") == 0 && i + 4 < argc) {
        outRc = handleTranslateMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--strip-mesh") == 0 && i + 1 < argc) {
        outRc = handleStripMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--rotate-mesh") == 0 && i + 2 < argc) {
        outRc = handleRotateMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--center-mesh") == 0 && i + 1 < argc) {
        outRc = handleCenterMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--flip-mesh-normals") == 0 && i + 1 < argc) {
        outRc = handleFlipMeshNormals(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--mirror-mesh") == 0 && i + 2 < argc) {
        outRc = handleMirrorMesh(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--smooth-mesh-normals") == 0 && i + 1 < argc) {
        outRc = handleSmoothMeshNormals(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--merge-meshes") == 0 && i + 3 < argc) {
        outRc = handleMergeMeshes(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
