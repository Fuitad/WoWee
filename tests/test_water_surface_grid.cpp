// Projecting a world position onto a water surface's own grid.
//
// Everything that asks what the water is doing under a point starts here: the
// height under the player, the nearest surface, the liquid type, and whether
// the water belongs to a building. water_renderer.cpp wrote the projection out
// four times, and none of the four was tested.
//
// The failures are all quiet. Project onto the wrong axis and the height comes
// from somewhere else in the lake, which shows up as the player swimming a
// little above or below the surface. Divide by a zero-length step and every
// point in the world is inside that surface.
#include <catch_amalgamated.hpp>

#include "rendering/water_surface_grid.hpp"

using wowee::rendering::sampleGridHeight;
using wowee::rendering::surfaceGridPosition;

namespace {

constexpr glm::vec3 kOrigin{100.0f, 200.0f, 30.0f};
/// An axis-aligned surface whose cells are 10 units across.
constexpr glm::vec3 kStepX{10.0f, 0.0f, 0.0f};
constexpr glm::vec3 kStepY{0.0f, 10.0f, 0.0f};

}  // namespace

TEST_CASE("the origin is grid zero", "[watergrid]") {
    const auto grid = surfaceGridPosition(kOrigin, kStepX, kStepY, 8, 8,
                                          kOrigin.x, kOrigin.y);
    REQUIRE(grid.has_value());
    CHECK(grid->x == Catch::Approx(0.0f));
    CHECK(grid->y == Catch::Approx(0.0f));
}

TEST_CASE("a position is measured in cells, not units", "[watergrid]") {
    // 25 units along a 10-unit step is 2.5 cells. Answering 25 would send the
    // caller off the end of a grid that only has nine corners.
    const auto grid = surfaceGridPosition(kOrigin, kStepX, kStepY, 8, 8,
                                          kOrigin.x + 25.0f, kOrigin.y + 5.0f);
    REQUIRE(grid.has_value());
    CHECK(grid->x == Catch::Approx(2.5f));
    CHECK(grid->y == Catch::Approx(0.5f));
}

TEST_CASE("the two axes do not swap", "[watergrid]") {
    // A surface twice as long in one direction as the other tells them apart;
    // a square one cannot. Swapping them reads the height from the far side of
    // the lake, which is a plausible height and therefore invisible.
    const glm::vec3 longX{20.0f, 0.0f, 0.0f};
    const auto grid = surfaceGridPosition(kOrigin, longX, kStepY, 8, 8,
                                          kOrigin.x + 20.0f, kOrigin.y + 10.0f);
    REQUIRE(grid.has_value());
    CHECK(grid->x == Catch::Approx(1.0f));
    CHECK(grid->y == Catch::Approx(1.0f));
}

TEST_CASE("a rotated surface projects onto its own axes", "[watergrid]") {
    // WMO water lies at whatever angle its building does. The projection is
    // onto the surface's axes, not the world's, so a point 10 units along a
    // diagonal step is one cell along, not 14.
    const glm::vec3 diagX{6.0f, 8.0f, 0.0f};    // length 10
    const glm::vec3 diagY{-8.0f, 6.0f, 0.0f};   // length 10, perpendicular
    const auto grid = surfaceGridPosition(kOrigin, diagX, diagY, 4, 4,
                                          kOrigin.x + 6.0f, kOrigin.y + 8.0f);
    REQUIRE(grid.has_value());
    CHECK(grid->x == Catch::Approx(1.0f));
    CHECK(grid->y == Catch::Approx(0.0f).margin(1e-5));
}

TEST_CASE("a point outside the surface is not on it", "[watergrid]") {
    CHECK_FALSE(surfaceGridPosition(kOrigin, kStepX, kStepY, 8, 8,
                                    kOrigin.x - 1.0f, kOrigin.y).has_value());
    CHECK_FALSE(surfaceGridPosition(kOrigin, kStepX, kStepY, 8, 8,
                                    kOrigin.x, kOrigin.y - 1.0f).has_value());
    // Eight cells of ten units is eighty; eighty-one is off the end.
    CHECK_FALSE(surfaceGridPosition(kOrigin, kStepX, kStepY, 8, 8,
                                    kOrigin.x + 81.0f, kOrigin.y).has_value());
}

TEST_CASE("the far edge belongs to the surface", "[watergrid]") {
    // A grid of eight cells has nine corners, and the last one is on it.
    // Excluding it leaves a one-unit seam of no water between two chunks,
    // which the player falls through.
    const auto grid = surfaceGridPosition(kOrigin, kStepX, kStepY, 8, 8,
                                          kOrigin.x + 80.0f, kOrigin.y + 80.0f);
    REQUIRE(grid.has_value());
    CHECK(grid->x == Catch::Approx(8.0f));
    CHECK(grid->y == Catch::Approx(8.0f));
}

TEST_CASE("a degenerate surface claims nothing", "[watergrid]") {
    // A zero-length step would divide by zero and put every point in the world
    // inside this surface, so a malformed chunk would roof the whole map.
    const glm::vec3 none{0.0f, 0.0f, 0.0f};
    CHECK_FALSE(surfaceGridPosition(kOrigin, none, kStepY, 8, 8,
                                    kOrigin.x, kOrigin.y).has_value());
    CHECK_FALSE(surfaceGridPosition(kOrigin, kStepX, none, 8, 8,
                                    kOrigin.x, kOrigin.y).has_value());
}

TEST_CASE("the z components of the steps do not enter into it", "[watergrid]") {
    // The grid is a flat projection: a sloped step vector still measures the
    // same distance across the map. Including z here would shrink the grid of
    // every surface that is not level.
    const glm::vec3 slopedX{10.0f, 0.0f, 5.0f};
    const auto grid = surfaceGridPosition(kOrigin, slopedX, kStepY, 8, 8,
                                          kOrigin.x + 10.0f, kOrigin.y);
    REQUIRE(grid.has_value());
    CHECK(grid->x == Catch::Approx(1.0f));
}

// ---- the height at a grid position ----

namespace {

/// A (width+1) x (height+1) corner grid filled by a rule.
std::vector<float> corners(int width, int height, float (*rule)(int, int)) {
    std::vector<float> out;
    out.reserve(static_cast<size_t>((width + 1) * (height + 1)));
    for (int y = 0; y <= height; ++y) {
        for (int x = 0; x <= width; ++x) out.push_back(rule(x, y));
    }
    return out;
}

}  // namespace

TEST_CASE("a level surface is the same height everywhere", "[watergrid]") {
    const auto heights = corners(8, 8, [](int, int) { return 42.0f; });
    for (float g = 0.0f; g <= 8.0f; g += 2.0f) {
        INFO("at " << g);
        const auto h = sampleGridHeight(heights, 8, 8, g, g);
        REQUIRE(h.has_value());
        CHECK(*h == Catch::Approx(42.0f));
    }
}

TEST_CASE("the height is interpolated across the cell", "[watergrid]") {
    // Height equals the x corner index, so halfway along a cell is halfway
    // between its corners. A sampler that snapped to a corner instead would
    // step the water surface in visible terraces.
    const auto heights = corners(8, 8, [](int x, int) { return static_cast<float>(x); });
    CHECK(*sampleGridHeight(heights, 8, 8, 0.5f, 0.0f) == Catch::Approx(0.5f));
    CHECK(*sampleGridHeight(heights, 8, 8, 3.25f, 4.0f) == Catch::Approx(3.25f));
}

TEST_CASE("both axes are interpolated", "[watergrid]") {
    const auto heights = corners(8, 8, [](int x, int y) {
        return static_cast<float>(x) + 10.0f * static_cast<float>(y);
    });
    CHECK(*sampleGridHeight(heights, 8, 8, 0.5f, 0.5f) == Catch::Approx(5.5f));
}

TEST_CASE("the far edge samples the last cell rather than falling off",
          "[watergrid]") {
    // A grid of eight cells has nine corners, so grid position 8 is the last
    // corner, not the start of a ninth cell. Reading it as a cell start walks
    // off the array and drops the answer, which leaves a seam between two
    // surfaces that the player falls through.
    const auto heights = corners(8, 8, [](int x, int) { return static_cast<float>(x); });
    const auto h = sampleGridHeight(heights, 8, 8, 8.0f, 8.0f);
    REQUIRE(h.has_value());
    CHECK(*h == Catch::Approx(8.0f));
}

TEST_CASE("a position behind the grid has no height", "[watergrid]") {
    const auto heights = corners(8, 8, [](int, int) { return 1.0f; });
    CHECK_FALSE(sampleGridHeight(heights, 8, 8, -0.5f, 0.0f).has_value());
    CHECK_FALSE(sampleGridHeight(heights, 8, 8, 0.0f, -0.5f).has_value());
}

TEST_CASE("a short height array answers nothing rather than reading past it",
          "[watergrid]") {
    // A surface whose heights failed to load. Answering a height read from
    // whatever follows the array is worse than answering none.
    const std::vector<float> tooShort(4, 5.0f);
    CHECK_FALSE(sampleGridHeight(tooShort, 8, 8, 4.0f, 4.0f).has_value());
    CHECK_FALSE(sampleGridHeight({}, 8, 8, 0.0f, 0.0f).has_value());
}

TEST_CASE("a non-square surface indexes by its own width", "[watergrid]") {
    // Row stride is width + 1, not height + 1. Swapping them reads the height
    // of a different part of the lake, which is a plausible number.
    const auto heights = corners(4, 8, [](int, int y) { return static_cast<float>(y); });
    CHECK(*sampleGridHeight(heights, 4, 8, 0.0f, 3.0f) == Catch::Approx(3.0f));
    CHECK(*sampleGridHeight(heights, 4, 8, 4.0f, 6.0f) == Catch::Approx(6.0f));
}
