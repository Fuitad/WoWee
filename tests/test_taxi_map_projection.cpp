#include <catch_amalgamated.hpp>

#include "core/coordinates.hpp"
#include "rendering/world_map/coordinate_projection.hpp"

using namespace wowee;
using rendering::world_map::ZoneBounds;
using rendering::world_map::renderPosToMapUV;

// The flight map places a node by the same chain the world map places the
// player: server position → canonical → render → UV against the continent
// rectangle. Every step of that is somewhere else's code; what is pinned here
// is the *order*, because getting it wrong does not fail, it draws every marker
// somewhere plausible and wrong.
//
// It has been wrong before. Feeding TaxiNodes.dbc positions straight into
// canonicalToRender — skipping the server-to-canonical swap because the numbers
// look like coordinates already — transposed every node on the flight map.

namespace {

/// What taxiNodeMapPos does, in the order it does it.
glm::vec2 nodeToMapUV(float serverX, float serverY, float serverZ,
                      const ZoneBounds& continent) {
    const glm::vec3 canonical =
        core::coords::serverToCanonical(glm::vec3(serverX, serverY, serverZ));
    const glm::vec3 render = core::coords::canonicalToRender(canonical);
    return renderPosToMapUV(render, continent, /*isContinent=*/true);
}

/// Shaped like a real continent row: the bounds descend left→right and
/// top→bottom, which is how WorldMapArea.dbc stores them.
ZoneBounds continent() {
    ZoneBounds b;
    b.locLeft = 16000.0f;  b.locRight  = -16000.0f;
    b.locTop  = 10000.0f;  b.locBottom = -10000.0f;
    return b;
}

} // namespace

TEST_CASE("The middle of the continent is the middle of the map", "[taxi][map]") {
    const glm::vec2 uv = nodeToMapUV(0.0f, 0.0f, 0.0f, continent());
    REQUIRE(uv.x == Catch::Approx(0.5f));
    REQUIRE(uv.y == Catch::Approx(0.5f));
}

TEST_CASE("Each corner of the rectangle lands in a corner of the map",
          "[taxi][map]") {
    const ZoneBounds c = continent();
    // Server order: x is the west axis, y is the north axis. The rectangle's
    // "left" bounds the axis renderPosToMapUV pairs with u, and "top" the one
    // it pairs with v — a pairing verified against real WorldMapArea values and
    // deliberately not "corrected" here.
    const glm::vec2 topLeft     = nodeToMapUV(c.locTop,    c.locLeft,  0.0f, c);
    const glm::vec2 bottomRight = nodeToMapUV(c.locBottom, c.locRight, 0.0f, c);

    REQUIRE(topLeft.x == Catch::Approx(0.0f));
    REQUIRE(topLeft.y == Catch::Approx(0.0f));
    REQUIRE(bottomRight.x == Catch::Approx(1.0f));
    REQUIRE(bottomRight.y == Catch::Approx(1.0f));
}

TEST_CASE("The two axes are not interchangeable", "[taxi][map]") {
    // The regression this file exists for. Swapping the pair moves a marker,
    // and if it did not, the order would not matter and nothing would have
    // broken the first time.
    const ZoneBounds c = continent();
    const glm::vec2 a = nodeToMapUV(4000.0f, 8000.0f, 0.0f, c);
    const glm::vec2 b = nodeToMapUV(8000.0f, 4000.0f, 0.0f, c);
    REQUIRE(a.x != Catch::Approx(b.x));
    REQUIRE(a.y != Catch::Approx(b.y));
}

TEST_CASE("Skipping the server-to-canonical swap moves the marker",
          "[taxi][map]") {
    // Stated as a test rather than a comment: this is the mistake that was
    // actually made, and it is silent — both paths return a perfectly good UV.
    const ZoneBounds c = continent();
    const float sx = 4000.0f, sy = -6000.0f;

    const glm::vec2 correct = nodeToMapUV(sx, sy, 0.0f, c);
    const glm::vec2 skipped = renderPosToMapUV(
        core::coords::canonicalToRender(glm::vec3(sx, sy, 0.0f)), c, true);

    REQUIRE(correct.x != Catch::Approx(skipped.x));
}

TEST_CASE("A node outside the continent still projects, off the map",
          "[taxi][map]") {
    // Not clamped: a node belonging to another continent should land outside
    // [0,1] so it reads as wrong, rather than being pinned to an edge where it
    // would look like a real place.
    const glm::vec2 uv = nodeToMapUV(0.0f, 32000.0f, 0.0f, continent());
    REQUIRE(uv.x < 0.0f);
}
