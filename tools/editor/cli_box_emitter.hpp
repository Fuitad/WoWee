#pragma once

#include "pipeline/wowee_model.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

// Initialize a fresh WoweeModel with the canonical procedural-
// primitive defaults: name derived from the base path's stem and
// version 3 (current). 64 handlers in cli_gen_mesh.cpp open-coded
// this 3-line block before extraction.
inline void initWomDefaults(wowee::pipeline::WoweeModel& wom,
                            const std::string& base) {
    wom.name = std::filesystem::path(base).stem().string();
    wom.version = 3;
}

// Set the WoweeModel's bound box for a primitive whose footprint
// is symmetric around the origin in X+Z and rises from y=0 to
// y=maxY. 21+ procedural mesh handlers use this exact pattern;
// extracting collapses two-line stanzas to one call.
inline void setCenteredBoundsXZ(wowee::pipeline::WoweeModel& wom,
                                float halfX, float halfZ, float maxY) {
    wom.boundMin = glm::vec3(-halfX, 0.0f, -halfZ);
    wom.boundMax = glm::vec3( halfX, maxY,  halfZ);
}

// Print the canonical "Wrote <base>.wom" success line shown at
// the start of every --gen-mesh-* handler's stat report. 72 sites
// each ran the same printf - hoisting collapses each to one call.
inline void printWomWrote(const std::string& base) {
    std::printf("Wrote %s.wom\n", base.c_str());
}

// Print the standard final two stat lines shown at the end of
// every --gen-mesh-* handler's report:
//   vertices   : N
//   triangles  : T
// 49+ handlers used this exact pair before extraction.
inline void printWomMeshStats(const wowee::pipeline::WoweeModel& wom) {
    std::printf("  vertices   : %zu\n", wom.vertices.size());
    std::printf("  triangles  : %zu\n", wom.indices.size() / 3);
}

// Save a WoweeModel and report a stderr message on failure.
// Returns true on success so the caller can do
// `if (!saveWomOrError(...)) return 1;`. The cmdName is included
// in the error message for context.
inline bool saveWomOrError(const wowee::pipeline::WoweeModel& wom,
                           const std::string& base,
                           const char* cmdName) {
    if (wowee::pipeline::WoweeModelLoader::save(wom, base)) return true;
    std::fprintf(stderr, "%s: failed to save %s.wom\n",
                 cmdName, base.c_str());
    return false;
}

// Strip a file-extension suffix from a base path if present. Used
// pervasively by --gen-mesh-* / --bake-* / --info-* handlers that
// accept either `path/foo` or `path/foo.ext` as input - the loader
// expects the bare base, so the trailing ".wom" / ".wob" / ".woc"
// must be removed if the user typed it.
//
// Pattern was open-coded as a 4-line if-block in 64+ sites
// across cli_gen_mesh.cpp; hoisted here for one-line callers.
inline void stripExt(std::string& base, const char* ext) {
    std::size_t extLen = 0;
    while (ext[extLen]) ++extLen;
    if (base.size() >= extLen &&
        base.compare(base.size() - extLen, extLen, ext) == 0) {
        base.resize(base.size() - extLen);
    }
}

// Append a single batch covering ALL of wom.indices to wom.batches.
// Called at the end of every gen-mesh primitive (the procedural
// builders emit just one batch per primitive). The same 4-line
// "construct + populate + push" boilerplate was repeated in 53
// handlers before extraction.
inline void finalizeAsSingleBatch(wowee::pipeline::WoweeModel& wom) {
    wowee::pipeline::WoweeModel::Batch batch;
    batch.indexStart = 0;
    batch.indexCount = static_cast<uint32_t>(wom.indices.size());
    batch.textureIndex = 0;
    wom.batches.push_back(batch);
}

// Append one vertex (position, normal, UV) to a WoweeModel and
// return its newly-assigned index. Inline because the procedural
// mesh primitives call this thousands of times per build and the
// abstraction shouldn't cost a function-call frame each time.
// Pre-extraction this was the same 5-line lambda copy-pasted into
// 21 different handlers.
inline uint32_t addVertex(wowee::pipeline::WoweeModel& wom,
                          glm::vec3 p, glm::vec3 n, glm::vec2 uv) {
    wowee::pipeline::WoweeModel::Vertex vtx;
    vtx.position = p;
    vtx.normal = n;
    vtx.texCoord = uv;
    wom.vertices.push_back(vtx);
    return static_cast<uint32_t>(wom.vertices.size() - 1);
}

// Per-float overload used by handlers that compute pos/normal/uv
// components inline rather than building intermediate glm vectors
// (--gen-mesh-stairs, --gen-mesh-tube, --gen-mesh-capsule,
// --gen-mesh-arch). Same semantics as the vec3/vec2 form.
inline uint32_t addVertex(wowee::pipeline::WoweeModel& wom,
                          float px, float py, float pz,
                          float nx, float ny, float nz,
                          float u,  float v) {
    return addVertex(wom, glm::vec3(px, py, pz), glm::vec3(nx, ny, nz),
                      glm::vec2(u, v));
}

// Append a closed Z-axis cylinder (side wall + ±Z end caps) to a
// WoweeModel, centered at (cx, cy) on the XY plane and spanning
// z=z0 to z=z1 with radius R. Used by primitives whose tubes lie
// horizontally rather than vertically - woodpile logs, bedroll,
// archery-target face, etc.
inline void addClosedCylinderZ(wowee::pipeline::WoweeModel& wom,
                               float cx, float cy,
                               float R, float z0, float z1, int sides) {
    constexpr float pi = 3.14159265358979f;
    uint32_t back = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        glm::vec3 dir(std::cos(ang), std::sin(ang), 0.0f);
        addVertex(wom, {cx + R * dir.x, cy + R * dir.y, z0}, dir, {u, 0});
    }
    uint32_t front = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        glm::vec3 dir(std::cos(ang), std::sin(ang), 0.0f);
        addVertex(wom, {cx + R * dir.x, cy + R * dir.y, z1}, dir, {u, 1});
    }
    for (int s = 0; s < sides; ++s) {
        wom.indices.insert(wom.indices.end(), {
            back + s, front + s, back + s + 1,
            back + s + 1, front + s, front + s + 1
        });
    }
    // -Z cap fan.
    uint32_t backCenter = addVertex(wom, {cx, cy, z0},
                                     {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f});
    uint32_t backRing = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        addVertex(wom, {cx + R * std::cos(ang), cy + R * std::sin(ang), z0},
                  {0.0f, 0.0f, -1.0f},
                  {0.5f + 0.5f * std::cos(ang),
                   0.5f + 0.5f * std::sin(ang)});
    }
    for (int s = 0; s < sides; ++s) {
        wom.indices.insert(wom.indices.end(),
            {backCenter, backRing + s + 1, backRing + s});
    }
    // +Z cap fan.
    uint32_t frontCenter = addVertex(wom, {cx, cy, z1},
                                      {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f});
    uint32_t frontRing = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        addVertex(wom, {cx + R * std::cos(ang), cy + R * std::sin(ang), z1},
                  {0.0f, 0.0f, 1.0f},
                  {0.5f + 0.5f * std::cos(ang),
                   0.5f + 0.5f * std::sin(ang)});
    }
    for (int s = 0; s < sides; ++s) {
        wom.indices.insert(wom.indices.end(),
            {frontCenter, frontRing + s, frontRing + s + 1});
    }
}

// Append a closed Y-axis cylinder (side wall + ±Y end caps) to a
// WoweeModel. The cylinder spans from y=y0 to y=y1 with radius R
// and `sides` segments around the circumference. Side wall faces
// outward radially; cap fans face -Y / +Y. Used by --gen-mesh-
// bird-bath and any future cylindrical garden / well / ornament
// primitive that needs a watertight Y-axis tube.
inline void addClosedCylinderY(wowee::pipeline::WoweeModel& wom,
                               float R, float y0, float y1, int sides) {
    constexpr float pi = 3.14159265358979f;
    uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        glm::vec3 dir(std::cos(ang), 0.0f, std::sin(ang));
        addVertex(wom, {R * dir.x, y0, R * dir.z}, dir, {u, 0});
    }
    uint32_t top = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        glm::vec3 dir(std::cos(ang), 0.0f, std::sin(ang));
        addVertex(wom, {R * dir.x, y1, R * dir.z}, dir, {u, 1});
    }
    for (int s = 0; s < sides; ++s) {
        wom.indices.insert(wom.indices.end(), {
            bot + s, top + s, bot + s + 1,
            bot + s + 1, top + s, top + s + 1
        });
    }
    uint32_t botCenter = addVertex(wom, {0.0f, y0, 0.0f},
                                    {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f});
    uint32_t botRing = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        addVertex(wom, {R * std::cos(ang), y0, R * std::sin(ang)},
                  {0.0f, -1.0f, 0.0f},
                  {0.5f + 0.5f * std::cos(ang),
                   0.5f + 0.5f * std::sin(ang)});
    }
    for (int s = 0; s < sides; ++s) {
        wom.indices.insert(wom.indices.end(),
            {botCenter, botRing + s + 1, botRing + s});
    }
    uint32_t topCenter = addVertex(wom, {0.0f, y1, 0.0f},
                                    {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f});
    uint32_t topRing = static_cast<uint32_t>(wom.vertices.size());
    for (int s = 0; s <= sides; ++s) {
        float u = static_cast<float>(s) / sides;
        float ang = u * 2.0f * pi;
        addVertex(wom, {R * std::cos(ang), y1, R * std::sin(ang)},
                  {0.0f, 1.0f, 0.0f},
                  {0.5f + 0.5f * std::cos(ang),
                   0.5f + 0.5f * std::sin(ang)});
    }
    for (int s = 0; s < sides; ++s) {
        wom.indices.insert(wom.indices.end(),
            {topCenter, topRing + s, topRing + s + 1});
    }
}

// Append a flat-shaded axis-aligned box to a WoweeModel. The box
// is centered at (cx, cy, cz) with half-extents (hx, hy, hz). Each
// of the 6 faces emits its own 4 vertices with the face's outward
// normal, so adjacent faces don't share normals - exactly what
// flat shading needs. UVs are 0..1 across each face.
//
// Used pervasively by --gen-mesh-* primitives that build meshes
// from axis-aligned box primitives (firepit stones, dock pilings,
// canopy posts, woodpile logs, tent walls before the door cutout
// added one-off triangles, etc.). Hoisted out of cli_gen_mesh.cpp
// where 36 identical lambdas duplicated this implementation.
void addFlatBox(wowee::pipeline::WoweeModel& wom,
                float cx, float cy, float cz,
                float hx, float hy, float hz);

// Overload taking lower/upper corner positions (lo, hi). Some
// callers (--gen-mesh-archway, --gen-mesh-fence) compute corners
// directly rather than center+halfsize.
void addFlatBox(wowee::pipeline::WoweeModel& wom,
                glm::vec3 lo, glm::vec3 hi);

} // namespace cli
} // namespace editor
} // namespace wowee
