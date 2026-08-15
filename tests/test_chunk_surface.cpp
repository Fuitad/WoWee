// Where a point inside a terrain chunk sits in the world.
//
// The ground-clutter scatterer and the doodad scatterer each had their own
// copy of this: interpolate the height between the four surrounding grid
// points, and cross the axes on the way out. Neither was tested.
//
// Nothing fails when it is wrong. Objects sit a little above the ground or a
// little inside it, or a whole chunk's worth of them lands mirrored about the
// diagonal, and it reads as the doodad placement data being wrong.
//
// The bilinear values below are worked out by hand: at (tx, ty) the height is
// h00(1-tx)(1-ty) + h10·tx(1-ty) + h01(1-tx)ty + h11·tx·ty.
#include <catch_amalgamated.hpp>

#include "pipeline/terrain_mesh.hpp"

using wowee::pipeline::HeightMap;
using wowee::pipeline::TerrainMeshGenerator;

namespace {

/// A height map filled by a rule, inner vertices included.
///
/// The layout is WoW's interleaved 9x9 outer + 8x8 inner, and the inner grid is
/// not decoration: the mesh fans four triangles from each quad's centre vertex,
/// so that vertex is the surface over the middle of every cell. Leaving it at
/// zero here, as this once did, describes a chunk that is spiked downwards
/// between every corner - which is nothing like the terrain being sampled.
/// Inner vertex (x, y) sits at grid (x + 0.5, y + 0.5).
template <typename Rule>
HeightMap gridWhere(Rule rule) {
    HeightMap map{};
    map.heights.fill(0.0f);
    map.loaded = true;
    for (int y = 0; y <= 8; ++y) {
        for (int x = 0; x <= 8; ++x) {
            map.heights[static_cast<size_t>(y * 17 + x)] =
                rule(static_cast<float>(x), static_cast<float>(y));
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            map.heights[static_cast<size_t>(9 + y * 17 + x)] =
                rule(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
        }
    }
    return map;
}

constexpr float kUnitSize = 4.1666665f;   // a chunk is 33.33 yards across 8 cells

}  // namespace

TEST_CASE("a flat chunk is flat everywhere", "[chunksurface]") {
    const HeightMap map = gridWhere([](float, float) { return 100.0f; });
    const float position[3] = {0.0f, 0.0f, 10.0f};

    for (float f = 0.0f; f <= 8.0f; f += 2.0f) {
        INFO("at " << f);
        const glm::vec3 point =
            TerrainMeshGenerator::chunkSurfacePoint(position, map, f, f, kUnitSize);
        CHECK(point.z == Catch::Approx(110.0f));
    }
}

TEST_CASE("the height is interpolated between grid points", "[chunksurface]") {
    // A ramp along the grid's x axis: height equals x.
    const HeightMap map = gridWhere([](float x, float) { return x; });
    const float position[3] = {0.0f, 0.0f, 0.0f};

    CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.0f, 0.0f, kUnitSize).z ==
          Catch::Approx(0.0f));
    CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.5f, 0.0f, kUnitSize).z ==
          Catch::Approx(0.5f));
    CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 3.25f, 5.0f, kUnitSize).z ==
          Catch::Approx(3.25f));
}

TEST_CASE("both axes contribute to the interpolation", "[chunksurface]") {
    // height = x + 10y, so a point halfway along both is 0.5 + 5.
    const HeightMap map =
        gridWhere([](float x, float y) { return x + 10.0f * y; });
    const float position[3] = {0.0f, 0.0f, 0.0f};

    const glm::vec3 point =
        TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.5f, 0.5f, kUnitSize);
    CHECK(point.z == Catch::Approx(5.5f));
}

TEST_CASE("world X comes from the grid's Y and world Y from its X",
          "[chunksurface]") {
    // The pairing that must not be "fixed": swapping these lays every
    // scattered object out mirrored about the chunk diagonal. A point at
    // grid (2, 6) is 6 units of size along negative world X and 2 along
    // negative world Y.
    const HeightMap map = gridWhere([](float, float) { return 0.0f; });
    const float position[3] = {1000.0f, 2000.0f, 0.0f};

    const glm::vec3 point =
        TerrainMeshGenerator::chunkSurfacePoint(position, map, 2.0f, 6.0f, kUnitSize);
    CHECK(point.x == Catch::Approx(1000.0f - 6.0f * kUnitSize));
    CHECK(point.y == Catch::Approx(2000.0f - 2.0f * kUnitSize));
}

TEST_CASE("the chunk position is the origin the point is measured from",
          "[chunksurface]") {
    const HeightMap map = gridWhere([](float, float) { return 7.0f; });
    const float position[3] = {-500.0f, 300.0f, 42.0f};

    const glm::vec3 point =
        TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.0f, 0.0f, kUnitSize);
    CHECK(point.x == Catch::Approx(-500.0f));
    CHECK(point.y == Catch::Approx(300.0f));
    CHECK(point.z == Catch::Approx(49.0f));
}

TEST_CASE("a coordinate past the edge clamps to it", "[chunksurface]") {
    // A caller that rounds a fraction slightly past 8 gets the edge height
    // rather than a zero, which would drop the object through the world.
    const HeightMap map = gridWhere([](float, float) { return 55.0f; });
    const float position[3] = {0.0f, 0.0f, 0.0f};

    CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 8.0f, 8.0f, kUnitSize).z ==
          Catch::Approx(55.0f));
    CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 8.5f, 8.5f, kUnitSize).z ==
          Catch::Approx(55.0f));
    CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, -1.0f, -1.0f, kUnitSize).z ==
          Catch::Approx(55.0f));
}

TEST_CASE("a slope is sampled continuously across a cell boundary",
          "[chunksurface]") {
    // Just either side of a grid line the height must agree, or scattered
    // objects step up and down along every cell edge.
    const HeightMap map =
        gridWhere([](float x, float y) { return x * 3.0f + y * 2.0f; });
    const float position[3] = {0.0f, 0.0f, 0.0f};

    const float justBelow =
        TerrainMeshGenerator::chunkSurfacePoint(position, map, 3.999f, 2.0f, kUnitSize).z;
    const float justAbove =
        TerrainMeshGenerator::chunkSurfacePoint(position, map, 4.001f, 2.0f, kUnitSize).z;
    CHECK(justBelow == Catch::Approx(justAbove).margin(0.01f));
}

// The surface the player walks on is the surface that gets drawn, and the mesh
// fans four triangles from each quad's centre vertex. Interpolating the four
// outer corners instead ignores that vertex entirely, and MCVT puts it wherever
// the artist needed it - commonly a yard or two off the plane of its corners on
// a hillside. The floor query then answered below the visible ground and the
// player sank into a slope that looked gentle.
TEST_CASE("the quad's centre vertex is part of the surface", "[chunksurface]") {
    HeightMap map{};
    map.heights.fill(0.0f);
    map.loaded = true;
    // One quad, corners at zero, centre raised two yards.
    map.heights[9] = 2.0f;   // inner (0, 0) -> grid (0.5, 0.5)
    const float position[3] = {0.0f, 0.0f, 0.0f};

    SECTION("dead centre reads the centre vertex, not the corner average") {
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.5f, 0.5f, kUnitSize).z ==
              Catch::Approx(2.0f));
    }

    SECTION("the corners are still exactly their own height") {
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.0f, 0.0f, kUnitSize).z ==
              Catch::Approx(0.0f));
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 1.0f, 0.0f, kUnitSize).z ==
              Catch::Approx(0.0f));
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.0f, 1.0f, kUnitSize).z ==
              Catch::Approx(0.0f));
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 1.0f, 1.0f, kUnitSize).z ==
              Catch::Approx(0.0f));
    }

    SECTION("every wedge rises toward the middle") {
        // One sample inside each of the four triangles, all half way from a
        // corner to the centre, so all four must agree.
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.5f, 0.25f, kUnitSize).z ==
              Catch::Approx(1.0f));   // top
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.75f, 0.5f, kUnitSize).z ==
              Catch::Approx(1.0f));   // right
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.5f, 0.75f, kUnitSize).z ==
              Catch::Approx(1.0f));   // bottom
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, 0.25f, 0.5f, kUnitSize).z ==
              Catch::Approx(1.0f));   // left
    }
}

// A plane is reproduced exactly by any correct barycentric split, so this
// catches a wedge whose weights do not sum to one or name the wrong corner.
TEST_CASE("a tilted plane is exact in all four wedges", "[chunksurface]") {
    const HeightMap map = gridWhere([](float x, float y) { return 3.0f * x + 7.0f * y + 5.0f; });
    const float position[3] = {0.0f, 0.0f, 0.0f};
    const float samples[][2] = {
        {2.5f, 2.25f}, {2.75f, 2.5f}, {2.5f, 2.75f}, {2.25f, 2.5f},
        {0.1f, 0.2f},  {7.9f, 7.8f},  {4.5f, 4.5f},  {3.3f, 6.7f},
    };
    for (const auto& s : samples) {
        INFO("at " << s[0] << ", " << s[1]);
        CHECK(TerrainMeshGenerator::chunkSurfacePoint(position, map, s[0], s[1], kUnitSize).z ==
              Catch::Approx(3.0f * s[0] + 7.0f * s[1] + 5.0f));
    }
}
