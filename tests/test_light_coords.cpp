// Where Light.dbc says a light volume is.
//
// The file stores positions and falloff radii in thirty-sixths of a yard, on
// the tile grid's axes: a corner origin, and the two horizontal axes mirrored
// and swapped against world space. None of that was applied - the scale was
// 1.0 with a comment saying to try 36 if distances seemed off.
//
// They were off. A player standing in Tirisfal is 2372 units from the nearest
// of map 0's 82 volumes, and that one's outer radius is 0. No volume matched
// anywhere on any map, so every zone fell back to default lighting and looked
// like an ordinary bright day, and no zone ever resolved a skybox, because the
// active skybox path is only filled by walking the volumes that matched.
//
// The oracle is the world itself. These are the real Light.dbc rows for map 0
// and the published world coordinates of the zones they light. A wrong scale
// or a wrong axis pairing puts every one of them somewhere else.
#include <catch_amalgamated.hpp>

#include "rendering/light_coords.hpp"

using wowee::rendering::lightPositionToWorld;
using wowee::rendering::LIGHT_COORD_UNITS_PER_YARD;

TEST_CASE("a radius divided by the unit lands on the tile grid", "[light]") {
    // Light 2's inner radius is 19200. That is one ADT tile, which is what
    // makes 36 an answer rather than a guess: a light volume sized in tiles is
    // a thing an artist would author, one sized in 19200 yards is not - the
    // whole map is 34133 across.
    CHECK(19200.0f / LIGHT_COORD_UNITS_PER_YARD ==
          Catch::Approx(wowee::core::coords::TILE_SIZE).epsilon(1e-4));
    CHECK(23040.0f / LIGHT_COORD_UNITS_PER_YARD ==
          Catch::Approx(640.0f).epsilon(1e-4));
}

TEST_CASE("the volumes lighting known zones contain those zones", "[light]") {
    // Row from Light.dbc (map 0), then the world position of the place it
    // lights. Each holds only under the mirrored, swapped, corner-origin
    // mapping; the other eleven orderings of the two axes miss at least one.
    struct Case {
        const char* zone;
        float dbcX, dbcY, dbcZ;   // Light.dbc fields, unconverted
        float outerRadius;        // also unconverted
        float worldX, worldY;     // where that zone is
    };
    const Case cases[] = {
        // light 28                                     outer      Tirisfal
        {"Tirisfal Glades", 564790.0f, 604416.0f, 0.0f, 65437.0f, 1676.0f, 1678.0f},
        // light 27                                                Undercity
        {"Undercity",       612864.0f, 516864.0f, 0.0f, 51722.0f, 1633.0f,  240.0f},
        // light 77                                                Stormwind
        {"Stormwind",       593586.0f, 931236.0f, 0.0f, 17814.0f, -8913.0f, 554.0f},
        // light 16                                                Ironforge
        {"Ironforge",       666312.0f, 800208.0f, 15688.0f, 43713.0f, -4981.0f, -881.0f},
        // light 8                                                 Westfall
        {"Westfall",        561600.0f, 1002240.0f, 0.0f, 31680.0f, -10600.0f, 1200.0f},
        // light 9                                                 Booty Bay
        {"Booty Bay",       614400.0f, 1094400.0f, 0.0f, 72192.0f, -14297.0f, 460.0f},
    };

    for (const Case& c : cases) {
        INFO(c.zone);
        const glm::vec3 pos = lightPositionToWorld(c.dbcX, c.dbcY, c.dbcZ);
        const float radius = c.outerRadius / LIGHT_COORD_UNITS_PER_YARD;
        const float dx = c.worldX - pos.x;
        const float dy = c.worldY - pos.y;
        INFO("volume at " << pos.x << "," << pos.y << " radius " << radius);
        CHECK(std::sqrt(dx * dx + dy * dy) < radius);
    }
}

TEST_CASE("the axes are mirrored and swapped, not copied", "[light]") {
    // The pairing core/coordinates.hpp warns not to "fix": the DBC's X feeds
    // the world's Y and its Y feeds the world's X, both negated about the grid
    // centre. Copying them across straight is the mistake that looks right
    // because both are called X.
    const glm::vec3 pos = lightPositionToWorld(564790.0f, 604416.0f, 3600.0f);

    CHECK(pos.x == Catch::Approx(wowee::core::coords::ZEROPOINT - 604416.0f / 36.0f)
                       .epsilon(1e-5));
    CHECK(pos.y == Catch::Approx(wowee::core::coords::ZEROPOINT - 564790.0f / 36.0f)
                       .epsilon(1e-5));
    // Height is the one axis that is neither mirrored nor swapped.
    CHECK(pos.z == Catch::Approx(100.0f).epsilon(1e-5));
}

TEST_CASE("the grid corner is the origin the file counts from", "[light]") {
    // A position of zero is the corner of the 64x64 grid, not its centre.
    const glm::vec3 corner = lightPositionToWorld(0.0f, 0.0f, 0.0f);
    CHECK(corner.x == Catch::Approx(wowee::core::coords::ZEROPOINT));
    CHECK(corner.y == Catch::Approx(wowee::core::coords::ZEROPOINT));

    // And the far corner is the other end of the map.
    const float fullGrid = 64.0f * wowee::core::coords::TILE_SIZE * 36.0f;
    const glm::vec3 far = lightPositionToWorld(fullGrid, fullGrid, 0.0f);
    CHECK(far.x == Catch::Approx(-wowee::core::coords::ZEROPOINT).margin(0.1f));
    CHECK(far.y == Catch::Approx(-wowee::core::coords::ZEROPOINT).margin(0.1f));
}

TEST_CASE("a volume no longer sits impossibly far from the world", "[light]") {
    // What the unconverted value looked like: the raw field as a coordinate
    // puts the volume 600000 units out, past every edge of a map that is
    // 34133 across, which is why nothing ever matched.
    const glm::vec3 pos = lightPositionToWorld(564790.0f, 604416.0f, 0.0f);
    CHECK(std::abs(pos.x) < wowee::core::coords::ZEROPOINT);
    CHECK(std::abs(pos.y) < wowee::core::coords::ZEROPOINT);
    CHECK(std::abs(564790.0f) > wowee::core::coords::ZEROPOINT);
}
