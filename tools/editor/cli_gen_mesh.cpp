#include "cli_gen_mesh.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_box_emitter.hpp"
#include "cli_arg_parse.hpp"

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
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleRock(int& i, int argc, char** argv) {
    // Procedural boulder. Starts as an octahedron, subdivides
    // each face N times to get a rounded base, then displaces
    // each vertex along its outward direction by a smooth
    // sin/cos noise term controlled by `seed` and `roughness`.
    // Result is a unique-shaped rock per seed - perfect for
    // scattering across a zone via random-populate-zone.
    //
    // The 16th procedural primitive in the WOM library.
    std::string womBase = argv[++i];
    float radius = 1.0f;
    float roughness = 0.25f;  // 0..1, fraction of radius
    int subdiv = 2;           // 0=8 tris, 1=32, 2=128, 3=512
    uint32_t seed = 1;
    parseOptFloat(i, argc, argv, radius);
    parseOptFloat(i, argc, argv, roughness);
    parseOptInt(i, argc, argv, subdiv);
    parseOptUint(i, argc, argv, seed);
    if (radius <= 0 || roughness < 0 || roughness > 1 ||
        subdiv < 0 || subdiv > 4) {
        std::fprintf(stderr,
            "gen-mesh-rock: radius>0, roughness 0..1, subdiv 0..4\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    // Build sphere via octahedron subdivision. Vertices are
    // accumulated in unit-length form first, then displaced.
    std::vector<glm::vec3> sv;  // sphere verts (unit)
    std::vector<glm::uvec3> st; // sphere tris (vertex indices)
    sv = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1},
    };
    st = {
        {0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4},
        {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5},
    };
    // Edge-midpoint cache so shared edges don't duplicate verts.
    for (int s = 0; s < subdiv; ++s) {
        std::map<std::pair<uint32_t,uint32_t>, uint32_t> midCache;
        auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
            auto key = std::make_pair(std::min(a,b), std::max(a,b));
            auto it = midCache.find(key);
            if (it != midCache.end()) return it->second;
            glm::vec3 m = glm::normalize((sv[a] + sv[b]) * 0.5f);
            uint32_t idx = static_cast<uint32_t>(sv.size());
            sv.push_back(m);
            midCache[key] = idx;
            return idx;
        };
        std::vector<glm::uvec3> next;
        next.reserve(st.size() * 4);
        for (auto& tri : st) {
            uint32_t a = tri.x, b = tri.y, c = tri.z;
            uint32_t ab = midpoint(a, b);
            uint32_t bc = midpoint(b, c);
            uint32_t ca = midpoint(c, a);
            next.push_back({a,  ab, ca});
            next.push_back({b,  bc, ab});
            next.push_back({c,  ca, bc});
            next.push_back({ab, bc, ca});
        }
        st.swap(next);
    }
    // Smooth pseudo-noise displacement. Three orthogonal sin
    // products give a coherent bumpy surface; phase shift uses
    // the seed so each value yields a distinct silhouette.
    float sf = static_cast<float>(seed);
    auto displace = [&](glm::vec3 p) -> float {
        float n = std::sin(p.x * 3.1f + sf * 0.91f) *
                  std::sin(p.y * 4.7f + sf * 1.37f) *
                  std::sin(p.z * 5.3f + sf * 0.43f);
        float n2 = std::sin(p.x * 7.1f + sf * 0.11f) *
                   std::sin(p.y * 8.3f + sf * 2.13f) *
                   std::sin(p.z * 9.7f + sf * 1.91f);
        return 1.0f + roughness * (0.7f * n + 0.3f * n2);
    };
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    std::vector<glm::vec3> finalPos(sv.size());
    for (size_t v = 0; v < sv.size(); ++v) {
        finalPos[v] = sv[v] * (radius * displace(sv[v]));
    }
    // Per-vertex normals from triangle face normals (averaged).
    std::vector<glm::vec3> normals(sv.size(), glm::vec3(0));
    for (auto& tri : st) {
        glm::vec3 a = finalPos[tri.x];
        glm::vec3 b = finalPos[tri.y];
        glm::vec3 c = finalPos[tri.z];
        glm::vec3 fn = glm::normalize(glm::cross(b - a, c - a));
        normals[tri.x] += fn;
        normals[tri.y] += fn;
        normals[tri.z] += fn;
    }
    for (auto& n : normals) n = glm::length(n) > 1e-6f
        ? glm::normalize(n) : glm::vec3(0, 1, 0);
    for (size_t v = 0; v < sv.size(); ++v) {
        wowee::pipeline::WoweeModel::Vertex vtx;
        vtx.position = finalPos[v];
        vtx.normal = normals[v];
        // Spherical UV unwrap. Visible seam at u=0/1 is
        // acceptable for rocks - usually hidden by terrain.
        glm::vec3 d = glm::normalize(sv[v]);
        vtx.texCoord = {
            0.5f + std::atan2(d.z, d.x) / (2.0f * 3.14159265f),
            0.5f - std::asin(d.y) / 3.14159265f,
        };
        wom.vertices.push_back(vtx);
    }
    for (auto& tri : st) {
        wom.indices.push_back(tri.x);
        wom.indices.push_back(tri.y);
        wom.indices.push_back(tri.z);
    }
    float bound = radius * (1.0f + roughness);
    wom.boundMin = glm::vec3(-bound);
    wom.boundMax = glm::vec3( bound);
    finalizeAsSingleBatch(wom);
    if (!saveWomOrError(wom, womBase, "gen-mesh-rock")) return 1;
    printWomWrote(womBase);
    std::printf("  radius    : %.3f\n", radius);
    std::printf("  roughness : %.3f\n", roughness);
    std::printf("  subdiv    : %d\n", subdiv);
    std::printf("  seed      : %u\n", seed);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handlePillar(int& i, int argc, char** argv) {
    // Procedural classical column. Central shaft is a
    // cylinder with N concave flutes (radius modulated by
    // cos²(theta*flutes/2)), capped above and below by
    // wider disc caps that act as a simple capital and
    // base. The 17th procedural mesh primitive - useful
    // for ruins, temples, dungeons, plaza decoration.
    std::string womBase = argv[++i];
    float radius = 0.4f;
    float height = 4.0f;
    int flutes = 12;
    float capScale = 1.25f;
    parseOptFloat(i, argc, argv, radius);
    parseOptFloat(i, argc, argv, height);
    parseOptInt(i, argc, argv, flutes);
    parseOptFloat(i, argc, argv, capScale);
    if (radius <= 0 || height <= 0 ||
        flutes < 4 || flutes > 64 ||
        capScale < 1.0f || capScale > 4.0f) {
        std::fprintf(stderr,
            "gen-mesh-pillar: radius>0, height>0, flutes 4..64, capScale 1..4\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    const float pi = 3.14159265358979f;
    // We use 8 segments per flute so the cosine-modulated
    // groove resolves smoothly. Vertical: 2 rings (top/bot
    // of shaft) + cap/base discs.
    const int radSegs = flutes * 8;
    const float fluteDepth = radius * 0.12f;
    float capR = radius * capScale;
    float capThick = radius * 0.25f;
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Shaft side ring at given y. radius modulated by flute count.
    auto buildShaftRing = [&](float y) -> uint32_t {
        uint32_t start = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= radSegs; ++sg) {
            float u = static_cast<float>(sg) / radSegs;
            float ang = u * 2.0f * pi;
            float c = std::cos(ang * flutes * 0.5f);
            float r = radius - fluteDepth * (c * c);
            glm::vec3 p(r * std::cos(ang), y, r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, glm::normalize(n), glm::vec2(u, y / height));
        }
        return start;
    };
    // Cap/base disc ring (constant radius capR) at given y.
    auto buildCapRing = [&](float y, float r) -> uint32_t {
        uint32_t start = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= radSegs; ++sg) {
            float u = static_cast<float>(sg) / radSegs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(r * std::cos(ang), y, r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, glm::normalize(n), glm::vec2(u, y / height));
        }
        return start;
    };
    // Layout (Y goes up):
    //   capThick: base disc bottom
    //   capThick: base disc top
    //   ...shaft from capThick to height-capThick...
    //   height-capThick: cap disc bottom
    //   height: cap disc top
    float shaftY0 = capThick;
    float shaftY1 = height - capThick;
    uint32_t baseBot = buildCapRing(0.0f, capR);
    uint32_t baseTop = buildCapRing(shaftY0, capR);
    uint32_t shaftBot = buildShaftRing(shaftY0);
    uint32_t shaftTop = buildShaftRing(shaftY1);
    uint32_t capBot = buildCapRing(shaftY1, capR);
    uint32_t capTop = buildCapRing(height, capR);
    // Quad connector helper.
    auto connect = [&](uint32_t a0, uint32_t a1) {
        for (int sg = 0; sg < radSegs; ++sg) {
            uint32_t i00 = a0 + sg;
            uint32_t i01 = a0 + sg + 1;
            uint32_t i10 = a1 + sg;
            uint32_t i11 = a1 + sg + 1;
            wom.indices.insert(wom.indices.end(),
                               { i00, i10, i01, i01, i10, i11 });
        }
    };
    connect(baseBot, baseTop);   // base side
    connect(shaftBot, shaftTop); // shaft
    connect(capBot, capTop);     // cap side
    // Bottom cap (downward fan), top cap (upward fan).
    uint32_t bottomCenter = addV({0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f});
    uint32_t topCenter = addV({0, height, 0}, {0, 1, 0}, {0.5f, 0.5f});
    for (int sg = 0; sg < radSegs; ++sg) {
        wom.indices.insert(wom.indices.end(),
            { bottomCenter, baseBot + sg + 1, baseBot + sg });
        wom.indices.insert(wom.indices.end(),
            { topCenter, capTop + sg, capTop + sg + 1 });
    }
    // Annular surfaces where caps meet shaft (top of base disc
    // out to shaft, etc.). Just connect the two rings - they
    // sit at the same Y so this looks like a flat ring.
    connect(baseTop, shaftBot);
    connect(shaftTop, capBot);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, capR, capR, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-pillar")) return 1;
    printWomWrote(womBase);
    std::printf("  radius    : %.3f\n", radius);
    std::printf("  height    : %.3f\n", height);
    std::printf("  flutes    : %d\n", flutes);
    std::printf("  cap scale : %.2fx (capR=%.3f)\n", capScale, capR);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleBridge(int& i, int argc, char** argv) {
    // Procedural plank bridge. Deck is N axis-aligned planks
    // running across the bridge's width with small gaps
    // between, plus two side rails (top + bottom rails on
    // posts). Bridge length runs along +X, width is on Z.
    // The 18th procedural mesh primitive - useful for
    // river crossings, dungeon catwalks, scenic overlooks.
    std::string womBase = argv[++i];
    float length = 6.0f;     // along X
    float width = 2.0f;      // along Z
    int planks = 6;          // plank count across the length
    float railHeight = 1.0f; // rail height above deck (0 = no rails)
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptInt(i, argc, argv, planks);
    parseOptFloat(i, argc, argv, railHeight);
    if (length <= 0 || width <= 0 ||
        planks < 1 || planks > 64 ||
        railHeight < 0 || railHeight > 4.0f) {
        std::fprintf(stderr,
            "gen-mesh-bridge: length>0, width>0, planks 1..64, rail 0..4\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Box helper - builds 24-vert / 12-tri box centered on
    // (cx, cy, cz) with half-extents (hx, hy, hz). Each face
    // gets unique vertices so flat-shading works. Indices are
    // pushed into wom.indices directly.
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Deck: planks along X, gap = 5% of plank pitch.
    float plankThickness = 0.08f;
    float plankPitch = length / planks;
    float plankWidth = plankPitch * 0.95f;
    for (int p = 0; p < planks; ++p) {
        float cx = -length * 0.5f + plankPitch * (p + 0.5f);
        addBox(cx, plankThickness * 0.5f, 0,
               plankWidth * 0.5f, plankThickness * 0.5f, width * 0.5f);
    }
    // Rails: 2 sides × (top rail + 3 posts) when railHeight > 0
    if (railHeight > 0.0f) {
        float postR = 0.06f;
        float topRailR = 0.08f;
        int postCount = 3;
        float rzOffset = width * 0.5f - postR;
        for (int side = 0; side < 2; ++side) {
            float zSign = (side == 0) ? 1.0f : -1.0f;
            float z = zSign * rzOffset;
            // Top rail: long thin box spanning length
            addBox(0, plankThickness + railHeight, z,
                   length * 0.5f, topRailR, topRailR);
            // Posts evenly spaced
            for (int p = 0; p < postCount; ++p) {
                float t = (postCount > 1)
                    ? static_cast<float>(p) / (postCount - 1)
                    : 0.5f;
                float cx = -length * 0.5f + length * t;
                if (p == 0) cx += postR;
                if (p == postCount - 1) cx -= postR;
                addBox(cx, plankThickness + railHeight * 0.5f, z,
                       postR, railHeight * 0.5f, postR);
            }
        }
    }
    finalizeAsSingleBatch(wom);
    float maxY = plankThickness + railHeight;
    setCenteredBoundsXZ(wom, length * 0.5f, width * 0.5f, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-bridge")) return 1;
    printWomWrote(womBase);
    std::printf("  length    : %.3f\n", length);
    std::printf("  width     : %.3f\n", width);
    std::printf("  planks    : %d\n", planks);
    std::printf("  rail H    : %.3f%s\n", railHeight,
                railHeight > 0 ? "" : " (no rails)");
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleTower(int& i, int argc, char** argv) {
    // Procedural castle tower. Solid cylindrical shaft with
    // crenellated battlements ringing the top: alternating
    // raised "merlons" and gaps. Each merlon is a thin
    // angular wedge sitting on the top rim. Useful for
    // keeps, watchtowers, perimeter walls.
    //
    // The 19th procedural mesh primitive.
    std::string womBase = argv[++i];
    float radius = 1.5f;
    float height = 8.0f;
    int battlements = 8;     // merlons around the rim
    float battlementH = 0.5f;
    parseOptFloat(i, argc, argv, radius);
    parseOptFloat(i, argc, argv, height);
    parseOptInt(i, argc, argv, battlements);
    parseOptFloat(i, argc, argv, battlementH);
    if (radius <= 0 || height <= 0 ||
        battlements < 4 || battlements > 64 ||
        battlementH < 0 || battlementH > 4.0f) {
        std::fprintf(stderr,
            "gen-mesh-tower: radius>0, height>0, battlements 4..64, bH 0..4\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    const int radSegs = std::max(24, battlements * 4);
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Cylinder shaft: side ring at y=0 and y=height.
    uint32_t botRing = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= radSegs; ++sg) {
        float u = static_cast<float>(sg) / radSegs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(radius * std::cos(ang), 0, radius * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, glm::vec2(u, 0));
    }
    uint32_t topRing = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= radSegs; ++sg) {
        float u = static_cast<float>(sg) / radSegs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(radius * std::cos(ang), height, radius * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, glm::vec2(u, 1));
    }
    for (int sg = 0; sg < radSegs; ++sg) {
        wom.indices.insert(wom.indices.end(), {
            botRing + sg, topRing + sg, botRing + sg + 1,
            botRing + sg + 1, topRing + sg, topRing + sg + 1
        });
    }
    // Top cap (fan toward upward-facing center).
    uint32_t topCenter = addV({0, height, 0}, {0, 1, 0}, {0.5f, 0.5f});
    for (int sg = 0; sg < radSegs; ++sg) {
        wom.indices.insert(wom.indices.end(),
            { topCenter, topRing + sg, topRing + sg + 1 });
    }
    // Bottom cap (fan toward downward-facing center).
    uint32_t botCenter = addV({0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f});
    for (int sg = 0; sg < radSegs; ++sg) {
        wom.indices.insert(wom.indices.end(),
            { botCenter, botRing + sg + 1, botRing + sg });
    }
    // Battlements: thin curved blocks around the top rim,
    // half the slots filled (alternating merlon/gap).
    // Each merlon is approximated by an extruded arc segment
    // at the wall radius extending outward slightly.
    if (battlementH > 0.0f) {
        int merlonSpan = radSegs / battlements;
        int merlonHalf = std::max(1, merlonSpan / 2);
        float outerR = radius * 1.05f;
        float innerR = radius * 0.95f;
        for (int b = 0; b < battlements; ++b) {
            int startSeg = b * merlonSpan;
            // Build 8-vert box-like segment between angles
            // covering merlonHalf slots (so half the rim is
            // filled, forming the merlon/gap pattern).
            float ang0 = 2.0f * pi * static_cast<float>(startSeg) / radSegs;
            float ang1 = 2.0f * pi * static_cast<float>(startSeg + merlonHalf) / radSegs;
            glm::vec3 outer0(outerR * std::cos(ang0), 0, outerR * std::sin(ang0));
            glm::vec3 outer1(outerR * std::cos(ang1), 0, outerR * std::sin(ang1));
            glm::vec3 inner0(innerR * std::cos(ang0), 0, innerR * std::sin(ang0));
            glm::vec3 inner1(innerR * std::cos(ang1), 0, innerR * std::sin(ang1));
            glm::vec3 yLow(0, height, 0);
            glm::vec3 yHigh(0, height + battlementH, 0);
            glm::vec3 norm = glm::normalize(
                outer0 + outer1 - inner0 - inner1);
            auto V = [&](glm::vec3 p, glm::vec3 n) {
                return addV(p, n, {0, 0});
            };
            // 8 verts: 4 corners × 2 heights
            uint32_t bbl = V(outer0 + yLow,  norm);   // bot outer left
            uint32_t bbr = V(outer1 + yLow,  norm);
            uint32_t btl = V(outer0 + yHigh, norm);   // top outer left
            uint32_t btr = V(outer1 + yHigh, norm);
            uint32_t ibl = V(inner0 + yLow,  -norm);  // bot inner left
            uint32_t ibr = V(inner1 + yLow,  -norm);
            uint32_t itl = V(inner0 + yHigh, -norm);  // top inner left
            uint32_t itr = V(inner1 + yHigh, -norm);
            // outer face
            wom.indices.insert(wom.indices.end(), {bbl, btl, bbr, bbr, btl, btr});
            // inner face
            wom.indices.insert(wom.indices.end(), {ibr, itr, ibl, ibl, itr, itl});
            // top face
            wom.indices.insert(wom.indices.end(), {btl, itl, btr, btr, itl, itr});
            // left and right end caps
            wom.indices.insert(wom.indices.end(), {bbl, ibl, btl, btl, ibl, itl});
            wom.indices.insert(wom.indices.end(), {bbr, btr, ibr, ibr, btr, itr});
        }
    }
    finalizeAsSingleBatch(wom);
    float maxY = height + battlementH;
    float maxR = radius * 1.05f;
    setCenteredBoundsXZ(wom, maxR, maxR, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-tower")) return 1;
    printWomWrote(womBase);
    std::printf("  radius      : %.3f\n", radius);
    std::printf("  height      : %.3f\n", height);
    std::printf("  battlements : %d (%.3fm tall)\n",
                battlements, battlementH);
    std::printf("  vertices    : %zu\n", wom.vertices.size());
    std::printf("  triangles   : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleHouse(int& i, int argc, char** argv) {
    // Simple procedural house: cube body + pyramid roof
    // meeting at a central apex above the body's roofline.
    // The pyramid sits flush on the body so the eaves
    // line up with the wall edges. No door cutout - that
    // can be added later via mesh boolean ops or texture.
    //
    // The 20th procedural mesh primitive.
    std::string womBase = argv[++i];
    float width = 4.0f;       // along X
    float depth = 4.0f;       // along Z
    float height = 3.0f;      // wall height (Y)
    float roofH = 2.0f;       // pyramid above walls
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, roofH);
    if (width <= 0 || depth <= 0 || height <= 0 ||
        roofH < 0 || roofH > 20.0f) {
        std::fprintf(stderr,
            "gen-mesh-house: width/depth/height>0, roof 0..20\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    float hx = width * 0.5f;
    float hz = depth * 0.5f;
    // 4 walls - each a quad with an outward-facing normal so
    // the house reads as solid even with backface culling on.
    struct Wall {
        glm::vec3 a, b, c, d;  // CCW from outside
        glm::vec3 n;
    };
    Wall walls[4] = {
        {{ hx, 0,  hz}, {-hx, 0,  hz}, {-hx, height,  hz}, { hx, height,  hz}, { 0, 0,  1}}, // +Z
        {{-hx, 0, -hz}, { hx, 0, -hz}, { hx, height, -hz}, {-hx, height, -hz}, { 0, 0, -1}}, // -Z
        {{ hx, 0, -hz}, { hx, 0,  hz}, { hx, height,  hz}, { hx, height, -hz}, { 1, 0,  0}}, // +X
        {{-hx, 0,  hz}, {-hx, 0, -hz}, {-hx, height, -hz}, {-hx, height,  hz}, {-1, 0,  0}}, // -X
    };
    for (const Wall& w : walls) {
        uint32_t a = addV(w.a, w.n, {0, 0});
        uint32_t b = addV(w.b, w.n, {1, 0});
        uint32_t c = addV(w.c, w.n, {1, 1});
        uint32_t d = addV(w.d, w.n, {0, 1});
        wom.indices.insert(wom.indices.end(), {a, b, c, a, c, d});
    }
    // Floor (single quad, normal-down so it shows from below;
    // texturable as a foundation slab).
    {
        uint32_t a = addV({-hx, 0, -hz}, {0, -1, 0}, {0, 0});
        uint32_t b = addV({ hx, 0, -hz}, {0, -1, 0}, {1, 0});
        uint32_t c = addV({ hx, 0,  hz}, {0, -1, 0}, {1, 1});
        uint32_t d = addV({-hx, 0,  hz}, {0, -1, 0}, {0, 1});
        wom.indices.insert(wom.indices.end(), {a, c, b, a, d, c});
    }
    // Roof: 4 triangles meeting at central apex.
    float apexY = height + roofH;
    glm::vec3 apex(0, apexY, 0);
    // Eave corners (Y = wall height) - each triangle shares
    // two adjacent corners + the apex. Per-face normal is
    // computed once so flat shading works.
    glm::vec3 eaves[4] = {
        {-hx, height,  hz},
        { hx, height,  hz},
        { hx, height, -hz},
        {-hx, height, -hz},
    };
    for (int s = 0; s < 4; ++s) {
        glm::vec3 e0 = eaves[s];
        glm::vec3 e1 = eaves[(s + 1) % 4];
        glm::vec3 fn = glm::normalize(glm::cross(e1 - e0, apex - e0));
        uint32_t a = addV(e0, fn, {0, 0});
        uint32_t b = addV(e1, fn, {1, 0});
        uint32_t c = addV(apex, fn, {0.5f, 1});
        wom.indices.insert(wom.indices.end(), {a, b, c});
    }
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, hx, hz, apexY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-house")) return 1;
    printWomWrote(womBase);
    std::printf("  width      : %.3f\n", width);
    std::printf("  depth      : %.3f\n", depth);
    std::printf("  wall H     : %.3f\n", height);
    std::printf("  roof H     : %.3f (apex %.3f)\n", roofH, apexY);
    printWomMeshStats(wom);
    return 0;
}

int handleFountain(int& i, int argc, char** argv) {
    // Procedural fountain: low cylindrical basin with a
    // narrower spout column rising from its center. Solid
    // basin (not hollow) for simplicity - readable as a
    // fountain because of the spout silhouette. Useful for
    // town squares, plazas, garden centerpieces.
    //
    // The 21st procedural mesh primitive.
    std::string womBase = argv[++i];
    float basinR = 1.5f;
    float basinH = 0.5f;
    float spoutR = 0.2f;
    float spoutH = 1.5f;
    parseOptFloat(i, argc, argv, basinR);
    parseOptFloat(i, argc, argv, basinH);
    parseOptFloat(i, argc, argv, spoutR);
    parseOptFloat(i, argc, argv, spoutH);
    if (basinR <= 0 || basinH <= 0 || spoutR <= 0 || spoutH <= 0 ||
        spoutR >= basinR) {
        std::fprintf(stderr,
            "gen-mesh-fountain: all dims > 0; spoutR must be < basinR\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    const int segs = 24;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Cylinder helper: build side ring + caps from y0 to y1
    // at given radius. Returns when done; indices appended
    // directly. Side ring is 2× (segs+1) verts at y0 then y1.
    auto cylinder = [&](float r, float y0, float y1) {
        uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segs; ++sg) {
            float u = static_cast<float>(sg) / segs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(r * std::cos(ang), y0, r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, glm::vec2(u, 0));
        }
        uint32_t top = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segs; ++sg) {
            float u = static_cast<float>(sg) / segs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(r * std::cos(ang), y1, r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, glm::vec2(u, 1));
        }
        for (int sg = 0; sg < segs; ++sg) {
            wom.indices.insert(wom.indices.end(), {
                bot + sg, top + sg, bot + sg + 1,
                bot + sg + 1, top + sg, top + sg + 1
            });
        }
        // Top cap (faces +Y)
        uint32_t topC = addV({0, y1, 0}, {0, 1, 0}, {0.5f, 0.5f});
        for (int sg = 0; sg < segs; ++sg) {
            wom.indices.insert(wom.indices.end(),
                {topC, top + sg, top + sg + 1});
        }
        // Bottom cap (faces -Y)
        uint32_t botC = addV({0, y0, 0}, {0, -1, 0}, {0.5f, 0.5f});
        for (int sg = 0; sg < segs; ++sg) {
            wom.indices.insert(wom.indices.end(),
                {botC, bot + sg + 1, bot + sg});
        }
    };
    // Basin: cylinder from y=0 to y=basinH at basinR.
    cylinder(basinR, 0.0f, basinH);
    // Spout: cylinder from y=basinH to y=basinH+spoutH at spoutR.
    cylinder(spoutR, basinH, basinH + spoutH);
    finalizeAsSingleBatch(wom);
    float maxY = basinH + spoutH;
    setCenteredBoundsXZ(wom, basinR, basinR, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-fountain")) return 1;
    printWomWrote(womBase);
    std::printf("  basin    : R=%.3f H=%.3f\n", basinR, basinH);
    std::printf("  spout    : R=%.3f H=%.3f\n", spoutR, spoutH);
    std::printf("  total H  : %.3f\n", maxY);
    std::printf("  vertices : %zu\n", wom.vertices.size());
    std::printf("  triangles: %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleStatue(int& i, int argc, char** argv) {
    // Humanoid placeholder: square pedestal block + tall
    // narrow body cylinder + head sphere. The silhouette
    // reads as a statue without needing limbs. Useful for
    // monuments, hero statues, plaza centerpieces, religious
    // shrines.
    //
    // The 22nd procedural mesh primitive.
    std::string womBase = argv[++i];
    float pedSize = 1.0f;     // pedestal width and depth
    float bodyH = 2.5f;       // body cylinder height
    float headR = 0.4f;       // head sphere radius
    parseOptFloat(i, argc, argv, pedSize);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, headR);
    if (pedSize <= 0 || bodyH <= 0 || headR <= 0) {
        std::fprintf(stderr,
            "gen-mesh-statue: all dims must be positive\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Pedestal: low square block (24 unique verts).
    float pedH = pedSize * 0.4f;
    float hp = pedSize * 0.5f;
    {
        struct Face { glm::vec3 n, du, dv; };
        Face faces[6] = {
            {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
            {{0,-1, 0}, {1, 0, 0}, {0, 0,-1}},
            {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
            {{-1,0, 0}, {0, 0,-1}, {0, 1, 0}},
            {{0, 0, 1}, {-1,0, 0}, {0, 1, 0}},
            {{0, 0,-1}, {1, 0, 0}, {0, 1, 0}},
        };
        glm::vec3 c(0, pedH * 0.5f, 0);
        glm::vec3 ext(hp, pedH * 0.5f, hp);
        for (const Face& f : faces) {
            glm::vec3 center = c + glm::vec3(f.n.x*ext.x, f.n.y*ext.y, f.n.z*ext.z);
            glm::vec3 du = glm::vec3(f.du.x*ext.x, f.du.y*ext.y, f.du.z*ext.z);
            glm::vec3 dv = glm::vec3(f.dv.x*ext.x, f.dv.y*ext.y, f.dv.z*ext.z);
            uint32_t base = static_cast<uint32_t>(wom.vertices.size());
            addV(center - du - dv, f.n, {0, 0});
            addV(center + du - dv, f.n, {1, 0});
            addV(center + du + dv, f.n, {1, 1});
            addV(center - du + dv, f.n, {0, 1});
            wom.indices.insert(wom.indices.end(),
                {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
    // Body cylinder from y=pedH to y=pedH+bodyH at radius pedSize*0.2
    float bodyR = pedSize * 0.2f;
    float bodyY0 = pedH;
    float bodyY1 = pedH + bodyH;
    const int segs = 16;
    uint32_t bodyBot = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segs; ++sg) {
        float u = static_cast<float>(sg) / segs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(bodyR * std::cos(ang), bodyY0, bodyR * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, {u, 0});
    }
    uint32_t bodyTop = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segs; ++sg) {
        float u = static_cast<float>(sg) / segs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(bodyR * std::cos(ang), bodyY1, bodyR * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, {u, 1});
    }
    for (int sg = 0; sg < segs; ++sg) {
        wom.indices.insert(wom.indices.end(), {
            bodyBot + sg, bodyTop + sg, bodyBot + sg + 1,
            bodyBot + sg + 1, bodyTop + sg, bodyTop + sg + 1
        });
    }
    // Head sphere centered above body. UV-sphere with 16
    // longitude × 12 latitude segments.
    float headY = bodyY1 + headR;
    const int headLon = 16;
    const int headLat = 12;
    uint32_t headStart = static_cast<uint32_t>(wom.vertices.size());
    for (int la = 0; la <= headLat; ++la) {
        float v = static_cast<float>(la) / headLat;
        float phi = v * pi;  // 0..pi
        float sphi = std::sin(phi), cphi = std::cos(phi);
        for (int lo = 0; lo <= headLon; ++lo) {
            float u = static_cast<float>(lo) / headLon;
            float theta = u * 2.0f * pi;
            glm::vec3 dir(sphi * std::cos(theta),
                          cphi,
                          sphi * std::sin(theta));
            glm::vec3 p = glm::vec3(0, headY, 0) + dir * headR;
            addV(p, dir, {u, v});
        }
    }
    int rowSize = headLon + 1;
    for (int la = 0; la < headLat; ++la) {
        for (int lo = 0; lo < headLon; ++lo) {
            uint32_t i00 = headStart + la * rowSize + lo;
            uint32_t i01 = headStart + la * rowSize + lo + 1;
            uint32_t i10 = headStart + (la + 1) * rowSize + lo;
            uint32_t i11 = headStart + (la + 1) * rowSize + lo + 1;
            wom.indices.insert(wom.indices.end(),
                {i00, i10, i01, i01, i10, i11});
        }
    }
    finalizeAsSingleBatch(wom);
    float maxY = headY + headR;
    setCenteredBoundsXZ(wom, hp, hp, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-statue")) return 1;
    printWomWrote(womBase);
    std::printf("  pedestal  : %.3f × %.3f × %.3f\n", pedSize, pedH, pedSize);
    std::printf("  body      : R=%.3f H=%.3f\n", bodyR, bodyH);
    std::printf("  head      : R=%.3f\n", headR);
    std::printf("  total H   : %.3f\n", maxY);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleAltar(int& i, int argc, char** argv) {
    // Round altar: stack of N stepped cylindrical discs,
    // each one wider and shorter than the next so the
    // silhouette descends like a wedding cake. Top disc is
    // the altar surface (where offerings would go); base
    // discs widen out to anchor the structure visually.
    //
    // The 23rd procedural mesh primitive - pairs naturally
    // with --gen-texture-marble for a temple aesthetic.
    std::string womBase = argv[++i];
    float topR = 0.7f;        // top altar disc radius
    float topH = 0.3f;        // top altar disc height
    int steps = 3;            // base steps below the top
    float stepStride = 0.3f;  // each step grows R by this much, shrinks H
    parseOptFloat(i, argc, argv, topR);
    parseOptFloat(i, argc, argv, topH);
    parseOptInt(i, argc, argv, steps);
    parseOptFloat(i, argc, argv, stepStride);
    if (topR <= 0 || topH <= 0 || steps < 0 || steps > 16 ||
        stepStride <= 0 || stepStride > 5.0f) {
        std::fprintf(stderr,
            "gen-mesh-altar: topR/topH > 0, steps 0..16, stride 0..5\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    const int segs = 24;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Build a cylindrical disc from y0 to y1 at radius r.
    // Side ring + top cap (faces +Y). Bottom of each disc
    // is hidden by the next disc below, so we skip a bottom
    // cap on all discs except the last (saves ~24 tris/disc).
    auto disc = [&](float r, float y0, float y1, bool capBottom) {
        uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segs; ++sg) {
            float u = static_cast<float>(sg) / segs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(r * std::cos(ang), y0, r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, {u, 0});
        }
        uint32_t top = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segs; ++sg) {
            float u = static_cast<float>(sg) / segs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(r * std::cos(ang), y1, r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, {u, 1});
        }
        for (int sg = 0; sg < segs; ++sg) {
            wom.indices.insert(wom.indices.end(), {
                bot + sg, top + sg, bot + sg + 1,
                bot + sg + 1, top + sg, top + sg + 1
            });
        }
        // Top cap fan (faces +Y).
        uint32_t tc = addV({0, y1, 0}, {0, 1, 0}, {0.5f, 0.5f});
        for (int sg = 0; sg < segs; ++sg) {
            wom.indices.insert(wom.indices.end(),
                {tc, top + sg, top + sg + 1});
        }
        if (capBottom) {
            uint32_t bc = addV({0, y0, 0}, {0, -1, 0}, {0.5f, 0.5f});
            for (int sg = 0; sg < segs; ++sg) {
                wom.indices.insert(wom.indices.end(),
                    {bc, bot + sg + 1, bot + sg});
            }
        }
    };
    // Build bottom-up so y0 starts at floor and tops stack.
    // Step k (k=0 is bottom-most) has radius = topR + (steps-k)*stride
    // and height = topH * (1 - 0.2 * k). Y position accumulates.
    float curY = 0.0f;
    for (int k = steps - 1; k >= 0; --k) {  // bottom step first
        float r = topR + (k + 1) * stepStride;
        float h = topH * (1.0f - 0.2f * k);
        if (h < topH * 0.4f) h = topH * 0.4f;
        bool isBottom = (k == steps - 1);
        disc(r, curY, curY + h, isBottom);
        curY += h;
    }
    // Top disc (the actual altar surface)
    disc(topR, curY, curY + topH, steps == 0);
    float maxY = curY + topH;
    finalizeAsSingleBatch(wom);
    float maxR = topR + steps * stepStride;
    setCenteredBoundsXZ(wom, maxR, maxR, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-altar")) return 1;
    printWomWrote(womBase);
    std::printf("  top      : R=%.3f H=%.3f\n", topR, topH);
    std::printf("  steps    : %d (stride %.3f)\n", steps, stepStride);
    std::printf("  base R   : %.3f\n", maxR);
    std::printf("  total H  : %.3f\n", maxY);
    std::printf("  vertices : %zu\n", wom.vertices.size());
    std::printf("  triangles: %zu\n", wom.indices.size() / 3);
    return 0;
}

int handlePortal(int& i, int argc, char** argv) {
    // Doorway portal: two vertical post boxes plus a
    // horizontal lintel box across the top. Posts run along
    // the Z axis (so width spans Z), opening faces +X. The
    // gap between the posts is the actual doorway. Useful
    // for entrances, gates, magical portals, ruins.
    //
    // The 24th procedural mesh primitive.
    std::string womBase = argv[++i];
    float width = 2.5f;          // outer-to-outer along Z
    float height = 4.0f;         // total Y
    float postThick = 0.4f;      // post width in X and Z
    float lintelH = 0.5f;        // top lintel height (Y)
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, postThick);
    parseOptFloat(i, argc, argv, lintelH);
    if (width <= 0 || height <= 0 || postThick <= 0 ||
        lintelH < 0 || postThick * 2 >= width ||
        lintelH > height) {
        std::fprintf(stderr,
            "gen-mesh-portal: posts must fit inside width; lintel <= height\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Box helper - same pattern as other multi-box meshes.
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Two posts at z = ±(width/2 - postThick/2). Each
    // post extends from y=0 to y=height-lintelH so it
    // tucks under the lintel.
    float postY = (height - lintelH) * 0.5f;
    float postHy = (height - lintelH) * 0.5f;
    float postZ = (width - postThick) * 0.5f;
    float postHt = postThick * 0.5f;
    addBox(0, postY,  postZ, postHt, postHy, postHt);
    addBox(0, postY, -postZ, postHt, postHy, postHt);
    // Lintel: spans full width across the top, same X
    // thickness as posts.
    if (lintelH > 0.0f) {
        float lintelY = height - lintelH * 0.5f;
        addBox(0, lintelY, 0,
               postHt, lintelH * 0.5f, width * 0.5f);
    }
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, postHt, width * 0.5f, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-portal")) return 1;
    printWomWrote(womBase);
    std::printf("  width      : %.3f\n", width);
    std::printf("  height     : %.3f\n", height);
    std::printf("  post thick : %.3f\n", postThick);
    std::printf("  lintel H   : %.3f%s\n", lintelH,
                lintelH > 0 ? "" : " (no lintel)");
    printWomMeshStats(wom);
    return 0;
}

int handleArchway(int& i, int argc, char** argv) {
    // Semicircular arched doorway. Two cylindrical pillars
    // hold up a curved keystone vault: the vault is a series
    // of N angular wedge segments tracing a half-circle from
    // pillar-top to pillar-top. The opening is the empty
    // semicircular space below.
    //
    // The 25th procedural mesh primitive - the "fancier"
    // sibling of --gen-mesh-portal which uses a flat lintel.
    std::string womBase = argv[++i];
    float width = 3.0f;        // outer-to-outer pillar centers along Z
    float pillarH = 3.0f;      // pillar height (Y)
    float thickness = 0.4f;    // pillar radius and arch radial thickness
    int archSegs = 12;         // segments around the half-circle
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, pillarH);
    parseOptFloat(i, argc, argv, thickness);
    parseOptInt(i, argc, argv, archSegs);
    if (width <= 0 || pillarH <= 0 || thickness <= 0 ||
        archSegs < 4 || archSegs > 64 ||
        thickness * 4 >= width) {
        std::fprintf(stderr,
            "gen-mesh-archway: thickness×4 < width, archSegs 4..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    const int pillarSegs = 16;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Cylindrical pillar at given (cx, cz), from y=0 to y=pillarH.
    auto pillar = [&](float cx, float cz) {
        float r = thickness;
        uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= pillarSegs; ++sg) {
            float u = static_cast<float>(sg) / pillarSegs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(cx + r * std::cos(ang), 0,
                        cz + r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, {u, 0});
        }
        uint32_t top = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= pillarSegs; ++sg) {
            float u = static_cast<float>(sg) / pillarSegs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(cx + r * std::cos(ang), pillarH,
                        cz + r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, {u, 1});
        }
        for (int sg = 0; sg < pillarSegs; ++sg) {
            wom.indices.insert(wom.indices.end(), {
                bot + sg, top + sg, bot + sg + 1,
                bot + sg + 1, top + sg, top + sg + 1
            });
        }
        // Caps
        uint32_t bc = addV({cx, 0, cz}, {0, -1, 0}, {0.5f, 0.5f});
        uint32_t tc = addV({cx, pillarH, cz}, {0, 1, 0}, {0.5f, 0.5f});
        for (int sg = 0; sg < pillarSegs; ++sg) {
            wom.indices.insert(wom.indices.end(),
                {bc, bot + sg + 1, bot + sg});
            wom.indices.insert(wom.indices.end(),
                {tc, top + sg, top + sg + 1});
        }
    };
    float pillarZ = (width - 2 * thickness) * 0.5f;
    pillar(0,  pillarZ);
    pillar(0, -pillarZ);
    // Arch vault: trace half-circle from (z = +pillarZ, y = pillarH)
    // up over to (z = -pillarZ, y = pillarH). Center of arch:
    // (z = 0, y = pillarH). Arch radius = pillarZ.
    // Inner arch (radius pillarZ - thickness*0.5) and outer
    // (radius pillarZ + thickness*0.5) - the vault sits between.
    float archCY = pillarH;
    float arcInner = pillarZ - thickness * 0.5f;
    float arcOuter = pillarZ + thickness * 0.5f;
    // Each segment: 4 verts (inner-near, outer-near, inner-far,
    // outer-far) extruded along X by thickness so the vault
    // has front and back faces.
    float archX = thickness * 0.5f;  // half-depth in X
    // Build vertex rings for inner and outer surfaces at
    // each segment boundary, then connect.
    // Top half-circle goes from theta=0 to theta=pi.
    std::vector<glm::vec3> innerRing;
    std::vector<glm::vec3> outerRing;
    for (int s = 0; s <= archSegs; ++s) {
        float t = static_cast<float>(s) / archSegs;
        float theta = t * pi;  // 0..pi
        float zi = arcInner * std::cos(theta);
        float yi = arcInner * std::sin(theta);
        float zo = arcOuter * std::cos(theta);
        float yo = arcOuter * std::sin(theta);
        innerRing.push_back({0, archCY + yi, zi});
        outerRing.push_back({0, archCY + yo, zo});
    }
    // For each segment, add 8 vertices (4 corners × front/back face)
    // and stitch them into 6 quads = 12 tris each.
    for (int s = 0; s < archSegs; ++s) {
        glm::vec3 i0 = innerRing[s];
        glm::vec3 i1 = innerRing[s + 1];
        glm::vec3 o0 = outerRing[s];
        glm::vec3 o1 = outerRing[s + 1];
        // Estimate outward (radial) normal as midpoint of o0+o1
        // direction from center.
        glm::vec3 outDir = glm::normalize(glm::vec3(0,
            (i0.y + i1.y + o0.y + o1.y) * 0.25f - archCY,
            (i0.z + i1.z + o0.z + o1.z) * 0.25f));
        glm::vec3 frontN(1, 0, 0);
        glm::vec3 backN(-1, 0, 0);
        auto V = [&](glm::vec3 p, glm::vec3 n) {
            return addV(p, n, {0, 0});
        };
        // Outer surface (top of arch): faces outward radially
        uint32_t a = V({-archX, o0.y, o0.z}, outDir);
        uint32_t b = V({ archX, o0.y, o0.z}, outDir);
        uint32_t c = V({ archX, o1.y, o1.z}, outDir);
        uint32_t d = V({-archX, o1.y, o1.z}, outDir);
        wom.indices.insert(wom.indices.end(), {a, b, c, a, c, d});
        // Inner surface (underside of arch): faces inward
        uint32_t e = V({-archX, i0.y, i0.z}, -outDir);
        uint32_t f = V({ archX, i0.y, i0.z}, -outDir);
        uint32_t g = V({ archX, i1.y, i1.z}, -outDir);
        uint32_t h = V({-archX, i1.y, i1.z}, -outDir);
        wom.indices.insert(wom.indices.end(), {e, g, f, e, h, g});
        // Front face (+X) of this wedge
        uint32_t fi0 = V({ archX, i0.y, i0.z}, frontN);
        uint32_t fo0 = V({ archX, o0.y, o0.z}, frontN);
        uint32_t fo1 = V({ archX, o1.y, o1.z}, frontN);
        uint32_t fi1 = V({ archX, i1.y, i1.z}, frontN);
        wom.indices.insert(wom.indices.end(),
            {fi0, fo0, fo1, fi0, fo1, fi1});
        // Back face (-X)
        uint32_t bi0 = V({-archX, i0.y, i0.z}, backN);
        uint32_t bo0 = V({-archX, o0.y, o0.z}, backN);
        uint32_t bo1 = V({-archX, o1.y, o1.z}, backN);
        uint32_t bi1 = V({-archX, i1.y, i1.z}, backN);
        wom.indices.insert(wom.indices.end(),
            {bi0, bo1, bo0, bi0, bi1, bo1});
    }
    finalizeAsSingleBatch(wom);
    float maxY = pillarH + arcOuter;
    setCenteredBoundsXZ(wom, thickness, width * 0.5f, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-archway")) return 1;
    printWomWrote(womBase);
    std::printf("  width      : %.3f\n", width);
    std::printf("  pillar H   : %.3f\n", pillarH);
    std::printf("  thickness  : %.3f\n", thickness);
    std::printf("  arch segs  : %d (radius %.3f)\n", archSegs, arcOuter);
    std::printf("  apex Y     : %.3f\n", maxY);
    printWomMeshStats(wom);
    return 0;
}

int handleBarrel(int& i, int argc, char** argv) {
    // Tapered barrel: cylindrical body whose radius bulges
    // smoothly from `topRadius` at the rims to `midRadius`
    // at the middle (the classic stave-cooper barrel
    // silhouette), plus 2 raised hoop bands at 25% and 75%
    // of the height. The 26th procedural mesh primitive.
    std::string womBase = argv[++i];
    float topR = 0.4f;        // radius at top and bottom rim
    float midR = 0.5f;        // radius at the middle bulge
    float height = 1.0f;
    float hoopThick = 0.06f;  // hoop band radial protrusion
    parseOptFloat(i, argc, argv, topR);
    parseOptFloat(i, argc, argv, midR);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, hoopThick);
    if (topR <= 0 || midR <= 0 || height <= 0 ||
        hoopThick < 0 || hoopThick > 0.5f) {
        std::fprintf(stderr,
            "gen-mesh-barrel: radii/height > 0, hoopThick 0..0.5\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    const int segs = 16;        // angular subdivisions
    const int rings = 12;       // vertical slices
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Radius profile: smooth cosine bulge from rim to mid.
    // r(t) = topR + (midR - topR) * sin(pi*t) where t in 0..1
    // gives 0 at t=0/1 and 1 at t=0.5 - exact rim fit.
    auto radiusAt = [&](float t) -> float {
        return topR + (midR - topR) * std::sin(pi * t);
    };
    uint32_t firstRing = static_cast<uint32_t>(wom.vertices.size());
    for (int ri = 0; ri <= rings; ++ri) {
        float t = static_cast<float>(ri) / rings;
        float y = t * height;
        float r = radiusAt(t);
        // Hoops: bump radius outward in two narrow bands.
        float hoop1 = std::abs(t - 0.25f);
        float hoop2 = std::abs(t - 0.75f);
        if (hoop1 < 0.04f) r += hoopThick * (1.0f - hoop1 / 0.04f);
        if (hoop2 < 0.04f) r += hoopThick * (1.0f - hoop2 / 0.04f);
        for (int sg = 0; sg <= segs; ++sg) {
            float u = static_cast<float>(sg) / segs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(r * std::cos(ang), y, r * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, {u, t});
        }
    }
    int rowSize = segs + 1;
    for (int ri = 0; ri < rings; ++ri) {
        for (int sg = 0; sg < segs; ++sg) {
            uint32_t i00 = firstRing + ri * rowSize + sg;
            uint32_t i01 = firstRing + ri * rowSize + sg + 1;
            uint32_t i10 = firstRing + (ri + 1) * rowSize + sg;
            uint32_t i11 = firstRing + (ri + 1) * rowSize + sg + 1;
            wom.indices.insert(wom.indices.end(),
                {i00, i10, i01, i01, i10, i11});
        }
    }
    // End caps (top + bottom). topR is also the bottom-most
    // and top-most ring radius since sin(0) = sin(pi) = 0.
    uint32_t botCenter = addV({0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f});
    uint32_t topCenter = addV({0, height, 0}, {0, 1, 0}, {0.5f, 0.5f});
    uint32_t botRing = firstRing;
    uint32_t topRing = firstRing + rings * rowSize;
    for (int sg = 0; sg < segs; ++sg) {
        wom.indices.insert(wom.indices.end(),
            {botCenter, botRing + sg + 1, botRing + sg});
        wom.indices.insert(wom.indices.end(),
            {topCenter, topRing + sg, topRing + sg + 1});
    }
    finalizeAsSingleBatch(wom);
    float maxR = midR + hoopThick;
    setCenteredBoundsXZ(wom, maxR, maxR, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-barrel")) return 1;
    printWomWrote(womBase);
    std::printf("  rim R     : %.3f\n", topR);
    std::printf("  bulge R   : %.3f\n", midR);
    std::printf("  height    : %.3f\n", height);
    std::printf("  hoops     : 2 (thickness %.3f)\n", hoopThick);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleChest(int& i, int argc, char** argv) {
    // Treasure chest: rectangular body box + smaller lid
    // box on top + 3 thin iron bands wrapping around the
    // body + a small lock plate on the front center face.
    // The 27th procedural mesh primitive - useful for
    // dungeon loot, room decoration, quest objectives.
    std::string womBase = argv[++i];
    float width = 1.4f;       // along X
    float depth = 0.9f;       // along Z
    float bodyH = 0.9f;       // body box height
    float lidH = 0.25f;       // lid height above body
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, lidH);
    if (width <= 0 || depth <= 0 || bodyH <= 0 || lidH < 0) {
        std::fprintf(stderr,
            "gen-mesh-chest: width/depth/bodyH > 0, lidH >= 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Box helper - adds 24 unique verts / 12 tris centered
    // on (cx, cy, cz) with half-extents (hx, hy, hz). Each
    // face gets unique normals for flat shading.
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float hx = width * 0.5f;
    float hz = depth * 0.5f;
    // Body: y=0 to y=bodyH
    addBox(0, bodyH * 0.5f, 0, hx, bodyH * 0.5f, hz);
    // Lid: smaller box on top, slightly inset on each side
    float lidInset = std::min(width, depth) * 0.04f;
    float lidHx = hx - lidInset;
    float lidHz = hz - lidInset;
    if (lidH > 0.0f && lidHx > 0 && lidHz > 0) {
        addBox(0, bodyH + lidH * 0.5f, 0,
               lidHx, lidH * 0.5f, lidHz);
    }
    // 3 iron bands wrapping the body - thin slabs
    // protruding ~3% radially on the sides + top.
    // Band positions: 15%, 50%, 85% of body width.
    float bandThickX = width * 0.04f;  // band depth along X
    float bandHy = bodyH * 0.5f + 0.005f;
    float bandHz = hz + 0.012f;
    float bandPositions[3] = {-hx * 0.7f, 0.0f, hx * 0.7f};
    for (float bx : bandPositions) {
        addBox(bx, bandHy, 0,
               bandThickX * 0.5f, bandHy, bandHz);
    }
    // Lock plate: small thin box on the front face, centered.
    // Front face is +Z. Plate sits at z = hz + tiny epsilon.
    float lockW = width * 0.10f;
    float lockH = bodyH * 0.18f;
    float lockY = bodyH * 0.65f;
    float lockEps = 0.008f;
    addBox(0, lockY, hz + lockEps,
           lockW * 0.5f, lockH * 0.5f, lockEps);
    finalizeAsSingleBatch(wom);
    float maxY = bodyH + lidH;
    wom.boundMin = glm::vec3(-hx, 0, -hz - 0.012f);
    wom.boundMax = glm::vec3( hx, maxY, hz + 0.012f);
    if (!saveWomOrError(wom, womBase, "gen-mesh-chest")) return 1;
    printWomWrote(womBase);
    std::printf("  width × depth : %.3f × %.3f\n", width, depth);
    std::printf("  body H        : %.3f\n", bodyH);
    std::printf("  lid H         : %.3f\n", lidH);
    std::printf("  components    : body + lid + 3 bands + lock\n");
    std::printf("  vertices      : %zu\n", wom.vertices.size());
    std::printf("  triangles     : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleAnvil(int& i, int argc, char** argv) {
    // Blacksmith anvil: stepped pedestal base + flat work
    // surface (the "face") + tapered horn extending forward.
    // Built from 3 boxes + a 4-vertex tapered prism for the
    // horn. The 28th procedural mesh primitive.
    std::string womBase = argv[++i];
    float length = 1.0f;       // along X (face length)
    float width = 0.4f;        // along Z
    float hornLen = 0.5f;      // horn extending past face
    float bodyH = 0.5f;        // total height
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, hornLen);
    parseOptFloat(i, argc, argv, bodyH);
    if (length <= 0 || width <= 0 || hornLen < 0 || bodyH <= 0) {
        std::fprintf(stderr,
            "gen-mesh-anvil: length/width/bodyH > 0, hornLen >= 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Pedestal: bottom 60% of total height, narrower base
    // (4-step taper would be classic but for simplicity we use
    // a wide base + narrow waist + wide cap structure as 3 boxes).
    float baseH = bodyH * 0.25f;
    float waistH = bodyH * 0.30f;
    float capH = bodyH * 0.20f;
    float faceH = bodyH * 0.25f;
    float baseHx = length * 0.45f;
    float baseHz = width * 0.55f;
    float waistHx = length * 0.30f;
    float waistHz = width * 0.40f;
    float capHx = length * 0.50f;
    float capHz = width * 0.55f;
    float faceHx = length * 0.50f;
    float faceHz = width * 0.50f;
    float y0 = 0.0f;
    addBox(0, y0 + baseH * 0.5f, 0, baseHx, baseH * 0.5f, baseHz);
    y0 += baseH;
    addBox(0, y0 + waistH * 0.5f, 0, waistHx, waistH * 0.5f, waistHz);
    y0 += waistH;
    addBox(0, y0 + capH * 0.5f, 0, capHx, capH * 0.5f, capHz);
    y0 += capH;
    addBox(0, y0 + faceH * 0.5f, 0, faceHx, faceH * 0.5f, faceHz);
    // Horn: tapered prism extending in +X past the face. 6 verts
    // (rectangle at face edge tapering to a point at the tip).
    if (hornLen > 0.0f) {
        float hornBaseX = faceHx;
        float hornTipX = faceHx + hornLen;
        float hornY0 = y0 + faceH * 0.25f;
        float hornY1 = y0 + faceH * 0.75f;
        float hornHz = faceHz * 0.6f;
        // 4 base verts + 2 tip verts (tip is a vertical edge)
        // Build 4 face triangles + 2 base/tip caps
        glm::vec3 b00(hornBaseX, hornY0,  hornHz);
        glm::vec3 b01(hornBaseX, hornY0, -hornHz);
        glm::vec3 b10(hornBaseX, hornY1,  hornHz);
        glm::vec3 b11(hornBaseX, hornY1, -hornHz);
        glm::vec3 t0 (hornTipX,  (hornY0 + hornY1) * 0.5f,  0);
        // Top face triangles (b10, b11, t0)
        auto addTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            uint32_t base = static_cast<uint32_t>(wom.vertices.size());
            wom.vertices.push_back({a, n, {0, 0}});
            wom.vertices.push_back({b, n, {1, 0}});
            wom.vertices.push_back({c, n, {0.5f, 1}});
            wom.indices.insert(wom.indices.end(), {base, base + 1, base + 2});
        };
        // 4 side faces converging to t0
        addTri(b00, b01, t0);          // bottom
        addTri(b11, b10, t0);          // top
        addTri(b10, b00, t0);          // +Z side
        addTri(b01, b11, t0);          // -Z side
        // Base of horn (closes the rectangle on the face side).
        // The base is hidden against the anvil face but include it
        // so the mesh is watertight.
        glm::vec3 baseN(-1, 0, 0);
        uint32_t base = static_cast<uint32_t>(wom.vertices.size());
        wom.vertices.push_back({b00, baseN, {0, 0}});
        wom.vertices.push_back({b10, baseN, {0, 1}});
        wom.vertices.push_back({b11, baseN, {1, 1}});
        wom.vertices.push_back({b01, baseN, {1, 0}});
        wom.indices.insert(wom.indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    finalizeAsSingleBatch(wom);
    float maxX = std::max(faceHx, faceHx + hornLen);
    float maxZ = std::max({baseHz, waistHz, capHz, faceHz});
    wom.boundMin = glm::vec3(-faceHx, 0, -maxZ);
    wom.boundMax = glm::vec3( maxX, bodyH, maxZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-anvil")) return 1;
    printWomWrote(womBase);
    std::printf("  length × width : %.3f × %.3f\n", length, width);
    std::printf("  body H         : %.3f\n", bodyH);
    std::printf("  horn length    : %.3f\n", hornLen);
    std::printf("  components     : 4 step pedestal + tapered horn\n");
    std::printf("  vertices       : %zu\n", wom.vertices.size());
    std::printf("  triangles      : %zu\n", wom.indices.size() / 3);
    return 0;
}


int handleStairs(int& i, int argc, char** argv) {
    // Procedural straight staircase along +X. N steps with
    // configurable rise/run/width. Each step is a closed
    // box, sharing no vertices with neighbors so per-face
    // normals are flat (looks correct without smoothing).
    //
    // Defaults: 5 steps, stepHeight=0.2, stepDepth=0.3,
    // width=1.0 - roughly 1m tall × 1.5m long × 1m wide,
    // a believable single flight.
    //
    // Useful for level-design placeholders ("I need a staircase
    // up to this platform"), test-bench geometry for camera/
    // movement, and quick prototyping of stepped terrain.
    std::string womBase = argv[++i];
    int steps = 5;
    float stepHeight = 0.2f, stepDepth = 0.3f, width = 1.0f;
    try { steps = std::stoi(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-mesh-stairs: <steps> must be an integer\n");
        return 1;
    }
    if (steps < 1 || steps > 256) {
        std::fprintf(stderr,
            "gen-mesh-stairs: steps %d out of range (1..256)\n", steps);
        return 1;
    }
    parseOptFloat(i, argc, argv, stepHeight);
    parseOptFloat(i, argc, argv, stepDepth);
    parseOptFloat(i, argc, argv, width);
    if (stepHeight <= 0 || stepDepth <= 0 || width <= 0) {
        std::fprintf(stderr,
            "gen-mesh-stairs: dimensions must be positive\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addV = [&](float px, float py, float pz,
                    float nx, float ny, float nz,
                    float u,  float v) -> uint32_t {
        return addVertex(wom, px, py, pz, nx, ny, nz, u, v);
    };
    float halfW = width * 0.5f;
    // Each step is a box from y=0 to y=(k+1)*stepHeight,
    // depth-wise from x=k*stepDepth to x=(k+1)*stepDepth,
    // width-wise from z=-halfW to z=+halfW. Six faces per
    // step, four verts each = 24 verts / 12 tris per step.
    for (int k = 0; k < steps; ++k) {
        float x0 = k * stepDepth;
        float x1 = (k + 1) * stepDepth;
        float y0 = 0.0f;
        float y1 = (k + 1) * stepHeight;
        float z0 = -halfW;
        float z1 =  halfW;
        struct Face { float nx, ny, nz; float verts[4][3]; };
        Face faces[6] = {
            { 0,  1,  0, {{x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}}},  // top  +Y
            { 0, -1,  0, {{x0,y0,z0},{x0,y0,z1},{x1,y0,z1},{x1,y0,z0}}},  // bot  -Y
            {-1,  0,  0, {{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{x0,y0,z1}}},  // back -X
            { 1,  0,  0, {{x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0}}},  // front+X (riser)
            { 0,  0, -1, {{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0}}},  // -Z
            { 0,  0,  1, {{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{x1,y0,z1}}},  // +Z
        };
        float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        for (auto& f : faces) {
            uint32_t base = static_cast<uint32_t>(wom.vertices.size());
            for (int q = 0; q < 4; ++q) {
                addV(f.verts[q][0], f.verts[q][1], f.verts[q][2],
                      f.nx, f.ny, f.nz, uvs[q][0], uvs[q][1]);
            }
            wom.indices.push_back(base + 0);
            wom.indices.push_back(base + 1);
            wom.indices.push_back(base + 2);
            wom.indices.push_back(base + 0);
            wom.indices.push_back(base + 2);
            wom.indices.push_back(base + 3);
        }
    }
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-stairs")) return 1;
    printWomWrote(womBase);
    std::printf("  steps     : %d\n", steps);
    std::printf("  stepHt    : %.3f\n", stepHeight);
    std::printf("  stepDep   : %.3f\n", stepDepth);
    std::printf("  width     : %.3f\n", width);
    std::printf("  vertices  : %zu (%d per step × %d)\n",
                wom.vertices.size(), 24, steps);
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    std::printf("  span      : %.3fL × %.3fH × %.3fW\n",
                steps * stepDepth, steps * stepHeight, width);
    return 0;
}

int handleGrid(int& i, int argc, char** argv) {
    // Flat plane subdivided into NxN cells. Useful for LOD
    // demos, deformable surfaces (later --displace passes),
    // testbench geometry that needs many triangles. Default
    // size is 1.0 (centered on origin). Hard cap at N=256
    // so a typo doesn't generate a mesh with 130k+ vertices.
    std::string womBase = argv[++i];
    int N = 0;
    try { N = std::stoi(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "gen-mesh-grid: <subdivisions> must be an integer\n");
        return 1;
    }
    if (N < 1 || N > 256) {
        std::fprintf(stderr,
            "gen-mesh-grid: subdivisions %d out of range (1..256)\n", N);
        return 1;
    }
    float size = 1.0f;
    parseOptFloat(i, argc, argv, size);
    if (size <= 0.0f) {
        std::fprintf(stderr,
            "gen-mesh-grid: size must be positive\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // (N+1)x(N+1) vertices on the XY plane centered on origin,
    // Z=0. Normals all point +Z; UVs are 0..1 across the grid.
    float halfSize = size * 0.5f;
    float cellSize = size / N;
    for (int j = 0; j <= N; ++j) {
        for (int k = 0; k <= N; ++k) {
            wowee::pipeline::WoweeModel::Vertex v;
            v.position = glm::vec3(-halfSize + k * cellSize,
                                    -halfSize + j * cellSize,
                                    0.0f);
            v.normal = glm::vec3(0, 0, 1);
            v.texCoord = glm::vec2(static_cast<float>(k) / N,
                                    static_cast<float>(j) / N);
            wom.vertices.push_back(v);
        }
    }
    int stride = N + 1;
    for (int j = 0; j < N; ++j) {
        for (int k = 0; k < N; ++k) {
            uint32_t a = j * stride + k;
            uint32_t b = a + 1;
            uint32_t c = a + stride;
            uint32_t d = c + 1;
            wom.indices.push_back(a);
            wom.indices.push_back(c);
            wom.indices.push_back(b);
            wom.indices.push_back(b);
            wom.indices.push_back(c);
            wom.indices.push_back(d);
        }
    }
    wom.boundMin = glm::vec3(-halfSize, -halfSize, 0);
    wom.boundMax = glm::vec3( halfSize,  halfSize, 0);
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-grid")) return 1;
    printWomWrote(womBase);
    std::printf("  subdivisions : %d (%dx%d cells)\n", N, N, N);
    std::printf("  size         : %.3f\n", size);
    std::printf("  vertices     : %zu = (N+1)²\n", wom.vertices.size());
    std::printf("  triangles    : %zu = 2N²\n", wom.indices.size() / 3);
    return 0;
}

int handleDisc(int& i, int argc, char** argv) {
    // Flat circular disc on XY centered at origin. Center
    // vertex + ring of <segments> verts, indexed as a fan.
    // Useful for magic circles, coin meshes, lily pads, top
    // caps for cylinders the user wants without making a
    // full cylinder.
    std::string womBase = argv[++i];
    float radius = 1.0f;
    int segments = 32;
    parseOptFloat(i, argc, argv, radius);
    parseOptInt(i, argc, argv, segments);
    if (radius <= 0.0f || segments < 3 || segments > 1024) {
        std::fprintf(stderr,
            "gen-mesh-disc: radius must be positive, segments 3..1024\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Center vertex.
    {
        wowee::pipeline::WoweeModel::Vertex v;
        v.position = glm::vec3(0, 0, 0);
        v.normal = glm::vec3(0, 0, 1);
        v.texCoord = glm::vec2(0.5f, 0.5f);
        wom.vertices.push_back(v);
    }
    // Ring vertices (one extra at end so UV-seam isn't shared).
    for (int k = 0; k <= segments; ++k) {
        float t = static_cast<float>(k) / segments;
        float ang = t * 2.0f * 3.14159265358979f;
        float ca = std::cos(ang), sa = std::sin(ang);
        wowee::pipeline::WoweeModel::Vertex v;
        v.position = glm::vec3(radius * ca, radius * sa, 0);
        v.normal = glm::vec3(0, 0, 1);
        v.texCoord = glm::vec2(0.5f + 0.5f * ca, 0.5f + 0.5f * sa);
        wom.vertices.push_back(v);
    }
    // Fan indices.
    for (int k = 0; k < segments; ++k) {
        wom.indices.push_back(0);
        wom.indices.push_back(1 + k);
        wom.indices.push_back(2 + k);
    }
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-disc")) return 1;
    printWomWrote(womBase);
    std::printf("  radius    : %.3f\n", radius);
    std::printf("  segments  : %d\n", segments);
    std::printf("  vertices  : %zu (1 center + %d ring)\n",
                wom.vertices.size(), segments + 1);
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleTube(int& i, int argc, char** argv) {
    // Hollow cylinder along Y axis. Outer + inner walls + top
    // and bottom annular caps. Useful for railings, fence
    // posts, pipes, hollow logs, ring towers - anywhere a
    // solid cylinder would feel wrong because you should be
    // able to see through the middle.
    std::string womBase = argv[++i];
    float outerR = 1.0f;
    float innerR = 0.7f;
    float height = 2.0f;
    int segments = 24;
    parseOptFloat(i, argc, argv, outerR);
    parseOptFloat(i, argc, argv, innerR);
    parseOptFloat(i, argc, argv, height);
    parseOptInt(i, argc, argv, segments);
    if (outerR <= 0 || innerR <= 0 || innerR >= outerR ||
        height <= 0 || segments < 3 || segments > 1024) {
        std::fprintf(stderr,
            "gen-mesh-tube: 0 < innerR < outerR, height > 0, segments 3..1024\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    float h = height * 0.5f;
    auto addV = [&](float px, float py, float pz,
                    float nx, float ny, float nz,
                    float u,  float v) -> uint32_t {
        return addVertex(wom, px, py, pz, nx, ny, nz, u, v);
    };
    // Outer wall: 2 rows × (segments+1) verts, normals point
    // radially outward.
    uint32_t outerStart = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * 3.14159265358979f;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(outerR * ca, -h, outerR * sa, ca, 0, sa, u, 0);
        addV(outerR * ca,  h, outerR * sa, ca, 0, sa, u, 1);
    }
    for (int sg = 0; sg < segments; ++sg) {
        uint32_t a = outerStart + sg * 2;
        uint32_t b = a + 1, c = a + 2, d = a + 3;
        wom.indices.push_back(a);
        wom.indices.push_back(c);
        wom.indices.push_back(b);
        wom.indices.push_back(b);
        wom.indices.push_back(c);
        wom.indices.push_back(d);
    }
    // Inner wall: normals point radially inward, winding
    // reversed so the inside-facing surfaces face the viewer
    // when looking through the tube.
    uint32_t innerStart = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * 3.14159265358979f;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(innerR * ca, -h, innerR * sa, -ca, 0, -sa, u, 0);
        addV(innerR * ca,  h, innerR * sa, -ca, 0, -sa, u, 1);
    }
    for (int sg = 0; sg < segments; ++sg) {
        uint32_t a = innerStart + sg * 2;
        uint32_t b = a + 1, c = a + 2, d = a + 3;
        wom.indices.push_back(a);
        wom.indices.push_back(b);
        wom.indices.push_back(c);
        wom.indices.push_back(b);
        wom.indices.push_back(d);
        wom.indices.push_back(c);
    }
    // Top annular cap: ring at +Y. Inner + outer ring of verts,
    // quads stitched between them, normal +Y.
    uint32_t topInner = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * 3.14159265358979f;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(innerR * ca, h, innerR * sa, 0, 1, 0,
               0.5f + 0.5f * (innerR / outerR) * ca,
               0.5f + 0.5f * (innerR / outerR) * sa);
    }
    uint32_t topOuter = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * 3.14159265358979f;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(outerR * ca, h, outerR * sa, 0, 1, 0,
               0.5f + 0.5f * ca, 0.5f + 0.5f * sa);
    }
    for (int sg = 0; sg < segments; ++sg) {
        uint32_t a = topInner + sg;
        uint32_t b = topInner + sg + 1;
        uint32_t c = topOuter + sg;
        uint32_t d = topOuter + sg + 1;
        wom.indices.push_back(a);
        wom.indices.push_back(c);
        wom.indices.push_back(b);
        wom.indices.push_back(b);
        wom.indices.push_back(c);
        wom.indices.push_back(d);
    }
    // Bottom annular cap, normal -Y, winding reversed.
    uint32_t botInner = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * 3.14159265358979f;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(innerR * ca, -h, innerR * sa, 0, -1, 0,
               0.5f + 0.5f * (innerR / outerR) * ca,
               0.5f - 0.5f * (innerR / outerR) * sa);
    }
    uint32_t botOuter = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * 3.14159265358979f;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(outerR * ca, -h, outerR * sa, 0, -1, 0,
               0.5f + 0.5f * ca, 0.5f - 0.5f * sa);
    }
    for (int sg = 0; sg < segments; ++sg) {
        uint32_t a = botInner + sg;
        uint32_t b = botInner + sg + 1;
        uint32_t c = botOuter + sg;
        uint32_t d = botOuter + sg + 1;
        wom.indices.push_back(a);
        wom.indices.push_back(b);
        wom.indices.push_back(c);
        wom.indices.push_back(b);
        wom.indices.push_back(d);
        wom.indices.push_back(c);
    }
    wom.boundMin = glm::vec3(-outerR, -h, -outerR);
    wom.boundMax = glm::vec3( outerR,  h,  outerR);
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-tube")) return 1;
    printWomWrote(womBase);
    std::printf("  outer R   : %.3f\n", outerR);
    std::printf("  inner R   : %.3f\n", innerR);
    std::printf("  height    : %.3f\n", height);
    std::printf("  segments  : %d\n", segments);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleCapsule(int& i, int argc, char** argv) {
    // Capsule along the Y axis: cylindrical body of length
    // cylHeight bookended by two hemispheres of radius. Total
    // height is cylHeight + 2*radius. Useful for character
    // collision shells, pill-shaped buttons, hot-dog props,
    // and physics-friendly placeholders.
    std::string womBase = argv[++i];
    float radius = 0.5f;
    float cylHeight = 1.0f;
    int segments = 16;
    int stacks = 8;  // per hemisphere
    parseOptFloat(i, argc, argv, radius);
    parseOptFloat(i, argc, argv, cylHeight);
    parseOptInt(i, argc, argv, segments);
    parseOptInt(i, argc, argv, stacks);
    if (radius <= 0 || cylHeight < 0 ||
        segments < 3 || segments > 1024 ||
        stacks < 1 || stacks > 256) {
        std::fprintf(stderr,
            "gen-mesh-capsule: radius > 0, cylHeight >= 0, segments 3..1024, stacks 1..256\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    float halfBody = cylHeight * 0.5f;
    float totalH = cylHeight + 2.0f * radius;
    auto addV = [&](float px, float py, float pz,
                    float nx, float ny, float nz,
                    float u,  float v) -> uint32_t {
        return addVertex(wom, px, py, pz, nx, ny, nz, u, v);
    };
    // Top hemisphere: stacks rings from north pole down to
    // body top. Vertex layout per ring: (segments+1) verts.
    const float pi = 3.14159265358979f;
    int totalVPerRing = segments + 1;
    // Top hemisphere rings: stacks+1 rings (ring 0 is the
    // pole). v texcoord goes 0..0.25 across the cap.
    for (int st = 0; st <= stacks; ++st) {
        float t = static_cast<float>(st) / stacks;
        float phi = t * (pi * 0.5f);  // 0 at pole, π/2 at body
        float sphi = std::sin(phi), cphi = std::cos(phi);
        float ringR = radius * sphi;
        float ringY = halfBody + radius * cphi;
        for (int sg = 0; sg <= segments; ++sg) {
            float u = static_cast<float>(sg) / segments;
            float ang = u * 2.0f * pi;
            float ca = std::cos(ang), sa = std::sin(ang);
            addV(ringR * ca, ringY, ringR * sa,
                  sphi * ca, cphi, sphi * sa,
                  u, t * 0.25f);
        }
    }
    // Body: 2 rings (top and bottom of cylinder), normal
    // radial (no Y component). UV goes 0.25..0.75.
    int bodyTopRingStart = static_cast<int>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * pi;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(radius * ca, halfBody, radius * sa, ca, 0, sa, u, 0.25f);
    }
    int bodyBotRingStart = static_cast<int>(wom.vertices.size());
    for (int sg = 0; sg <= segments; ++sg) {
        float u = static_cast<float>(sg) / segments;
        float ang = u * 2.0f * pi;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(radius * ca, -halfBody, radius * sa, ca, 0, sa, u, 0.75f);
    }
    // Bottom hemisphere: mirror of top.
    int botHemiStart = static_cast<int>(wom.vertices.size());
    for (int st = 0; st <= stacks; ++st) {
        float t = static_cast<float>(st) / stacks;
        float phi = t * (pi * 0.5f);
        float sphi = std::sin(phi), cphi = std::cos(phi);
        float ringR = radius * cphi;
        float ringY = -halfBody - radius * sphi;
        for (int sg = 0; sg <= segments; ++sg) {
            float u = static_cast<float>(sg) / segments;
            float ang = u * 2.0f * pi;
            float ca = std::cos(ang), sa = std::sin(ang);
            addV(ringR * ca, ringY, ringR * sa,
                  cphi * ca, -sphi, cphi * sa,
                  u, 0.75f + t * 0.25f);
        }
    }
    // Index the rings: top hemi (stacks rings → stacks-1
    // bands), body (1 band), bottom hemi (stacks bands).
    auto stitch = [&](int topRingStart, int botRingStart) {
        for (int sg = 0; sg < segments; ++sg) {
            uint32_t a = topRingStart + sg;
            uint32_t b = a + 1;
            uint32_t c = botRingStart + sg;
            uint32_t d = c + 1;
            wom.indices.push_back(a);
            wom.indices.push_back(c);
            wom.indices.push_back(b);
            wom.indices.push_back(b);
            wom.indices.push_back(c);
            wom.indices.push_back(d);
        }
    };
    // Top hemisphere bands.
    for (int st = 0; st < stacks; ++st) {
        stitch(st * totalVPerRing, (st + 1) * totalVPerRing);
    }
    // Body band: between bodyTopRingStart and bodyBotRingStart.
    stitch(bodyTopRingStart, bodyBotRingStart);
    // Bottom hemisphere bands.
    for (int st = 0; st < stacks; ++st) {
        stitch(botHemiStart + st * totalVPerRing,
                botHemiStart + (st + 1) * totalVPerRing);
    }
    wom.boundMin = glm::vec3(-radius, -totalH * 0.5f, -radius);
    wom.boundMax = glm::vec3( radius,  totalH * 0.5f,  radius);
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-capsule")) return 1;
    printWomWrote(womBase);
    std::printf("  radius     : %.3f\n", radius);
    std::printf("  cylHeight  : %.3f\n", cylHeight);
    std::printf("  total H    : %.3f\n", totalH);
    std::printf("  segments   : %d\n", segments);
    std::printf("  stacks     : %d (per hemisphere)\n", stacks);
    printWomMeshStats(wom);
    return 0;
}

int handleArch(int& i, int argc, char** argv) {
    // Doorway/portal arch: two rectangular columns connected
    // by a semicircular top band. Total width = openingWidth +
    // 2*thickness; total height = openingHeight + thickness +
    // archRadius (where archRadius = openingWidth/2). Depth
    // is the Y-axis thickness (extruded slab).
    //
    // Two box columns + curved arch band on top. Useful for
    // doorways, portal frames, gates. Aligned so the inside
    // of the opening is centered on the Y axis.
    std::string womBase = argv[++i];
    float openingW = 1.0f, openingH = 1.5f;
    float thickness = 0.2f;  // column thickness (X)
    float depth = 0.3f;       // Y extrusion
    int segments = 12;        // arch curve segments
    parseOptFloat(i, argc, argv, openingW);
    parseOptFloat(i, argc, argv, openingH);
    parseOptFloat(i, argc, argv, thickness);
    parseOptFloat(i, argc, argv, depth);
    parseOptInt(i, argc, argv, segments);
    if (openingW <= 0 || openingH <= 0 ||
        thickness <= 0 || depth <= 0 ||
        segments < 2 || segments > 256) {
        std::fprintf(stderr,
            "gen-mesh-arch: dimensions must be positive, segments 2..256\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Helper to push a vertex.
    auto addV = [&](float px, float py, float pz,
                    float nx, float ny, float nz,
                    float u,  float v) -> uint32_t {
        return addVertex(wom, px, py, pz, nx, ny, nz, u, v);
    };
    // Helper to emit an axis-aligned box from min to max.
    auto addBox = [&](glm::vec3 lo, glm::vec3 hi) {
        addFlatBox(wom, lo, hi);
    };
    float halfOW = openingW * 0.5f;
    float halfD = depth * 0.5f;
    // Left column.
    addBox(glm::vec3(-halfOW - thickness, -halfD, 0),
            glm::vec3(-halfOW, halfD, openingH));
    // Right column.
    addBox(glm::vec3(halfOW, -halfD, 0),
            glm::vec3(halfOW + thickness, halfD, openingH));
    // Arch top band: curve from (-halfOW, openingH) through
    // (0, openingH+halfOW) to (halfOW, openingH). Radius =
    // halfOW. Outer surface follows the curve, inner surface
    // is the underside. Built from <segments> bands of 4
    // verts each (front + back faces handled per band).
    float archCenterZ = openingH;
    float archR = halfOW;
    float pi = 3.14159265358979f;
    for (int sg = 0; sg < segments; ++sg) {
        float t0 = static_cast<float>(sg) / segments;
        float t1 = static_cast<float>(sg + 1) / segments;
        float a0 = pi - t0 * pi;  // start at 180°, sweep to 0°
        float a1 = pi - t1 * pi;
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        // Outer ring point at angle a.
        glm::vec3 outer0(archR * c0, 0, archCenterZ + archR * s0);
        glm::vec3 outer1(archR * c1, 0, archCenterZ + archR * s1);
        // The arch band is a flat strip along the curve, no
        // explicit inner ring. Top face of band points radially
        // outward from arch center.
        glm::vec3 n((c0 + c1) * 0.5f, 0, (s0 + s1) * 0.5f);
        n = glm::normalize(n);
        uint32_t base = static_cast<uint32_t>(wom.vertices.size());
        addV(outer0.x, outer0.y - halfD, outer0.z, n.x, 0, n.z, 0, 0);
        addV(outer1.x, outer1.y - halfD, outer1.z, n.x, 0, n.z, 1, 0);
        addV(outer1.x, outer1.y + halfD, outer1.z, n.x, 0, n.z, 1, 1);
        addV(outer0.x, outer0.y + halfD, outer0.z, n.x, 0, n.z, 0, 1);
        wom.indices.push_back(base + 0);
        wom.indices.push_back(base + 1);
        wom.indices.push_back(base + 2);
        wom.indices.push_back(base + 0);
        wom.indices.push_back(base + 2);
        wom.indices.push_back(base + 3);
    }
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-arch")) return 1;
    printWomWrote(womBase);
    std::printf("  opening    : %.3f W × %.3f H\n", openingW, openingH);
    std::printf("  thickness  : %.3f (column), depth %.3f (Y)\n", thickness, depth);
    std::printf("  segments   : %d (arch curve)\n", segments);
    printWomMeshStats(wom);
    std::printf("  bounds     : (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    return 0;
}

int handlePyramid(int& i, int argc, char** argv) {
    // N-sided polygonal pyramid with apex at +Y. 4 sides
    // gives a square pyramid; 3 gives a tetrahedron-like
    // shape; 8+ approaches a cone.
    //
    // Different from --gen-mesh cone: cone has smooth
    // round sides with per-vertex radial-ish normals;
    // pyramid has flat per-face normals on N triangular
    // sides + a flat polygonal base.
    std::string womBase = argv[++i];
    int sides = 4;
    float baseR = 1.0f;
    float height = 1.0f;
    parseOptInt(i, argc, argv, sides);
    parseOptFloat(i, argc, argv, baseR);
    parseOptFloat(i, argc, argv, height);
    if (sides < 3 || sides > 256 || baseR <= 0 || height <= 0) {
        std::fprintf(stderr,
            "gen-mesh-pyramid: sides 3..256, baseR > 0, height > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Build base ring vertices (one per side).
    std::vector<glm::vec3> basePts;
    for (int k = 0; k < sides; ++k) {
        float a = static_cast<float>(k) / sides * 2.0f * pi;
        basePts.push_back(glm::vec3(baseR * std::cos(a), 0,
                                      baseR * std::sin(a)));
    }
    glm::vec3 apex(0, height, 0);
    // Side faces: per-face flat normals (cross of two edges).
    for (int k = 0; k < sides; ++k) {
        glm::vec3 a = basePts[k];
        glm::vec3 b = basePts[(k + 1) % sides];
        glm::vec3 e1 = b - a;
        glm::vec3 e2 = apex - a;
        glm::vec3 n = glm::normalize(glm::cross(e1, e2));
        float u0 = static_cast<float>(k) / sides;
        float u1 = static_cast<float>(k + 1) / sides;
        uint32_t i0 = addV(a, n, glm::vec2(u0, 1));
        uint32_t i1 = addV(b, n, glm::vec2(u1, 1));
        uint32_t i2 = addV(apex, n, glm::vec2(0.5f * (u0 + u1), 0));
        wom.indices.push_back(i0);
        wom.indices.push_back(i1);
        wom.indices.push_back(i2);
    }
    // Base: fan from a center vertex (normal -Y).
    uint32_t baseCenter = addV(glm::vec3(0, 0, 0),
                                 glm::vec3(0, -1, 0),
                                 glm::vec2(0.5f, 0.5f));
    uint32_t baseRingStart = static_cast<uint32_t>(wom.vertices.size());
    for (int k = 0; k < sides; ++k) {
        float a = static_cast<float>(k) / sides * 2.0f * pi;
        addV(basePts[k], glm::vec3(0, -1, 0),
               glm::vec2(0.5f + 0.5f * std::cos(a),
                          0.5f - 0.5f * std::sin(a)));
    }
    for (int k = 0; k < sides; ++k) {
        wom.indices.push_back(baseCenter);
        wom.indices.push_back(baseRingStart + (k + 1) % sides);
        wom.indices.push_back(baseRingStart + k);
    }
    setCenteredBoundsXZ(wom, baseR, baseR, height);
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-pyramid")) return 1;
    printWomWrote(womBase);
    std::printf("  sides     : %d\n", sides);
    std::printf("  base R    : %.3f\n", baseR);
    std::printf("  height    : %.3f\n", height);
    std::printf("  vertices  : %zu (%d side tris × 3 + 1 base center + %d base ring)\n",
                wom.vertices.size(), sides, sides);
    std::printf("  triangles : %zu (%d sides + %d base)\n",
                wom.indices.size() / 3, sides, sides);
    return 0;
}

int handleFence(int& i, int argc, char** argv) {
    // Repeating fence: N square posts along +X spaced
    // <postSpacing> apart, with two horizontal rails (top
    // and bottom) connecting consecutive posts. Posts span
    // from Y=0 up to Y=postHeight; each post is a small box
    // of width = railThick × 2.
    //
    // Useful for fences around plots, pen boundaries,
    // walkway dividers, garden beds.
    std::string womBase = argv[++i];
    int posts = 5;
    float spacing = 1.0f;
    float postH = 1.0f;
    float rt = 0.05f;  // rail/post thickness
    parseOptInt(i, argc, argv, posts);
    parseOptFloat(i, argc, argv, spacing);
    parseOptFloat(i, argc, argv, postH);
    parseOptFloat(i, argc, argv, rt);
    if (posts < 2 || posts > 256 ||
        spacing <= 0 || postH <= 0 || rt <= 0) {
        std::fprintf(stderr,
            "gen-mesh-fence: posts 2..256, spacing/height/thick > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](glm::vec3 lo, glm::vec3 hi) {
        addFlatBox(wom, lo, hi);
    };
    float postHalfW = rt;
    // Posts along +X starting at X=0.
    for (int k = 0; k < posts; ++k) {
        float cx = k * spacing;
        addBox(glm::vec3(cx - postHalfW, -postHalfW, 0),
                glm::vec3(cx + postHalfW,  postHalfW, postH));
    }
    // Rails between consecutive posts. Two rails per gap:
    // top (~80% up) and bottom (~30% up).
    float topRailZ = postH * 0.8f;
    float botRailZ = postH * 0.3f;
    float railHalfH = rt * 0.5f;  // rail is thinner than posts
    for (int k = 0; k + 1 < posts; ++k) {
        float xL = k * spacing + postHalfW;
        float xR = (k + 1) * spacing - postHalfW;
        if (xR <= xL) continue;  // posts touching
        addBox(glm::vec3(xL, -railHalfH, topRailZ - railHalfH),
                glm::vec3(xR,  railHalfH, topRailZ + railHalfH));
        addBox(glm::vec3(xL, -railHalfH, botRailZ - railHalfH),
                glm::vec3(xR,  railHalfH, botRailZ + railHalfH));
    }
    // Bounds.
    wom.boundMin = glm::vec3(-postHalfW, -postHalfW, 0);
    wom.boundMax = glm::vec3((posts - 1) * spacing + postHalfW,
                               postHalfW, postH);
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-fence")) return 1;
    printWomWrote(womBase);
    std::printf("  posts     : %d\n", posts);
    std::printf("  spacing   : %.3f\n", spacing);
    std::printf("  height    : %.3f\n", postH);
    std::printf("  thickness : %.3f\n", rt);
    std::printf("  span X    : %.3f\n", (posts - 1) * spacing);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleTree(int& i, int argc, char** argv) {
    // Procedural tree: cylinder trunk + UV-sphere foliage.
    // Trunk goes from Y=0 up to Y=trunkHeight; foliage sphere
    // centered at trunk-top + foliageRadius/2 so the trunk
    // pokes up into the bottom of the canopy.
    //
    // Useful for ambient zone decoration, distant tree
    // placeholders, magic-grove props. The 15th procedural
    // primitive - pairs naturally with --add-texture-to-mesh
    // for trunk-bark and leaf textures (or just one texture
    // since this is a single-batch mesh).
    std::string womBase = argv[++i];
    float trunkR = 0.1f;
    float trunkH = 2.0f;
    float foliR = 0.7f;
    parseOptFloat(i, argc, argv, trunkR);
    parseOptFloat(i, argc, argv, trunkH);
    parseOptFloat(i, argc, argv, foliR);
    if (trunkR <= 0 || trunkH <= 0 || foliR <= 0) {
        std::fprintf(stderr,
            "gen-mesh-tree: trunkR / trunkH / foliR must be positive\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Trunk cylinder: 12 segments, side ring + top + bottom.
    const int trunkSegs = 12;
    uint32_t trunkSideStart = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= trunkSegs; ++sg) {
        float u = static_cast<float>(sg) / trunkSegs;
        float ang = u * 2.0f * pi;
        float ca = std::cos(ang), sa = std::sin(ang);
        addV(glm::vec3(trunkR * ca, 0, trunkR * sa),
               glm::vec3(ca, 0, sa),
               glm::vec2(u, 0));
        addV(glm::vec3(trunkR * ca, trunkH, trunkR * sa),
               glm::vec3(ca, 0, sa),
               glm::vec2(u, 1));
    }
    for (int sg = 0; sg < trunkSegs; ++sg) {
        uint32_t a = trunkSideStart + sg * 2;
        uint32_t b = a + 1, c = a + 2, d = a + 3;
        wom.indices.push_back(a);
        wom.indices.push_back(c);
        wom.indices.push_back(b);
        wom.indices.push_back(b);
        wom.indices.push_back(c);
        wom.indices.push_back(d);
    }
    // Foliage UV sphere: 12 segments × 8 stacks. Center at
    // (0, trunkH + foliR * 0.7, 0) so the trunk pokes into
    // the bottom of the canopy.
    const int fSegs = 12;
    const int fStacks = 8;
    float foliCY = trunkH + foliR * 0.7f;
    uint32_t foliStart = static_cast<uint32_t>(wom.vertices.size());
    for (int st = 0; st <= fStacks; ++st) {
        float v = static_cast<float>(st) / fStacks;
        float phi = v * pi;
        float sphi = std::sin(phi), cphi = std::cos(phi);
        for (int sg = 0; sg <= fSegs; ++sg) {
            float u = static_cast<float>(sg) / fSegs;
            float theta = u * 2.0f * pi;
            float ctheta = std::cos(theta), stheta = std::sin(theta);
            float nx = sphi * ctheta;
            float ny = cphi;
            float nz = sphi * stheta;
            addV(glm::vec3(foliR * nx, foliCY + foliR * ny, foliR * nz),
                   glm::vec3(nx, ny, nz),
                   glm::vec2(u, v));
        }
    }
    int fStride = fSegs + 1;
    for (int st = 0; st < fStacks; ++st) {
        for (int sg = 0; sg < fSegs; ++sg) {
            uint32_t a = foliStart + st * fStride + sg;
            uint32_t b = a + 1;
            uint32_t c = a + fStride;
            uint32_t d = c + 1;
            wom.indices.push_back(a);
            wom.indices.push_back(c);
            wom.indices.push_back(b);
            wom.indices.push_back(b);
            wom.indices.push_back(c);
            wom.indices.push_back(d);
        }
    }
    setCenteredBoundsXZ(wom, foliR, foliR, foliCY + foliR);
    setModelBounds(wom);
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
    if (!saveWomOrError(wom, womBase, "gen-mesh-tree")) return 1;
    printWomWrote(womBase);
    std::printf("  trunk R   : %.3f\n", trunkR);
    std::printf("  trunk H   : %.3f\n", trunkH);
    std::printf("  foliage R : %.3f\n", foliR);
    std::printf("  total H   : %.3f\n", foliCY + foliR);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleMeshDispatch(int& i, int argc, char** argv) {
    // Synthesize a procedural primitive WOM. Generates proper
    // per-face normals, planar UVs, a bounding box, and a
    // single batch covering all indices so the model renders
    // immediately in the editor without further processing.
    //
    // Shapes:
    //   cube   - 24 verts / 12 tris, axis-aligned, ±size/2
    //   plane  - 4 verts / 2 tris, on XY plane (Z=0), ±size/2
    //   sphere - UV sphere, 16 segments × 12 stacks, radius=size/2
    std::string womBase = argv[++i];
    std::string shape = argv[++i];
    float size = 1.0f;
    parseOptFloat(i, argc, argv, size);
    if (size <= 0.0f) {
        std::fprintf(stderr,
            "gen-mesh: size must be positive (got %g)\n", size);
        return 1;
    }
    // Strip .wom if user passed a full filename - saver expects base.
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Helper to push a vertex with explicit normal + uv.
    auto addVertex = [&](float x, float y, float z,
                          float nx, float ny, float nz,
                          float u, float v) -> uint32_t {
        wowee::pipeline::WoweeModel::Vertex vtx;
        vtx.position = glm::vec3(x, y, z);
        vtx.normal = glm::vec3(nx, ny, nz);
        vtx.texCoord = glm::vec2(u, v);
        wom.vertices.push_back(vtx);
        return static_cast<uint32_t>(wom.vertices.size() - 1);
    };
    std::string s = shape;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    float h = size * 0.5f;
    if (s == "cube") {
        // 6 faces, 4 verts each (so per-face normals are flat).
        struct Face { float nx, ny, nz; float verts[4][3]; };
        Face faces[6] = {
            { 0,  0,  1, {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}}},  // +Z
            { 0,  0, -1, {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}}},  // -Z
            { 1,  0,  0, {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}}},  // +X
            {-1,  0,  0, {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}}},  // -X
            { 0,  1,  0, {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}}},  // +Y
            { 0, -1,  0, {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}}},  // -Y
        };
        float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        for (auto& f : faces) {
            uint32_t base = static_cast<uint32_t>(wom.vertices.size());
            for (int k = 0; k < 4; ++k) {
                addVertex(f.verts[k][0], f.verts[k][1], f.verts[k][2],
                          f.nx, f.ny, f.nz, uvs[k][0], uvs[k][1]);
            }
            wom.indices.push_back(base + 0);
            wom.indices.push_back(base + 1);
            wom.indices.push_back(base + 2);
            wom.indices.push_back(base + 0);
            wom.indices.push_back(base + 2);
            wom.indices.push_back(base + 3);
        }
    } else if (s == "plane") {
        addVertex(-h, -h, 0,  0, 0, 1,  0, 0);
        addVertex( h, -h, 0,  0, 0, 1,  1, 0);
        addVertex( h,  h, 0,  0, 0, 1,  1, 1);
        addVertex(-h,  h, 0,  0, 0, 1,  0, 1);
        wom.indices = {0, 1, 2, 0, 2, 3};
    } else if (s == "sphere") {
        const int segments = 16;
        const int stacks = 12;
        float r = h;
        for (int st = 0; st <= stacks; ++st) {
            float v = static_cast<float>(st) / stacks;
            float phi = v * 3.14159265358979f;
            float sphi = std::sin(phi), cphi = std::cos(phi);
            for (int sg = 0; sg <= segments; ++sg) {
                float u = static_cast<float>(sg) / segments;
                float theta = u * 2.0f * 3.14159265358979f;
                float stheta = std::sin(theta), ctheta = std::cos(theta);
                float nx = sphi * ctheta;
                float ny = sphi * stheta;
                float nz = cphi;
                addVertex(r * nx, r * ny, r * nz, nx, ny, nz, u, v);
            }
        }
        int stride = segments + 1;
        for (int st = 0; st < stacks; ++st) {
            for (int sg = 0; sg < segments; ++sg) {
                uint32_t a = st * stride + sg;
                uint32_t b = a + 1;
                uint32_t c = a + stride;
                uint32_t d = c + 1;
                wom.indices.push_back(a);
                wom.indices.push_back(c);
                wom.indices.push_back(b);
                wom.indices.push_back(b);
                wom.indices.push_back(c);
                wom.indices.push_back(d);
            }
        }
    } else if (s == "cylinder") {
        // Capped cylinder along the Y axis. radius=size/2,
        // height=size. 24 side segments - smooth enough for
        // pillars and torches without exploding the vertex
        // count. UVs: side wraps the texture once around;
        // caps map [0..1] from a square sampled at the disc.
        const int segments = 24;
        float r = h;
        // Side ring: 2 vertex rows (top, bottom), each with
        // (segments+1) verts so UV-seam doesn't share verts.
        for (int sg = 0; sg <= segments; ++sg) {
            float u = static_cast<float>(sg) / segments;
            float ang = u * 2.0f * 3.14159265358979f;
            float ca = std::cos(ang), sa = std::sin(ang);
            // Bottom ring (Y = -h).
            addVertex(r * ca, -h, r * sa, ca, 0, sa, u, 0);
            // Top ring (Y = +h).
            addVertex(r * ca,  h, r * sa, ca, 0, sa, u, 1);
        }
        // Side quad indices.
        for (int sg = 0; sg < segments; ++sg) {
            uint32_t a = sg * 2;
            uint32_t b = a + 1;
            uint32_t c = a + 2;
            uint32_t d = a + 3;
            wom.indices.push_back(a);
            wom.indices.push_back(c);
            wom.indices.push_back(b);
            wom.indices.push_back(b);
            wom.indices.push_back(c);
            wom.indices.push_back(d);
        }
        // Top cap fan.
        uint32_t topCenter = static_cast<uint32_t>(wom.vertices.size());
        addVertex(0, h, 0, 0, 1, 0, 0.5f, 0.5f);
        uint32_t topRingStart = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segments; ++sg) {
            float u = static_cast<float>(sg) / segments;
            float ang = u * 2.0f * 3.14159265358979f;
            float ca = std::cos(ang), sa = std::sin(ang);
            addVertex(r * ca, h, r * sa, 0, 1, 0,
                       0.5f + 0.5f * ca, 0.5f + 0.5f * sa);
        }
        for (int sg = 0; sg < segments; ++sg) {
            wom.indices.push_back(topCenter);
            wom.indices.push_back(topRingStart + sg);
            wom.indices.push_back(topRingStart + sg + 1);
        }
        // Bottom cap fan (winding flipped so normal points -Y).
        uint32_t botCenter = static_cast<uint32_t>(wom.vertices.size());
        addVertex(0, -h, 0, 0, -1, 0, 0.5f, 0.5f);
        uint32_t botRingStart = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segments; ++sg) {
            float u = static_cast<float>(sg) / segments;
            float ang = u * 2.0f * 3.14159265358979f;
            float ca = std::cos(ang), sa = std::sin(ang);
            addVertex(r * ca, -h, r * sa, 0, -1, 0,
                       0.5f + 0.5f * ca, 0.5f - 0.5f * sa);
        }
        for (int sg = 0; sg < segments; ++sg) {
            wom.indices.push_back(botCenter);
            wom.indices.push_back(botRingStart + sg + 1);
            wom.indices.push_back(botRingStart + sg);
        }
    } else if (s == "torus") {
        // Torus around the Y axis. Major radius (ring center
        // distance from origin) = size/2, minor radius (tube
        // thickness) = size/8 - the 4:1 ratio reads as a
        // ring rather than a fat donut. 32 ring segments × 16
        // tube segments = ~544 verts / ~1024 tris.
        const int ringSeg = 32;
        const int tubeSeg = 16;
        float R = h;          // major radius
        float r = h * 0.25f;  // minor radius (h/4)
        for (int i2 = 0; i2 <= ringSeg; ++i2) {
            float u = static_cast<float>(i2) / ringSeg;
            float theta = u * 2.0f * 3.14159265358979f;
            float ct = std::cos(theta), st = std::sin(theta);
            for (int j2 = 0; j2 <= tubeSeg; ++j2) {
                float v = static_cast<float>(j2) / tubeSeg;
                float phi = v * 2.0f * 3.14159265358979f;
                float cp = std::cos(phi), sp = std::sin(phi);
                // Position on the surface.
                float x = (R + r * cp) * ct;
                float y = r * sp;
                float z = (R + r * cp) * st;
                // Normal: from the tube center outward.
                float nx = cp * ct;
                float ny = sp;
                float nz = cp * st;
                addVertex(x, y, z, nx, ny, nz, u, v);
            }
        }
        int stride = tubeSeg + 1;
        for (int i2 = 0; i2 < ringSeg; ++i2) {
            for (int j2 = 0; j2 < tubeSeg; ++j2) {
                uint32_t a = i2 * stride + j2;
                uint32_t b = a + 1;
                uint32_t c = a + stride;
                uint32_t d = c + 1;
                wom.indices.push_back(a);
                wom.indices.push_back(c);
                wom.indices.push_back(b);
                wom.indices.push_back(b);
                wom.indices.push_back(c);
                wom.indices.push_back(d);
            }
        }
    } else if (s == "cone") {
        // Cone with apex at +Y. radius=size/2, height=size.
        // 24 side segments. Side has smooth radial-ish normals
        // (slanted up by half the slope angle) for a curved
        // shaded surface; bottom cap has flat -Y normal.
        const int segments = 24;
        float r = h;
        float H = size;
        // Slant length used for the side normal Y component.
        // Side normal direction: (cos(a), nyComponent, sin(a))
        // where the slope is r/H per unit of horizontal travel.
        // Normalize so the normal has unit length.
        float sideXZScale = H / std::sqrt(H * H + r * r);
        float sideY = r / std::sqrt(H * H + r * r);
        // Side ring (apex repeated per segment so each tri has
        // its own apex vertex with the correct normal).
        for (int sg = 0; sg <= segments; ++sg) {
            float u = static_cast<float>(sg) / segments;
            float ang = u * 2.0f * 3.14159265358979f;
            float ca = std::cos(ang), sa = std::sin(ang);
            // Base vertex (Y = 0).
            addVertex(r * ca, 0.0f, r * sa,
                       sideXZScale * ca, sideY, sideXZScale * sa,
                       u, 1.0f);
            // Apex vertex (Y = H), one per ring step so the
            // top vertex carries the segment-specific normal.
            addVertex(0.0f, H, 0.0f,
                       sideXZScale * ca, sideY, sideXZScale * sa,
                       u, 0.0f);
        }
        // Side triangle indices.
        for (int sg = 0; sg < segments; ++sg) {
            uint32_t base = sg * 2;
            // Two tris per quad band. The apex collapses to a
            // point, so really one triangle per segment, but
            // emitting both keeps the indexing uniform across
            // the cylinder/cone code paths.
            uint32_t a = base + 0;     // base k
            uint32_t b = base + 1;     // apex k
            uint32_t c = base + 2;     // base k+1
            uint32_t d = base + 3;     // apex k+1
            wom.indices.push_back(a);
            wom.indices.push_back(c);
            wom.indices.push_back(b);
            // Second triangle would be (b,c,d) but b == d at
            // the apex visually - we still emit it so the
            // per-vertex normals on b and d shade the joining
            // seam smoothly.
            wom.indices.push_back(b);
            wom.indices.push_back(c);
            wom.indices.push_back(d);
        }
        // Bottom cap fan (flat -Y normal).
        uint32_t botCenter = static_cast<uint32_t>(wom.vertices.size());
        addVertex(0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f);
        uint32_t botRingStart = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segments; ++sg) {
            float u = static_cast<float>(sg) / segments;
            float ang = u * 2.0f * 3.14159265358979f;
            float ca = std::cos(ang), sa = std::sin(ang);
            addVertex(r * ca, 0.0f, r * sa, 0.0f, -1.0f, 0.0f,
                       0.5f + 0.5f * ca, 0.5f - 0.5f * sa);
        }
        for (int sg = 0; sg < segments; ++sg) {
            wom.indices.push_back(botCenter);
            wom.indices.push_back(botRingStart + sg + 1);
            wom.indices.push_back(botRingStart + sg);
        }
    } else if (s == "ramp") {
        // Right-triangular prism: a wedge that climbs along
        // +X. Footprint is size×size on XY (centered on origin
        // in X, Y from 0 to size); rises from Z=0 at -X to
        // Z=size at +X. Useful for ramps onto platforms,
        // simple roof slopes, cliff faces.
        //
        // 6 verts × 5 faces = 18 verts so per-face normals
        // stay flat: top slope, bottom, back-tall, +Y side,
        // -Y side. Front-short (X = -size/2) is open since
        // the ramp meets ground there at zero height.
        // Actually we still emit 5 faces - the "front" edge
        // is just where slope and ground meet, no separate
        // face needed.
        float xMin = -h, xMax = h;
        float yMin = 0,  yMax = size;
        float zMin = 0,  zMax = size;
        // Faces: top slope (normal = normalize(-1,0,1) since
        // the slope rises with +X going up, normal points
        // up-and-back).
        float slopeLen = std::sqrt(size * size + size * size);
        float nSlopeX = -size / slopeLen;
        float nSlopeZ =  size / slopeLen;
        struct Face { float nx, ny, nz; float verts[4][3]; };
        Face faces[5] = {
            // Top sloped quad: from (xMin, yMin, zMin) up to
            // (xMax, yMin/yMax, zMax)
            { nSlopeX, 0, nSlopeZ,
               {{xMin, yMin, zMin},{xMin, yMax, zMin},
                {xMax, yMax, zMax},{xMax, yMin, zMax}}},
            // Bottom (-Z normal)
            { 0, 0, -1,
               {{xMin, yMin, zMin},{xMax, yMin, zMin},
                {xMax, yMax, zMin},{xMin, yMax, zMin}}},
            // Back-tall vertical wall (+X)
            { 1, 0, 0,
               {{xMax, yMin, zMin},{xMax, yMin, zMax},
                {xMax, yMax, zMax},{xMax, yMax, zMin}}},
            // -Y side triangle (degenerate quad - last 2 verts
            // collapse to a point - but indexing uniformly is
            // simpler than a special tri path)
            { 0, -1, 0,
               {{xMin, yMin, zMin},{xMax, yMin, zMin},
                {xMax, yMin, zMax},{xMax, yMin, zMax}}},
            // +Y side triangle (same shape mirrored)
            { 0, 1, 0,
               {{xMin, yMax, zMin},{xMax, yMax, zMax},
                {xMax, yMax, zMin},{xMax, yMax, zMin}}},
        };
        float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        for (auto& f : faces) {
            uint32_t base = static_cast<uint32_t>(wom.vertices.size());
            for (int k = 0; k < 4; ++k) {
                addVertex(f.verts[k][0], f.verts[k][1], f.verts[k][2],
                           f.nx, f.ny, f.nz, uvs[k][0], uvs[k][1]);
            }
            wom.indices.push_back(base + 0);
            wom.indices.push_back(base + 1);
            wom.indices.push_back(base + 2);
            wom.indices.push_back(base + 0);
            wom.indices.push_back(base + 2);
            wom.indices.push_back(base + 3);
        }
    } else {
        std::fprintf(stderr,
            "gen-mesh: shape must be cube, plane, sphere, cylinder, torus, cone, or ramp (got '%s')\n",
            shape.c_str());
        return 1;
    }
    // Compute bounds from the vertex positions we just emitted.
    setModelBounds(wom);
    // Single material batch covering everything - keeps the
    // model immediately renderable.
    wowee::pipeline::WoweeModel::Batch b;
    b.indexStart = 0;
    b.indexCount = static_cast<uint32_t>(wom.indices.size());
    b.textureIndex = 0;
    b.blendMode = 0;
    b.flags = 0;
    wom.batches.push_back(b);
    // Empty texture path slot so batch.textureIndex=0 is a
    // valid index into texturePaths. The user can later set a
    // real path or run --gen-texture next to it.
    wom.texturePaths.push_back("");
    std::filesystem::path womPath(womBase);
    ensureParentDirectory(womPath);
    if (!saveWomOrError(wom, womBase, "gen-mesh")) return 1;
    printWomWrote(womBase);
    std::printf("  shape    : %s\n", s.c_str());
    std::printf("  size     : %.3f\n", size);
    std::printf("  vertices : %zu\n", wom.vertices.size());
    std::printf("  indices  : %zu (%zu tri%s)\n",
                wom.indices.size(), wom.indices.size() / 3,
                wom.indices.size() / 3 == 1 ? "" : "s");
    std::printf("  bounds   : (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
                wom.boundMin.x, wom.boundMin.y, wom.boundMin.z,
                wom.boundMax.x, wom.boundMax.y, wom.boundMax.z);
    return 0;
}

int handleTextured(int& i, int argc, char** argv) {
    // One-shot composer: --gen-mesh + --gen-texture wired
    // together so the resulting WOM's texturePaths[0] points
    // at the freshly-written PNG sidecar. Output is a model
    // that renders with the synthesized texture out of the
    // box - useful for prototyping textured props without
    // chaining three commands by hand.
    //
    // The texture is written next to the mesh as
    //   <wom-base>.png
    // and the WOM's texturePaths[0] is set to that filename
    // (just the leaf - runtime resolves it relative to the
    // model's own directory).
    std::string womBase = argv[++i];
    std::string shape = argv[++i];
    std::string colorSpec = argv[++i];
    std::string sizeArg;
    if (i + 1 < argc && argv[i + 1][0] != '-') sizeArg = argv[++i];
    // Strip .wom if user passed full filename.
    stripExt(womBase, ".wom");
    std::string self = argv[0];
    // 1) Mesh.
    std::string meshCmd = "\"" + self + "\" --gen-mesh \"" + womBase +
                           "\" " + shape;
    if (!sizeArg.empty()) meshCmd += " " + sizeArg;
    meshCmd += " >/dev/null 2>&1";
    int rc = std::system(meshCmd.c_str());
    if (rc != 0) {
        std::fprintf(stderr,
            "gen-mesh-textured: gen-mesh step failed (rc=%d)\n", rc);
        return 1;
    }
    // 2) Texture as a PNG sidecar at the mesh's base path.
    std::string pngPath = womBase + ".png";
    std::string texCmd = "\"" + self + "\" --gen-texture \"" + pngPath +
                          "\" \"" + colorSpec + "\" 256 256";
    texCmd += " >/dev/null 2>&1";
    rc = std::system(texCmd.c_str());
    if (rc != 0) {
        std::fprintf(stderr,
            "gen-mesh-textured: gen-texture step failed (rc=%d)\n", rc);
        return 1;
    }
    // 3) Load the WOM, set texturePaths[0] to the PNG leaf,
    //    and re-save so the binding is permanent.
    auto wom = wowee::pipeline::WoweeModelLoader::load(womBase);
    if (!wom.isValid()) {
        std::fprintf(stderr,
            "gen-mesh-textured: cannot load %s.wom after gen-mesh\n",
            womBase.c_str());
        return 1;
    }
    std::string pngLeaf = std::filesystem::path(pngPath).filename().string();
    if (wom.texturePaths.empty()) {
        wom.texturePaths.push_back(pngLeaf);
    } else {
        wom.texturePaths[0] = pngLeaf;
    }
    if (!saveWomOrError(wom, womBase, "gen-mesh-textured")) return 1;
    std::printf("Wrote %s.wom + %s\n", womBase.c_str(), pngPath.c_str());
    std::printf("  shape    : %s\n", shape.c_str());
    std::printf("  color    : %s\n", colorSpec.c_str());
    std::printf("  vertices : %zu\n", wom.vertices.size());
    std::printf("  texture  : %s (wired into batch 0)\n", pngLeaf.c_str());
    return 0;
}

int handleMushroom(int& i, int argc, char** argv) {
    // Mushroom: cylindrical stalk + UV-sphere top half (cap).
    // Cap radius is independent so users get the classic
    // narrow-stalk-wide-cap silhouette of a forest mushroom.
    // The 29th procedural mesh primitive.
    std::string womBase = argv[++i];
    float stalkR = 0.1f;
    float stalkH = 0.6f;
    float capR = 0.4f;
    parseOptFloat(i, argc, argv, stalkR);
    parseOptFloat(i, argc, argv, stalkH);
    parseOptFloat(i, argc, argv, capR);
    if (stalkR <= 0 || stalkH <= 0 || capR <= 0) {
        std::fprintf(stderr,
            "gen-mesh-mushroom: all dims must be positive\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Stalk: 12-segment cylinder from y=0 to y=stalkH.
    const int segs = 12;
    uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segs; ++sg) {
        float u = static_cast<float>(sg) / segs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(stalkR * std::cos(ang), 0, stalkR * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, {u, 0});
    }
    uint32_t top = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= segs; ++sg) {
        float u = static_cast<float>(sg) / segs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(stalkR * std::cos(ang), stalkH,
                    stalkR * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, {u, 1});
    }
    for (int sg = 0; sg < segs; ++sg) {
        wom.indices.insert(wom.indices.end(), {
            bot + sg, top + sg, bot + sg + 1,
            bot + sg + 1, top + sg, top + sg + 1
        });
    }
    // Bottom cap (faces -Y) so the stalk is closed
    uint32_t bc = addV({0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f});
    for (int sg = 0; sg < segs; ++sg) {
        wom.indices.insert(wom.indices.end(),
            {bc, bot + sg + 1, bot + sg});
    }
    // Cap: top half of UV sphere centered at (0, stalkH, 0).
    // Latitude 0..pi/2 (top hemisphere only). 16 longitude × 8
    // latitude segments.
    const int capLon = 16;
    const int capLat = 8;
    uint32_t capStart = static_cast<uint32_t>(wom.vertices.size());
    for (int la = 0; la <= capLat; ++la) {
        float v = static_cast<float>(la) / capLat;
        float phi = (1.0f - v) * pi * 0.5f;  // pi/2 down to 0
        float sphi = std::sin(phi), cphi = std::cos(phi);
        for (int lo = 0; lo <= capLon; ++lo) {
            float u = static_cast<float>(lo) / capLon;
            float theta = u * 2.0f * pi;
            glm::vec3 dir(cphi * std::cos(theta),
                          sphi,
                          cphi * std::sin(theta));
            glm::vec3 p(dir.x * capR, stalkH + dir.y * capR,
                        dir.z * capR);
            addV(p, dir, {u, v});
        }
    }
    int rowSize = capLon + 1;
    for (int la = 0; la < capLat; ++la) {
        for (int lo = 0; lo < capLon; ++lo) {
            uint32_t i00 = capStart + la * rowSize + lo;
            uint32_t i01 = capStart + la * rowSize + lo + 1;
            uint32_t i10 = capStart + (la + 1) * rowSize + lo;
            uint32_t i11 = capStart + (la + 1) * rowSize + lo + 1;
            wom.indices.insert(wom.indices.end(),
                {i00, i10, i01, i01, i10, i11});
        }
    }
    // Underside of cap (the "gills" disc, faces -Y) so the
    // mushroom is watertight viewed from below.
    uint32_t capBot = addV({0, stalkH, 0}, {0, -1, 0}, {0.5f, 0.5f});
    for (int sg = 0; sg < capLon; ++sg) {
        uint32_t edge0 = capStart + capLat * rowSize + sg;
        uint32_t edge1 = capStart + capLat * rowSize + sg + 1;
        wom.indices.insert(wom.indices.end(),
            {capBot, edge1, edge0});
    }
    finalizeAsSingleBatch(wom);
    float maxY = stalkH + capR;
    setCenteredBoundsXZ(wom, capR, capR, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-mushroom")) return 1;
    printWomWrote(womBase);
    std::printf("  stalk     : R=%.3f H=%.3f\n", stalkR, stalkH);
    std::printf("  cap       : R=%.3f\n", capR);
    std::printf("  total H   : %.3f\n", maxY);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleCart(int& i, int argc, char** argv) {
    // Wooden cart: rectangular bed box + 2 cylindrical wheels
    // mounted axis-along-Z on the sides at the bottom of the
    // bed. Wheels are full cylinders (16-segment) so the round
    // silhouette reads from any angle. The 30th procedural mesh
    // primitive.
    std::string womBase = argv[++i];
    float bedLen = 1.6f;     // along X (cart length)
    float bedWidth = 0.8f;   // along Z
    float bedH = 0.5f;       // bed height (Y)
    float wheelR = 0.35f;    // wheel radius
    parseOptFloat(i, argc, argv, bedLen);
    parseOptFloat(i, argc, argv, bedWidth);
    parseOptFloat(i, argc, argv, bedH);
    parseOptFloat(i, argc, argv, wheelR);
    if (bedLen <= 0 || bedWidth <= 0 || bedH <= 0 || wheelR <= 0) {
        std::fprintf(stderr,
            "gen-mesh-cart: all dims must be positive\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Bed sits at y = wheelR (so wheels touch ground at y=0)
    // up to y = wheelR + bedH.
    float bedY = wheelR + bedH * 0.5f;
    addBox(0, bedY, 0, bedLen * 0.5f, bedH * 0.5f, bedWidth * 0.5f);
    // Wheels: cylinder with axis along Z, mounted on each side
    // of the bed. Each wheel has 16 angular segments + 2 caps.
    const int wheelSegs = 16;
    float wheelThick = bedWidth * 0.08f;  // wheel thickness (along Z)
    float wheelOffsetZ = bedWidth * 0.5f + wheelThick * 0.5f;
    auto addWheel = [&](float cz) {
        // Front face (z = cz + wheelThick/2)
        float zFront = cz + wheelThick * 0.5f;
        float zBack = cz - wheelThick * 0.5f;
        uint32_t frontStart = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= wheelSegs; ++sg) {
            float u = static_cast<float>(sg) / wheelSegs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(wheelR * std::cos(ang), wheelR + wheelR * std::sin(ang), zFront);
            addV(p, {0, 0, 1}, {0.5f + 0.5f * std::cos(ang),
                                 0.5f + 0.5f * std::sin(ang)});
        }
        uint32_t backStart = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= wheelSegs; ++sg) {
            float u = static_cast<float>(sg) / wheelSegs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(wheelR * std::cos(ang), wheelR + wheelR * std::sin(ang), zBack);
            addV(p, {0, 0, -1}, {0.5f + 0.5f * std::cos(ang),
                                  0.5f + 0.5f * std::sin(ang)});
        }
        // Front cap fan
        uint32_t fc = addV({0, wheelR, zFront}, {0, 0, 1}, {0.5f, 0.5f});
        for (int sg = 0; sg < wheelSegs; ++sg) {
            wom.indices.insert(wom.indices.end(),
                {fc, frontStart + sg, frontStart + sg + 1});
        }
        // Back cap fan (reversed winding)
        uint32_t bc = addV({0, wheelR, zBack}, {0, 0, -1}, {0.5f, 0.5f});
        for (int sg = 0; sg < wheelSegs; ++sg) {
            wom.indices.insert(wom.indices.end(),
                {bc, backStart + sg + 1, backStart + sg});
        }
        // Side ring: connect each pair of front/back rim verts
        // with a quad. Side normals point outward radially.
        for (int sg = 0; sg < wheelSegs; ++sg) {
            float u = static_cast<float>(sg) / wheelSegs;
            float ang = u * 2.0f * pi;
            float u2 = static_cast<float>(sg + 1) / wheelSegs;
            float ang2 = u2 * 2.0f * pi;
            glm::vec3 n0(std::cos(ang), std::sin(ang), 0);
            glm::vec3 n1(std::cos(ang2), std::sin(ang2), 0);
            uint32_t a = addV({wheelR * std::cos(ang), wheelR + wheelR * std::sin(ang), zFront}, n0, {u, 0});
            uint32_t b = addV({wheelR * std::cos(ang), wheelR + wheelR * std::sin(ang), zBack}, n0, {u, 1});
            uint32_t c = addV({wheelR * std::cos(ang2), wheelR + wheelR * std::sin(ang2), zBack}, n1, {u2, 1});
            uint32_t d = addV({wheelR * std::cos(ang2), wheelR + wheelR * std::sin(ang2), zFront}, n1, {u2, 0});
            wom.indices.insert(wom.indices.end(),
                {a, b, c, a, c, d});
        }
    };
    addWheel( wheelOffsetZ);
    addWheel(-wheelOffsetZ);
    finalizeAsSingleBatch(wom);
    float maxY = wheelR + bedH;
    float maxZ = wheelOffsetZ + wheelThick * 0.5f;
    wom.boundMin = glm::vec3(-bedLen * 0.5f, 0, -maxZ);
    wom.boundMax = glm::vec3( bedLen * 0.5f, std::max(maxY, 2 * wheelR), maxZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-cart")) return 1;
    printWomWrote(womBase);
    std::printf("  bed       : %.3f × %.3f × %.3f\n",
                bedLen, bedWidth, bedH);
    std::printf("  wheels    : 2 × R=%.3f thickness=%.3f\n",
                wheelR, wheelThick);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleBanner(int& i, int argc, char** argv) {
    // Banner: vertical pole + rectangular flag hanging off it.
    // Pole is a 12-segment cylinder along Y. Flag is a flat
    // rectangle attached at the top of the pole, draped along
    // -Z. Flag has both front (+X) and back (-X) faces so it
    // reads from any viewing angle. The 31st mesh primitive.
    std::string womBase = argv[++i];
    float poleH = 3.0f;
    float poleR = 0.05f;
    float flagW = 0.8f;       // along -Z (drape direction)
    float flagH = 1.2f;       // along Y (down from top)
    parseOptFloat(i, argc, argv, poleH);
    parseOptFloat(i, argc, argv, poleR);
    parseOptFloat(i, argc, argv, flagW);
    parseOptFloat(i, argc, argv, flagH);
    if (poleH <= 0 || poleR <= 0 || flagW <= 0 || flagH <= 0 ||
        flagH > poleH) {
        std::fprintf(stderr,
            "gen-mesh-banner: all dims > 0; flagH must be <= poleH\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    // Pole cylinder (12 segments)
    const int poleSegs = 12;
    uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= poleSegs; ++sg) {
        float u = static_cast<float>(sg) / poleSegs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(poleR * std::cos(ang), 0, poleR * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, {u, 0});
    }
    uint32_t top = static_cast<uint32_t>(wom.vertices.size());
    for (int sg = 0; sg <= poleSegs; ++sg) {
        float u = static_cast<float>(sg) / poleSegs;
        float ang = u * 2.0f * pi;
        glm::vec3 p(poleR * std::cos(ang), poleH, poleR * std::sin(ang));
        glm::vec3 n(std::cos(ang), 0, std::sin(ang));
        addV(p, n, {u, 1});
    }
    for (int sg = 0; sg < poleSegs; ++sg) {
        wom.indices.insert(wom.indices.end(), {
            bot + sg, top + sg, bot + sg + 1,
            bot + sg + 1, top + sg, top + sg + 1
        });
    }
    // Pole top + bottom caps
    uint32_t bc = addV({0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f});
    uint32_t tc = addV({0, poleH, 0}, {0, 1, 0}, {0.5f, 0.5f});
    for (int sg = 0; sg < poleSegs; ++sg) {
        wom.indices.insert(wom.indices.end(),
            {bc, bot + sg + 1, bot + sg});
        wom.indices.insert(wom.indices.end(),
            {tc, top + sg, top + sg + 1});
    }
    // Flag: rectangle from (poleR, poleH-flagH, 0) to
    // (poleR, poleH, -flagW). Two faces (front +X, back -X)
    // so it reads from both sides.
    float fy0 = poleH - flagH;
    float fy1 = poleH;
    float fz0 = 0;
    float fz1 = -flagW;
    float fx = poleR;
    glm::vec3 frontN(1, 0, 0);
    glm::vec3 backN(-1, 0, 0);
    // Front face (faces +X, looking at it from outside)
    uint32_t fa = addV({fx, fy0, fz0}, frontN, {0, 0});
    uint32_t fb = addV({fx, fy0, fz1}, frontN, {1, 0});
    uint32_t fc_ = addV({fx, fy1, fz1}, frontN, {1, 1});
    uint32_t fd = addV({fx, fy1, fz0}, frontN, {0, 1});
    wom.indices.insert(wom.indices.end(), {fa, fb, fc_, fa, fc_, fd});
    // Back face (faces -X)
    uint32_t ba = addV({fx, fy0, fz0}, backN, {0, 0});
    uint32_t bb = addV({fx, fy1, fz0}, backN, {0, 1});
    uint32_t bc_v = addV({fx, fy1, fz1}, backN, {1, 1});
    uint32_t bd = addV({fx, fy0, fz1}, backN, {1, 0});
    wom.indices.insert(wom.indices.end(), {ba, bb, bc_v, ba, bc_v, bd});
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-poleR, 0, fz1);
    wom.boundMax = glm::vec3(fx + poleR, poleH, poleR);
    if (!saveWomOrError(wom, womBase, "gen-mesh-banner")) return 1;
    printWomWrote(womBase);
    std::printf("  pole       : R=%.3f H=%.3f\n", poleR, poleH);
    std::printf("  flag       : W=%.3f H=%.3f (drapes -Z)\n",
                flagW, flagH);
    printWomMeshStats(wom);
    return 0;
}

int handleGrave(int& i, int argc, char** argv) {
    // Tombstone: low rectangular base + vertical tablet on top.
    // Tablet sits centered on the base; base is wider so the
    // grave reads with a stable foundation. The 32nd procedural
    // mesh primitive - useful for graveyards, undead zones,
    // memorial set dressing.
    std::string womBase = argv[++i];
    float tabletW = 0.6f;     // along X
    float tabletH = 1.0f;     // along Y
    float tabletT = 0.15f;    // along Z (thickness)
    float baseW = 0.8f;       // base wider than tablet
    parseOptFloat(i, argc, argv, tabletW);
    parseOptFloat(i, argc, argv, tabletH);
    parseOptFloat(i, argc, argv, tabletT);
    parseOptFloat(i, argc, argv, baseW);
    if (tabletW <= 0 || tabletH <= 0 || tabletT <= 0 || baseW <= 0 ||
        baseW < tabletW) {
        std::fprintf(stderr,
            "gen-mesh-grave: all dims > 0; baseW must be >= tabletW\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Base: wider, lower. Sits at y=0 to baseH where baseH = 20% of tablet H.
    float baseH = tabletH * 0.2f;
    float baseDepth = tabletT * 1.5f;  // deeper than tablet for stability
    addBox(0, baseH * 0.5f, 0,
           baseW * 0.5f, baseH * 0.5f, baseDepth * 0.5f);
    // Tablet: sits on top of base, centered.
    float tabletY = baseH + tabletH * 0.5f;
    addBox(0, tabletY, 0,
           tabletW * 0.5f, tabletH * 0.5f, tabletT * 0.5f);
    finalizeAsSingleBatch(wom);
    float maxY = baseH + tabletH;
    float maxXZ = std::max(baseW * 0.5f, tabletW * 0.5f);
    setCenteredBoundsXZ(wom, maxXZ, baseDepth * 0.5f, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-grave")) return 1;
    printWomWrote(womBase);
    std::printf("  base       : %.3f × %.3f (h=%.3f)\n",
                baseW, baseDepth, baseH);
    std::printf("  tablet     : %.3f × %.3f × %.3f\n",
                tabletW, tabletH, tabletT);
    std::printf("  total H    : %.3f\n", maxY);
    printWomMeshStats(wom);
    return 0;
}

int handleBench(int& i, int argc, char** argv) {
    // Wooden bench: long thin seat plank (X×Z plane) supported
    // by 2 leg slabs (vertical Y rectangles) at each end. Legs
    // are 90% of the bench's depth and span the full seat
    // height down to the floor. The 33rd procedural mesh
    // primitive - useful for taverns, plazas, roadside rest
    // stops.
    std::string womBase = argv[++i];
    float length = 1.5f;       // along X (bench length)
    float seatY = 0.5f;        // seat top height
    float seatT = 0.06f;       // seat plank thickness (Y)
    float seatW = 0.4f;        // seat width (Z)
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, seatY);
    parseOptFloat(i, argc, argv, seatT);
    parseOptFloat(i, argc, argv, seatW);
    if (length <= 0 || seatY <= 0 || seatT <= 0 || seatW <= 0 ||
        seatT > seatY) {
        std::fprintf(stderr,
            "gen-mesh-bench: all dims > 0; seatT must be <= seatY\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Seat: top plank at y=seatY-seatT to y=seatY.
    float seatCY = seatY - seatT * 0.5f;
    addBox(0, seatCY, 0, length * 0.5f, seatT * 0.5f, seatW * 0.5f);
    // Two leg slabs: thin Y slabs at the +X and -X ends, span
    // 90% of the seat depth, 5% of bench length thick, full
    // height from floor to bottom-of-seat.
    float legHy = (seatY - seatT) * 0.5f;
    float legCY = legHy;
    float legHx = length * 0.025f;     // ~2.5% of length on each side
    float legHz = seatW * 0.45f;
    float legX = length * 0.45f;       // legs at 90% of length out
    addBox( legX, legCY, 0, legHx, legHy, legHz);
    addBox(-legX, legCY, 0, legHx, legHy, legHz);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, length * 0.5f, seatW * 0.5f, seatY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-bench")) return 1;
    printWomWrote(womBase);
    std::printf("  length    : %.3f\n", length);
    std::printf("  seat Y    : %.3f (thickness %.3f)\n", seatY, seatT);
    std::printf("  seat W    : %.3f\n", seatW);
    std::printf("  legs      : 2 (at ±%.3f along X)\n", legX);
    std::printf("  vertices  : %zu\n", wom.vertices.size());
    std::printf("  triangles : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleShrine(int& i, int argc, char** argv) {
    // Small open canopy: square base + 4 cylindrical pillars
    // at the corners + a flat roof slab covering all 4. Useful
    // for wayside shrines, gazebos, well covers, market stalls.
    // The 34th procedural mesh primitive.
    std::string womBase = argv[++i];
    float size = 1.5f;          // base width = depth
    float pillarH = 2.0f;       // pillar height
    float pillarR = 0.10f;      // pillar radius
    float roofT = 0.15f;        // roof thickness
    parseOptFloat(i, argc, argv, size);
    parseOptFloat(i, argc, argv, pillarH);
    parseOptFloat(i, argc, argv, pillarR);
    parseOptFloat(i, argc, argv, roofT);
    if (size <= 0 || pillarH <= 0 || pillarR <= 0 || roofT <= 0 ||
        pillarR * 2 >= size) {
        std::fprintf(stderr,
            "gen-mesh-shrine: dims > 0; pillarR×2 must fit inside size\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float pi = 3.14159265358979f;
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Base: low square slab, 10% of pillar height tall.
    float baseH = pillarH * 0.1f;
    float halfSize = size * 0.5f;
    addBox(0, baseH * 0.5f, 0, halfSize, baseH * 0.5f, halfSize);
    // 4 pillars at corners (inset by pillarR so they sit fully
    // on the base). Each is a 12-segment cylinder.
    const int segs = 12;
    float pillarOffset = halfSize - pillarR;
    auto addPillar = [&](float cx, float cz) {
        float y0 = baseH;
        float y1 = baseH + pillarH;
        uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segs; ++sg) {
            float u = static_cast<float>(sg) / segs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(cx + pillarR * std::cos(ang), y0,
                        cz + pillarR * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, {u, 0});
        }
        uint32_t top = static_cast<uint32_t>(wom.vertices.size());
        for (int sg = 0; sg <= segs; ++sg) {
            float u = static_cast<float>(sg) / segs;
            float ang = u * 2.0f * pi;
            glm::vec3 p(cx + pillarR * std::cos(ang), y1,
                        cz + pillarR * std::sin(ang));
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV(p, n, {u, 1});
        }
        for (int sg = 0; sg < segs; ++sg) {
            wom.indices.insert(wom.indices.end(), {
                bot + sg, top + sg, bot + sg + 1,
                bot + sg + 1, top + sg, top + sg + 1
            });
        }
    };
    addPillar( pillarOffset,  pillarOffset);
    addPillar(-pillarOffset,  pillarOffset);
    addPillar( pillarOffset, -pillarOffset);
    addPillar(-pillarOffset, -pillarOffset);
    // Roof: flat slab on top of pillars, slightly larger than
    // the base so it overhangs the pillars.
    float roofY = baseH + pillarH;
    float roofHalfSize = halfSize * 1.05f;
    addBox(0, roofY + roofT * 0.5f, 0,
           roofHalfSize, roofT * 0.5f, roofHalfSize);
    finalizeAsSingleBatch(wom);
    float maxY = roofY + roofT;
    setCenteredBoundsXZ(wom, roofHalfSize, roofHalfSize, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-shrine")) return 1;
    printWomWrote(womBase);
    std::printf("  size       : %.3f × %.3f\n", size, size);
    std::printf("  pillars    : 4 × R=%.3f H=%.3f\n", pillarR, pillarH);
    std::printf("  roof       : %.3f thick (%.3f overhang)\n",
                roofT, halfSize * 0.05f);
    std::printf("  total H    : %.3f\n", maxY);
    printWomMeshStats(wom);
    return 0;
}

int handleTotem(int& i, int argc, char** argv) {
    // Tribal totem: stack of N square blocks alternating wide/
    // narrow widths so each carved face reads as distinct.
    // Even-indexed blocks are full width, odd are 70% - gives
    // the carved-segment look characteristic of totem poles.
    // The 35th procedural mesh primitive.
    std::string womBase = argv[++i];
    float baseW = 0.5f;        // base block half-width × 2
    int segments = 5;          // number of stacked blocks
    float segH = 0.5f;         // height of each block
    parseOptFloat(i, argc, argv, baseW);
    parseOptInt(i, argc, argv, segments);
    parseOptFloat(i, argc, argv, segH);
    if (baseW <= 0 || segH <= 0 || segments < 1 || segments > 32) {
        std::fprintf(stderr,
            "gen-mesh-totem: dims > 0, segments 1..32\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Stack blocks bottom-up. Bottom block always full width.
    // Even blocks (0, 2, 4...) get full width, odd blocks 70%.
    for (int s = 0; s < segments; ++s) {
        float cy = (s + 0.5f) * segH;
        float halfW = (s & 1) ? (baseW * 0.5f * 0.70f) : (baseW * 0.5f);
        addBox(0, cy, 0, halfW, segH * 0.5f, halfW);
    }
    finalizeAsSingleBatch(wom);
    float maxY = segments * segH;
    float maxXZ = baseW * 0.5f;
    setCenteredBoundsXZ(wom, maxXZ, maxXZ, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-totem")) return 1;
    printWomWrote(womBase);
    std::printf("  base width : %.3f\n", baseW);
    std::printf("  segments   : %d (each %.3f tall)\n", segments, segH);
    std::printf("  total H    : %.3f\n", maxY);
    printWomMeshStats(wom);
    return 0;
}

int handleCage(int& i, int argc, char** argv) {
    // Square cage: top + bottom thin frame slabs + 4 corner
    // posts + N evenly spaced bars on each of the 4 sides.
    // Bars are thin square cross-section so they read as
    // metal rods. Useful for prison cells, animal pens,
    // dungeon set dressing.
    std::string womBase = argv[++i];
    float width = 1.5f;        // along X = Z (square footprint)
    float height = 2.0f;
    int barsPerSide = 5;
    float barRadius = 0.04f;
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptInt(i, argc, argv, barsPerSide);
    parseOptFloat(i, argc, argv, barRadius);
    if (width <= 0 || height <= 0 || barRadius <= 0 ||
        barsPerSide < 0 || barsPerSide > 64) {
        std::fprintf(stderr,
            "gen-mesh-cage: dims > 0, barsPerSide 0..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfW = width * 0.5f;
    float frameT = barRadius * 1.5f;  // top/bottom slab thickness
    // Top + bottom frame slabs
    addBox(0, frameT * 0.5f, 0, halfW, frameT * 0.5f, halfW);
    addBox(0, height - frameT * 0.5f, 0, halfW, frameT * 0.5f, halfW);
    // 4 corner posts (thicker than bars)
    float postR = barRadius * 1.5f;
    float postCY = height * 0.5f;
    float postHy = height * 0.5f;
    float corner = halfW - postR;
    addBox( corner, postCY,  corner, postR, postHy, postR);
    addBox(-corner, postCY,  corner, postR, postHy, postR);
    addBox( corner, postCY, -corner, postR, postHy, postR);
    addBox(-corner, postCY, -corner, postR, postHy, postR);
    // Bars: N bars per side, evenly distributed between corners.
    // Side spans from -corner to +corner; bars at (k+1)/(N+1)
    // along the span so they're inset (no overlap with corners).
    float barHy = (height - 2 * frameT) * 0.5f;
    float barCYadj = frameT + barHy;
    int barTotal = 0;
    for (int k = 0; k < barsPerSide; ++k) {
        float t = (k + 1.0f) / (barsPerSide + 1.0f);
        float pos = -corner + t * 2.0f * corner;  // from -corner to +corner
        // +Z and -Z sides (bars span X)
        addBox(pos, barCYadj,  halfW - barRadius,
               barRadius, barHy, barRadius);
        addBox(pos, barCYadj, -halfW + barRadius,
               barRadius, barHy, barRadius);
        // +X and -X sides (bars span Z)
        addBox( halfW - barRadius, barCYadj, pos,
               barRadius, barHy, barRadius);
        addBox(-halfW + barRadius, barCYadj, pos,
               barRadius, barHy, barRadius);
        barTotal += 4;
    }
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, halfW, halfW, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-cage")) return 1;
    printWomWrote(womBase);
    std::printf("  width × height : %.3f × %.3f\n", width, height);
    std::printf("  bars per side  : %d (%d total)\n",
                barsPerSide, barTotal);
    std::printf("  bar radius     : %.3f\n", barRadius);
    std::printf("  vertices       : %zu\n", wom.vertices.size());
    std::printf("  triangles      : %zu\n", wom.indices.size() / 3);
    return 0;
}

int handleThrone(int& i, int argc, char** argv) {
    // Throne: pedestal slab + seat block + tall backrest +
    // 2 armrests on either side. Reads as a regal seat from
    // any angle. The 37th procedural mesh primitive.
    std::string womBase = argv[++i];
    float seatW = 0.8f;        // along X
    float seatH = 0.5f;        // top of seat above pedestal
    float backH = 1.5f;        // backrest extends this above seat
    float pedSize = 1.2f;      // pedestal width = depth
    parseOptFloat(i, argc, argv, seatW);
    parseOptFloat(i, argc, argv, seatH);
    parseOptFloat(i, argc, argv, backH);
    parseOptFloat(i, argc, argv, pedSize);
    if (seatW <= 0 || seatH <= 0 || backH <= 0 || pedSize <= 0 ||
        pedSize < seatW) {
        std::fprintf(stderr,
            "gen-mesh-throne: dims > 0; pedSize must be >= seatW\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Pedestal: low square slab at the floor
    float pedH = seatH * 0.4f;
    float halfPed = pedSize * 0.5f;
    addBox(0, pedH * 0.5f, 0, halfPed, pedH * 0.5f, halfPed);
    // Seat: thick square cushion sitting on pedestal
    float seatT = seatH * 0.3f;       // seat thickness (along Y)
    float seatCY = pedH + seatT * 0.5f;
    float halfSeat = seatW * 0.5f;
    addBox(0, seatCY, 0, halfSeat, seatT * 0.5f, halfSeat);
    // Backrest: tall vertical slab at -Z edge of seat, slim in Z
    float backT = seatT * 0.6f;
    float backCY = pedH + seatT + backH * 0.5f;
    addBox(0, backCY, -halfSeat + backT * 0.5f,
           halfSeat, backH * 0.5f, backT * 0.5f);
    // Armrests: 2 small blocks on the sides
    float armW = backT * 0.8f;
    float armH = seatH * 0.4f;
    float armCY = pedH + seatT + armH * 0.5f;
    float armDepth = halfSeat * 0.7f;
    addBox( halfSeat - armW * 0.5f, armCY, 0,
           armW * 0.5f, armH * 0.5f, armDepth);
    addBox(-halfSeat + armW * 0.5f, armCY, 0,
           armW * 0.5f, armH * 0.5f, armDepth);
    finalizeAsSingleBatch(wom);
    float maxY = pedH + seatT + backH;
    setCenteredBoundsXZ(wom, halfPed, halfPed, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-throne")) return 1;
    printWomWrote(womBase);
    std::printf("  pedestal   : %.3f × %.3f (h=%.3f)\n",
                pedSize, pedSize, pedH);
    std::printf("  seat       : %.3f × %.3f\n", seatW, seatT);
    std::printf("  backrest   : H=%.3f\n", backH);
    std::printf("  total H    : %.3f\n", maxY);
    printWomMeshStats(wom);
    return 0;
}

int handleCoffin(int& i, int argc, char** argv) {
    // Coffin: classic 6-sided "hexagonal" prism with the
    // characteristic narrow-head / wide-shoulder / tapered-foot
    // top-down profile that reads as a coffin from any angle.
    // Six side faces + top lid + bottom panel - face-shared
    // normals via separate vertex sets per face. The 38th
    // procedural mesh primitive - useful for graveyard set
    // dressing alongside --gen-mesh-grave.
    std::string womBase = argv[++i];
    float length = 2.0f;     // along Z
    float width  = 0.8f;     // shoulder width along X
    float height = 0.6f;     // along Y
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    if (length <= 0 || width <= 0 || height <= 0) {
        std::fprintf(stderr,
            "gen-mesh-coffin: length/width/height must be > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Top-down hexagonal coffin profile (CCW from head looking
    // down +Y). Head end is narrow, shoulder is widest, feet
    // taper to a narrow toe - the canonical "casket" silhouette.
    float hL = length * 0.5f;
    float hW = width * 0.5f;
    glm::vec2 ring[6] = {
        { 0.0f,         hL          },  // p0 head tip
        {-hW,           hL * 0.6f   },  // p1 left shoulder (widest)
        {-hW * 0.8f,   -hL * 0.6f   },  // p2 left hip
        { 0.0f,        -hL          },  // p3 foot tip
        { hW * 0.8f,   -hL * 0.6f   },  // p4 right hip
        { hW,           hL * 0.6f   },  // p5 right shoulder
    };
    auto addQuad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                       glm::vec3 n) {
        uint32_t base = static_cast<uint32_t>(wom.vertices.size());
        auto push = [&](glm::vec3 p, float u, float v) {
            wowee::pipeline::WoweeModel::Vertex vtx;
            vtx.position = p; vtx.normal = n; vtx.texCoord = {u, v};
            wom.vertices.push_back(vtx);
        };
        push(a, 0, 0);
        push(b, 1, 0);
        push(c, 1, 1);
        push(d, 0, 1);
        wom.indices.insert(wom.indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    // Six side faces - each a quad from bottom-edge to top-edge
    // of one segment of the hexagon. Normal is the outward
    // perpendicular to the side edge in the XZ plane.
    for (int s = 0; s < 6; ++s) {
        const glm::vec2& a = ring[s];
        const glm::vec2& b = ring[(s + 1) % 6];
        glm::vec3 bot0(a.x, 0.0f,    a.y);
        glm::vec3 bot1(b.x, 0.0f,    b.y);
        glm::vec3 top1(b.x, height,  b.y);
        glm::vec3 top0(a.x, height,  a.y);
        // Outward normal: 90° CW rotation of edge vector in XZ
        // (since vertices wind CCW looking down, outward is +X
        // when edge goes -Z, i.e. swap & negate one component).
        glm::vec2 edge = b - a;
        glm::vec3 n(edge.y, 0.0f, -edge.x);
        n = glm::normalize(n);
        addQuad(bot0, bot1, top1, top0, n);
    }
    // Top lid: fan of 4 triangles from p0, all sharing +Y normal.
    {
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        uint32_t base = static_cast<uint32_t>(wom.vertices.size());
        for (int v = 0; v < 6; ++v) {
            wowee::pipeline::WoweeModel::Vertex vtx;
            vtx.position = glm::vec3(ring[v].x, height, ring[v].y);
            vtx.normal = normal;
            // Cheap planar UV from top-down ring coords.
            vtx.texCoord = { ring[v].x / width  + 0.5f,
                             ring[v].y / length + 0.5f };
            wom.vertices.push_back(vtx);
        }
        for (int t = 1; t < 5; ++t) {
            wom.indices.insert(wom.indices.end(),
                {base, base + static_cast<uint32_t>(t),
                 base + static_cast<uint32_t>(t + 1)});
        }
    }
    // Bottom panel: same fan but reversed winding for -Y normal.
    {
        glm::vec3 normal(0.0f, -1.0f, 0.0f);
        uint32_t base = static_cast<uint32_t>(wom.vertices.size());
        for (int v = 0; v < 6; ++v) {
            wowee::pipeline::WoweeModel::Vertex vtx;
            vtx.position = glm::vec3(ring[v].x, 0.0f, ring[v].y);
            vtx.normal = normal;
            vtx.texCoord = { ring[v].x / width  + 0.5f,
                             ring[v].y / length + 0.5f };
            wom.vertices.push_back(vtx);
        }
        for (int t = 1; t < 5; ++t) {
            wom.indices.insert(wom.indices.end(),
                {base, base + static_cast<uint32_t>(t + 1),
                 base + static_cast<uint32_t>(t)});
        }
    }
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-hW, 0.0f,    -hL);
    wom.boundMax = glm::vec3( hW, height,   hL);
    if (!saveWomOrError(wom, womBase, "gen-mesh-coffin")) return 1;
    printWomWrote(womBase);
    std::printf("  length     : %.3f\n", length);
    std::printf("  width      : %.3f (shoulder)\n", width);
    std::printf("  height     : %.3f\n", height);
    printWomMeshStats(wom);
    return 0;
}

int handleArchwayDouble(int& i, int argc, char** argv) {
    // Double archway: 5-box twin-opening passage - 3 vertical
    // posts (left / shared center / right) plus 2 horizontal
    // lintels spanning each opening. Pairs with the existing
    // single --gen-mesh-archway for plaza approaches, double-
    // door tomb fronts, formal garden entrances. The 58th
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float openingWidth  = 1.40f;     // each opening's width
    float openingHeight = 2.40f;     // post height under lintel
    float postT         = 0.18f;
    float lintelT       = 0.20f;
    parseOptFloat(i, argc, argv, openingWidth);
    parseOptFloat(i, argc, argv, openingHeight);
    parseOptFloat(i, argc, argv, postT);
    parseOptFloat(i, argc, argv, lintelT);
    if (openingWidth <= 0 || openingHeight <= 0 ||
        postT <= 0 || lintelT <= 0) {
        std::fprintf(stderr,
            "gen-mesh-archway-double: all dims must be > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfPost = postT * 0.5f;
    float halfLintel = lintelT * 0.5f;
    // Post X positions: -X = left edge of left opening, 0 = shared
    // center, +X = right edge of right opening. Posts straddle
    // those positions so the inside opening stays openingWidth.
    float leftPostX  = -(openingWidth + postT * 0.5f);
    float rightPostX =  (openingWidth + postT * 0.5f);
    float centerPostX = 0.0f;
    float postCY = openingHeight * 0.5f;
    addBox(leftPostX,   postCY, 0, halfPost, openingHeight * 0.5f, halfPost);
    addBox(centerPostX, postCY, 0, halfPost, openingHeight * 0.5f, halfPost);
    addBox(rightPostX,  postCY, 0, halfPost, openingHeight * 0.5f, halfPost);
    // 2 lintels: each spans from the outer post inner-face to
    // the center post inner-face. Lintel center sits at the
    // midpoint of (leftPost, centerPost) for the left opening,
    // and (centerPost, rightPost) for the right opening.
    float lintelCY = openingHeight + halfLintel;
    float halfLintelLen = openingWidth * 0.5f + halfPost;
    float leftLintelX  = (leftPostX + centerPostX) * 0.5f;
    float rightLintelX = (centerPostX + rightPostX) * 0.5f;
    addBox(leftLintelX,  lintelCY, 0, halfLintelLen, halfLintel, halfPost);
    addBox(rightLintelX, lintelCY, 0, halfLintelLen, halfLintel, halfPost);
    finalizeAsSingleBatch(wom);
    float totalH = openingHeight + lintelT;
    float halfTotalX = rightPostX + halfPost;
    wom.boundMin = glm::vec3(-halfTotalX, 0.0f,    -halfPost);
    wom.boundMax = glm::vec3( halfTotalX, totalH,   halfPost);
    if (!saveWomOrError(wom, womBase, "gen-mesh-archway-double")) return 1;
    printWomWrote(womBase);
    std::printf("  total W    : %.3f (2 openings × %.3f + 3 posts × %.3f)\n",
                halfTotalX * 2, openingWidth, postT);
    std::printf("  height     : %.3f opening + %.3f lintel\n",
                openingHeight, lintelT);
    std::printf("  total H    : %.3f\n", totalH);
    printWomMeshStats(wom);
    return 0;
}

int handleBrazier(int& i, int argc, char** argv) {
    // Brazier: 7-box fire-pit on a pedestal - square base
    // plate, narrow vertical stem, wider bowl on top of the
    // stem, and 3 small flame boxes of varying heights rising
    // from the bowl. Useful for dungeons, temples, watchtowers,
    // throne rooms - anywhere a fantasy world needs visible
    // light sources. The 57th procedural mesh primitive.
    std::string womBase = argv[++i];
    float bowlSize = 0.55f;
    float stemHeight = 0.80f;
    float stemT = 0.10f;
    float baseSize = 0.35f;
    parseOptFloat(i, argc, argv, bowlSize);
    parseOptFloat(i, argc, argv, stemHeight);
    parseOptFloat(i, argc, argv, stemT);
    parseOptFloat(i, argc, argv, baseSize);
    if (bowlSize <= 0 || stemHeight <= 0 || stemT <= 0 || baseSize <= 0 ||
        stemT >= baseSize || stemT >= bowlSize) {
        std::fprintf(stderr,
            "gen-mesh-brazier: dims > 0; stem must fit in base & bowl\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Base plate.
    float baseHeight = baseSize * 0.20f;
    float halfBase = baseSize * 0.5f;
    addBox(0, baseHeight * 0.5f, 0,
           halfBase, baseHeight * 0.5f, halfBase);
    // Stem rising from base to where the bowl will sit.
    float halfStemT = stemT * 0.5f;
    float stemCY = baseHeight + stemHeight * 0.5f;
    addBox(0, stemCY, 0,
           halfStemT, stemHeight * 0.5f, halfStemT);
    // Bowl: wide thin slab on top of the stem.
    float bowlH = bowlSize * 0.25f;
    float halfBowl = bowlSize * 0.5f;
    float bowlCY = baseHeight + stemHeight + bowlH * 0.5f;
    addBox(0, bowlCY, 0, halfBowl, bowlH * 0.5f, halfBowl);
    // 3 flame boxes rising from the bowl, varying heights so
    // the silhouette reads as a fire rather than a uniform
    // block. Centered triangle layout.
    float flameTop = baseHeight + stemHeight + bowlH;
    float flameW = bowlSize * 0.18f;
    float halfFW = flameW * 0.5f;
    struct Flame { float dx, dz, h; };
    Flame flames[3] = {
        { 0.0f,                 0.0f, bowlSize * 0.50f },  // tallest center
        { bowlSize * 0.18f, bowlSize * 0.10f, bowlSize * 0.30f },
        {-bowlSize * 0.18f, bowlSize * 0.10f, bowlSize * 0.35f },
    };
    for (const auto& f : flames) {
        addBox(f.dx, flameTop + f.h * 0.5f, f.dz,
               halfFW, f.h * 0.5f, halfFW);
    }
    finalizeAsSingleBatch(wom);
    float totalH = flameTop + bowlSize * 0.50f;
    wom.boundMin = glm::vec3(-halfBowl, 0.0f,    -halfBowl);
    wom.boundMax = glm::vec3( halfBowl, totalH,   halfBowl);
    if (!saveWomOrError(wom, womBase, "gen-mesh-brazier")) return 1;
    printWomWrote(womBase);
    std::printf("  base       : %.3f square × %.3f thick\n",
                baseSize, baseHeight);
    std::printf("  stem       : %.3f square × %.3f tall\n",
                stemT, stemHeight);
    std::printf("  bowl       : %.3f wide × %.3f thick\n", bowlSize, bowlH);
    std::printf("  flames     : 3 (varied heights)\n");
    std::printf("  total H    : %.3f\n", totalH);
    printWomMeshStats(wom);
    return 0;
}

int handlePodium(int& i, int argc, char** argv) {
    // Podium: 4-box stepped pyramid speaker stand - large
    // bottom step, medium middle step, small top platform,
    // and a small lectern box on top of the platform. Useful
    // for throne rooms, ceremonies, NPC speaker positions,
    // monument bases. The 56th procedural mesh primitive.
    std::string womBase = argv[++i];
    float baseSize    = 1.60f;     // bottom step width = depth
    float baseHeight  = 0.20f;
    int   stepCount   = 3;          // total stepped tiers (incl. top)
    float lecternSize = 0.30f;      // lectern at the very top
    parseOptFloat(i, argc, argv, baseSize);
    parseOptFloat(i, argc, argv, baseHeight);
    parseOptInt(i, argc, argv, stepCount);
    parseOptFloat(i, argc, argv, lecternSize);
    if (baseSize <= 0 || baseHeight <= 0 || lecternSize <= 0 ||
        stepCount < 2 || stepCount > 8) {
        std::fprintf(stderr,
            "gen-mesh-podium: dims > 0; stepCount 2..8\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Each step is shorter on each side by ~15% of base size,
    // and the top platform's footprint is half the base. Step
    // heights are equal for visual rhythm.
    float topSize     = baseSize * 0.50f;
    float sizeStep    = (baseSize - topSize) / (stepCount - 1);
    float stepHeight  = baseHeight;
    for (int s = 0; s < stepCount; ++s) {
        float halfSide = (baseSize - s * sizeStep) * 0.5f;
        float stepCY = stepHeight * 0.5f + s * stepHeight;
        addBox(0, stepCY, 0, halfSide, stepHeight * 0.5f, halfSide);
    }
    // Lectern: small box on top of the top platform, at the
    // back so a speaker has room in front. Faces +Z.
    float halfL    = lecternSize * 0.5f;
    float lecternH = lecternSize * 1.2f;
    float platformTopY = stepHeight * stepCount;
    float lecternCY = platformTopY + lecternH * 0.5f;
    float lecternZ  = -topSize * 0.25f;     // pushed back
    addBox(0, lecternCY, lecternZ,
           halfL, lecternH * 0.5f, halfL * 0.4f);
    finalizeAsSingleBatch(wom);
    float totalH = lecternCY + lecternH * 0.5f;
    float halfBase = baseSize * 0.5f;
    wom.boundMin = glm::vec3(-halfBase, 0.0f,    -halfBase);
    wom.boundMax = glm::vec3( halfBase, totalH,   halfBase);
    if (!saveWomOrError(wom, womBase, "gen-mesh-podium")) return 1;
    printWomWrote(womBase);
    std::printf("  base       : %.3f square × %.3f thick\n",
                baseSize, baseHeight);
    std::printf("  steps      : %d (top %.3f square)\n", stepCount, topSize);
    std::printf("  lectern    : %.3f wide (at back)\n", lecternSize);
    std::printf("  total H    : %.3f\n", totalH);
    printWomMeshStats(wom);
    return 0;
}

int handleSundial(int& i, int argc, char** argv) {
    // Sundial: 8-box garden timekeeper - square base plate at
    // floor, central vertical gnomon slab spanning the diameter
    // (long axis along Z, simulates the angled blade that casts
    // a shadow), and 4 small hour-marker nubs at the cardinal
    // points around the rim. Useful for monastery courtyards,
    // mage tower observatories, druidic stone circles. The
    // 55th procedural mesh primitive.
    std::string womBase = argv[++i];
    float baseSize    = 0.80f;
    float baseHeight  = 0.06f;
    float gnomonHeight = 0.35f;
    float gnomonT     = 0.04f;
    parseOptFloat(i, argc, argv, baseSize);
    parseOptFloat(i, argc, argv, baseHeight);
    parseOptFloat(i, argc, argv, gnomonHeight);
    parseOptFloat(i, argc, argv, gnomonT);
    if (baseSize <= 0 || baseHeight <= 0 ||
        gnomonHeight <= 0 || gnomonT <= 0 ||
        gnomonT * 2 >= baseSize) {
        std::fprintf(stderr,
            "gen-mesh-sundial: dims > 0; gnomonT must fit in base\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfBase = baseSize * 0.5f;
    // Base plate at floor.
    addBox(0, baseHeight * 0.5f, 0,
           halfBase, baseHeight * 0.5f, halfBase);
    // Gnomon: vertical slab centered on the base, spanning the
    // diameter along Z (the long axis). Sits on top of the base.
    float halfGnomonT = gnomonT * 0.5f;
    float halfGnomonZ = baseSize * 0.45f;     // slightly inset from base edges
    float gnomonCY    = baseHeight + gnomonHeight * 0.5f;
    addBox(0, gnomonCY, 0,
           halfGnomonT, gnomonHeight * 0.5f, halfGnomonZ);
    // 4 hour-marker nubs at cardinal positions (N, S, E, W) on
    // the base's top face. Small protrusions above the base
    // plate so they read as raised markers.
    float markerW = baseSize * 0.06f;
    float markerH = baseHeight * 1.5f;
    float halfMW  = markerW * 0.5f;
    float markerOff = halfBase * 0.85f;
    float markerCY  = baseHeight + markerH * 0.5f;
    addBox( markerOff, markerCY, 0,        halfMW, markerH * 0.5f, halfMW); // E
    addBox(-markerOff, markerCY, 0,        halfMW, markerH * 0.5f, halfMW); // W
    addBox(0,          markerCY,  markerOff, halfMW, markerH * 0.5f, halfMW); // N
    addBox(0,          markerCY, -markerOff, halfMW, markerH * 0.5f, halfMW); // S
    finalizeAsSingleBatch(wom);
    float totalH = baseHeight + gnomonHeight;
    wom.boundMin = glm::vec3(-halfBase, 0.0f,    -halfBase);
    wom.boundMax = glm::vec3( halfBase, totalH,   halfBase);
    if (!saveWomOrError(wom, womBase, "gen-mesh-sundial")) return 1;
    printWomWrote(womBase);
    std::printf("  base       : %.3f square × %.3f thick\n",
                baseSize, baseHeight);
    std::printf("  gnomon     : %.3f tall × %.3f thick (along Z)\n",
                gnomonHeight, gnomonT);
    std::printf("  markers    : 4 (N/S/E/W cardinal points)\n");
    std::printf("  total H    : %.3f\n", totalH);
    printWomMeshStats(wom);
    return 0;
}

int handleScarecrow(int& i, int argc, char** argv) {
    // Scarecrow: 5-box cruciform farm pest deterrent - anchor
    // post into the ground, vertical body, horizontal arm cross,
    // round-ish head box at the top, and a brimmed hat box on
    // the head. The cross silhouette reads as a scarecrow even
    // without rotated geometry. The 54th procedural mesh
    // primitive - useful for crop fields, abandoned villages,
    // harvest set dressing.
    std::string womBase = argv[++i];
    float bodyHeight = 1.80f;
    float armSpan    = 1.40f;     // total cross-arm width
    float postT      = 0.06f;
    float headSize   = 0.22f;
    float hatSize    = 0.32f;
    parseOptFloat(i, argc, argv, bodyHeight);
    parseOptFloat(i, argc, argv, armSpan);
    parseOptFloat(i, argc, argv, postT);
    parseOptFloat(i, argc, argv, headSize);
    parseOptFloat(i, argc, argv, hatSize);
    if (bodyHeight <= 0 || armSpan <= 0 || postT <= 0 ||
        headSize <= 0 || hatSize <= 0) {
        std::fprintf(stderr, "gen-mesh-scarecrow: all dims must be > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Vertical body post - full bodyHeight.
    float halfPost = postT * 0.5f;
    float bodyCY = bodyHeight * 0.5f;
    addBox(0, bodyCY, 0, halfPost, bodyHeight * 0.5f, halfPost);
    // Cross-arm - horizontal, sits about 75% up the body.
    float armT     = postT * 0.85f;
    float halfArmT = armT * 0.5f;
    float armCY    = bodyHeight * 0.72f;
    addBox(0, armCY, 0, armSpan * 0.5f, halfArmT, halfArmT);
    // Head - sits on top of the body. Slightly above the post
    // tip so it visually sits on the post rather than passing
    // through it.
    float halfHead = headSize * 0.5f;
    float headCY   = bodyHeight + halfHead;
    addBox(0, headCY, 0, halfHead, halfHead, halfHead);
    // Hat - wider than the head (the brim) but shorter
    // (so the head still pokes through visually).
    float halfHat = hatSize * 0.5f;
    float hatH    = headSize * 0.40f;
    float hatCY   = headCY + halfHead - hatH * 0.3f;
    addBox(0, hatCY, 0, halfHat, hatH * 0.5f, halfHat);
    // Hat crown - taller, narrower top of the hat (so the
    // overall hat reads as a brim + crown silhouette).
    float crownSize = hatSize * 0.55f;
    float crownH    = headSize * 0.65f;
    float halfCrown = crownSize * 0.5f;
    float crownCY   = hatCY + hatH * 0.5f + crownH * 0.5f;
    addBox(0, crownCY, 0, halfCrown, crownH * 0.5f, halfCrown);
    finalizeAsSingleBatch(wom);
    float totalH = crownCY + crownH * 0.5f;
    float halfArm = armSpan * 0.5f;
    wom.boundMin = glm::vec3(-halfArm, 0.0f,    -halfHead);
    wom.boundMax = glm::vec3( halfArm, totalH,   halfHead);
    if (!saveWomOrError(wom, womBase, "gen-mesh-scarecrow")) return 1;
    printWomWrote(womBase);
    std::printf("  total H    : %.3f\n", totalH);
    std::printf("  body       : %.3f tall (%.3f square post)\n",
                bodyHeight, postT);
    std::printf("  arm span   : %.3f wide\n", armSpan);
    std::printf("  head/hat   : %.3f / %.3f\n", headSize, hatSize);
    printWomMeshStats(wom);
    return 0;
}

int handleWeathervane(int& i, int argc, char** argv) {
    // Weathervane: 6-box rooftop wind indicator - base plate,
    // tall vertical post, perpendicular N-S and E-W cross arms
    // (cardinal direction markers), a long horizontal arrow on
    // top of the cross, and a small tail box at the back end of
    // the arrow that visually balances the head. Useful for
    // farm rooftops, chapel spires, town halls, lighthouse caps.
    // The 53rd procedural mesh primitive.
    std::string womBase = argv[++i];
    float postHeight = 1.50f;
    float postT      = 0.05f;
    float baseSize   = 0.30f;
    float armLen     = 0.40f;     // half-length of each cross arm
    float arrowLen   = 0.55f;     // half-length of the arrow body
    parseOptFloat(i, argc, argv, postHeight);
    parseOptFloat(i, argc, argv, postT);
    parseOptFloat(i, argc, argv, baseSize);
    parseOptFloat(i, argc, argv, armLen);
    parseOptFloat(i, argc, argv, arrowLen);
    if (postHeight <= 0 || postT <= 0 || baseSize <= 0 ||
        armLen <= 0 || arrowLen <= 0 || postT >= baseSize) {
        std::fprintf(stderr,
            "gen-mesh-weathervane: dims > 0; post must fit in base\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Base plate at floor.
    float baseHeight = baseSize * 0.30f;
    float halfBase = baseSize * 0.5f;
    addBox(0, baseHeight * 0.5f, 0,
           halfBase, baseHeight * 0.5f, halfBase);
    // Vertical post.
    float halfPost = postT * 0.5f;
    float poleBottomY = baseHeight;
    float poleTopY    = baseHeight + postHeight;
    float poleCY      = (poleBottomY + poleTopY) * 0.5f;
    addBox(0, poleCY, 0, halfPost, postHeight * 0.5f, halfPost);
    // Cross arms at the top of the post - 2 perpendicular thin
    // bars forming the cardinal-direction "+" marker.
    float armT       = postT * 0.7f;
    float halfArmT   = armT * 0.5f;
    float crossY     = poleTopY - armT * 1.0f;
    addBox(0, crossY, 0, armLen, halfArmT, halfArmT);   // E-W (along X)
    addBox(0, crossY, 0, halfArmT, halfArmT, armLen);   // N-S (along Z)
    // Arrow body on top of the cross - long thin bar that
    // would rotate to the wind direction. Aligned along +X by
    // default (designers can rotate at placement time).
    float arrowY  = poleTopY + armT * 0.7f;
    float arrowT2 = armT * 1.1f;
    float halfAT  = arrowT2 * 0.5f;
    addBox(0, arrowY, 0, arrowLen, halfAT, halfAT);
    // Arrow tail: small box at -X end so the arrow looks
    // directional rather than symmetric.
    float tailLen = arrowLen * 0.3f;
    float tailX   = -arrowLen + tailLen;
    addBox(tailX, arrowY, 0, halfAT, arrowT2 * 1.4f, halfAT);
    finalizeAsSingleBatch(wom);
    float totalH = arrowY + arrowT2 * 1.4f;
    float maxX = std::max({halfBase, arrowLen, tailX + halfAT});
    wom.boundMin = glm::vec3(-maxX,    0.0f,    -armLen);
    wom.boundMax = glm::vec3( maxX,    totalH,   armLen);
    if (!saveWomOrError(wom, womBase, "gen-mesh-weathervane")) return 1;
    printWomWrote(womBase);
    std::printf("  total H    : %.3f\n", totalH);
    std::printf("  base       : %.3f square × %.3f tall\n",
                baseSize, baseHeight);
    std::printf("  post       : %.3f square × %.3f tall\n",
                postT, postHeight);
    std::printf("  cross arms : 2 × %.3f half-length (N-S + E-W)\n", armLen);
    std::printf("  arrow      : %.3f half-length (with tail at -X)\n",
                arrowLen);
    printWomMeshStats(wom);
    return 0;
}

int handleBeehive(int& i, int argc, char** argv) {
    // Beehive: 5-box woven straw skep - stacked tiers of
    // decreasing width approximating a dome, with a small
    // entrance notch box at the front. Useful for druidic
    // groves, beekeeper farms, hunter camps. The 52nd
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float baseWidth = 0.70f;     // bottom tier width
    float height    = 0.85f;     // total dome height (excluding base plate)
    float plateH    = 0.05f;     // optional foundation plate thickness
    parseOptFloat(i, argc, argv, baseWidth);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, plateH);
    if (baseWidth <= 0 || height <= 0 || plateH < 0) {
        std::fprintf(stderr,
            "gen-mesh-beehive: baseWidth/height > 0; plateH >= 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Optional wooden base plate, slightly wider than the
    // bottom tier so it reads as a foundation.
    float halfBaseW = baseWidth * 0.5f;
    float halfPlateW = baseWidth * 0.55f;
    if (plateH > 0) {
        addBox(0, plateH * 0.5f, 0, halfPlateW, plateH * 0.5f, halfPlateW);
    }
    // 4 stacked tiers approximating a conical dome. Tier widths
    // ramp 100% -> 90% -> 70% -> 40% of baseWidth. Each tier
    // takes 1/4 of the dome height.
    float tierHeight = height / 4.0f;
    float tierWidths[4] = {1.00f, 0.90f, 0.70f, 0.40f};
    float tierBase = plateH;
    for (int t = 0; t < 4; ++t) {
        float halfW = baseWidth * tierWidths[t] * 0.5f;
        float tierCY = tierBase + tierHeight * 0.5f + t * tierHeight;
        addBox(0, tierCY, 0, halfW, tierHeight * 0.5f, halfW);
    }
    // Entrance notch: a small dark box at the front (+Z face)
    // of the bottom tier, slightly proud of it so it reads as
    // a separate cutout rather than texture detail.
    float entryW = baseWidth * 0.20f;
    float entryH = tierHeight * 0.55f;
    float entryT = baseWidth * 0.04f;
    float entryCY = tierBase + entryH * 0.5f;
    float entryCZ = halfBaseW + entryT * 0.5f;
    addBox(0, entryCY, entryCZ,
           entryW * 0.5f, entryH * 0.5f, entryT * 0.5f);
    finalizeAsSingleBatch(wom);
    float totalH = plateH + height;
    wom.boundMin = glm::vec3(-halfPlateW, 0.0f,    -halfPlateW);
    wom.boundMax = glm::vec3( halfPlateW, totalH,   halfBaseW + entryT);
    if (!saveWomOrError(wom, womBase, "gen-mesh-beehive")) return 1;
    printWomWrote(womBase);
    std::printf("  base width : %.3f (plate %.3f thick)\n", baseWidth, plateH);
    std::printf("  height     : %.3f dome (4 tapered tiers)\n", height);
    std::printf("  total H    : %.3f\n", totalH);
    std::printf("  entrance   : %.3f wide × %.3f tall on +Z face\n",
                entryW, entryH);
    printWomMeshStats(wom);
    return 0;
}

int handleGate(int& i, int argc, char** argv) {
    // Gate: 5-box wooden farm gate - 2 vertical posts on either
    // side and 3 horizontal cross rails (top, middle, bottom)
    // spanning the opening. The opening sits flat in the X-Y
    // plane (rails along X, posts along Y) so it can hang in
    // a wall slot without rotation. The 51st procedural mesh
    // primitive - useful for fenced fields, manor entrances,
    // pen openings, courtyard barriers.
    std::string womBase = argv[++i];
    float openingWidth = 1.80f;     // gap between posts (rail span)
    float postHeight   = 1.30f;     // post height (= gate frame height)
    float postT        = 0.10f;     // post square cross-section
    float railT        = 0.06f;     // rail square cross-section
    parseOptFloat(i, argc, argv, openingWidth);
    parseOptFloat(i, argc, argv, postHeight);
    parseOptFloat(i, argc, argv, postT);
    parseOptFloat(i, argc, argv, railT);
    if (openingWidth <= 0 || postHeight <= 0 || postT <= 0 ||
        railT <= 0 || railT >= postHeight / 4) {
        std::fprintf(stderr,
            "gen-mesh-gate: dims > 0; railT < postHeight/4\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Total gate width = openingWidth + 2*postT (posts sit flush
    // against the rails so the rail length = openingWidth).
    float halfPost  = postT * 0.5f;
    float halfRail  = railT * 0.5f;
    float postX     = openingWidth * 0.5f + halfPost;
    float postCY    = postHeight * 0.5f;
    // 2 vertical posts.
    addBox( postX, postCY, 0, halfPost, postHeight * 0.5f, halfPost);
    addBox(-postX, postCY, 0, halfPost, postHeight * 0.5f, halfPost);
    // 3 horizontal rails: top, middle, bottom. Bottom sits a
    // little above the floor so it reads as a gate rather than
    // bouncing off the ground; top sits a little below the post
    // top so the post crowns are visible.
    float halfRailLen = openingWidth * 0.5f;
    float topRailY    = postHeight - halfRail * 1.5f;
    float bottomRailY = halfRail * 2.0f;
    float midRailY    = (topRailY + bottomRailY) * 0.5f;
    addBox(0, topRailY,    0, halfRailLen, halfRail, halfRail);
    addBox(0, midRailY,    0, halfRailLen, halfRail, halfRail);
    addBox(0, bottomRailY, 0, halfRailLen, halfRail, halfRail);
    finalizeAsSingleBatch(wom);
    float halfTotalX = postX + halfPost;
    wom.boundMin = glm::vec3(-halfTotalX, 0.0f,        -halfPost);
    wom.boundMax = glm::vec3( halfTotalX, postHeight,   halfPost);
    if (!saveWomOrError(wom, womBase, "gen-mesh-gate")) return 1;
    printWomWrote(womBase);
    std::printf("  total W    : %.3f (opening %.3f + 2 posts)\n",
                openingWidth + postT * 2, openingWidth);
    std::printf("  posts      : 2 × %.3f square × %.3f tall\n",
                postT, postHeight);
    std::printf("  rails      : 3 × %.3f square (top/mid/bottom)\n", railT);
    printWomMeshStats(wom);
    return 0;
}

int handleCauldron(int& i, int argc, char** argv) {
    // Cauldron: 7-box witch's pot - 4 small corner legs at the
    // floor, narrow bottom-bowl tier, wider mid-bowl tier, and
    // a still-wider thin rim at the top. The stacked tiers
    // approximate the curved silhouette of a cast-iron pot
    // without needing rotated faces. The 50th procedural mesh
    // primitive - pairs with --gen-mesh-shrine / --gen-mesh-totem
    // for ritual / alchemy set dressing.
    std::string womBase = argv[++i];
    float rimWidth   = 0.80f;   // top-rim extent (widest dim)
    float bodyHeight = 0.70f;   // total height excluding legs
    float legHeight  = 0.10f;
    parseOptFloat(i, argc, argv, rimWidth);
    parseOptFloat(i, argc, argv, bodyHeight);
    parseOptFloat(i, argc, argv, legHeight);
    if (rimWidth <= 0 || bodyHeight <= 0 || legHeight <= 0) {
        std::fprintf(stderr,
            "gen-mesh-cauldron: all dims must be > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Tier proportions (bottom → top): 60% / 90% / 100% of rimWidth.
    // Heights split: legs / 30% body / 55% body / 15% body (rim).
    float bottomW = rimWidth * 0.60f;
    float midW    = rimWidth * 0.90f;
    float bottomH = bodyHeight * 0.30f;
    float midH    = bodyHeight * 0.55f;
    float rimH    = bodyHeight * 0.15f;
    // 4 legs at corners of a footprint slightly smaller than the
    // bottom tier so the legs visually carry the pot's weight.
    float legT = bottomW * 0.18f;
    float halfLegT = legT * 0.5f;
    float legX = bottomW * 0.5f - halfLegT * 1.4f;
    float legCY = legHeight * 0.5f;
    addBox( legX, legCY,  legX, halfLegT, legHeight * 0.5f, halfLegT);
    addBox(-legX, legCY,  legX, halfLegT, legHeight * 0.5f, halfLegT);
    addBox( legX, legCY, -legX, halfLegT, legHeight * 0.5f, halfLegT);
    addBox(-legX, legCY, -legX, halfLegT, legHeight * 0.5f, halfLegT);
    // Bottom tier (narrow): sits on top of the legs.
    float halfBottom = bottomW * 0.5f;
    float bottomCY = legHeight + bottomH * 0.5f;
    addBox(0, bottomCY, 0, halfBottom, bottomH * 0.5f, halfBottom);
    // Middle tier (widest body): main bulge of the pot.
    float halfMid = midW * 0.5f;
    float midCY = legHeight + bottomH + midH * 0.5f;
    addBox(0, midCY, 0, halfMid, midH * 0.5f, halfMid);
    // Rim: thin slab capping the body, slightly wider than mid.
    float halfRim = rimWidth * 0.5f;
    float rimCY = legHeight + bottomH + midH + rimH * 0.5f;
    addBox(0, rimCY, 0, halfRim, rimH * 0.5f, halfRim);
    finalizeAsSingleBatch(wom);
    float totalH = legHeight + bodyHeight;
    wom.boundMin = glm::vec3(-halfRim, 0.0f,    -halfRim);
    wom.boundMax = glm::vec3( halfRim, totalH,   halfRim);
    if (!saveWomOrError(wom, womBase, "gen-mesh-cauldron")) return 1;
    printWomWrote(womBase);
    std::printf("  rim width  : %.3f (widest)\n", rimWidth);
    std::printf("  body H     : %.3f (legs %.3f tall)\n", bodyHeight, legHeight);
    std::printf("  tiers      : bottom %.3f / mid %.3f / rim %.3f\n",
                bottomW, midW, rimWidth);
    std::printf("  total H    : %.3f\n", totalH);
    printWomMeshStats(wom);
    return 0;
}

int handleStool(int& i, int argc, char** argv) {
    // Stool: 5-box small backless seat - flat round-ish seat
    // (square here, since axis-aligned) on 4 short legs at the
    // corners. Pairs with --gen-mesh-table for taverns and
    // workshops. Smaller-footprint counterpart to --gen-mesh-bench.
    // The 49th procedural mesh primitive.
    std::string womBase = argv[++i];
    float seatSize  = 0.36f;     // seat side length
    float seatT     = 0.04f;     // seat thickness
    float legHeight = 0.45f;
    float legT      = 0.04f;     // square leg cross-section
    parseOptFloat(i, argc, argv, seatSize);
    parseOptFloat(i, argc, argv, seatT);
    parseOptFloat(i, argc, argv, legHeight);
    parseOptFloat(i, argc, argv, legT);
    if (seatSize <= 0 || seatT <= 0 || legHeight <= 0 || legT <= 0 ||
        legT * 2 >= seatSize) {
        std::fprintf(stderr,
            "gen-mesh-stool: dims > 0; legT must fit in seatSize\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfSeat = seatSize * 0.5f;
    float halfLeg  = legT * 0.5f;
    float seatTopY = legHeight + seatT;
    // Seat: flat slab on top of the legs.
    addBox(0, legHeight + seatT * 0.5f, 0,
           halfSeat, seatT * 0.5f, halfSeat);
    // 4 legs: corner-inset by halfLeg so they sit flush with
    // the seat's edge.
    float legX = halfSeat - halfLeg;
    float legCY = legHeight * 0.5f;
    addBox( legX, legCY,  legX, halfLeg, legHeight * 0.5f, halfLeg);
    addBox(-legX, legCY,  legX, halfLeg, legHeight * 0.5f, halfLeg);
    addBox( legX, legCY, -legX, halfLeg, legHeight * 0.5f, halfLeg);
    addBox(-legX, legCY, -legX, halfLeg, legHeight * 0.5f, halfLeg);
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-halfSeat, 0.0f,    -halfSeat);
    wom.boundMax = glm::vec3( halfSeat, seatTopY, halfSeat);
    if (!saveWomOrError(wom, womBase, "gen-mesh-stool")) return 1;
    printWomWrote(womBase);
    std::printf("  seat       : %.3f square × %.3f thick\n", seatSize, seatT);
    std::printf("  legs       : 4 × %.3f square (%.3f tall)\n",
                legT, legHeight);
    std::printf("  total H    : %.3f\n", seatTopY);
    printWomMeshStats(wom);
    return 0;
}

int handleCrate(int& i, int argc, char** argv) {
    // Crate: 5-box wooden shipping crate - main cube body
    // plus 4 reinforcement posts running along the vertical
    // edges. The posts are slightly proud of the body so they
    // read as separate rails rather than texture detail. The
    // 48th procedural mesh primitive - useful for dock yards,
    // warehouse interiors, dungeon room set dressing.
    std::string womBase = argv[++i];
    float size       = 0.80f;     // cube side length
    float postRadius = 0.05f;     // half-thickness of corner posts
    parseOptFloat(i, argc, argv, size);
    parseOptFloat(i, argc, argv, postRadius);
    if (size <= 0 || postRadius <= 0 || postRadius * 4 >= size) {
        std::fprintf(stderr,
            "gen-mesh-crate: size/postRadius > 0; postRadius < size/4\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfBody = size * 0.5f;
    // Main body: cube centered at (0, halfBody, 0).
    addBox(0, halfBody, 0, halfBody, halfBody, halfBody);
    // 4 corner posts: thin boxes running the full height,
    // positioned at the 4 vertical edges of the cube. Posts
    // extend slightly proud of the body on each axis (from
    // halfBody to halfBody + postRadius) so they're visible
    // from any angle without z-fighting the body's faces.
    float postOffset = halfBody;
    float postCY = halfBody;
    float postHeight = size;
    float halfPost = postRadius;
    addBox( postOffset, postCY,  postOffset, halfPost, postHeight * 0.5f, halfPost);
    addBox(-postOffset, postCY,  postOffset, halfPost, postHeight * 0.5f, halfPost);
    addBox( postOffset, postCY, -postOffset, halfPost, postHeight * 0.5f, halfPost);
    addBox(-postOffset, postCY, -postOffset, halfPost, postHeight * 0.5f, halfPost);
    finalizeAsSingleBatch(wom);
    float halfTotal = halfBody + halfPost;
    wom.boundMin = glm::vec3(-halfTotal, 0.0f,    -halfTotal);
    wom.boundMax = glm::vec3( halfTotal, size,     halfTotal);
    if (!saveWomOrError(wom, womBase, "gen-mesh-crate")) return 1;
    printWomWrote(womBase);
    std::printf("  size       : %.3f cube\n", size);
    std::printf("  posts      : 4 × %.3f square (full height)\n",
                postRadius * 2);
    printWomMeshStats(wom);
    return 0;
}

int handleTombstone(int& i, int argc, char** argv) {
    // Tombstone: 3-box vertical headstone - wide low base
    // plinth, tall thin main slab on top, and a small
    // decorative crown / cornice at the very top. Pairs
    // naturally with --gen-mesh-grave and --gen-mesh-coffin
    // for graveyards. The 47th procedural mesh primitive.
    std::string womBase = argv[++i];
    float width    = 0.60f;     // along X (face width)
    float height   = 1.10f;     // total tombstone height including base + crown
    float depth    = 0.18f;     // along Z (slab thickness)
    float baseScale = 1.45f;    // base extends this much beyond slab in X & Z
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, baseScale);
    if (width <= 0 || height <= 0 || depth <= 0 ||
        baseScale < 1.0f || baseScale > 5.0f) {
        std::fprintf(stderr,
            "gen-mesh-tombstone: dims > 0; baseScale 1.0..5.0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Vertical layout: base 15%, slab 75%, crown 10% of total height.
    float baseH  = height * 0.15f;
    float crownH = height * 0.10f;
    float slabH  = height - baseH - crownH;
    // Base plinth: wider in X & Z than the slab so it reads as
    // an explicit foundation.
    float halfBaseW = width * baseScale * 0.5f;
    float halfBaseD = depth * baseScale * 0.5f;
    addBox(0, baseH * 0.5f, 0,
           halfBaseW, baseH * 0.5f, halfBaseD);
    // Main slab: thin tall rectangle centered above the base.
    float slabCY = baseH + slabH * 0.5f;
    float halfW = width * 0.5f;
    float halfD = depth * 0.5f;
    addBox(0, slabCY, 0, halfW, slabH * 0.5f, halfD);
    // Crown: slightly wider/deeper than the slab, sits on top.
    // Acts as a decorative cornice (a flat-cap variant of the
    // arched-top headstone shape that we can't do with
    // axis-aligned boxes alone).
    float crownScale = 1.18f;
    float halfCrownW = width * crownScale * 0.5f;
    float halfCrownD = depth * crownScale * 0.5f;
    float crownCY = baseH + slabH + crownH * 0.5f;
    addBox(0, crownCY, 0, halfCrownW, crownH * 0.5f, halfCrownD);
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-halfBaseW, 0.0f,    -halfBaseD);
    wom.boundMax = glm::vec3( halfBaseW, height,   halfBaseD);
    if (!saveWomOrError(wom, womBase, "gen-mesh-tombstone")) return 1;
    printWomWrote(womBase);
    std::printf("  total H    : %.3f (base %.3f + slab %.3f + crown %.3f)\n",
                height, baseH, slabH, crownH);
    std::printf("  slab       : %.3f wide × %.3f deep\n", width, depth);
    std::printf("  base scale : %.2fx (base %.3f wide × %.3f deep)\n",
                baseScale, halfBaseW * 2, halfBaseD * 2);
    printWomMeshStats(wom);
    return 0;
}

int handleMailbox(int& i, int argc, char** argv) {
    // Mailbox: 4-box wayside prop - vertical post, horizontal
    // box body mounted on top of the post (long axis along Z),
    // small rectangular flag mounted on the right side near the
    // front of the body. Useful for inns, post stations, manor
    // gates, frontier outposts. The 46th procedural mesh.
    std::string womBase = argv[++i];
    float postHeight    = 1.10f;
    float postThickness = 0.08f;
    float boxLength     = 0.45f;   // along Z
    float boxWidth      = 0.20f;   // along X
    float boxHeight     = 0.20f;   // along Y
    parseOptFloat(i, argc, argv, postHeight);
    parseOptFloat(i, argc, argv, postThickness);
    parseOptFloat(i, argc, argv, boxLength);
    parseOptFloat(i, argc, argv, boxWidth);
    parseOptFloat(i, argc, argv, boxHeight);
    if (postHeight <= 0 || postThickness <= 0 ||
        boxLength <= 0 || boxWidth <= 0 || boxHeight <= 0) {
        std::fprintf(stderr,
            "gen-mesh-mailbox: all dims must be > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Vertical post from y=0 to y=postHeight.
    float halfPost = postThickness * 0.5f;
    addBox(0, postHeight * 0.5f, 0,
           halfPost, postHeight * 0.5f, halfPost);
    // Mailbox body: sits on top of the post, slightly wider than
    // the post on each axis so the body visually caps the post.
    float bodyCY = postHeight + boxHeight * 0.5f;
    float halfBoxW = boxWidth  * 0.5f;
    float halfBoxH = boxHeight * 0.5f;
    float halfBoxL = boxLength * 0.5f;
    addBox(0, bodyCY, 0, halfBoxW, halfBoxH, halfBoxL);
    // Small rectangular flag mounted on the right side (+X face)
    // of the body near the front (+Z end). Flag pole is a thin
    // box; the flag itself is a thin square plate at the top of
    // the pole.
    float flagPoleH    = boxHeight * 0.7f;
    float flagPoleT    = postThickness * 0.4f;
    float halfFlagPole = flagPoleT * 0.5f;
    float flagPoleX    = halfBoxW + halfFlagPole;  // sits flush against +X face
    float flagPoleZ    = halfBoxL - flagPoleT * 1.5f;
    float flagPoleCY   = postHeight + boxHeight + flagPoleH * 0.5f;
    addBox(flagPoleX, flagPoleCY, flagPoleZ,
           halfFlagPole, flagPoleH * 0.5f, halfFlagPole);
    // Flag plate at the top of the pole, extending +X away from
    // the body so it reads as a raised flag.
    float flagPlateW = boxHeight * 0.6f;     // along Y (vertical extent)
    float flagPlateL = boxHeight * 0.7f;     // along X (away from body)
    float flagPlateT = flagPoleT * 0.6f;     // along Z (thickness)
    float halfFlagL  = flagPlateL * 0.5f;
    float flagPlateX = flagPoleX + halfFlagL;
    float flagPlateCY = postHeight + boxHeight + flagPoleH - flagPlateW * 0.5f;
    addBox(flagPlateX, flagPlateCY, flagPoleZ,
           halfFlagL, flagPlateW * 0.5f, flagPlateT * 0.5f);
    finalizeAsSingleBatch(wom);
    float totalH = postHeight + boxHeight + flagPoleH;
    float maxX = std::max(halfBoxW, flagPlateX + halfFlagL);
    wom.boundMin = glm::vec3(-halfBoxW, 0.0f,    -halfBoxL);
    wom.boundMax = glm::vec3( maxX,     totalH,   halfBoxL);
    if (!saveWomOrError(wom, womBase, "gen-mesh-mailbox")) return 1;
    printWomWrote(womBase);
    std::printf("  total H    : %.3f\n", totalH);
    std::printf("  post       : %.3f square × %.3f tall\n",
                postThickness, postHeight);
    std::printf("  box body   : %.3f L × %.3f W × %.3f H\n",
                boxLength, boxWidth, boxHeight);
    std::printf("  flag       : pole + plate on +X side near front\n");
    printWomMeshStats(wom);
    return 0;
}

int handleSignpost(int& i, int argc, char** argv) {
    // Signpost: 4-box wayfinding prop - stone base anchor at the
    // ground, tall vertical pole, decorative cap, and one
    // horizontal sign board mounted face-out from the pole near
    // the top. Useful for crossroads, tavern fronts, town
    // entrances, dungeon area markers. The 45th procedural mesh.
    std::string womBase = argv[++i];
    float postHeight    = 2.5f;
    float postThickness = 0.10f;
    float baseSize      = 0.30f;
    float signWidth     = 0.80f;   // along Z (perpendicular to pole face)
    float signHeight    = 0.35f;   // along Y
    parseOptFloat(i, argc, argv, postHeight);
    parseOptFloat(i, argc, argv, postThickness);
    parseOptFloat(i, argc, argv, baseSize);
    parseOptFloat(i, argc, argv, signWidth);
    parseOptFloat(i, argc, argv, signHeight);
    if (postHeight <= 0 || postThickness <= 0 || baseSize <= 0 ||
        signWidth <= 0 || signHeight <= 0 ||
        postThickness >= baseSize) {
        std::fprintf(stderr,
            "gen-mesh-signpost: dims > 0; post must fit in base\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Base plinth at the floor.
    float baseHeight = baseSize * 0.45f;
    float halfBase = baseSize * 0.5f;
    addBox(0, baseHeight * 0.5f, 0,
           halfBase, baseHeight * 0.5f, halfBase);
    // Pole rising above the base.
    float poleBottomY = baseHeight;
    float poleTopY    = baseHeight + postHeight;
    float poleCY      = (poleBottomY + poleTopY) * 0.5f;
    float halfPole    = postThickness * 0.5f;
    addBox(0, poleCY, 0,
           halfPole, postHeight * 0.5f, halfPole);
    // Sign board: thin rectangle mounted on the pole near the top.
    // signWidth runs along Z (the long axis), signHeight along Y,
    // and a sliver of postThickness along X - a billboard that
    // reads as a sign when viewed from either +Z or -Z.
    float signCenterY = poleTopY - signHeight * 0.7f;
    float signThickness = postThickness * 0.6f;
    addBox(0, signCenterY, 0,
           signThickness * 0.5f, signHeight * 0.5f, signWidth * 0.5f);
    // Decorative cap on top of the pole.
    float capHeight = postThickness * 0.8f;
    float capCY = poleTopY + capHeight * 0.5f;
    float halfCap = postThickness * 0.9f;
    addBox(0, capCY, 0,
           halfCap, capHeight * 0.5f, halfCap);
    finalizeAsSingleBatch(wom);
    float totalH = capCY + capHeight * 0.5f;
    float halfSignZ = signWidth * 0.5f;
    wom.boundMin = glm::vec3(-std::max(halfBase, halfSignZ), 0.0f,    -halfSignZ);
    wom.boundMax = glm::vec3( std::max(halfBase, halfSignZ), totalH,   halfSignZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-signpost")) return 1;
    printWomWrote(womBase);
    std::printf("  total H    : %.3f\n", totalH);
    std::printf("  base       : %.3f square × %.3f tall\n",
                baseSize, baseHeight);
    std::printf("  pole       : %.3f square × %.3f tall\n",
                postThickness, postHeight);
    std::printf("  sign board : %.3f × %.3f (wide × tall)\n",
                signWidth, signHeight);
    printWomMeshStats(wom);
    return 0;
}

int handleWell(int& i, int argc, char** argv) {
    // Well: 4 stone walls arranged in a square ring (hollow
    // interior so a player can see down the shaft) + 2 vertical
    // roof posts on opposite sides + 1 horizontal cross beam at
    // the top (where the rope/bucket would mount). Useful for
    // village squares, courtyards, dungeon water sources.
    // The 44th procedural mesh primitive.
    std::string womBase = argv[++i];
    float outerSize = 1.4f;     // square wall outer footprint
    float wallH     = 0.8f;     // wall height above ground
    float wallT     = 0.15f;    // wall thickness
    float postH     = 1.6f;     // roof post height above wall
    float postT     = 0.12f;    // roof post thickness (square)
    parseOptFloat(i, argc, argv, outerSize);
    parseOptFloat(i, argc, argv, wallH);
    parseOptFloat(i, argc, argv, wallT);
    parseOptFloat(i, argc, argv, postH);
    parseOptFloat(i, argc, argv, postT);
    if (outerSize <= 0 || wallH <= 0 || wallT <= 0 ||
        postH <= 0 || postT <= 0 || wallT * 2 >= outerSize) {
        std::fprintf(stderr,
            "gen-mesh-well: dims > 0; wallT must fit in outerSize\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfOuter = outerSize * 0.5f;
    float halfWallT = wallT * 0.5f;
    float halfPostT = postT * 0.5f;
    // 4 wall panels arranged in a hollow square. Each panel
    // spans full outerSize along its long axis. Walls along X
    // sit at z = ±(halfOuter - halfWallT); walls along Z sit at
    // x = ±(halfOuter - halfWallT) but shortened so they don't
    // overlap the X walls (interior length = outerSize - 2*wallT).
    float wallCY = wallH * 0.5f;
    // North wall (+Z edge) - full outerSize wide.
    addBox(0, wallCY, halfOuter - halfWallT,
           halfOuter, wallH * 0.5f, halfWallT);
    // South wall (-Z edge) - full outerSize wide.
    addBox(0, wallCY, -halfOuter + halfWallT,
           halfOuter, wallH * 0.5f, halfWallT);
    // East wall (+X edge) - interior length only.
    float eastWestLen = outerSize - 2 * wallT;
    addBox(halfOuter - halfWallT, wallCY, 0,
           halfWallT, wallH * 0.5f, eastWestLen * 0.5f);
    // West wall (-X edge) - interior length only.
    addBox(-halfOuter + halfWallT, wallCY, 0,
           halfWallT, wallH * 0.5f, eastWestLen * 0.5f);
    // 2 vertical roof posts mounted on top of the east and west
    // walls, centred in z. Posts rise from the top of the walls
    // (y=wallH) by postH.
    float postCY = wallH + postH * 0.5f;
    float postX = halfOuter - halfPostT;
    addBox( postX, postCY, 0, halfPostT, postH * 0.5f, halfPostT);
    addBox(-postX, postCY, 0, halfPostT, postH * 0.5f, halfPostT);
    // Horizontal cross beam connecting the post tops. The beam
    // spans the full distance between posts (so it ends inside
    // each post). Beam is square in cross section, slightly
    // thicker than the posts so it visually overlaps the joint.
    float beamT = postT * 1.2f;
    float halfBeamT = beamT * 0.5f;
    float beamCY = wallH + postH - halfBeamT;
    addBox(0, beamCY, 0,
           halfOuter * 0.85f, halfBeamT, halfBeamT);
    finalizeAsSingleBatch(wom);
    float totalH = wallH + postH;
    wom.boundMin = glm::vec3(-halfOuter, 0.0f,    -halfOuter);
    wom.boundMax = glm::vec3( halfOuter, totalH,   halfOuter);
    if (!saveWomOrError(wom, womBase, "gen-mesh-well")) return 1;
    printWomWrote(womBase);
    std::printf("  outerSize  : %.3f square\n", outerSize);
    std::printf("  wall       : %.3f tall, %.3f thick\n", wallH, wallT);
    std::printf("  roof posts : 2 × %.3f tall\n", postH);
    std::printf("  total H    : %.3f (with cross beam)\n", totalH);
    printWomMeshStats(wom);
    return 0;
}

int handleLadder(int& i, int argc, char** argv) {
    // Ladder: 2 vertical rails + N horizontal rungs evenly
    // spaced between them. Sits flat against +Z (the climbing
    // face) so it can be parented to walls / wagons / ship
    // hulls. The 43rd procedural mesh primitive - useful for
    // attics, ship rigging, dungeons, mage towers.
    std::string womBase = argv[++i];
    float height = 3.0f;
    float width  = 0.6f;
    int   rungs  = 8;
    float railT  = 0.06f;
    float rungT  = 0.04f;
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, width);
    parseOptInt(i, argc, argv, rungs);
    parseOptFloat(i, argc, argv, railT);
    parseOptFloat(i, argc, argv, rungT);
    if (height <= 0 || width <= 0 || railT <= 0 || rungT <= 0 ||
        rungs < 2 || rungs > 64 || railT * 2 >= width) {
        std::fprintf(stderr,
            "gen-mesh-ladder: dims > 0; rungs 2..64; rails must fit in width\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfW = width  * 0.5f;
    float halfRail = railT * 0.5f;
    float halfRung = rungT * 0.5f;
    // 2 rails: full-height vertical boxes at x = ±(halfW - halfRail).
    float railX = halfW - halfRail;
    float railCY = height * 0.5f;
    addBox( railX, railCY, 0, halfRail, height * 0.5f, halfRail);
    addBox(-railX, railCY, 0, halfRail, height * 0.5f, halfRail);
    // N rungs: horizontal boxes between rails, evenly spaced.
    // First rung is rungSpacing/2 from the bottom; last is the
    // same distance from the top - keeps the ladder symmetric.
    // Rung interior length is width - 2*railT (between the rails).
    float rungLen = width - 2 * railT;
    float halfRungLen = rungLen * 0.5f;
    float rungSpacing = height / static_cast<float>(rungs + 1);
    for (int r = 0; r < rungs; ++r) {
        float rungCY = (r + 1) * rungSpacing;
        addBox(0, rungCY, 0, halfRungLen, halfRung, halfRung);
    }
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-halfW, 0.0f,    -halfRail);
    wom.boundMax = glm::vec3( halfW, height,   halfRail);
    if (!saveWomOrError(wom, womBase, "gen-mesh-ladder")) return 1;
    printWomWrote(womBase);
    std::printf("  size       : %.3f wide × %.3f tall\n", width, height);
    std::printf("  rails      : 2 × %.3f square (full height)\n", railT);
    std::printf("  rungs      : %d × %.3f (spacing %.3f)\n",
                rungs, rungT, rungSpacing);
    printWomMeshStats(wom);
    return 0;
}

int handleBed(int& i, int argc, char** argv) {
    // Bed: 7-box bedroom prop - flat mattress slab, tall
    // headboard at one end, short shorter footboard at the
    // other, 4 corner legs, and a small pillow box at the
    // headboard end. Pairs with --gen-mesh-table /
    // --gen-mesh-bookshelf for inn rooms, manor bedrooms,
    // barracks. The 42nd procedural mesh primitive.
    std::string womBase = argv[++i];
    float length    = 2.0f;     // along Z (head-to-foot)
    float width     = 1.2f;     // along X
    float legHeight = 0.30f;
    float matThick  = 0.20f;
    float headH     = 1.0f;     // headboard height above mattress
    float footH     = 0.4f;     // footboard height above mattress
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, legHeight);
    parseOptFloat(i, argc, argv, matThick);
    parseOptFloat(i, argc, argv, headH);
    parseOptFloat(i, argc, argv, footH);
    if (length <= 0 || width <= 0 || legHeight <= 0 ||
        matThick <= 0 || headH <= 0 || footH <= 0) {
        std::fprintf(stderr, "gen-mesh-bed: all dims must be > 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfL = length * 0.5f;
    float halfW = width  * 0.5f;
    float legT  = std::min(width, length) * 0.06f;  // square cross-section
    float halfLeg = legT * 0.5f;
    // Head end is at +Z, foot end at -Z.
    // 4 legs: one per corner, inset from edges by half a leg-thickness.
    float legX = halfW - halfLeg;
    float legZ = halfL - halfLeg;
    float legCY = legHeight * 0.5f;
    addBox( legX, legCY,  legZ, halfLeg, legHeight * 0.5f, halfLeg);
    addBox(-legX, legCY,  legZ, halfLeg, legHeight * 0.5f, halfLeg);
    addBox( legX, legCY, -legZ, halfLeg, legHeight * 0.5f, halfLeg);
    addBox(-legX, legCY, -legZ, halfLeg, legHeight * 0.5f, halfLeg);
    // Mattress: spans full width × length, sits on top of legs.
    float matBottomY = legHeight;
    float matCY = matBottomY + matThick * 0.5f;
    addBox(0, matCY, 0, halfW, matThick * 0.5f, halfL);
    // Headboard: tall thin slab at +Z end, spanning full width.
    // Sits on top of the mattress base (its bottom is at matBottomY).
    float headThick = legT * 1.4f;
    float headCY = matBottomY + headH * 0.5f;
    addBox(0, headCY, halfL - headThick * 0.5f,
           halfW, headH * 0.5f, headThick * 0.5f);
    // Footboard: shorter slab at -Z end.
    float footCY = matBottomY + footH * 0.5f;
    addBox(0, footCY, -halfL + headThick * 0.5f,
           halfW, footH * 0.5f, headThick * 0.5f);
    // Pillow: small box on the mattress, near the headboard end.
    float pillowW    = halfW * 1.6f;     // 80% of mattress width
    float pillowL    = halfL * 0.25f;    // ~12.5% of mattress length
    float pillowH    = matThick * 0.5f;
    float pillowCY   = matBottomY + matThick + pillowH * 0.5f;
    float pillowZ    = halfL - pillowL - headThick;
    addBox(0, pillowCY, pillowZ,
           pillowW * 0.5f, pillowH * 0.5f, pillowL * 0.5f);
    finalizeAsSingleBatch(wom);
    float totalH = matBottomY + std::max({matThick + pillowH, headH, footH});
    wom.boundMin = glm::vec3(-halfW, 0.0f,    -halfL);
    wom.boundMax = glm::vec3( halfW, totalH,   halfL);
    if (!saveWomOrError(wom, womBase, "gen-mesh-bed")) return 1;
    printWomWrote(womBase);
    std::printf("  size       : %.3f x %.3f x %.3f (W x H x L)\n",
                width, totalH, length);
    std::printf("  mattress   : %.3f thick at y=%.3f\n",
                matThick, matBottomY);
    std::printf("  headboard  : %.3f tall (foot %.3f tall)\n",
                headH, footH);
    printWomMeshStats(wom);
    return 0;
}

int handleLamppost(int& i, int argc, char** argv) {
    // Lamppost: 4-box urban prop - square base plinth, tall
    // vertical pole, lantern body box around the pole top,
    // and a small cap box on top. Useful for streets, plazas,
    // taverns, anywhere that wants explicit lighting fixtures.
    // The 41st procedural mesh primitive.
    std::string womBase = argv[++i];
    float postHeight    = 3.0f;
    float postThickness = 0.12f;
    float baseSize      = 0.4f;
    float lanternSize   = 0.35f;
    float lanternHeight = 0.5f;
    parseOptFloat(i, argc, argv, postHeight);
    parseOptFloat(i, argc, argv, postThickness);
    parseOptFloat(i, argc, argv, baseSize);
    parseOptFloat(i, argc, argv, lanternSize);
    parseOptFloat(i, argc, argv, lanternHeight);
    if (postHeight <= 0 || postThickness <= 0 || baseSize <= 0 ||
        lanternSize <= 0 || lanternHeight <= 0 ||
        postThickness >= baseSize || postThickness >= lanternSize) {
        std::fprintf(stderr,
            "gen-mesh-lamppost: dims > 0; post must fit in base & lantern\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Base plinth: low square slab at the floor.
    float baseHeight = baseSize * 0.4f;
    float halfBase = baseSize * 0.5f;
    addBox(0, baseHeight * 0.5f, 0,
           halfBase, baseHeight * 0.5f, halfBase);
    // Vertical pole: thin square box from top of base to top.
    float poleBottomY = baseHeight;
    float poleTopY    = baseHeight + postHeight;
    float poleCY      = (poleBottomY + poleTopY) * 0.5f;
    float halfPole    = postThickness * 0.5f;
    addBox(0, poleCY, 0,
           halfPole, postHeight * 0.5f, halfPole);
    // Lantern body: box centred on the top of the pole; bottom
    // of the box overlaps the pole so the lamp visually 'caps'
    // the pole rather than just floating above it.
    float halfLantern = lanternSize * 0.5f;
    float lanternBottomY = poleTopY - lanternHeight * 0.3f;
    float lanternCY = lanternBottomY + lanternHeight * 0.5f;
    addBox(0, lanternCY, 0,
           halfLantern, lanternHeight * 0.5f, halfLantern);
    // Cap: thin square plate on top of the lantern. Slightly
    // wider than the lantern body so the cap reads as an awning.
    float capH    = lanternHeight * 0.18f;
    float capSize = lanternSize * 1.15f;
    float halfCap = capSize * 0.5f;
    float capCY   = lanternBottomY + lanternHeight + capH * 0.5f;
    addBox(0, capCY, 0,
           halfCap, capH * 0.5f, halfCap);
    finalizeAsSingleBatch(wom);
    float totalH = capCY + capH * 0.5f;
    wom.boundMin = glm::vec3(-halfBase, 0.0f,    -halfBase);
    wom.boundMax = glm::vec3( halfBase, totalH,   halfBase);
    if (!saveWomOrError(wom, womBase, "gen-mesh-lamppost")) return 1;
    printWomWrote(womBase);
    std::printf("  total H    : %.3f\n", totalH);
    std::printf("  base       : %.3f square × %.3f tall\n",
                baseSize, baseHeight);
    std::printf("  pole       : %.3f square × %.3f tall\n",
                postThickness, postHeight);
    std::printf("  lantern    : %.3f square × %.3f tall (with cap)\n",
                lanternSize, lanternHeight);
    printWomMeshStats(wom);
    return 0;
}

int handleTable(int& i, int argc, char** argv) {
    // Table: 5 boxes - flat tabletop slab on top of 4 vertical
    // legs at each corner. Thinnest of the furniture meshes,
    // pairs naturally with --gen-mesh-bench / --gen-mesh-throne
    // for taverns and dining halls. The 40th procedural mesh
    // primitive.
    std::string womBase = argv[++i];
    float width  = 1.6f;     // along X
    float depth  = 1.0f;     // along Z
    float height = 0.85f;    // along Y (top of tabletop)
    float legT   = 0.10f;    // leg thickness (square cross-section)
    float topT   = 0.06f;    // tabletop thickness
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, legT);
    parseOptFloat(i, argc, argv, topT);
    if (width <= 0 || depth <= 0 || height <= 0 || legT <= 0 ||
        topT <= 0 || legT * 2 > width || legT * 2 > depth ||
        topT >= height) {
        std::fprintf(stderr,
            "gen-mesh-table: dims > 0; legT must fit in width/depth; topT < height\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    float halfW = width  * 0.5f;
    float halfD = depth  * 0.5f;
    float halfLeg = legT * 0.5f;
    float legHeight = height - topT;
    // Tabletop: spans full width × depth, sits at y=height-topT to y=height.
    addBox(0, height - topT * 0.5f, 0,
           halfW, topT * 0.5f, halfD);
    // 4 legs: one at each corner, inset by legT/2 from the edge.
    float legCY = legHeight * 0.5f;
    float legX  = halfW - halfLeg;
    float legZ  = halfD - halfLeg;
    addBox( legX, legCY,  legZ, halfLeg, legHeight * 0.5f, halfLeg);
    addBox(-legX, legCY,  legZ, halfLeg, legHeight * 0.5f, halfLeg);
    addBox( legX, legCY, -legZ, halfLeg, legHeight * 0.5f, halfLeg);
    addBox(-legX, legCY, -legZ, halfLeg, legHeight * 0.5f, halfLeg);
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-halfW, 0.0f,    -halfD);
    wom.boundMax = glm::vec3( halfW, height,   halfD);
    if (!saveWomOrError(wom, womBase, "gen-mesh-table")) return 1;
    printWomWrote(womBase);
    std::printf("  size       : %.3f x %.3f x %.3f\n", width, height, depth);
    std::printf("  legs       : 4 × %.3f square (%.3f tall)\n",
                legT, legHeight);
    std::printf("  top thick  : %.3f\n", topT);
    printWomMeshStats(wom);
    return 0;
}

int handleBookshelf(int& i, int argc, char** argv) {
    // Bookshelf: cabinet (5 panels: back / left / right / top /
    // bottom) divided by N-1 horizontal shelves, with rows of
    // thin "book" boxes at varying heights on each shelf.
    // Books sway in width and height pseudo-randomly so the
    // shelf doesn't read as a perfect grid. The 39th procedural
    // mesh primitive.
    std::string womBase = argv[++i];
    float width  = 1.5f;
    float height = 2.0f;
    float depth  = 0.4f;
    int shelves  = 4;
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, depth);
    parseOptInt(i, argc, argv, shelves);
    if (width <= 0 || height <= 0 || depth <= 0 ||
        shelves < 2 || shelves > 12) {
        std::fprintf(stderr,
            "gen-mesh-bookshelf: dims > 0; shelves must be 2..12\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Cabinet skin: thickness scales with the smaller cabinet
    // dimension so the shelf reads at any size without becoming
    // either too chunky or too flimsy.
    float panelT = std::min(width, depth) * 0.06f;
    float halfW = width  * 0.5f;
    float halfD = depth  * 0.5f;
    // Bottom + top panels span full width and depth.
    addBox(0, panelT * 0.5f, 0,
           halfW, panelT * 0.5f, halfD);
    addBox(0, height - panelT * 0.5f, 0,
           halfW, panelT * 0.5f, halfD);
    // Left + right side panels span between bottom and top panels.
    float sideCY = (panelT + (height - panelT)) * 0.5f;
    float sideHY = (height - 2 * panelT) * 0.5f;
    addBox(-halfW + panelT * 0.5f, sideCY, 0,
           panelT * 0.5f, sideHY, halfD);
    addBox( halfW - panelT * 0.5f, sideCY, 0,
           panelT * 0.5f, sideHY, halfD);
    // Back panel - thin slab at the rear of the cabinet.
    addBox(0, sideCY, -halfD + panelT * 0.5f,
           halfW - panelT, sideHY, panelT * 0.5f);
    // Horizontal shelves divide the interior into 'shelves' bays.
    // shelf[0] is the cabinet bottom, shelf[shelves] is the top -
    // we only emit the (shelves-1) interior shelves between them.
    float interiorTop = height - panelT;
    float interiorBottom = panelT;
    float bayHeight = (interiorTop - interiorBottom) /
                      static_cast<float>(shelves);
    float shelfT = panelT;  // shelf thickness matches panel skin
    float interiorHalfW = halfW - panelT;
    for (int s = 1; s < shelves; ++s) {
        float shelfCY = interiorBottom + s * bayHeight - shelfT * 0.5f;
        addBox(0, shelfCY, 0,
               interiorHalfW, shelfT * 0.5f, halfD - panelT * 0.5f);
    }
    // Books: per-bay row of thin boxes leaning along the shelf.
    // Pseudo-random width/height variation seeded by bay index so
    // re-generating the same shelf gives the same layout.
    auto rngStep = [](uint32_t& s) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    };
    int totalBooks = 0;
    for (int s = 0; s < shelves; ++s) {
        // Bottom Y of this bay's books = top of the shelf below.
        float bayBottomY = (s == 0) ? interiorBottom
                                    : interiorBottom + s * bayHeight;
        float bayTopY    = interiorBottom + (s + 1) * bayHeight - shelfT;
        if (s == shelves - 1) bayTopY = interiorTop - shelfT;
        float availableH = bayTopY - bayBottomY;
        if (availableH < bayHeight * 0.3f) continue;
        // Lay books from left to right with narrow gaps. Variable
        // book widths are 50–120% of nominal - yields ~6 books per
        // bay at default size.
        float nominalBookW = bayHeight * 0.18f;
        float bookHalfD    = (halfD - panelT) * 0.7f;
        uint32_t rng = static_cast<uint32_t>(s * 0x9E3779B9u + 1);
        float cursor = -interiorHalfW + nominalBookW * 0.6f;
        while (cursor + nominalBookW < interiorHalfW) {
            float wScale = 0.5f + (rngStep(rng) & 0xFFFF) / 65535.0f * 0.7f;
            float hScale = 0.7f + (rngStep(rng) & 0xFFFF) / 65535.0f * 0.3f;
            float bookW  = nominalBookW * wScale;
            float bookH  = availableH * 0.85f * hScale;
            if (cursor + bookW > interiorHalfW) break;
            addBox(cursor + bookW * 0.5f,
                   bayBottomY + bookH * 0.5f,
                   0,
                   bookW * 0.5f, bookH * 0.5f, bookHalfD);
            cursor += bookW + nominalBookW * 0.05f;
            totalBooks++;
        }
    }
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-halfW, 0.0f,    -halfD);
    wom.boundMax = glm::vec3( halfW, height,   halfD);
    if (!saveWomOrError(wom, womBase, "gen-mesh-bookshelf")) return 1;
    printWomWrote(womBase);
    std::printf("  size       : %.3f x %.3f x %.3f\n", width, height, depth);
    std::printf("  shelves    : %d (%d books across all bays)\n",
                shelves, totalBooks);
    printWomMeshStats(wom);
    return 0;
}

int handleTent(int& i, int argc, char** argv) {
    // A-frame canvas tent: ridge running along X from
    // (-L/2, H, 0) to (+L/2, H, 0); rectangular footprint LxW
    // on the ground; two sloped roof panels meeting at the ridge
    // and two triangular gables closing the ends. Optionally a
    // simple inverted-V door notch is cut from the +X gable so
    // there is a visible entrance. Watertight bottom face is
    // included so the model is a closed solid for collision
    // baking. The 53rd procedural mesh primitive.
    std::string womBase = argv[++i];
    float length = 1.6f;
    float width  = 1.0f;
    float height = 0.9f;
    float doorH  = 0.5f;
    float doorW  = 0.4f;
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, doorH);
    parseOptFloat(i, argc, argv, doorW);
    if (length <= 0 || width <= 0 || height <= 0 ||
        doorH < 0 || doorH >= height ||
        doorW < 0 || doorW >= width) {
        std::fprintf(stderr,
            "gen-mesh-tent: dims > 0; doorH < height; doorW < width\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    const float L2 = length * 0.5f;
    const float W2 = width  * 0.5f;
    // Slope normals for the two roof panels - built from the panel
    // edge vectors then normalized so adjacent vertices share the
    // same per-face shading.
    glm::vec3 nBack = glm::normalize(glm::vec3(0.0f, W2, -height));
    glm::vec3 nFront = glm::normalize(glm::vec3(0.0f, W2,  height));
    // Back roof panel (faces -Z and +Y): A=(-L2,0,-W2), B=(+L2,0,-W2),
    // R1=(+L2,H,0), R0=(-L2,H,0). Quad → 2 triangles, CCW from outside.
    {
        uint32_t a = addV({-L2, 0, -W2}, nBack, {0, 0});
        uint32_t b = addV({+L2, 0, -W2}, nBack, {1, 0});
        uint32_t r1 = addV({+L2, height, 0}, nBack, {1, 1});
        uint32_t r0 = addV({-L2, height, 0}, nBack, {0, 1});
        wom.indices.insert(wom.indices.end(), {a, b, r1, a, r1, r0});
    }
    // Front roof panel (faces +Z and +Y).
    {
        uint32_t d = addV({-L2, 0, +W2}, nFront, {0, 0});
        uint32_t r0 = addV({-L2, height, 0}, nFront, {0, 1});
        uint32_t r1 = addV({+L2, height, 0}, nFront, {1, 1});
        uint32_t c = addV({+L2, 0, +W2}, nFront, {1, 0});
        wom.indices.insert(wom.indices.end(), {d, r0, r1, d, r1, c});
    }
    // -X gable (full triangle, no door): A=(-L2,0,-W2), R0=(-L2,H,0),
    // D=(-L2,0,+W2). Faces -X.
    {
        glm::vec3 n(-1, 0, 0);
        uint32_t a = addV({-L2, 0, -W2}, n, {0, 0});
        uint32_t r0 = addV({-L2, height, 0}, n, {0.5f, 1});
        uint32_t d = addV({-L2, 0, +W2}, n, {1, 0});
        wom.indices.insert(wom.indices.end(), {a, r0, d});
    }
    // +X gable: B=(+L2,0,-W2), C=(+L2,0,+W2), R1=(+L2,H,0). Faces +X.
    // If doorH>0 we carve out a tapered notch - bottom edge of width
    // doorW, apex on the centerline at height doorH. The remaining
    // polygon (B → bl → dt → br → C → R1, going CCW from -X view to
    // match the existing gable winding) is triangulated as a fan
    // from R1: 4 triangles, no overlap with the door area, every
    // vertex referenced by at least one triangle.
    {
        glm::vec3 n(+1, 0, 0);
        if (doorH > 0 && doorW > 0) {
            uint32_t r1 = addV({+L2, height, 0}, n, {0.5f, 1});
            uint32_t b  = addV({+L2, 0, -W2}, n, {0, 0});
            uint32_t bl = addV({+L2, 0, -doorW * 0.5f}, n,
                               {0.5f - doorW / (2 * width), 0});
            uint32_t dt = addV({+L2, doorH, 0}, n,
                               {0.5f, doorH / height});
            uint32_t br = addV({+L2, 0, +doorW * 0.5f}, n,
                               {0.5f + doorW / (2 * width), 0});
            uint32_t c  = addV({+L2, 0, +W2}, n, {1, 0});
            wom.indices.insert(wom.indices.end(), {r1, b,  bl});
            wom.indices.insert(wom.indices.end(), {r1, bl, dt});
            wom.indices.insert(wom.indices.end(), {r1, dt, br});
            wom.indices.insert(wom.indices.end(), {r1, br, c});
        } else {
            uint32_t b = addV({+L2, 0, -W2}, n, {0, 0});
            uint32_t c = addV({+L2, 0, +W2}, n, {1, 0});
            uint32_t r1 = addV({+L2, height, 0}, n, {0.5f, 1});
            wom.indices.insert(wom.indices.end(), {b, c, r1});
        }
    }
    // Ground face (faces -Y) so the tent is a closed solid for
    // collision baking.
    {
        glm::vec3 n(0, -1, 0);
        uint32_t a = addV({-L2, 0, -W2}, n, {0, 0});
        uint32_t b = addV({+L2, 0, -W2}, n, {1, 0});
        uint32_t c = addV({+L2, 0, +W2}, n, {1, 1});
        uint32_t d = addV({-L2, 0, +W2}, n, {0, 1});
        wom.indices.insert(wom.indices.end(), {a, d, c, a, c, b});
    }
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-L2, 0, -W2);
    wom.boundMax = glm::vec3(+L2, height, +W2);
    if (!saveWomOrError(wom, womBase, "gen-mesh-tent")) return 1;
    printWomWrote(womBase);
    std::printf("  footprint  : %.3f x %.3f\n", length, width);
    std::printf("  height     : %.3f (ridge along X)\n", height);
    if (doorH > 0 && doorW > 0) {
        std::printf("  door       : H=%.3f W=%.3f on +X gable\n",
                    doorH, doorW);
    } else {
        std::printf("  door       : (none)\n");
    }
    printWomMeshStats(wom);
    return 0;
}

int handleMug(int& i, int argc, char** argv) {
    // Drinking mug / tankard: closed cylindrical body plus a
    // small box handle attached to the side. Without rotation
    // the handle is a thin slab rather than a real C-loop, but
    // the asymmetric silhouette still reads as a handled cup
    // distinct from --gen-mesh-chalice (3-tier goblet) and
    // --gen-mesh-well-pail (top-handle bucket). The 87th
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float bodyR    = 0.05f;
    float bodyH    = 0.12f;
    float handleW  = 0.025f;     // handle thickness (inset from body)
    float handleH  = 0.08f;      // handle vertical extent
    float handleArm = 0.04f;     // how far handle extends from body
    int   sides    = 14;
    parseOptFloat(i, argc, argv, bodyR);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, handleW);
    parseOptFloat(i, argc, argv, handleH);
    parseOptFloat(i, argc, argv, handleArm);
    parseOptInt(i, argc, argv, sides);
    if (bodyR <= 0 || bodyH <= 0 || handleW <= 0 || handleH <= 0 ||
        handleArm <= 0 || handleH >= bodyH ||
        sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-mug: dims > 0; sides 6..64; handleH < bodyH\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    addClosedCylinderY(wom, bodyR, 0.0f, bodyH, sides);
    // Handle: thin box positioned on +X side of body, centered
    // vertically. Pushes outward by handleArm/2 + bodyR so its
    // inner face touches the body.
    const float handleCY = bodyH * 0.5f;
    const float handleCX = bodyR + handleArm * 0.5f;
    addFlatBox(wom, handleCX, handleCY, 0.0f,
               handleArm * 0.5f, handleH * 0.5f, handleW * 0.5f);
    finalizeAsSingleBatch(wom);
    float maxX = bodyR + handleArm;
    setCenteredBoundsXZ(wom, maxX, bodyR, bodyH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-mug")) return 1;
    printWomWrote(womBase);
    std::printf("  body       : R=%.3f x %.3f tall (%d sides)\n",
                bodyR, bodyH, sides);
    std::printf("  handle     : %.3f arm x %.3f tall x %.3f thick\n",
                handleArm, handleH, handleW);
    printWomMeshStats(wom);
    return 0;
}

int handleMortarPestle(int& i, int argc, char** argv) {
    // Mortar and pestle: a wider-than-tall closed cylinder
    // (the mortar bowl) with a thin tall closed cylinder
    // (the pestle) standing inside its rim. The bowl reads
    // as carved stone or wood; the pestle reads as a small
    // grinding rod. The 90th procedural mesh primitive.
    //
    // Distinct from --gen-mesh-cauldron (much larger, with
    // legs) and --gen-mesh-chalice (3-tier goblet with no
    // separate utensil).
    std::string womBase = argv[++i];
    float bowlR    = 0.06f;
    float bowlH    = 0.05f;
    float pestleR  = 0.012f;
    float pestleH  = 0.10f;
    int   sides    = 14;
    parseOptFloat(i, argc, argv, bowlR);
    parseOptFloat(i, argc, argv, bowlH);
    parseOptFloat(i, argc, argv, pestleR);
    parseOptFloat(i, argc, argv, pestleH);
    parseOptInt(i, argc, argv, sides);
    if (bowlR <= 0 || bowlH <= 0 || pestleR <= 0 || pestleH <= 0 ||
        pestleR >= bowlR || sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-mortar-pestle: dims > 0; sides 6..64; "
            "pestleR < bowlR\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Bowl sits on the ground; pestle is centered laterally
    // and rises up out of the bowl.
    addClosedCylinderY(wom, bowlR, 0.0f, bowlH, sides);
    addClosedCylinderY(wom, pestleR, bowlH * 0.5f,
                       bowlH * 0.5f + pestleH, sides);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, bowlR, bowlR, bowlH + pestleH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-mortar-pestle")) return 1;
    printWomWrote(womBase);
    std::printf("  bowl       : R=%.3f x %.3f tall (%d sides)\n",
                bowlR, bowlH, sides);
    std::printf("  pestle     : R=%.3f x %.3f tall\n",
                pestleR, pestleH);
    printWomMeshStats(wom);
    return 0;
}

int handleRuneStone(int& i, int argc, char** argv) {
    // Standing rune stone: a wide flat base block plus a tall
    // narrow monolith standing on top. Reads as a small carved
    // standing-stone marker - fits alongside --gen-mesh-altar
    // / --gen-mesh-shrine / --gen-mesh-tombstone in the
    // ritual-prop family. Distinct from --gen-mesh-tombstone
    // (curved top, narrower) and --gen-mesh-pillar (round
    // column). The 91st procedural mesh primitive.
    std::string womBase = argv[++i];
    float baseW    = 0.40f;     // base block width  (X)
    float baseD    = 0.30f;     // base block depth  (Z)
    float baseH    = 0.10f;     // base block height (Y)
    float stoneW   = 0.20f;     // monolith width
    float stoneD   = 0.12f;     // monolith depth
    float stoneH   = 0.80f;     // monolith height
    parseOptFloat(i, argc, argv, baseW);
    parseOptFloat(i, argc, argv, baseD);
    parseOptFloat(i, argc, argv, baseH);
    parseOptFloat(i, argc, argv, stoneW);
    parseOptFloat(i, argc, argv, stoneD);
    parseOptFloat(i, argc, argv, stoneH);
    if (baseW <= 0 || baseD <= 0 || baseH <= 0 ||
        stoneW <= 0 || stoneD <= 0 || stoneH <= 0 ||
        stoneW > baseW || stoneD > baseD) {
        std::fprintf(stderr,
            "gen-mesh-rune-stone: dims > 0; stone(W,D) <= base(W,D)\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Base block sits on the ground.
    addFlatBox(wom, 0.0f, baseH * 0.5f, 0.0f,
               baseW * 0.5f, baseH * 0.5f, baseD * 0.5f);
    // Monolith stands centered on the base.
    const float stoneCY = baseH + stoneH * 0.5f;
    addFlatBox(wom, 0.0f, stoneCY, 0.0f,
               stoneW * 0.5f, stoneH * 0.5f, stoneD * 0.5f);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, baseW * 0.5f, baseD * 0.5f, baseH + stoneH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-rune-stone")) return 1;
    printWomWrote(womBase);
    std::printf("  base       : %.2f x %.2f x %.2f tall\n",
                baseW, baseD, baseH);
    std::printf("  monolith   : %.2f x %.2f x %.2f tall\n",
                stoneW, stoneD, stoneH);
    printWomMeshStats(wom);
    return 0;
}

int handleWellPail(int& i, int argc, char** argv) {
    // Wooden well-pail / bucket: a closed cylindrical body
    // (collision-watertight) plus a thin horizontal handle bar
    // floating just above the open-top side. Without rotation
    // the handle is a straight box rather than a true semicircle,
    // but the silhouette still reads as a bucket. The 86th
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float bodyR    = 0.14f;
    float bodyH    = 0.18f;
    float handleW  = 0.32f;       // horizontal extent of handle
    float handleT  = 0.015f;      // handle thickness
    float handleArc = 0.08f;      // distance handle floats above the rim
    int   sides    = 14;
    parseOptFloat(i, argc, argv, bodyR);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, handleW);
    parseOptFloat(i, argc, argv, handleT);
    parseOptFloat(i, argc, argv, handleArc);
    parseOptInt(i, argc, argv, sides);
    if (bodyR <= 0 || bodyH <= 0 || handleW <= 0 || handleT <= 0 ||
        handleArc < 0 || sides < 6 || sides > 64 ||
        handleW * 0.5f < bodyR) {
        std::fprintf(stderr,
            "gen-mesh-well-pail: dims > 0; sides 6..64; "
            "handleW/2 >= bodyR\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    addClosedCylinderY(wom, bodyR, 0.0f, bodyH, sides);
    // Handle: thin box centered above the rim, spanning the
    // pail diameter plus a small overhang so its ends look like
    // they meet the rim's outside.
    const float handleCY = bodyH + handleArc + handleT * 0.5f;
    addFlatBox(wom, 0.0f, handleCY, 0.0f,
               handleW * 0.5f, handleT * 0.5f, handleT * 0.5f);
    finalizeAsSingleBatch(wom);
    float maxR = std::max(bodyR, handleW * 0.5f);
    float topY = bodyH + handleArc + handleT;
    setCenteredBoundsXZ(wom, maxR, maxR, topY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-well-pail")) return 1;
    printWomWrote(womBase);
    std::printf("  body       : R=%.3f x %.3f tall\n", bodyR, bodyH);
    std::printf("  handle     : %.3f wide x %.3f thick at +%.3f arc\n",
                handleW, handleT, handleArc);
    printWomMeshStats(wom);
    return 0;
}

int handleStove(int& i, int argc, char** argv) {
    // Pot-bellied wood stove: wide cylindrical body + thin
    // tall chimney column rising from the top of the body.
    // Distinct from --gen-mesh-forge (square stone hearth +
    // hood + chimney) - stove is the round-cylinder home/
    // workshop variant. The 85th procedural mesh primitive.
    std::string womBase = argv[++i];
    float bodyR    = 0.30f;
    float bodyH    = 0.65f;
    float chimneyR = 0.07f;
    float chimneyH = 1.10f;
    int   sides    = 16;
    parseOptFloat(i, argc, argv, bodyR);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, chimneyR);
    parseOptFloat(i, argc, argv, chimneyH);
    parseOptInt(i, argc, argv, sides);
    if (bodyR <= 0 || bodyH <= 0 || chimneyR <= 0 || chimneyH <= 0 ||
        sides < 6 || sides > 64 || chimneyR >= bodyR) {
        std::fprintf(stderr,
            "gen-mesh-stove: dims > 0; sides 6..64; "
            "chimneyR < bodyR\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    addClosedCylinderY(wom, bodyR, 0.0f, bodyH, sides);
    addClosedCylinderY(wom, chimneyR, bodyH, bodyH + chimneyH, sides);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, bodyR, bodyR, bodyH + chimneyH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-stove")) return 1;
    printWomWrote(womBase);
    std::printf("  body       : R=%.3f x %.3f tall\n", bodyR, bodyH);
    std::printf("  chimney    : R=%.3f x %.3f tall (%d sides)\n",
                chimneyR, chimneyH, sides);
    printWomMeshStats(wom);
    return 0;
}

int handleScrollCase(int& i, int argc, char** argv) {
    // Cylindrical scroll case / map tube: thin tall cylindrical
    // body with an optional shorter wider cap on top. Distinct
    // from --gen-mesh-chalice (foot + stem + bowl) and
    // --gen-mesh-lantern (4-tier base/globe/neck/cap) - scroll
    // case is the simplest 1-or-2-tier "tube with lid"
    // silhouette. The 84th procedural mesh primitive.
    std::string womBase = argv[++i];
    float bodyR = 0.04f;
    float bodyH = 0.45f;
    float capR  = 0.05f;       // 0 → no cap (just a stick)
    float capH  = 0.04f;
    int   sides = 14;
    parseOptFloat(i, argc, argv, bodyR);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, capR);
    parseOptFloat(i, argc, argv, capH);
    parseOptInt(i, argc, argv, sides);
    if (bodyR <= 0 || bodyH <= 0 || capR < 0 ||
        (capR > 0 && capR < bodyR) ||
        (capR > 0 && capH <= 0) ||
        sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-scroll-case: dims > 0; sides 6..64; "
            "capR >= bodyR (or 0 to skip)\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    addClosedCylinderY(wom, bodyR, 0.0f, bodyH, sides);
    float topY = bodyH;
    if (capR > 0.0f) {
        addClosedCylinderY(wom, capR, bodyH, bodyH + capH, sides);
        topY += capH;
    }
    finalizeAsSingleBatch(wom);
    float maxR = std::max(bodyR, capR);
    setCenteredBoundsXZ(wom, maxR, maxR, topY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-scroll-case")) return 1;
    printWomWrote(womBase);
    std::printf("  body       : R=%.3f x %.3f tall\n", bodyR, bodyH);
    if (capR > 0.0f)
        std::printf("  cap        : R=%.3f x %.3f thick\n", capR, capH);
    else
        std::printf("  cap        : (none)\n");
    std::printf("  sides      : %d\n", sides);
    printWomMeshStats(wom);
    return 0;
}

int handleStandingTorch(int& i, int argc, char** argv) {
    // Standing floor torch: tall thin post with a wider shallow
    // fire-bowl cylinder on top. Distinct from --gen-mesh-brazier
    // (squat fire-bowl on a stem with a bowl-on-base silhouette)
    // - standing-torch is the tall thin walking-height variant
    // for lining hallways, ceremonial paths, dungeon entries.
    // The 83rd procedural mesh primitive.
    std::string womBase = argv[++i];
    float postR  = 0.04f;
    float postH  = 1.20f;
    float bowlR  = 0.10f;
    float bowlH  = 0.08f;
    int   sides  = 14;
    parseOptFloat(i, argc, argv, postR);
    parseOptFloat(i, argc, argv, postH);
    parseOptFloat(i, argc, argv, bowlR);
    parseOptFloat(i, argc, argv, bowlH);
    parseOptInt(i, argc, argv, sides);
    if (postR <= 0 || postH <= 0 || bowlR <= 0 || bowlH <= 0 ||
        sides < 6 || sides > 64 || postR >= bowlR) {
        std::fprintf(stderr,
            "gen-mesh-standing-torch: dims > 0; sides 6..64; "
            "postR < bowlR\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    addClosedCylinderY(wom, postR, 0.0f, postH, sides);
    addClosedCylinderY(wom, bowlR, postH, postH + bowlH, sides);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, bowlR, bowlR, postH + bowlH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-standing-torch")) return 1;
    printWomWrote(womBase);
    std::printf("  post       : R=%.3f x %.3f tall\n", postR, postH);
    std::printf("  bowl       : R=%.3f x %.3f thick (%d sides)\n",
                bowlR, bowlH, sides);
    printWomMeshStats(wom);
    return 0;
}

int handleChalice(int& i, int argc, char** argv) {
    // Ceremonial chalice / goblet: foot (wide flat base) →
    // stem (thin tall connecting column) → bowl (wider shallow
    // cup). 3-tier vertical-cylinder shape with the classic
    // wide-narrow-wide silhouette of a wine goblet. The 82nd
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float footR  = 0.10f;
    float footH  = 0.02f;
    float stemR  = 0.025f;
    float stemH  = 0.16f;
    float bowlR  = 0.09f;
    float bowlH  = 0.10f;
    int   sides  = 14;
    parseOptFloat(i, argc, argv, footR);
    parseOptFloat(i, argc, argv, footH);
    parseOptFloat(i, argc, argv, stemR);
    parseOptFloat(i, argc, argv, stemH);
    parseOptFloat(i, argc, argv, bowlR);
    parseOptFloat(i, argc, argv, bowlH);
    parseOptInt(i, argc, argv, sides);
    if (footR <= 0 || footH <= 0 || stemR <= 0 || stemH <= 0 ||
        bowlR <= 0 || bowlH <= 0 || sides < 6 || sides > 64 ||
        stemR >= footR || stemR >= bowlR) {
        std::fprintf(stderr,
            "gen-mesh-chalice: dims > 0; sides 6..64; "
            "stemR < footR; stemR < bowlR\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    float y = 0.0f;
    addClosedCylinderY(wom, footR, y, y + footH, sides);
    y += footH;
    addClosedCylinderY(wom, stemR, y, y + stemH, sides);
    y += stemH;
    addClosedCylinderY(wom, bowlR, y, y + bowlH, sides);
    y += bowlH;
    finalizeAsSingleBatch(wom);
    float maxR = std::max({footR, stemR, bowlR});
    setCenteredBoundsXZ(wom, maxR, maxR, y);
    if (!saveWomOrError(wom, womBase, "gen-mesh-chalice")) return 1;
    printWomWrote(womBase);
    std::printf("  foot/stem/bowl : R=%.3f / %.3f / %.3f\n",
                footR, stemR, bowlR);
    std::printf("  total H    : %.3f (%d sides)\n", y, sides);
    printWomMeshStats(wom);
    return 0;
}

int handleLantern(int& i, int argc, char** argv) {
    // Hand lantern: small base disc → wider glass-globe cylinder
    // → narrow neck → wider top cap. 4 cylindrical tiers stacked,
    // mimicking a hooded oil-lantern silhouette. Distinct from
    // --gen-mesh-candle (just wax + saucer) and --gen-mesh-urn
    // (4-tier pottery shape) - lantern's wide-narrow-wide tier
    // sequence reads as glass enclosed by metal hood. The 81st
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float baseR  = 0.10f;
    float baseH  = 0.03f;
    float globeR = 0.13f;
    float globeH = 0.18f;
    float neckR  = 0.06f;
    float neckH  = 0.04f;
    float capR   = 0.14f;
    float capH   = 0.05f;
    int   sides  = 14;
    parseOptFloat(i, argc, argv, baseR);
    parseOptFloat(i, argc, argv, baseH);
    parseOptFloat(i, argc, argv, globeR);
    parseOptFloat(i, argc, argv, globeH);
    parseOptFloat(i, argc, argv, neckR);
    parseOptFloat(i, argc, argv, neckH);
    parseOptFloat(i, argc, argv, capR);
    parseOptFloat(i, argc, argv, capH);
    parseOptInt(i, argc, argv, sides);
    if (baseR <= 0 || baseH <= 0 || globeR <= 0 || globeH <= 0 ||
        neckR <= 0 || neckH <= 0 || capR <= 0 || capH <= 0 ||
        sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-lantern: dims > 0; sides 6..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    float y = 0.0f;
    addClosedCylinderY(wom, baseR, y, y + baseH, sides);
    y += baseH;
    addClosedCylinderY(wom, globeR, y, y + globeH, sides);
    y += globeH;
    addClosedCylinderY(wom, neckR, y, y + neckH, sides);
    y += neckH;
    addClosedCylinderY(wom, capR, y, y + capH, sides);
    y += capH;
    finalizeAsSingleBatch(wom);
    float maxR = std::max({baseR, globeR, capR});
    setCenteredBoundsXZ(wom, maxR, maxR, y);
    if (!saveWomOrError(wom, womBase, "gen-mesh-lantern")) return 1;
    printWomWrote(womBase);
    std::printf("  base / globe / neck / cap : %.3f / %.3f / %.3f / %.3f R\n",
                baseR, globeR, neckR, capR);
    std::printf("  total H    : %.3f (%d sides)\n", y, sides);
    printWomMeshStats(wom);
    return 0;
}

int handleCandle(int& i, int argc, char** argv) {
    // Wax pillar candle: thin tall wax cylinder optionally
    // standing on a wider shallow saucer base (the drip catcher).
    // Both pieces use addClosedCylinderY - same pattern as
    // --gen-mesh-bird-bath but with a much skinnier upper
    // cylinder. The 80th procedural mesh primitive.
    std::string womBase = argv[++i];
    float waxR  = 0.04f;
    float waxH  = 0.30f;
    float saucerR = 0.08f;       // 0 → no saucer
    float saucerH = 0.015f;
    int   sides = 14;
    parseOptFloat(i, argc, argv, waxR);
    parseOptFloat(i, argc, argv, waxH);
    parseOptFloat(i, argc, argv, saucerR);
    parseOptFloat(i, argc, argv, saucerH);
    parseOptInt(i, argc, argv, sides);
    if (waxR <= 0 || waxH <= 0 || saucerR < 0 ||
        (saucerR > 0 && saucerR <= waxR) ||
        (saucerR > 0 && saucerH <= 0) ||
        sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-candle: dims > 0; saucerR > waxR (or 0); sides 6..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    float yBase = 0.0f;
    if (saucerR > 0.0f) {
        addClosedCylinderY(wom, saucerR, 0.0f, saucerH, sides);
        yBase = saucerH;
    }
    addClosedCylinderY(wom, waxR, yBase, yBase + waxH, sides);
    finalizeAsSingleBatch(wom);
    float maxR = std::max(waxR, saucerR);
    setCenteredBoundsXZ(wom, maxR, maxR, yBase + waxH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-candle")) return 1;
    printWomWrote(womBase);
    std::printf("  wax        : R=%.3f x %.3f tall\n", waxR, waxH);
    if (saucerR > 0.0f)
        std::printf("  saucer     : R=%.3f x %.3f thick\n", saucerR, saucerH);
    else
        std::printf("  saucer     : (none)\n");
    std::printf("  sides      : %d\n", sides);
    printWomMeshStats(wom);
    return 0;
}

int handleUrn(int& i, int argc, char** argv) {
    // Multi-tier vertical urn: foot (wide short cylinder) →
    // body (tall main cylinder) → neck (narrow constriction) →
    // lip (slightly wider rim). All four tiers use the shared
    // addClosedCylinderY helper, so the urn is a stack of four
    // independent watertight tubes. Useful for temple offerings,
    // mausoleum interiors, kitchen storage, alchemist labs,
    // funerary scenes. The 79th procedural mesh primitive.
    std::string womBase = argv[++i];
    float bodyR  = 0.18f;
    float bodyH  = 0.40f;
    float footR  = 0.22f;
    float footH  = 0.06f;
    float neckR  = 0.12f;
    float neckH  = 0.10f;
    float lipR   = 0.16f;
    float lipH   = 0.04f;
    int   sides  = 18;
    parseOptFloat(i, argc, argv, bodyR);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, footR);
    parseOptFloat(i, argc, argv, footH);
    parseOptFloat(i, argc, argv, neckR);
    parseOptFloat(i, argc, argv, neckH);
    parseOptFloat(i, argc, argv, lipR);
    parseOptFloat(i, argc, argv, lipH);
    parseOptInt(i, argc, argv, sides);
    if (bodyR <= 0 || bodyH <= 0 || footR <= 0 || footH <= 0 ||
        neckR <= 0 || neckH <= 0 || lipR <= 0 || lipH <= 0 ||
        sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-urn: dims > 0; sides 6..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Stack 4 tiers from ground up.
    float y = 0.0f;
    addClosedCylinderY(wom, footR, y, y + footH, sides);
    y += footH;
    addClosedCylinderY(wom, bodyR, y, y + bodyH, sides);
    y += bodyH;
    addClosedCylinderY(wom, neckR, y, y + neckH, sides);
    y += neckH;
    addClosedCylinderY(wom, lipR, y, y + lipH, sides);
    y += lipH;
    finalizeAsSingleBatch(wom);
    float maxR = std::max({footR, bodyR, lipR});
    setCenteredBoundsXZ(wom, maxR, maxR, y);
    if (!saveWomOrError(wom, womBase, "gen-mesh-urn")) return 1;
    printWomWrote(womBase);
    std::printf("  tiers      : foot R=%.3f, body R=%.3f, neck R=%.3f, "
                "lip R=%.3f\n", footR, bodyR, neckR, lipR);
    std::printf("  total H    : %.3f (%d sides)\n", y, sides);
    printWomMeshStats(wom);
    return 0;
}

int handlePlanterBox(int& i, int argc, char** argv) {
    // Window-sill / garden planter box: long open-top wooden
    // basin (5-piece bottom + 4 walls construction) plus a
    // visible "soil" slab inside that fills most of the cavity
    // a bit below the rim - the natural "filled with potting
    // dirt" look. Distinct from --gen-mesh-water-trough (square
    // basin without a soil-fill block) - planter-box is the
    // long elongated garden variant. The 78th procedural mesh
    // primitive.
    std::string womBase = argv[++i];
    float length = 1.2f;
    float width  = 0.30f;
    float height = 0.30f;
    float wallT  = 0.04f;
    float soilTopFrac = 0.85f;     // soil top as fraction of height
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, wallT);
    parseOptFloat(i, argc, argv, soilTopFrac);
    if (length <= 0 || width <= 0 || height <= 0 || wallT <= 0 ||
        wallT * 2 >= std::min(length, width) || wallT >= height ||
        soilTopFrac <= 0.0f || soilTopFrac > 1.0f) {
        std::fprintf(stderr,
            "gen-mesh-planter-box: dims > 0; soilTopFrac (0,1]\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float L2 = length * 0.5f;
    const float W2 = width  * 0.5f;
    // Bottom slab (planter floor).
    addFlatBox(wom, 0.0f, wallT * 0.5f, 0.0f, L2, wallT * 0.5f, W2);
    // 4 perimeter walls (same scheme as --gen-mesh-water-trough).
    const float wallCY = wallT + (height - wallT) * 0.5f;
    const float wallHY = (height - wallT) * 0.5f;
    addFlatBox(wom, +L2 - wallT * 0.5f, wallCY, 0.0f,
               wallT * 0.5f, wallHY, W2);
    addFlatBox(wom, -L2 + wallT * 0.5f, wallCY, 0.0f,
               wallT * 0.5f, wallHY, W2);
    const float innerL2 = L2 - wallT;
    addFlatBox(wom, 0.0f, wallCY, +W2 - wallT * 0.5f,
               innerL2, wallHY, wallT * 0.5f);
    addFlatBox(wom, 0.0f, wallCY, -W2 + wallT * 0.5f,
               innerL2, wallHY, wallT * 0.5f);
    // Soil block: fills the inner cavity from the floor up to
    // soilTopFrac * height. Slightly inset so it visually sits
    // "inside" the walls instead of breaking the wall texture.
    const float soilTopY = height * soilTopFrac;
    const float soilCY = wallT + (soilTopY - wallT) * 0.5f;
    const float soilHY = (soilTopY - wallT) * 0.5f;
    if (soilHY > 0.0f) {
        const float innerW2 = W2 - wallT;
        addFlatBox(wom, 0.0f, soilCY, 0.0f,
                   innerL2 - 0.005f, soilHY, innerW2 - 0.005f);
    }
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, L2, W2, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-planter-box")) return 1;
    printWomWrote(womBase);
    std::printf("  box        : %.3fL x %.3fW x %.3fH (wallT %.3f)\n",
                length, width, height, wallT);
    std::printf("  soil       : top at %.0f%% of height\n",
                soilTopFrac * 100.0f);
    printWomMeshStats(wom);
    return 0;
}

int handleBirdBath(int& i, int argc, char** argv) {
    // Garden bird-bath: thin cylindrical stem topped by a wide
    // shallow disc basin. Distinct from --gen-mesh-fountain
    // (basin + spout column, larger water feature) - this is
    // the small ornamental garden version. The 77th procedural
    // mesh primitive.
    std::string womBase = argv[++i];
    float stemR    = 0.06f;       // stem (column) radius
    float stemH    = 0.85f;       // stem height
    float basinR   = 0.30f;       // basin top radius
    float basinH   = 0.10f;       // basin disc thickness
    int   sides    = 16;          // cylinder smoothness
    parseOptFloat(i, argc, argv, stemR);
    parseOptFloat(i, argc, argv, stemH);
    parseOptFloat(i, argc, argv, basinR);
    parseOptFloat(i, argc, argv, basinH);
    parseOptInt(i, argc, argv, sides);
    if (stemR <= 0 || stemH <= 0 || basinR <= 0 || basinH <= 0 ||
        sides < 6 || sides > 64 || stemR >= basinR) {
        std::fprintf(stderr,
            "gen-mesh-bird-bath: dims > 0; sides 6..64; "
            "stemR < basinR\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    addClosedCylinderY(wom, stemR, 0.0f, stemH, sides);
    addClosedCylinderY(wom, basinR, stemH, stemH + basinH, sides);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, basinR, basinR, stemH + basinH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-bird-bath")) return 1;
    printWomWrote(womBase);
    std::printf("  stem       : R=%.3f x %.3f tall\n", stemR, stemH);
    std::printf("  basin      : R=%.3f x %.3f thick (%d sides)\n",
                basinR, basinH, sides);
    printWomMeshStats(wom);
    return 0;
}

int handleStatueBase(int& i, int argc, char** argv) {
    // Statue / monument pedestal: 3-tier stacked-box pedestal
    // (plinth foundation, tall body, capital cap). Distinct from
    // --gen-mesh-podium (stepped pyramid with lectern) and
    // --gen-mesh-altar (stacked discs) - this is the classic
    // single-statue square pedestal for monuments, plazas,
    // hero-of-the-revolution memorials. The 76th procedural mesh
    // primitive.
    std::string womBase = argv[++i];
    float bodyW = 0.45f;            // square footprint of body
    float bodyH = 1.10f;            // body height (most of the pedestal)
    float plinthExtra = 0.12f;      // plinth wider than body per side
    float plinthH = 0.10f;          // plinth thickness
    float capitalExtra = 0.08f;     // capital wider than body per side
    float capitalH = 0.08f;         // capital thickness
    parseOptFloat(i, argc, argv, bodyW);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, plinthExtra);
    parseOptFloat(i, argc, argv, plinthH);
    parseOptFloat(i, argc, argv, capitalExtra);
    parseOptFloat(i, argc, argv, capitalH);
    if (bodyW <= 0 || bodyH <= 0 ||
        plinthExtra < 0 || plinthH <= 0 ||
        capitalExtra < 0 || capitalH <= 0) {
        std::fprintf(stderr,
            "gen-mesh-statue-base: dims > 0; extras >= 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float bodyHalf = bodyW * 0.5f;
    // Plinth: bottom box, wider than body.
    const float plinthHalf = bodyHalf + plinthExtra;
    addFlatBox(wom, 0.0f, plinthH * 0.5f, 0.0f,
               plinthHalf, plinthH * 0.5f, plinthHalf);
    // Body: tall central pedestal sitting on the plinth.
    const float bodyCY = plinthH + bodyH * 0.5f;
    addFlatBox(wom, 0.0f, bodyCY, 0.0f,
               bodyHalf, bodyH * 0.5f, bodyHalf);
    // Capital: small wider cap on top of the body.
    const float capitalCY = plinthH + bodyH + capitalH * 0.5f;
    const float capitalHalf = bodyHalf + capitalExtra;
    addFlatBox(wom, 0.0f, capitalCY, 0.0f,
               capitalHalf, capitalH * 0.5f, capitalHalf);
    finalizeAsSingleBatch(wom);
    float maxHalf = std::max(plinthHalf, capitalHalf);
    setCenteredBoundsXZ(wom, maxHalf, maxHalf,
                         plinthH + bodyH + capitalH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-statue-base")) return 1;
    printWomWrote(womBase);
    std::printf("  body       : %.3f square x %.3f tall\n", bodyW, bodyH);
    std::printf("  plinth     : %.3f thick, +%.3f per side\n",
                plinthH, plinthExtra);
    std::printf("  capital    : %.3f thick, +%.3f per side\n",
                capitalH, capitalExtra);
    printWomMeshStats(wom);
    return 0;
}

int handlePillarRow(int& i, int argc, char** argv) {
    // Row of N stone pillars evenly spaced along the X axis.
    // Each pillar is a single tall rectangular box, optionally
    // crowned by a slightly-wider square cap. Useful for ruined
    // temples, colonnades, palace walks, dungeon hallways. The
    // 75th procedural mesh primitive - and the third "scene"
    // composite (after crate-stack and gravel-pile) using the
    // simple regular-grid placement.
    std::string womBase = argv[++i];
    int   count    = 4;
    float span     = 4.0f;        // total length spanned by the row
    float height   = 2.5f;        // pillar height
    float pillarW  = 0.30f;       // pillar square footprint
    float capH     = 0.10f;       // 0 → no caps
    float capExtra = 0.06f;       // caps wider than pillar by this on each side
    parseOptInt(i, argc, argv, count);
    parseOptFloat(i, argc, argv, span);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, pillarW);
    parseOptFloat(i, argc, argv, capH);
    parseOptFloat(i, argc, argv, capExtra);
    if (count < 2 || count > 64 ||
        span <= 0 || height <= 0 || pillarW <= 0 ||
        capH < 0 || capExtra < 0 ||
        pillarW * count >= span) {
        std::fprintf(stderr,
            "gen-mesh-pillar-row: count 2..64; pillars must fit in span\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float availSpan = span - pillarW;
    const float pillarHY = (height - capH) * 0.5f;
    const float pillarCY = pillarHY;
    for (int k = 0; k < count; ++k) {
        float t = static_cast<float>(k) / (count - 1);
        float cx = -availSpan * 0.5f + t * availSpan;
        addFlatBox(wom, cx, pillarCY, 0.0f,
                   pillarW * 0.5f, pillarHY, pillarW * 0.5f);
        if (capH > 0.0f) {
            const float capCY = height - capH * 0.5f;
            const float capHX = pillarW * 0.5f + capExtra;
            addFlatBox(wom, cx, capCY, 0.0f,
                       capHX, capH * 0.5f, capHX);
        }
    }
    finalizeAsSingleBatch(wom);
    float halfX = span * 0.5f + (capH > 0 ? capExtra : 0);
    float halfZ = pillarW * 0.5f + (capH > 0 ? capExtra : 0);
    setCenteredBoundsXZ(wom, halfX, halfZ, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-pillar-row")) return 1;
    printWomWrote(womBase);
    std::printf("  pillars    : %d across %.3fL, %.3f tall (%.3f square)\n",
                count, span, height, pillarW);
    std::printf("  caps       : %s\n",
                capH > 0 ? std::to_string(capH).c_str() : "(none)");
    printWomMeshStats(wom);
    return 0;
}

int handleHitchingRail(int& i, int argc, char** argv) {
    // Long hitching rail: a horizontal bar held up by N evenly-
    // spaced vertical posts. Distinct from --gen-mesh-hitching-
    // post (which is just 2 posts + bar) - this is the longer
    // multi-post variant for taverns, stockyards, racecourse
    // parking, market days. The 74th procedural mesh primitive.
    std::string womBase = argv[++i];
    float length = 4.0f;
    float height = 1.2f;
    int   posts  = 4;
    float postW  = 0.10f;
    float barT   = 0.08f;
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, height);
    parseOptInt(i, argc, argv, posts);
    parseOptFloat(i, argc, argv, postW);
    parseOptFloat(i, argc, argv, barT);
    if (length <= 0 || height <= 0 || postW <= 0 || barT <= 0 ||
        posts < 2 || posts > 64 ||
        postW * posts >= length || barT >= height) {
        std::fprintf(stderr,
            "gen-mesh-hitching-rail: dims > 0; posts 2..64; "
            "posts must fit within length\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float L2 = length * 0.5f;
    // Posts evenly spaced from -L2 to +L2, inset by postW so end
    // posts align with the rail ends.
    const float postCY = (height - barT) * 0.5f;
    const float postHY = (height - barT) * 0.5f;
    const float availSpan = length - postW;
    for (int k = 0; k < posts; ++k) {
        float t = static_cast<float>(k) / (posts - 1);
        float cx = -availSpan * 0.5f + t * availSpan;
        addFlatBox(wom, cx, postCY, 0.0f,
                   postW * 0.5f, postHY, postW * 0.5f);
    }
    // Long horizontal rail spanning the full length on top.
    addFlatBox(wom, 0.0f, height - barT * 0.5f, 0.0f,
               L2, barT * 0.5f, barT * 0.5f);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, L2, postW * 0.5f, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-hitching-rail")) return 1;
    printWomWrote(womBase);
    std::printf("  rail       : %.3fL at H=%.3f, %d posts (W=%.3f)\n",
                length, height, posts, postW);
    std::printf("  bar        : %.3f thick\n", barT);
    printWomMeshStats(wom);
    return 0;
}

int handleMineCart(int& i, int argc, char** argv) {
    // Mine cart: rectangular open-top bin on 4 wheel boxes. The
    // bin uses the 5-piece basin construction from
    // --gen-mesh-water-trough (bottom + 4 walls); wheels are
    // 4 thin cube boxes at the corners. All axis-aligned -
    // exercises every shared helper. Useful for mines, dwarven
    // forges, gnomish junk-yards, abandoned-tunnel set dressing.
    // The 73rd procedural mesh primitive.
    std::string womBase = argv[++i];
    float length = 0.9f;
    float width  = 0.5f;
    float bodyH  = 0.45f;
    float wallT  = 0.04f;
    float wheelR = 0.08f;        // wheel half-edge (square approx)
    float wheelInset = 0.05f;    // wheel inset from cart corners
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, bodyH);
    parseOptFloat(i, argc, argv, wallT);
    parseOptFloat(i, argc, argv, wheelR);
    parseOptFloat(i, argc, argv, wheelInset);
    if (length <= 0 || width <= 0 || bodyH <= 0 || wallT <= 0 ||
        wallT * 2 >= std::min(length, width) || wallT >= bodyH ||
        wheelR <= 0 || wheelInset < 0 ||
        wheelInset * 2 + wheelR * 2 > std::min(length, width)) {
        std::fprintf(stderr,
            "gen-mesh-mine-cart: dims > 0; walls/wheels must fit\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float L2 = length * 0.5f;
    const float W2 = width  * 0.5f;
    // Wheels first - sit on the ground (y from 0 to 2*wheelR),
    // pushed inward from cart corners.
    const float wheelY = wheelR;
    const float wheelX = L2 - wheelInset - wheelR;
    const float wheelZ = W2 - wheelInset - wheelR;
    addFlatBox(wom, +wheelX, wheelY, +wheelZ, wheelR, wheelR, wheelR);
    addFlatBox(wom, -wheelX, wheelY, +wheelZ, wheelR, wheelR, wheelR);
    addFlatBox(wom, +wheelX, wheelY, -wheelZ, wheelR, wheelR, wheelR);
    addFlatBox(wom, -wheelX, wheelY, -wheelZ, wheelR, wheelR, wheelR);
    // Bin sits on top of the wheels: y from 2*wheelR to 2*wheelR+bodyH.
    const float binBaseY = 2.0f * wheelR;
    // Bin bottom slab.
    addFlatBox(wom, 0.0f, binBaseY + wallT * 0.5f, 0.0f,
               L2, wallT * 0.5f, W2);
    // 4 perimeter walls.
    const float wallCY = binBaseY + wallT + (bodyH - wallT) * 0.5f;
    const float wallHY = (bodyH - wallT) * 0.5f;
    addFlatBox(wom, +L2 - wallT * 0.5f, wallCY, 0.0f,
               wallT * 0.5f, wallHY, W2);
    addFlatBox(wom, -L2 + wallT * 0.5f, wallCY, 0.0f,
               wallT * 0.5f, wallHY, W2);
    const float innerL2 = L2 - wallT;
    addFlatBox(wom, 0.0f, wallCY, +W2 - wallT * 0.5f,
               innerL2, wallHY, wallT * 0.5f);
    addFlatBox(wom, 0.0f, wallCY, -W2 + wallT * 0.5f,
               innerL2, wallHY, wallT * 0.5f);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, L2, W2, binBaseY + bodyH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-mine-cart")) return 1;
    printWomWrote(womBase);
    std::printf("  bin        : %.3f x %.3f x %.3f (wallT %.3f)\n",
                length, width, bodyH, wallT);
    std::printf("  wheels     : 4 corners (R=%.3f, inset %.3f)\n",
                wheelR, wheelInset);
    printWomMeshStats(wom);
    return 0;
}

int handleStoneBench(int& i, int argc, char** argv) {
    // Long stone bench: horizontal seat slab on 2 vertical block
    // supports near the ends. Distinct from --gen-mesh-bench
    // (wooden 4-leg bench with thinner construction) - this is
    // the heavier stone variant for parks, temple courtyards,
    // ruined cities, dwarven mead halls. The 72nd procedural
    // mesh primitive.
    std::string womBase = argv[++i];
    float length    = 2.0f;
    float depth     = 0.4f;
    float seatH     = 0.45f;       // total bench top height
    float seatT     = 0.10f;       // seat slab thickness
    float supportW  = 0.20f;       // horizontal extent of each support
    float supportInset = 0.15f;    // distance from end to support outer face
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, seatH);
    parseOptFloat(i, argc, argv, seatT);
    parseOptFloat(i, argc, argv, supportW);
    parseOptFloat(i, argc, argv, supportInset);
    if (length <= 0 || depth <= 0 || seatH <= 0 || seatT <= 0 ||
        seatT >= seatH || supportW <= 0 ||
        supportInset < 0 ||
        supportW + supportInset * 2 > length) {
        std::fprintf(stderr,
            "gen-mesh-stone-bench: dims > 0; seatT < seatH; "
            "supports must fit within length\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float L2 = length * 0.5f;
    const float D2 = depth  * 0.5f;
    const float supportH = seatH - seatT;
    // Two stone supports at the ends, inset by supportInset.
    const float supportCX = L2 - supportInset - supportW * 0.5f;
    addFlatBox(wom, +supportCX, supportH * 0.5f, 0.0f,
               supportW * 0.5f, supportH * 0.5f, D2);
    addFlatBox(wom, -supportCX, supportH * 0.5f, 0.0f,
               supportW * 0.5f, supportH * 0.5f, D2);
    // Long seat slab spanning the full length on top of supports.
    addFlatBox(wom, 0.0f, supportH + seatT * 0.5f, 0.0f,
               L2, seatT * 0.5f, D2);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, L2, D2, seatH);
    if (!saveWomOrError(wom, womBase, "gen-mesh-stone-bench")) return 1;
    printWomWrote(womBase);
    std::printf("  bench      : %.3fL x %.3fD x %.3fH (seat %.3f thick)\n",
                length, depth, seatH, seatT);
    std::printf("  supports   : 2 at ±%.3f (W=%.3f)\n",
                supportCX, supportW);
    printWomMeshStats(wom);
    return 0;
}

int handleGravelPile(int& i, int argc, char** argv) {
    // Irregular pile of small stones distributed in a roughly
    // conical heap. N cube-shaped stones get hash-derived
    // (x, z, y, size) such that more numerous stones land near
    // the base and the pile thins toward the top. Useful for
    // mine entrances, construction sites, quarries, ruined
    // walls, abandoned-fort rubble. The 71st procedural mesh
    // primitive - and the second multi-box "scene" composite
    // (after --gen-mesh-crate-stack), but using irregular
    // hashed placement instead of a regular grid.
    std::string womBase = argv[++i];
    int   stoneCount = 24;
    float baseR      = 0.6f;        // base radius of the cone
    float pileH      = 0.5f;        // approx height of pile
    float maxStoneSize = 0.10f;
    uint32_t seed    = 1;
    parseOptInt(i, argc, argv, stoneCount);
    parseOptFloat(i, argc, argv, baseR);
    parseOptFloat(i, argc, argv, pileH);
    parseOptFloat(i, argc, argv, maxStoneSize);
    parseOptUint(i, argc, argv, seed);
    if (baseR <= 0 || pileH <= 0 || maxStoneSize <= 0 ||
        stoneCount < 1 || stoneCount > 1024) {
        std::fprintf(stderr,
            "gen-mesh-gravel-pile: dims > 0; stoneCount 1..1024\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto hash32 = [](uint32_t x) -> uint32_t {
        x ^= x >> 16; x *= 0x7feb352d;
        x ^= x >> 15; x *= 0x846ca68b;
        x ^= x >> 16; return x;
    };
    auto rand01 = [&](int idx, uint32_t salt) {
        return (hash32(static_cast<uint32_t>(idx) ^ salt ^ seed) % 10000)
                / 10000.0f;
    };
    for (int k = 0; k < stoneCount; ++k) {
        // Polar (r, θ) gives even distribution across the disc.
        // sqrt(rand) for r so stones aren't bunched at the center.
        float radial = std::sqrt(rand01(k, 0)) * baseR;
        float ang    = rand01(k, 1) * 2.0f * 3.14159265f;
        float cx = radial * std::cos(ang);
        float cz = radial * std::sin(ang);
        // Stones with smaller radial position can stack higher.
        float yMax = pileH * (1.0f - radial / baseR);
        float cy = rand01(k, 2) * yMax;
        float size = maxStoneSize *
                      (0.4f + 0.6f * rand01(k, 3));   // 40-100% of max
        addFlatBox(wom, cx, cy + size * 0.5f, cz,
                   size * 0.5f, size * 0.5f, size * 0.5f);
    }
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, baseR + maxStoneSize, baseR + maxStoneSize,
                         pileH + maxStoneSize);
    if (!saveWomOrError(wom, womBase, "gen-mesh-gravel-pile")) return 1;
    printWomWrote(womBase);
    std::printf("  stones     : %d (max size %.3f)\n",
                stoneCount, maxStoneSize);
    std::printf("  pile       : R=%.3f, H=%.3f (seed %u)\n",
                baseR, pileH, seed);
    printWomMeshStats(wom);
    return 0;
}

int handleArcheryTarget(int& i, int argc, char** argv) {
    // Archery target: round face on a 2-post stand. The face is a
    // short cylinder oriented along the Z axis (its flat circular
    // surfaces face ±Z, where archers shoot from). Stand uses two
    // vertical posts joined by a horizontal cross-beam underneath
    // the face. The 70th procedural mesh primitive.
    std::string womBase = argv[++i];
    float faceR    = 0.40f;     // target face radius
    float faceT    = 0.08f;     // target face thickness (Z extent)
    int   sides    = 24;        // face cylinder smoothness
    float postH    = 1.2f;      // height from ground to face center
    float postW    = 0.08f;     // post thickness
    float beamT    = 0.06f;     // cross-beam thickness
    parseOptFloat(i, argc, argv, faceR);
    parseOptFloat(i, argc, argv, faceT);
    parseOptInt(i, argc, argv, sides);
    parseOptFloat(i, argc, argv, postH);
    parseOptFloat(i, argc, argv, postW);
    parseOptFloat(i, argc, argv, beamT);
    if (faceR <= 0 || faceT <= 0 || sides < 6 || sides > 64 ||
        postH <= 0 || postW <= 0 || beamT <= 0 ||
        postW * 2 >= faceR * 2) {
        std::fprintf(stderr,
            "gen-mesh-archery-target: dims > 0; sides 6..64; "
            "postW*2 < faceR*2\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float halfStanceX = faceR + postW * 0.5f;
    // Two vertical posts of the stand, sized to reach from ground
    // to the bottom of the face.
    const float postTopY = postH - faceR;
    if (postTopY > 0.0f) {
        addFlatBox(wom, +halfStanceX, postTopY * 0.5f, 0.0f,
                   postW * 0.5f, postTopY * 0.5f, postW * 0.5f);
        addFlatBox(wom, -halfStanceX, postTopY * 0.5f, 0.0f,
                   postW * 0.5f, postTopY * 0.5f, postW * 0.5f);
    }
    // Horizontal cross-beam underneath the face joining the two
    // posts (gives the stand visual rigidity).
    const float beamCY = postTopY - beamT * 0.5f;
    if (beamCY > 0.0f) {
        addFlatBox(wom, 0.0f, beamCY, 0.0f,
                   halfStanceX, beamT * 0.5f, beamT * 0.5f);
    }
    // Target face: closed cylinder along the Z axis centered at
    // (0, postH, 0). Flat ±Z caps face the archer line; side
    // wall is the rim.
    const float halfT = faceT * 0.5f;
    addClosedCylinderZ(wom, 0.0f, postH, faceR, -halfT, +halfT, sides);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, halfStanceX + postW * 0.5f, halfT,
                         postH + faceR);
    if (!saveWomOrError(wom, womBase, "gen-mesh-archery-target")) return 1;
    printWomWrote(womBase);
    std::printf("  face       : R=%.3f x %.3f deep, %d sides\n",
                faceR, faceT, sides);
    std::printf("  stand      : posts at ±%.3f, beam %.3f thick\n",
                halfStanceX, beamT);
    printWomMeshStats(wom);
    return 0;
}

int handleForge(int& i, int argc, char** argv) {
    // Blacksmith forge: rectangular stone hearth with a smaller
    // hood on top and an optional thin chimney rising from the
    // hood. Pairs naturally with --gen-mesh-anvil and
    // --gen-mesh-workbench for forge / smithy scenes. The 69th
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float width    = 1.4f;
    float depth    = 1.0f;
    float baseH    = 0.9f;        // stone hearth height
    float hoodH    = 0.5f;        // hood height (smaller footprint)
    float hoodInset = 0.15f;       // hood is `inset` smaller per side
    float chimneyH = 1.2f;        // 0 → no chimney
    float chimneyW = 0.25f;       // chimney square footprint
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, baseH);
    parseOptFloat(i, argc, argv, hoodH);
    parseOptFloat(i, argc, argv, hoodInset);
    parseOptFloat(i, argc, argv, chimneyH);
    parseOptFloat(i, argc, argv, chimneyW);
    if (width <= 0 || depth <= 0 || baseH <= 0 || hoodH <= 0 ||
        hoodInset < 0 || hoodInset * 2 >= std::min(width, depth) ||
        chimneyH < 0 ||
        chimneyW <= 0 ||
        chimneyW * 2 >= std::min(width - 2 * hoodInset,
                                  depth - 2 * hoodInset)) {
        std::fprintf(stderr,
            "gen-mesh-forge: dims > 0; insets/chimney must fit inside\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float W2 = width * 0.5f;
    const float D2 = depth * 0.5f;
    // Stone hearth base.
    addFlatBox(wom, 0.0f, baseH * 0.5f, 0.0f, W2, baseH * 0.5f, D2);
    // Hood: smaller footprint, sitting on top of hearth.
    const float hW2 = W2 - hoodInset;
    const float hD2 = D2 - hoodInset;
    const float hoodCY = baseH + hoodH * 0.5f;
    addFlatBox(wom, 0.0f, hoodCY, 0.0f, hW2, hoodH * 0.5f, hD2);
    float topY = baseH + hoodH;
    if (chimneyH > 0.0f) {
        const float chCY = topY + chimneyH * 0.5f;
        addFlatBox(wom, 0.0f, chCY, 0.0f,
                   chimneyW * 0.5f, chimneyH * 0.5f, chimneyW * 0.5f);
        topY += chimneyH;
    }
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, W2, D2, topY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-forge")) return 1;
    printWomWrote(womBase);
    std::printf("  hearth     : %.3f x %.3f x %.3f\n", width, depth, baseH);
    std::printf("  hood       : %.3f x %.3f x %.3f (inset %.3f)\n",
                width - 2 * hoodInset, depth - 2 * hoodInset, hoodH,
                hoodInset);
    std::printf("  chimney    : %s\n",
                chimneyH > 0
                    ? (std::to_string(chimneyW) + " x " +
                       std::to_string(chimneyH) + " tall").c_str()
                    : "(none)");
    printWomMeshStats(wom);
    return 0;
}

int handleOuthouse(int& i, int argc, char** argv) {
    // Small wooden shed / outhouse: solid body box with an
    // inset door slab on the +Z face and a slightly-larger flat
    // roof slab overhanging the body. Distinct from
    // --gen-mesh-house (multi-walled, peaked-roof dwelling) - an
    // outhouse is the single-room privy / tool-shed variant.
    // The 68th procedural mesh primitive.
    std::string womBase = argv[++i];
    float width  = 0.9f;
    float depth  = 1.0f;
    float height = 1.8f;
    float doorH  = 1.4f;
    float doorW  = 0.5f;
    float roofOverhang = 0.10f;
    float roofT  = 0.06f;
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, doorH);
    parseOptFloat(i, argc, argv, doorW);
    parseOptFloat(i, argc, argv, roofOverhang);
    parseOptFloat(i, argc, argv, roofT);
    if (width <= 0 || depth <= 0 || height <= 0 ||
        doorH <= 0 || doorH >= height ||
        doorW <= 0 || doorW >= width ||
        roofOverhang < 0 || roofT <= 0 || roofT >= height) {
        std::fprintf(stderr,
            "gen-mesh-outhouse: dims > 0; doorH < height; doorW < width\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float W2 = width  * 0.5f;
    const float D2 = depth  * 0.5f;
    const float bodyH = height - roofT;
    // Body: solid rectangular box.
    addFlatBox(wom, 0.0f, bodyH * 0.5f, 0.0f,
               W2, bodyH * 0.5f, D2);
    // Door slab on the +Z face: thin box pushed slightly outward
    // so it visually sits on the wall (gives doorframe-like depth).
    const float doorT = 0.03f;
    addFlatBox(wom, 0.0f, doorH * 0.5f, +D2 + doorT * 0.5f,
               doorW * 0.5f, doorH * 0.5f, doorT * 0.5f);
    // Roof slab: slightly larger than the body footprint and
    // sitting on top of the body.
    const float rofW2 = W2 + roofOverhang;
    const float rofD2 = D2 + roofOverhang;
    addFlatBox(wom, 0.0f, bodyH + roofT * 0.5f, 0.0f,
               rofW2, roofT * 0.5f, rofD2);
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, std::max(W2, rofW2),
                        std::max(D2 + doorT, rofD2), height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-outhouse")) return 1;
    printWomWrote(womBase);
    std::printf("  body       : %.3f x %.3f x %.3f\n", width, depth, bodyH);
    std::printf("  door       : %.3f x %.3f on +Z face\n", doorW, doorH);
    std::printf("  roof       : %.3f thick, %.3f overhang\n",
                roofT, roofOverhang);
    printWomMeshStats(wom);
    return 0;
}

int handleHitchingPost(int& i, int argc, char** argv) {
    // Hitching post: two vertical posts joined by a horizontal
    // cross-bar at upper height. Standard town/stable fixture
    // for tying up mounts. All axis-aligned boxes - exercises
    // the new addFlatBox helper. The 67th procedural mesh
    // primitive.
    std::string womBase = argv[++i];
    float span    = 1.6f;             // distance between post centers
    float height  = 1.2f;             // post height
    float postW   = 0.10f;            // post thickness (square)
    float barT    = 0.08f;            // cross-bar thickness
    float capH    = 0.05f;            // 0 → no decorative caps
    parseOptFloat(i, argc, argv, span);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, postW);
    parseOptFloat(i, argc, argv, barT);
    parseOptFloat(i, argc, argv, capH);
    if (span <= 0 || height <= 0 || postW <= 0 || barT <= 0 ||
        capH < 0 || postW * 2 >= span || barT >= height) {
        std::fprintf(stderr,
            "gen-mesh-hitching-post: dims > 0; postW*2 < span; barT < height\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float halfSpan = span * 0.5f;
    // Two vertical posts.
    addFlatBox(wom, +halfSpan, height * 0.5f, 0.0f,
               postW * 0.5f, height * 0.5f, postW * 0.5f);
    addFlatBox(wom, -halfSpan, height * 0.5f, 0.0f,
               postW * 0.5f, height * 0.5f, postW * 0.5f);
    // Cross-bar at upper post height (between post tops). Inset
    // by postW so it tucks BETWEEN the post inner faces, not over
    // them - matches the standard 4-rail fence look.
    const float barCY = height - barT * 0.5f;
    const float barHX = (span - postW) * 0.5f;
    addFlatBox(wom, 0.0f, barCY, 0.0f,
               barHX, barT * 0.5f, barT * 0.5f);
    // Optional decorative caps on each post.
    float topY = height;
    if (capH > 0.0f) {
        const float capCY = height + capH * 0.5f;
        const float capHX = postW * 0.7f;          // a bit wider than post
        addFlatBox(wom, +halfSpan, capCY, 0.0f,
                   capHX, capH * 0.5f, capHX);
        addFlatBox(wom, -halfSpan, capCY, 0.0f,
                   capHX, capH * 0.5f, capHX);
        topY = height + capH;
    }
    finalizeAsSingleBatch(wom);
    float halfX = halfSpan + (capH > 0 ? postW * 0.7f : postW * 0.5f);
    float halfZ = (capH > 0 ? postW * 0.7f : postW * 0.5f);
    wom.boundMin = glm::vec3(-halfX, 0, -halfZ);
    wom.boundMax = glm::vec3(+halfX, topY, +halfZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-hitching-post")) return 1;
    printWomWrote(womBase);
    std::printf("  posts      : 2 separated by %.3f, %.3f tall\n",
                span, height);
    std::printf("  cross-bar  : %.3f thick at upper post height\n", barT);
    std::printf("  caps       : %s\n",
                capH > 0 ? std::to_string(capH).c_str() : "(none)");
    printWomMeshStats(wom);
    return 0;
}

int handleTrainingDummy(int& i, int argc, char** argv) {
    // Combat training dummy: vertical pole with a cubic torso block
    // and a horizontal cross-bar simulating outstretched arms. All
    // axis-aligned boxes - uses every shared helper from
    // cli_box_emitter. Pairs with --gen-mesh-anvil / --gen-mesh-
    // workbench / --gen-mesh-fence for sparring grounds, training
    // yards, militia drill squares. The 66th procedural mesh
    // primitive.
    std::string womBase = argv[++i];
    float baseH    = 1.0f;            // post height to bottom of torso
    float postW    = 0.10f;           // post thickness
    float torsoSize = 0.40f;          // cubic torso edge
    float armSpan  = 0.90f;           // total cross-bar length (X axis)
    float armT     = 0.06f;           // cross-bar thickness
    float headSize = 0.18f;           // 0 → no head
    parseOptFloat(i, argc, argv, baseH);
    parseOptFloat(i, argc, argv, postW);
    parseOptFloat(i, argc, argv, torsoSize);
    parseOptFloat(i, argc, argv, armSpan);
    parseOptFloat(i, argc, argv, armT);
    parseOptFloat(i, argc, argv, headSize);
    if (baseH <= 0 || postW <= 0 || torsoSize <= 0 ||
        armSpan <= 0 || armT <= 0 || headSize < 0 ||
        postW * 2 >= torsoSize) {
        std::fprintf(stderr,
            "gen-mesh-training-dummy: dims > 0; postW*2 < torsoSize\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Vertical post from y=0 to y=baseH.
    addFlatBox(wom, 0.0f, baseH * 0.5f, 0.0f,
               postW * 0.5f, baseH * 0.5f, postW * 0.5f);
    // Torso cube centered at the top of the post.
    const float torsoCY = baseH + torsoSize * 0.5f;
    addFlatBox(wom, 0.0f, torsoCY, 0.0f,
               torsoSize * 0.5f, torsoSize * 0.5f, torsoSize * 0.5f);
    // Horizontal cross-bar (arms) running along X through the
    // upper third of the torso.
    const float armCY = torsoCY + torsoSize * 0.15f;
    addFlatBox(wom, 0.0f, armCY, 0.0f,
               armSpan * 0.5f, armT * 0.5f, armT * 0.5f);
    // Optional head: smaller cube on top of the torso.
    float topY = torsoCY + torsoSize * 0.5f;
    if (headSize > 0.0f) {
        const float headCY = topY + headSize * 0.5f;
        addFlatBox(wom, 0.0f, headCY, 0.0f,
                   headSize * 0.5f, headSize * 0.5f, headSize * 0.5f);
        topY = headCY + headSize * 0.5f;
    }
    finalizeAsSingleBatch(wom);
    float halfX = std::max(armSpan * 0.5f, torsoSize * 0.5f);
    float halfZ = std::max(torsoSize * 0.5f, armT * 0.5f);
    wom.boundMin = glm::vec3(-halfX, 0, -halfZ);
    wom.boundMax = glm::vec3(+halfX, topY, +halfZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-training-dummy")) return 1;
    printWomWrote(womBase);
    std::printf("  post       : %.3f tall, %.3f square\n", baseH, postW);
    std::printf("  torso      : %.3f cube at y=%.3f\n", torsoSize, torsoCY);
    std::printf("  arms       : %.3f span x %.3f thick\n", armSpan, armT);
    std::printf("  head       : %s\n",
                headSize > 0 ? std::to_string(headSize).c_str() : "(none)");
    printWomMeshStats(wom);
    return 0;
}

int handleWaterTrough(int& i, int argc, char** argv) {
    // Open-top water trough / horse trough: a 4-walled rectangular
    // basin with a flat floor. 5 boxes total - bottom slab plus
    // 4 perimeter walls. Perimeter walls are sized so they butt up
    // against the floor and each other without overlap; the inner
    // cavity (length-2*wallT × height-wallT × width-2*wallT) is
    // the open water volume. Useful for stables, farmsteads,
    // taverns, stockyards. The 65th procedural mesh primitive.
    std::string womBase = argv[++i];
    float length = 1.4f;
    float width  = 0.5f;
    float height = 0.5f;
    float wallT  = 0.06f;
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, wallT);
    if (length <= 0 || width <= 0 || height <= 0 || wallT <= 0 ||
        wallT * 2 >= std::min(length, width) || wallT >= height) {
        std::fprintf(stderr,
            "gen-mesh-water-trough: dims > 0; wallT*2 < length/width; "
            "wallT < height\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float L2 = length * 0.5f;
    const float W2 = width  * 0.5f;
    // Bottom slab spans full footprint, sits at y=0.
    addFlatBox(wom, 0.0f, wallT * 0.5f, 0.0f,
               L2, wallT * 0.5f, W2);
    // Perimeter walls. The +X / -X walls span the full length;
    // the +Z / -Z walls span the inner length so they tuck
    // INSIDE the X walls without overlap.
    const float wallCY = wallT + (height - wallT) * 0.5f;
    const float wallHY = (height - wallT) * 0.5f;
    addFlatBox(wom, +L2 - wallT * 0.5f, wallCY, 0.0f,
               wallT * 0.5f, wallHY, W2);
    addFlatBox(wom, -L2 + wallT * 0.5f, wallCY, 0.0f,
               wallT * 0.5f, wallHY, W2);
    const float innerL2 = L2 - wallT;
    addFlatBox(wom, 0.0f, wallCY, +W2 - wallT * 0.5f,
               innerL2, wallHY, wallT * 0.5f);
    addFlatBox(wom, 0.0f, wallCY, -W2 + wallT * 0.5f,
               innerL2, wallHY, wallT * 0.5f);
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-L2, 0, -W2);
    wom.boundMax = glm::vec3(+L2, height, +W2);
    if (!saveWomOrError(wom, womBase, "gen-mesh-water-trough")) return 1;
    printWomWrote(womBase);
    std::printf("  basin      : %.3f x %.3f x %.3f (wallT %.3f)\n",
                length, width, height, wallT);
    std::printf("  cavity     : %.3f x %.3f x %.3f\n",
                length - 2 * wallT, height - wallT, width - 2 * wallT);
    printWomMeshStats(wom);
    return 0;
}

int handleWatchpost(int& i, int argc, char** argv) {
    // Sentry watchpost: tall central pole topped by a wider square
    // platform, with optional corner railing posts. Distinct from
    // --gen-mesh-tower (round castle tower with battlements) - a
    // watchpost is the rough scout/lookout variant. Pairs with
    // --gen-mesh-tent / --gen-mesh-firepit for outdoor camps and
    // forward outposts. The 64th procedural mesh primitive.
    std::string womBase = argv[++i];
    float postH       = 3.0f;
    float postW       = 0.18f;
    float platformSize = 0.8f;
    float platformT   = 0.10f;
    float railingH    = 0.45f;        // 0 → no railing
    float railingW    = 0.06f;
    parseOptFloat(i, argc, argv, postH);
    parseOptFloat(i, argc, argv, postW);
    parseOptFloat(i, argc, argv, platformSize);
    parseOptFloat(i, argc, argv, platformT);
    parseOptFloat(i, argc, argv, railingH);
    parseOptFloat(i, argc, argv, railingW);
    if (postH <= 0 || postW <= 0 ||
        platformSize <= 0 || platformT <= 0 ||
        railingH < 0 || railingW <= 0 ||
        postW * 2 >= platformSize) {
        std::fprintf(stderr,
            "gen-mesh-watchpost: dims > 0; postW*2 < platformSize\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    // Central pole: square box from y=0 to y=postH.
    addFlatBox(wom, 0.0f, postH * 0.5f, 0.0f,
               postW * 0.5f, postH * 0.5f, postW * 0.5f);
    // Platform slab on top of the pole.
    const float platformY = postH + platformT * 0.5f;
    const float halfPlat = platformSize * 0.5f;
    addFlatBox(wom, 0.0f, platformY, 0.0f,
               halfPlat, platformT * 0.5f, halfPlat);
    // Optional 4 corner railing posts above the platform.
    if (railingH > 0.0f) {
        const float railCY = postH + platformT + railingH * 0.5f;
        const float halfRail = railingW * 0.5f;
        const float railX = halfPlat - halfRail;
        addFlatBox(wom, +railX, railCY, +railX,
                   halfRail, railingH * 0.5f, halfRail);
        addFlatBox(wom, -railX, railCY, +railX,
                   halfRail, railingH * 0.5f, halfRail);
        addFlatBox(wom, +railX, railCY, -railX,
                   halfRail, railingH * 0.5f, halfRail);
        addFlatBox(wom, -railX, railCY, -railX,
                   halfRail, railingH * 0.5f, halfRail);
    }
    finalizeAsSingleBatch(wom);
    float topY = postH + platformT + (railingH > 0 ? railingH : 0.0f);
    wom.boundMin = glm::vec3(-halfPlat, 0, -halfPlat);
    wom.boundMax = glm::vec3(+halfPlat, topY, +halfPlat);
    if (!saveWomOrError(wom, womBase, "gen-mesh-watchpost")) return 1;
    printWomWrote(womBase);
    std::printf("  post       : %.3f tall, %.3f square\n", postH, postW);
    std::printf("  platform   : %.3f square, %.3f thick\n",
                platformSize, platformT);
    std::printf("  railing    : %s\n",
                railingH > 0 ? std::to_string(railingH).c_str() : "(none)");
    printWomMeshStats(wom);
    return 0;
}

int handleCrateStack(int& i, int argc, char** argv) {
    // Multi-crate stack: an N×M×K arrangement of cube crates with
    // a small gap between each so they read as discrete shipping
    // boxes rather than one merged solid. The first procedural
    // mesh that explicitly composes a *scene* of multiple objects
    // - useful for warehouses, cargo holds, dock loading bays,
    // market stalls, dwarven mining caches. The 63rd procedural
    // mesh primitive.
    std::string womBase = argv[++i];
    float crateSize = 0.40f;
    int   columns = 2;             // X axis
    int   rows    = 2;             // Z axis
    int   layers  = 2;             // Y axis
    float gap     = 0.02f;
    parseOptFloat(i, argc, argv, crateSize);
    parseOptInt(i, argc, argv, columns);
    parseOptInt(i, argc, argv, rows);
    parseOptInt(i, argc, argv, layers);
    parseOptFloat(i, argc, argv, gap);
    if (crateSize <= 0 || gap < 0 ||
        columns < 1 || columns > 32 ||
        rows < 1 || rows > 32 ||
        layers < 1 || layers > 32) {
        std::fprintf(stderr,
            "gen-mesh-crate-stack: crateSize > 0; columns/rows/layers 1..32\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float cell = crateSize + gap;
    const float halfBlock = crateSize * 0.5f;
    const float xStart = -(columns - 1) * cell * 0.5f;
    const float zStart = -(rows - 1) * cell * 0.5f;
    int total = 0;
    for (int ly = 0; ly < layers; ++ly) {
        float cy = ly * cell + halfBlock;
        for (int rz = 0; rz < rows; ++rz) {
            float cz = zStart + rz * cell;
            for (int cx = 0; cx < columns; ++cx) {
                float xPos = xStart + cx * cell;
                addFlatBox(wom, xPos, cy, cz,
                           halfBlock, halfBlock, halfBlock);
                ++total;
            }
        }
    }
    finalizeAsSingleBatch(wom);
    float halfX = (columns - 1) * cell * 0.5f + halfBlock;
    float halfZ = (rows - 1) * cell * 0.5f + halfBlock;
    float topY  = (layers - 1) * cell + crateSize;
    wom.boundMin = glm::vec3(-halfX, 0, -halfZ);
    wom.boundMax = glm::vec3(+halfX, topY, +halfZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-crate-stack")) return 1;
    printWomWrote(womBase);
    std::printf("  layout     : %d × %d × %d (%d crates)\n",
                columns, rows, layers, total);
    std::printf("  crate      : %.3f cube, gap %.3f\n", crateSize, gap);
    std::printf("  span       : %.3f × %.3f × %.3f\n",
                halfX * 2.0f, topY, halfZ * 2.0f);
    printWomMeshStats(wom);
    return 0;
}

int handleWorkbench(int& i, int argc, char** argv) {
    // Crafter's workbench: flat top slab on 4 corner legs, plus
    // an optional vise box at the +X end of the top and a small
    // raised tool tray along the +Z back edge. All axis-aligned
    // boxes - uses cli_box_emitter::addFlatBox throughout. Pairs
    // naturally with --gen-mesh-anvil (existing) for blacksmith
    // shop set dressing. The 62nd procedural mesh primitive.
    std::string womBase = argv[++i];
    float length   = 1.6f;
    float depth    = 0.7f;
    float height   = 0.85f;
    float legR     = 0.05f;
    float topT     = 0.06f;
    float viseSize = 0.18f;          // 0 → no vise
    float trayH    = 0.15f;          // 0 → no tray
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, legR);
    parseOptFloat(i, argc, argv, topT);
    parseOptFloat(i, argc, argv, viseSize);
    parseOptFloat(i, argc, argv, trayH);
    if (length <= 0 || depth <= 0 || height <= 0 || topT <= 0 ||
        legR <= 0 || legR * 2 >= std::min(length, depth) ||
        viseSize < 0 || trayH < 0) {
        std::fprintf(stderr,
            "gen-mesh-workbench: dims > 0; legR*2 < length/depth\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float L2 = length * 0.5f;
    const float D2 = depth  * 0.5f;
    const float legH = height - topT;
    const float legCY = legH * 0.5f;
    const float legX = L2 - legR;
    const float legZ = D2 - legR;
    addFlatBox(wom, +legX, legCY, +legZ, legR, legH * 0.5f, legR);
    addFlatBox(wom, -legX, legCY, +legZ, legR, legH * 0.5f, legR);
    addFlatBox(wom, +legX, legCY, -legZ, legR, legH * 0.5f, legR);
    addFlatBox(wom, -legX, legCY, -legZ, legR, legH * 0.5f, legR);
    // Top slab.
    addFlatBox(wom, 0.0f, legH + topT * 0.5f, 0.0f,
               L2, topT * 0.5f, D2);
    if (viseSize > 0.0f) {
        // Vise: small box at the +X end, sitting on the top slab.
        const float vCX = L2 - viseSize * 0.5f;
        const float vCY = legH + topT + viseSize * 0.5f;
        addFlatBox(wom, vCX, vCY, 0.0f,
                   viseSize * 0.5f, viseSize * 0.5f, viseSize * 0.5f);
    }
    if (trayH > 0.0f) {
        // Back tool tray: thin raised lip along the +Z edge of the
        // top slab. Width = full length minus a corner inset, depth
        // = a small fraction of bench depth so it's clearly behind
        // the working area.
        const float trayD = depth * 0.12f;
        const float trayCZ = D2 - trayD * 0.5f;
        const float trayCY = legH + topT + trayH * 0.5f;
        addFlatBox(wom, 0.0f, trayCY, trayCZ,
                   L2 - legR, trayH * 0.5f, trayD * 0.5f);
    }
    finalizeAsSingleBatch(wom);
    float maxY = legH + topT;
    if (viseSize > 0) maxY = std::max(maxY, legH + topT + viseSize);
    if (trayH > 0)    maxY = std::max(maxY, legH + topT + trayH);
    wom.boundMin = glm::vec3(-L2, 0, -D2);
    wom.boundMax = glm::vec3(+L2, maxY, +D2);
    if (!saveWomOrError(wom, womBase, "gen-mesh-workbench")) return 1;
    printWomWrote(womBase);
    std::printf("  bench      : %.3f x %.3f x %.3f (top %.3f thick)\n",
                length, depth, height, topT);
    std::printf("  legs       : 4 corners (R=%.3f)\n", legR);
    std::printf("  vise       : %s\n",
                viseSize > 0 ? std::to_string(viseSize).c_str() : "(none)");
    std::printf("  tray       : %s\n",
                trayH > 0 ? std::to_string(trayH).c_str() : "(none)");
    printWomMeshStats(wom);
    return 0;
}

int handleBedroll(int& i, int argc, char** argv) {
    // Camp bedroll: a horizontal closed cylinder lying along the
    // Z axis at ground level (y = R), with an optional flatter
    // pillow box at the +Z end. Pairs naturally with --gen-mesh-
    // tent / --gen-mesh-firepit for camp set dressing. Uses the
    // shared addVertex helper plus addFlatBox for the pillow.
    // The 61st procedural mesh primitive.
    std::string womBase = argv[++i];
    float length = 1.4f;
    float radius = 0.16f;
    int   sides  = 12;
    float pillowSize = 0.18f;       // 0 → no pillow
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, radius);
    parseOptInt(i, argc, argv, sides);
    parseOptFloat(i, argc, argv, pillowSize);
    if (length <= 0 || radius <= 0 || sides < 6 || sides > 64 ||
        pillowSize < 0 || pillowSize >= length * 0.5f) {
        std::fprintf(stderr,
            "gen-mesh-bedroll: dims > 0; sides 6..64; pillow < length/2\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float halfL = length * 0.5f;
    // Z-axis cylinder centered at (0, radius). Sits on the ground
    // (cy = radius means the bottom of the cylinder is at y=0).
    addClosedCylinderZ(wom, 0.0f, radius, radius, -halfL, +halfL, sides);
    // Optional pillow box at +Z end. Sits flat on the ground and
    // pushes a bit past the bedroll's front cap so it reads as a
    // separate prop.
    if (pillowSize > 0) {
        const float pHalf = pillowSize * 0.5f;
        const float pHeight = pillowSize * 0.5f;  // squashed
        addFlatBox(wom, 0.0f, pHeight * 0.5f, halfL + pHalf,
                   pHalf, pHeight * 0.5f, pHalf);
    }
    finalizeAsSingleBatch(wom);
    float maxZ = halfL + (pillowSize > 0 ? pillowSize : 0);
    wom.boundMin = glm::vec3(-radius, 0, -halfL);
    wom.boundMax = glm::vec3(+radius, 2.0f * radius, +maxZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-bedroll")) return 1;
    printWomWrote(womBase);
    std::printf("  bedroll    : len=%.3f, R=%.3f, %d sides\n",
                length, radius, sides);
    if (pillowSize > 0)
        std::printf("  pillow     : %.3f cube at +Z end\n", pillowSize);
    else
        std::printf("  pillow     : (none)\n");
    printWomMeshStats(wom);
    return 0;
}

int handleChimney(int& i, int argc, char** argv) {
    // Brick chimney: rectangular shaft topped by a slightly-wider
    // cap (the protective crown that throws rain off the masonry).
    // All axis-aligned boxes - uses cli_box_emitter::addFlatBox.
    // The 60th procedural mesh primitive.
    std::string womBase = argv[++i];
    float width  = 0.45f;
    float depth  = 0.45f;
    float height = 1.8f;
    float capH   = 0.10f;
    float capExtra = 0.05f;
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, capH);
    parseOptFloat(i, argc, argv, capExtra);
    if (width <= 0 || depth <= 0 || height <= 0 ||
        capH < 0 || capH >= height || capExtra < 0) {
        std::fprintf(stderr,
            "gen-mesh-chimney: dims > 0; capH < height; capExtra >= 0\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float W2 = width  * 0.5f;
    const float D2 = depth  * 0.5f;
    const float shaftH = height - capH;
    addFlatBox(wom, 0.0f, shaftH * 0.5f, 0.0f,
               W2, shaftH * 0.5f, D2);
    if (capH > 0.0f) {
        const float capW2 = W2 + capExtra;
        const float capD2 = D2 + capExtra;
        addFlatBox(wom, 0.0f, shaftH + capH * 0.5f, 0.0f,
                   capW2, capH * 0.5f, capD2);
    }
    finalizeAsSingleBatch(wom);
    float maxX = W2 + (capH > 0 ? capExtra : 0);
    float maxZ = D2 + (capH > 0 ? capExtra : 0);
    wom.boundMin = glm::vec3(-maxX, 0, -maxZ);
    wom.boundMax = glm::vec3(+maxX, height, +maxZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-chimney")) return 1;
    printWomWrote(womBase);
    std::printf("  shaft      : %.3f x %.3f x %.3f\n", width, depth, shaftH);
    if (capH > 0)
        std::printf("  cap        : %.3f thick, %.3f wider\n", capH, capExtra);
    else
        std::printf("  cap        : (none)\n");
    printWomMeshStats(wom);
    return 0;
}

int handlePergola(int& i, int argc, char** argv) {
    // Pergola: 4 corner posts holding 2 long perimeter beams plus
    // N cross-beams running between the long beams. Distinct from
    // --gen-mesh-canopy because there's no flat overhead panel -
    // the open lattice top reads as a garden arbor / sun-trellis
    // rather than a closed-top market stall. The 59th procedural
    // mesh primitive.
    std::string womBase = argv[++i];
    float length = 2.4f;
    float width  = 1.6f;
    float height = 2.2f;
    float postR  = 0.06f;
    float beamT  = 0.05f;
    int   crossbeams = 5;
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, postR);
    parseOptFloat(i, argc, argv, beamT);
    parseOptInt(i, argc, argv, crossbeams);
    if (length <= 0 || width <= 0 || height <= 0 ||
        postR <= 0 || postR * 2 >= std::min(length, width) ||
        beamT <= 0 || crossbeams < 0 || crossbeams > 32) {
        std::fprintf(stderr,
            "gen-mesh-pergola: dims > 0; postR*2 < length/width; "
            "crossbeams 0..32\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float L2 = length * 0.5f;
    const float W2 = width  * 0.5f;
    // Posts: full height at the 4 corners, inset by postR so the
    // outer face lines up with the (-L2..+L2, -W2..+W2) footprint.
    const float postX = L2 - postR;
    const float postZ = W2 - postR;
    const float postH = height - beamT;       // posts stop at beam underside
    const float postCY = postH * 0.5f;
    const float postHY = postH * 0.5f;
    addFlatBox(wom, +postX, postCY, +postZ, postR, postHY, postR);
    addFlatBox(wom, -postX, postCY, +postZ, postR, postHY, postR);
    addFlatBox(wom, +postX, postCY, -postZ, postR, postHY, postR);
    addFlatBox(wom, -postX, postCY, -postZ, postR, postHY, postR);
    // Two long perimeter beams along ±Z, spanning the full length
    // and sitting on top of the posts.
    const float beamCY = postH + beamT * 0.5f;
    const float beamHY = beamT * 0.5f;
    addFlatBox(wom, 0, beamCY, +postZ, L2, beamHY, postR);
    addFlatBox(wom, 0, beamCY, -postZ, L2, beamHY, postR);
    // N cross beams running along ±X, spaced evenly between the
    // perimeter beams. They sit ON TOP of the perimeter beams so
    // the lattice has visible crossings.
    if (crossbeams > 0) {
        const float xbCY = postH + beamT + beamT * 0.5f;
        const float xbHY = beamT * 0.5f;
        const float xbHZ = postR * 0.6f;       // a bit thinner than perimeter
        for (int k = 0; k < crossbeams; ++k) {
            float t = (crossbeams == 1) ? 0.5f
                       : static_cast<float>(k) / (crossbeams - 1);
            float cx = -L2 + postR + t * (length - 2.0f * postR);
            addFlatBox(wom, cx, xbCY, 0, xbHZ, xbHY, W2);
        }
    }
    finalizeAsSingleBatch(wom);
    float topY = (crossbeams > 0) ? (postH + 2.0f * beamT) : (postH + beamT);
    wom.boundMin = glm::vec3(-L2, 0, -W2);
    wom.boundMax = glm::vec3(+L2, topY, +W2);
    if (!saveWomOrError(wom, womBase, "gen-mesh-pergola")) return 1;
    printWomWrote(womBase);
    std::printf("  footprint  : %.3f x %.3f\n", length, width);
    std::printf("  height     : %.3f (post %.3f + beam %.3f)\n",
                height, postH, beamT);
    std::printf("  posts      : 4 corners (R=%.3f)\n", postR);
    std::printf("  cross beams: %d\n", crossbeams);
    printWomMeshStats(wom);
    return 0;
}

int handleDock(int& i, int argc, char** argv) {
    // Wooden dock / pier: a flat plank deck supported by N pairs
    // of square pilings. Distinct from --gen-mesh-bridge which
    // arcs OVER an obstacle - a dock walks straight out from a
    // shoreline on stilts to the water. The 58th procedural mesh
    // primitive.
    std::string womBase = argv[++i];
    float length = 3.0f;
    float width  = 1.0f;
    float height = 0.6f;
    int   pilingsPerSide = 3;
    float pilingW = 0.10f;
    float deckT  = 0.10f;
    parseOptFloat(i, argc, argv, length);
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, height);
    parseOptInt(i, argc, argv, pilingsPerSide);
    parseOptFloat(i, argc, argv, pilingW);
    parseOptFloat(i, argc, argv, deckT);
    if (length <= 0 || width <= 0 || height <= 0 ||
        deckT <= 0 || pilingW <= 0 || pilingW * 2 >= width ||
        pilingsPerSide < 1 || pilingsPerSide > 16) {
        std::fprintf(stderr,
            "gen-mesh-dock: dims > 0; pilingW*2 < width; pilings 1..16\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    const float W2 = width * 0.5f;
    const float L2 = length * 0.5f;
    // Deck slab spans the full footprint at the top.
    addBox(0, height + deckT * 0.5f, 0, W2, deckT * 0.5f, L2);
    // N pairs of pilings: pilingsPerSide along each long edge,
    // evenly spaced. Outer face of each piling sits inside the deck
    // outline by pilingW so the deck overhangs the pilings slightly
    // - the standard "boards rest ON TOP of the posts" look.
    const float pilingHY = height * 0.5f;
    const float pilingX = W2 - pilingW;
    for (int p = 0; p < pilingsPerSide; ++p) {
        float t = (pilingsPerSide == 1) ? 0.5f
                   : static_cast<float>(p) / (pilingsPerSide - 1);
        float cz = -L2 + pilingW + t * (length - 2.0f * pilingW);
        addBox(+pilingX, pilingHY, cz, pilingW, pilingHY, pilingW);
        addBox(-pilingX, pilingHY, cz, pilingW, pilingHY, pilingW);
    }
    finalizeAsSingleBatch(wom);
    wom.boundMin = glm::vec3(-W2, 0, -L2);
    wom.boundMax = glm::vec3( W2, height + deckT, +L2);
    if (!saveWomOrError(wom, womBase, "gen-mesh-dock")) return 1;
    printWomWrote(womBase);
    std::printf("  deck       : %.3fW x %.3fL x %.3f thick at H=%.3f\n",
                width, length, deckT, height);
    std::printf("  pilings    : %d per side × 2 (W=%.3f)\n",
                pilingsPerSide, pilingW);
    printWomMeshStats(wom);
    return 0;
}

int handleHaystack(int& i, int argc, char** argv) {
    // Layered farm haystack: 3+ stacked frustums, each smaller than
    // the one below, with the topmost layer tapering to a point.
    // The terraced silhouette reads as bound straw shocks rather
    // than a smooth cone (which is what --gen-mesh-pyramid produces).
    // The 57th procedural mesh primitive.
    std::string womBase = argv[++i];
    float baseR  = 0.6f;
    float height = 0.9f;
    int   layers = 3;
    int   sides  = 12;
    parseOptFloat(i, argc, argv, baseR);
    parseOptFloat(i, argc, argv, height);
    parseOptInt(i, argc, argv, layers);
    parseOptInt(i, argc, argv, sides);
    if (baseR <= 0 || height <= 0 ||
        layers < 2 || layers > 16 ||
        sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-haystack: dims > 0; layers 2..16; sides 6..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) -> uint32_t {
        return addVertex(wom, p, n, uv);
    };
    const float pi = 3.14159265358979f;
    const float layerH = height / layers;
    // Per-layer radii: outer = base * (1 - i/(layers+1)) so the top
    // layer still has visible thickness and only the FINAL apex
    // collapses to a point. Without the +1 the top frustum would
    // already have radius 0.
    auto rOf = [&](int li) {
        return baseR * (1.0f - static_cast<float>(li) / (layers + 1));
    };
    // Each layer i: outer ring at y=i*layerH, inner ring at
    // y=i*layerH (smaller, just inside the layer above's overhang),
    // and side wall + top "shelf" annulus.
    for (int li = 0; li < layers - 1; ++li) {
        float y0 = li * layerH;
        float y1 = (li + 1) * layerH;
        float r0 = rOf(li);
        float r1 = rOf(li + 1);
        // Outer side wall: ring at (y0, r0) → ring at (y1, r0).
        // Slope normal points outward. Vertical sides give the
        // "stacked layer" look (no taper within a layer).
        uint32_t bot = static_cast<uint32_t>(wom.vertices.size());
        for (int s = 0; s <= sides; ++s) {
            float u = static_cast<float>(s) / sides;
            float ang = u * 2.0f * pi;
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV({r0 * std::cos(ang), y0, r0 * std::sin(ang)},
                 n, {u, 0});
        }
        uint32_t top = static_cast<uint32_t>(wom.vertices.size());
        for (int s = 0; s <= sides; ++s) {
            float u = static_cast<float>(s) / sides;
            float ang = u * 2.0f * pi;
            glm::vec3 n(std::cos(ang), 0, std::sin(ang));
            addV({r0 * std::cos(ang), y1, r0 * std::sin(ang)},
                 n, {u, 1});
        }
        for (int s = 0; s < sides; ++s) {
            wom.indices.insert(wom.indices.end(),
                {bot + s, top + s, bot + s + 1,
                 bot + s + 1, top + s, top + s + 1});
        }
        // Top shelf annulus: this is what makes the terraced look -
        // the visible step where this layer meets the smaller
        // layer above. Faces +Y.
        uint32_t shelfOuter = static_cast<uint32_t>(wom.vertices.size());
        for (int s = 0; s <= sides; ++s) {
            float u = static_cast<float>(s) / sides;
            float ang = u * 2.0f * pi;
            addV({r0 * std::cos(ang), y1, r0 * std::sin(ang)},
                 {0, 1, 0}, {0.5f + 0.5f * std::cos(ang),
                              0.5f + 0.5f * std::sin(ang)});
        }
        uint32_t shelfInner = static_cast<uint32_t>(wom.vertices.size());
        for (int s = 0; s <= sides; ++s) {
            float u = static_cast<float>(s) / sides;
            float ang = u * 2.0f * pi;
            float ratio = r1 / r0;
            addV({r1 * std::cos(ang), y1, r1 * std::sin(ang)},
                 {0, 1, 0},
                 {0.5f + 0.5f * ratio * std::cos(ang),
                  0.5f + 0.5f * ratio * std::sin(ang)});
        }
        for (int s = 0; s < sides; ++s) {
            wom.indices.insert(wom.indices.end(),
                {shelfOuter + s, shelfOuter + s + 1, shelfInner + s,
                 shelfInner + s, shelfOuter + s + 1, shelfInner + s + 1});
        }
    }
    // Top layer: cone from the top frustum's inner radius to a
    // single apex point.
    {
        int li = layers - 1;
        float y0 = li * layerH;
        float y1 = height;
        float r0 = rOf(li);
        glm::vec3 apex(0, y1, 0);
        // Side cone fan.
        for (int s = 0; s < sides; ++s) {
            float u0 = static_cast<float>(s) / sides;
            float u1 = static_cast<float>(s + 1) / sides;
            float ang0 = u0 * 2.0f * pi;
            float ang1 = u1 * 2.0f * pi;
            glm::vec3 b0(r0 * std::cos(ang0), y0, r0 * std::sin(ang0));
            glm::vec3 b1(r0 * std::cos(ang1), y0, r0 * std::sin(ang1));
            // Per-triangle normal so each face is flat-shaded.
            glm::vec3 n = glm::normalize(glm::cross(b1 - b0, apex - b0));
            uint32_t i0 = addV(b0, n, {u0, 0});
            uint32_t i1 = addV(b1, n, {u1, 0});
            uint32_t i2 = addV(apex, n, {(u0 + u1) * 0.5f, 1});
            wom.indices.insert(wom.indices.end(), {i0, i1, i2});
        }
    }
    // Bottom disc faces -Y so the haystack is closed at ground level.
    {
        uint32_t center = addV({0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f});
        uint32_t ring = static_cast<uint32_t>(wom.vertices.size());
        for (int s = 0; s <= sides; ++s) {
            float u = static_cast<float>(s) / sides;
            float ang = u * 2.0f * pi;
            addV({baseR * std::cos(ang), 0, baseR * std::sin(ang)},
                 {0, -1, 0},
                 {0.5f + 0.5f * std::cos(ang),
                  0.5f + 0.5f * std::sin(ang)});
        }
        for (int s = 0; s < sides; ++s) {
            wom.indices.insert(wom.indices.end(),
                {center, ring + s + 1, ring + s});
        }
    }
    finalizeAsSingleBatch(wom);
    setCenteredBoundsXZ(wom, baseR, baseR, height);
    if (!saveWomOrError(wom, womBase, "gen-mesh-haystack")) return 1;
    printWomWrote(womBase);
    std::printf("  base R     : %.3f, height %.3f\n", baseR, height);
    std::printf("  layers     : %d (%d sides each)\n", layers, sides);
    printWomMeshStats(wom);
    return 0;
}

int handleCanopy(int& i, int argc, char** argv) {
    // Market-stall canopy: 4 corner posts holding a flat fabric
    // panel overhead. Optional drape lip hanging down from each
    // edge of the panel for a real awning look. All axis-aligned
    // boxes - closed solid for collision baking. The 56th
    // procedural mesh primitive.
    std::string womBase = argv[++i];
    float width  = 1.6f;
    float depth  = 1.2f;
    float height = 2.0f;
    float postR  = 0.05f;
    float panelT = 0.03f;
    float drape  = 0.15f;
    parseOptFloat(i, argc, argv, width);
    parseOptFloat(i, argc, argv, depth);
    parseOptFloat(i, argc, argv, height);
    parseOptFloat(i, argc, argv, postR);
    parseOptFloat(i, argc, argv, panelT);
    parseOptFloat(i, argc, argv, drape);
    if (width <= 0 || depth <= 0 || height <= 0 ||
        postR <= 0 || postR * 2 >= std::min(width, depth) ||
        panelT <= 0 || drape < 0 || drape >= height) {
        std::fprintf(stderr,
            "gen-mesh-canopy: dims > 0; postR*2 < width/depth; drape < height\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    const float W2 = width * 0.5f;
    const float D2 = depth * 0.5f;
    const float postH = height - panelT;
    const float postCY = postH * 0.5f;
    const float postHY = postH * 0.5f;
    // Posts inset by postR so the outer corners line up with the
    // panel edges above.
    const float postX = W2 - postR;
    const float postZ = D2 - postR;
    addBox(+postX, postCY, +postZ, postR, postHY, postR);
    addBox(-postX, postCY, +postZ, postR, postHY, postR);
    addBox(+postX, postCY, -postZ, postR, postHY, postR);
    addBox(-postX, postCY, -postZ, postR, postHY, postR);
    // Top fabric panel - a thin slab spanning the full footprint.
    addBox(0, postH + panelT * 0.5f, 0, W2, panelT * 0.5f, D2);
    // Optional drape lips hanging down from each panel edge.
    if (drape > 0.0f) {
        const float drapeT = panelT * 0.5f;     // half thickness of drape
        const float drapeCY = postH - drape * 0.5f + panelT;
        const float drapeHY = drape * 0.5f;
        // Front edge (+Z): box spans width, hangs over front edge.
        addBox(0, drapeCY, +D2 + drapeT, W2, drapeHY, drapeT);
        addBox(0, drapeCY, -D2 - drapeT, W2, drapeHY, drapeT);
        addBox(+W2 + drapeT, drapeCY, 0, drapeT, drapeHY, D2);
        addBox(-W2 - drapeT, drapeCY, 0, drapeT, drapeHY, D2);
    }
    finalizeAsSingleBatch(wom);
    float maxX = W2 + (drape > 0 ? panelT * 0.5f : 0);
    float maxZ = D2 + (drape > 0 ? panelT * 0.5f : 0);
    wom.boundMin = glm::vec3(-maxX, 0, -maxZ);
    wom.boundMax = glm::vec3( maxX, height, +maxZ);
    if (!saveWomOrError(wom, womBase, "gen-mesh-canopy")) return 1;
    printWomWrote(womBase);
    std::printf("  footprint  : %.3f x %.3f\n", width, depth);
    std::printf("  height     : %.3f (post %.3f + panel %.3f)\n",
                height, postH, panelT);
    std::printf("  posts      : 4 corners (R=%.3f)\n", postR);
    if (drape > 0)
        std::printf("  drape      : %.3f hanging from each edge\n", drape);
    else
        std::printf("  drape      : (none)\n");
    printWomMeshStats(wom);
    return 0;
}

int handleWoodpile(int& i, int argc, char** argv) {
    // Stacked-firewood pile: N=6 cylindrical logs aligned along
    // the Z axis, packed into a tight 3-2-1 pyramid (3 logs on
    // the bottom row, 2 in the middle, 1 on top). The middle and
    // top rows nestle into the gaps between the logs below using
    // exact cos(30°) = sqrt(3)/2 vertical spacing so adjacent
    // logs touch tangentially. The 55th procedural mesh primitive.
    std::string womBase = argv[++i];
    float logR   = 0.10f;
    float logLen = 0.80f;
    int   sides  = 12;
    parseOptFloat(i, argc, argv, logR);
    parseOptFloat(i, argc, argv, logLen);
    parseOptInt(i, argc, argv, sides);
    if (logR <= 0 || logLen <= 0 || sides < 6 || sides > 64) {
        std::fprintf(stderr,
            "gen-mesh-woodpile: dims > 0; sides 6..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    const float halfL = logLen * 0.5f;
    auto addLog = [&](float cx, float cy) {
        addClosedCylinderZ(wom, cx, cy, logR, -halfL, +halfL, sides);
    };
    // 3-2-1 stack: bottom row of 3 logs sitting on the ground,
    // middle row of 2 nestled in their gaps, top single log
    // crowning the pile. cos(30°) ≈ sqrt(3)/2 vertical step.
    const float yStep = logR * std::sqrt(3.0f);
    addLog(-2.0f * logR, logR);          // bottom-left
    addLog( 0.0f,         logR);          // bottom-center
    addLog(+2.0f * logR, logR);          // bottom-right
    addLog(-1.0f * logR, logR + yStep);   // middle-left
    addLog(+1.0f * logR, logR + yStep);   // middle-right
    addLog( 0.0f,         logR + 2 * yStep);  // top
    finalizeAsSingleBatch(wom);
    float maxX = 3.0f * logR;
    float maxY = 2.0f * logR + 2.0f * yStep;
    wom.boundMin = glm::vec3(-maxX, 0, -halfL);
    wom.boundMax = glm::vec3( maxX, maxY, +halfL);
    if (!saveWomOrError(wom, womBase, "gen-mesh-woodpile")) return 1;
    printWomWrote(womBase);
    std::printf("  logs       : 6 in 3-2-1 stack (R=%.3f, len=%.3f, sides=%d)\n",
                logR, logLen, sides);
    std::printf("  span       : %.3fW x %.3fH x %.3fL\n",
                maxX * 2.0f, maxY, logLen);
    printWomMeshStats(wom);
    return 0;
}

int handleFirepit(int& i, int argc, char** argv) {
    // Camp firepit: a ring of N stone cubes around two crossed log
    // boxes (one along X, one along Z, slightly raised). Pairs
    // naturally with --gen-mesh-tent for outdoor camp set dressing.
    // The 54th procedural mesh primitive.
    std::string womBase = argv[++i];
    float ringR = 0.5f;
    int   stones = 8;
    float stoneSize = 0.10f;
    float logLen = 0.45f;
    float logThick = 0.05f;
    parseOptFloat(i, argc, argv, ringR);
    parseOptInt(i, argc, argv, stones);
    parseOptFloat(i, argc, argv, stoneSize);
    parseOptFloat(i, argc, argv, logLen);
    parseOptFloat(i, argc, argv, logThick);
    if (ringR <= 0 || stoneSize <= 0 || logLen <= 0 || logThick <= 0 ||
        stones < 3 || stones > 64) {
        std::fprintf(stderr,
            "gen-mesh-firepit: dims > 0; stones must be 3..64\n");
        return 1;
    }
    stripExt(womBase, ".wom");
    wowee::pipeline::WoweeModel wom;
    initWomDefaults(wom, womBase);
    auto addBox = [&](float cx, float cy, float cz,
                      float hx, float hy, float hz) {
        addFlatBox(wom, cx, cy, cz, hx, hy, hz);
    };
    // Ring of stones - N axis-aligned cube stones evenly placed
    // around the firepit center. Slight Y offset puts them sitting
    // on the ground rather than sunk into it.
    const float pi = 3.14159265358979f;
    for (int s = 0; s < stones; ++s) {
        float ang = (2.0f * pi * s) / stones;
        float cx = ringR * std::cos(ang);
        float cz = ringR * std::sin(ang);
        addBox(cx, stoneSize, cz, stoneSize, stoneSize, stoneSize);
    }
    // Two crossed logs at center, raised so they sit on the ash
    // bed. The two-log cross is the unmistakable visual cue that
    // separates a firepit from a generic stone ring.
    float logCY = logThick;
    addBox(0, logCY, 0, logLen * 0.5f, logThick * 0.5f, logThick * 0.5f);
    addBox(0, logCY + logThick, 0, logThick * 0.5f, logThick * 0.5f,
           logLen * 0.5f);
    finalizeAsSingleBatch(wom);
    float maxR = std::max(ringR + stoneSize, logLen * 0.5f);
    float maxY = std::max(stoneSize * 2.0f, logCY + logThick * 1.5f);
    setCenteredBoundsXZ(wom, maxR, maxR, maxY);
    if (!saveWomOrError(wom, womBase, "gen-mesh-firepit")) return 1;
    printWomWrote(womBase);
    std::printf("  ring       : R=%.3f, %d stones (%.3f cubes)\n",
                ringR, stones, stoneSize);
    std::printf("  logs       : 2 crossed (len %.3f, thick %.3f)\n",
                logLen, logThick);
    printWomMeshStats(wom);
    return 0;
}

// Composite-pack item: a flag name, the handler that builds it,
// and the basename to write under <outDir>. Used by --gen-camp-pack
// and --gen-blacksmith-pack to enumerate their member primitives.
struct PackItem {
    const char* flag;
    int (*fn)(int&, int, char**);
    const char* leaf;
};

// Emit every PackItem in `items` into <outDir>/. Returns 0 on full
// success or the first non-zero rc the handler produced. Each
// handler runs with a synthetic argv whose index 0 is the flag and
// index 1 is the destination wom-base - handlers do argv[++i] from
// the flag's position to read that base, so this matches the normal
// CLI invocation contract exactly.
int emitMeshPack(const std::string& outDir, const char* packName,
                 const std::vector<PackItem>& items) {
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr,
            "%s: cannot create %s: %s\n",
            packName, outDir.c_str(), ec.message().c_str());
        return 1;
    }
    int produced = 0;
    for (const auto& it : items) {
        std::string path = outDir + "/" + it.leaf;
        const char* args[2] = {it.flag, path.c_str()};
        std::vector<char*> mut;
        mut.reserve(2);
        for (auto* a : args) mut.push_back(const_cast<char*>(a));
        int idx = 0;
        if (it.fn(idx, 2, mut.data()) != 0) {
            std::fprintf(stderr,
                "%s: %s sub-handler failed\n", packName, it.flag);
            return 1;
        }
        ++produced;
    }
    std::printf("\nWrote %s to %s/ - %d primitives\n",
                packName, outDir.c_str(), produced);
    return 0;
}

int handleGenCampPack(int& i, int /*argc*/, char** argv) {
    // Outdoor-camp scene: tent, firepit, bedroll, canopy,
    // woodpile, haystack. See emitMeshPack for the synthetic-argv
    // contract. Users wanting custom dimensions should call the
    // individual --gen-mesh-* commands directly.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "camp pack", {
        {"--gen-mesh-tent",     handleTent,     "tent"},
        {"--gen-mesh-firepit",  handleFirepit,  "firepit"},
        {"--gen-mesh-bedroll",  handleBedroll,  "bedroll"},
        {"--gen-mesh-canopy",   handleCanopy,   "canopy"},
        {"--gen-mesh-woodpile", handleWoodpile, "woodpile"},
        {"--gen-mesh-haystack", handleHaystack, "haystack"},
    });
}

int handleGenBlacksmithPack(int& i, int /*argc*/, char** argv) {
    // Blacksmith / smithy scene: forge (the hot work), anvil
    // (where iron gets struck), workbench (where finished tools
    // land), water-trough (for tempering hot metal), crate-stack
    // (for raw-material storage), hitching-post (for delivery
    // mounts that drop off charcoal and ore).
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "blacksmith pack", {
        {"--gen-mesh-forge",          handleForge,         "forge"},
        {"--gen-mesh-anvil",          handleAnvil,         "anvil"},
        {"--gen-mesh-workbench",      handleWorkbench,     "workbench"},
        {"--gen-mesh-water-trough",   handleWaterTrough,   "trough"},
        {"--gen-mesh-crate-stack",    handleCrateStack,    "crates"},
        {"--gen-mesh-hitching-post",  handleHitchingPost,  "hitching"},
    });
}

int handleGenArenaPack(int& i, int /*argc*/, char** argv) {
    // Combat training / gladiator pit / militia drill yard scene:
    // training-dummy (the practice target), archery-target (range
    // station), workbench (weapons-rack stand), crate-stack
    // (gear storage), bench (audience seating), water-trough
    // (between-bout refresh), hitching-rail (mount parking).
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "arena pack", {
        {"--gen-mesh-training-dummy", handleTrainingDummy, "dummy"},
        {"--gen-mesh-archery-target", handleArcheryTarget, "target"},
        {"--gen-mesh-workbench",      handleWorkbench,     "rack"},
        {"--gen-mesh-crate-stack",    handleCrateStack,    "crates"},
        {"--gen-mesh-bench",          handleBench,         "bench"},
        {"--gen-mesh-water-trough",   handleWaterTrough,   "trough"},
        {"--gen-mesh-hitching-rail",  handleHitchingRail,  "rail"},
    });
}

int handleGenMiningPack(int& i, int /*argc*/, char** argv) {
    // Mining shaft / quarry scene: gravel-pile (loose rubble),
    // crate-stack (raw-ore cargo), mine-cart (underground
    // transport), pillar-row (roof supports), lantern (light
    // source), workbench (assay / sorting station), hitching-
    // post (mule / cart tie). Together these form a complete
    // working mine entrance / quarry-floor layout.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "mining pack", {
        {"--gen-mesh-gravel-pile",   handleGravelPile,    "gravel"},
        {"--gen-mesh-crate-stack",   handleCrateStack,    "crates"},
        {"--gen-mesh-mine-cart",     handleMineCart,      "cart"},
        {"--gen-mesh-pillar-row",    handlePillarRow,     "pillars"},
        {"--gen-mesh-lantern",       handleLantern,       "lantern"},
        {"--gen-mesh-workbench",     handleWorkbench,     "workbench"},
        {"--gen-mesh-hitching-post", handleHitchingPost,  "hitching"},
    });
}

int handleGenTavernPack(int& i, int /*argc*/, char** argv) {
    // Inn / tavern scene: house (the inn building), chimney
    // (above the kitchen hearth), table + bench (the common
    // room), barrel (ale/cider stock), bookshelf (back-wall
    // record / register / library), signpost (inn-sign post).
    // Together these form the minimum-viable tavern setup for
    // any roadside inn or town watering-hole.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "tavern pack", {
        {"--gen-mesh-house",      handleHouse,       "house"},
        {"--gen-mesh-chimney",    handleChimney,     "chimney"},
        {"--gen-mesh-table",      handleTable,       "table"},
        {"--gen-mesh-bench",      handleBench,       "bench"},
        {"--gen-mesh-barrel",     handleBarrel,      "barrel"},
        {"--gen-mesh-bookshelf",  handleBookshelf,   "shelf"},
        {"--gen-mesh-signpost",   handleSignpost,    "sign"},
    });
}

int handleGenDockPack(int& i, int /*argc*/, char** argv) {
    // Harbor / dockyard scene: dock (the pier itself), crate-
    // stack (cargo waiting to be loaded), barrel (sailor stash),
    // canopy (shade for the dockmaster's station), bench (waiting
    // area for passengers), signpost (port marker), hitching-post
    // (for tying up small boats / mounts arriving with goods).
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "dock pack", {
        {"--gen-mesh-dock",          handleDock,          "dock"},
        {"--gen-mesh-crate-stack",   handleCrateStack,    "crates"},
        {"--gen-mesh-barrel",        handleBarrel,        "barrel"},
        {"--gen-mesh-canopy",        handleCanopy,        "canopy"},
        {"--gen-mesh-bench",         handleBench,         "bench"},
        {"--gen-mesh-signpost",      handleSignpost,      "signpost"},
        {"--gen-mesh-hitching-post", handleHitchingPost,  "hitching"},
    });
}

int handleGenGardenPack(int& i, int /*argc*/, char** argv) {
    // Garden / courtyard scene: pergola (vine-arbor frame),
    // fountain (centerpiece), stone-bench (seating), shrine
    // (decorative focal), beehive (productive prop), scarecrow
    // (rural touch), well (water source). Together these form
    // a recognizable kitchen-garden / monastery courtyard /
    // small park.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "garden pack", {
        {"--gen-mesh-pergola",      handlePergola,     "pergola"},
        {"--gen-mesh-fountain",     handleFountain,    "fountain"},
        {"--gen-mesh-stone-bench",  handleStoneBench,  "bench"},
        {"--gen-mesh-shrine",       handleShrine,      "shrine"},
        {"--gen-mesh-beehive",      handleBeehive,     "beehive"},
        {"--gen-mesh-scarecrow",    handleScarecrow,   "scarecrow"},
        {"--gen-mesh-well",         handleWell,        "well"},
    });
}

int handleGenGraveyardPack(int& i, int /*argc*/, char** argv) {
    // Cemetery / graveyard scene: grave (filled plot mound),
    // tombstone (marker stone), coffin (above-ground or open),
    // statue (mourning angel / patron deity), stone-bench (for
    // visitors), gravel-pile (loose earth from a fresh dig),
    // cage (cemetery-fence section / mausoleum gate). Together
    // these form the standard town-edge burial yard.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "graveyard pack", {
        {"--gen-mesh-grave",        handleGrave,       "grave"},
        {"--gen-mesh-tombstone",    handleTombstone,   "tombstone"},
        {"--gen-mesh-coffin",       handleCoffin,      "coffin"},
        {"--gen-mesh-statue",       handleStatue,      "statue"},
        {"--gen-mesh-stone-bench",  handleStoneBench,  "bench"},
        {"--gen-mesh-gravel-pile",  handleGravelPile,  "rubble"},
        {"--gen-mesh-cage",         handleCage,        "fence"},
    });
}

int handleGenKitchenPack(int& i, int /*argc*/, char** argv) {
    // Tavern kitchen / cookhouse scene: stove (the cookfire),
    // cauldron (large hanging pot), table (prep surface), stool
    // (the cook's seat), barrel (water / ale / flour storage),
    // mug (drink ready for the inn customer), mortar-pestle
    // (herb / spice grinding). The 11th themed pack - pairs
    // with --gen-tavern-pack as the back-of-house complement
    // to that pack's front-of-house common-room props.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "kitchen pack", {
        {"--gen-mesh-stove",          handleStove,         "stove"},
        {"--gen-mesh-cauldron",       handleCauldron,      "cauldron"},
        {"--gen-mesh-table",          handleTable,         "prep-table"},
        {"--gen-mesh-stool",          handleStool,         "stool"},
        {"--gen-mesh-barrel",         handleBarrel,        "barrel"},
        {"--gen-mesh-mug",            handleMug,           "mug"},
        {"--gen-mesh-mortar-pestle",  handleMortarPestle,  "mortar"},
    });
}

int handleGenTemplePack(int& i, int /*argc*/, char** argv) {
    // Temple / shrine scene: altar (the focal point), shrine
    // (auxiliary devotional), brazier (lit on either side of the
    // altar), 2 pillars (flanking the altar approach), statue
    // (deity figure), portal (entrance gateway). A full ritual
    // hall in 7 primitives.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "temple pack", {
        {"--gen-mesh-altar",     handleAltar,    "altar"},
        {"--gen-mesh-shrine",    handleShrine,   "shrine"},
        {"--gen-mesh-brazier",   handleBrazier,  "brazier"},
        {"--gen-mesh-pillar",    handlePillar,   "pillar"},
        {"--gen-mesh-statue",    handleStatue,   "statue"},
        {"--gen-mesh-portal",    handlePortal,   "portal"},
        {"--gen-mesh-podium",    handlePodium,   "podium"},
    });
}

int handleGenVillagePack(int& i, int /*argc*/, char** argv) {
    // Village square scene: a small house, an outhouse beside it,
    // a chimney for the house roof piece, a hitching post at the
    // hitching rail, a well for the village square, a signpost
    // pointing the way out, and a haystack from the nearby farm.
    // Together these primitives form a recognizable rural-village
    // hub when arranged in a zone.
    std::string outDir = argv[++i];
    return emitMeshPack(outDir, "village pack", {
        {"--gen-mesh-house",          handleHouse,         "house"},
        {"--gen-mesh-outhouse",       handleOuthouse,      "outhouse"},
        {"--gen-mesh-chimney",        handleChimney,       "chimney"},
        {"--gen-mesh-hitching-post",  handleHitchingPost,  "hitching"},
        {"--gen-mesh-well",           handleWell,          "well"},
        {"--gen-mesh-signpost",       handleSignpost,      "signpost"},
        {"--gen-mesh-haystack",       handleHaystack,      "haystack"},
    });
}

}  // namespace

namespace {
// Table-driven dispatch for every --gen-mesh-* flag. minNextArgs
// is the count of *required* positional args after the flag - used
// as a guard so a bare `--gen-mesh-X` (or `--gen-mesh-X` followed
// only by the next switch) is rejected by the dispatcher even
// without consulting kArgRequired. Every primitive in this file
// must have an entry here OR it will not be reachable from the CLI.
struct MeshEntry {
    const char* flag;
    int minNextArgs;
    int (*fn)(int&, int, char**);
};

constexpr MeshEntry kMeshTable[] = {
    {"--gen-mesh-textured",       3, handleTextured},
    {"--gen-mesh",                2, handleMeshDispatch},
    {"--gen-mesh-stairs",         2, handleStairs},
    {"--gen-mesh-grid",           2, handleGrid},
    {"--gen-mesh-disc",           1, handleDisc},
    {"--gen-mesh-tube",           1, handleTube},
    {"--gen-mesh-capsule",        1, handleCapsule},
    {"--gen-mesh-arch",           1, handleArch},
    {"--gen-mesh-pyramid",        1, handlePyramid},
    {"--gen-mesh-fence",          1, handleFence},
    {"--gen-mesh-tree",           1, handleTree},
    {"--gen-mesh-rock",           1, handleRock},
    {"--gen-mesh-pillar",         1, handlePillar},
    {"--gen-mesh-bridge",         1, handleBridge},
    {"--gen-mesh-tower",          1, handleTower},
    {"--gen-mesh-house",          1, handleHouse},
    {"--gen-mesh-fountain",       1, handleFountain},
    {"--gen-mesh-statue",         1, handleStatue},
    {"--gen-mesh-altar",          1, handleAltar},
    {"--gen-mesh-portal",         1, handlePortal},
    {"--gen-mesh-archway",        1, handleArchway},
    {"--gen-mesh-archway-double", 1, handleArchwayDouble},
    {"--gen-mesh-barrel",         1, handleBarrel},
    {"--gen-mesh-chest",          1, handleChest},
    {"--gen-mesh-anvil",          1, handleAnvil},
    {"--gen-mesh-mushroom",       1, handleMushroom},
    {"--gen-mesh-cart",           1, handleCart},
    {"--gen-mesh-banner",         1, handleBanner},
    {"--gen-mesh-grave",          1, handleGrave},
    {"--gen-mesh-bench",          1, handleBench},
    {"--gen-mesh-shrine",         1, handleShrine},
    {"--gen-mesh-totem",          1, handleTotem},
    {"--gen-mesh-cage",           1, handleCage},
    {"--gen-mesh-throne",         1, handleThrone},
    {"--gen-mesh-coffin",         1, handleCoffin},
    {"--gen-mesh-bookshelf",      1, handleBookshelf},
    {"--gen-mesh-tent",           1, handleTent},
    {"--gen-mesh-firepit",        1, handleFirepit},
    {"--gen-mesh-woodpile",       1, handleWoodpile},
    {"--gen-mesh-canopy",         1, handleCanopy},
    {"--gen-mesh-haystack",       1, handleHaystack},
    {"--gen-mesh-dock",           1, handleDock},
    {"--gen-mesh-pergola",        1, handlePergola},
    {"--gen-mesh-chimney",        1, handleChimney},
    {"--gen-mesh-bedroll",        1, handleBedroll},
    {"--gen-mesh-workbench",      1, handleWorkbench},
    {"--gen-mesh-crate-stack",    1, handleCrateStack},
    {"--gen-mesh-watchpost",      1, handleWatchpost},
    {"--gen-mesh-water-trough",   1, handleWaterTrough},
    {"--gen-mesh-training-dummy", 1, handleTrainingDummy},
    {"--gen-mesh-hitching-post",  1, handleHitchingPost},
    {"--gen-mesh-outhouse",       1, handleOuthouse},
    {"--gen-mesh-forge",          1, handleForge},
    {"--gen-mesh-archery-target", 1, handleArcheryTarget},
    {"--gen-mesh-gravel-pile",    1, handleGravelPile},
    {"--gen-mesh-stone-bench",    1, handleStoneBench},
    {"--gen-mesh-mine-cart",      1, handleMineCart},
    {"--gen-mesh-hitching-rail",  1, handleHitchingRail},
    {"--gen-mesh-pillar-row",     1, handlePillarRow},
    {"--gen-mesh-statue-base",    1, handleStatueBase},
    {"--gen-mesh-bird-bath",      1, handleBirdBath},
    {"--gen-mesh-planter-box",    1, handlePlanterBox},
    {"--gen-mesh-urn",            1, handleUrn},
    {"--gen-mesh-candle",         1, handleCandle},
    {"--gen-mesh-lantern",        1, handleLantern},
    {"--gen-mesh-chalice",        1, handleChalice},
    {"--gen-mesh-standing-torch", 1, handleStandingTorch},
    {"--gen-mesh-scroll-case",    1, handleScrollCase},
    {"--gen-mesh-stove",          1, handleStove},
    {"--gen-mesh-well-pail",      1, handleWellPail},
    {"--gen-mesh-mug",            1, handleMug},
    {"--gen-mesh-mortar-pestle",  1, handleMortarPestle},
    {"--gen-mesh-rune-stone",     1, handleRuneStone},
    {"--gen-camp-pack",           1, handleGenCampPack},
    {"--gen-blacksmith-pack",     1, handleGenBlacksmithPack},
    {"--gen-village-pack",        1, handleGenVillagePack},
    {"--gen-temple-pack",         1, handleGenTemplePack},
    {"--gen-graveyard-pack",      1, handleGenGraveyardPack},
    {"--gen-garden-pack",         1, handleGenGardenPack},
    {"--gen-dock-pack",           1, handleGenDockPack},
    {"--gen-tavern-pack",         1, handleGenTavernPack},
    {"--gen-mining-pack",         1, handleGenMiningPack},
    {"--gen-arena-pack",          1, handleGenArenaPack},
    {"--gen-kitchen-pack",        1, handleGenKitchenPack},
    {"--gen-mesh-table",          1, handleTable},
    {"--gen-mesh-lamppost",       1, handleLamppost},
    {"--gen-mesh-bed",            1, handleBed},
    {"--gen-mesh-ladder",         1, handleLadder},
    {"--gen-mesh-well",           1, handleWell},
    {"--gen-mesh-signpost",       1, handleSignpost},
    {"--gen-mesh-mailbox",        1, handleMailbox},
    {"--gen-mesh-tombstone",      1, handleTombstone},
    {"--gen-mesh-crate",          1, handleCrate},
    {"--gen-mesh-stool",          1, handleStool},
    {"--gen-mesh-cauldron",       1, handleCauldron},
    {"--gen-mesh-gate",           1, handleGate},
    {"--gen-mesh-beehive",        1, handleBeehive},
    {"--gen-mesh-weathervane",    1, handleWeathervane},
    {"--gen-mesh-scarecrow",      1, handleScarecrow},
    {"--gen-mesh-sundial",        1, handleSundial},
    {"--gen-mesh-podium",         1, handlePodium},
    {"--gen-mesh-brazier",        1, handleBrazier},
};
}  // namespace

bool handleGenMesh(int& i, int argc, char** argv, int& outRc) {
    for (const auto& e : kMeshTable) {
        if (std::strcmp(argv[i], e.flag) == 0 && i + e.minNextArgs < argc) {
            outRc = e.fn(i, argc, argv);
            return true;
        }
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
