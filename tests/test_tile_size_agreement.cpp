// The size of an ADT tile, which three headers each declared for themselves.
//
// 533.33333 is not a number anyone chose: a tile is 1600/3 yards, and the
// whole 64x64 grid is 34133.33 across. Everything that turns a world position
// into a tile index divides by it, and everything that turns a tile index into
// a world position multiplies by it, so the two halves have to agree to the
// bit or a tile boundary lands in a different place depending on which
// direction you crossed it.
//
// Two files had it as 533.333, three decimals rather than five. Across the map
// that is 0.021 yards - far too small to see, and far too small to find, which
// is exactly why it should not be spelled out twice.
//
// The oracle is arithmetic rather than the code: a tile is 1600/3, and the
// world is 64 tiles.
#include <catch_amalgamated.hpp>

#include <cmath>

#include "core/coordinates.hpp"
#include "pipeline/terrain_mesh.hpp"

TEST_CASE("a tile is sixteen hundred thirds of a yard", "[coordinates]") {
    // Not 533.33 and not 533.3333333: the client's own value is a five-decimal
    // truncation, and everything that shares it has to share that truncation
    // rather than a more accurate one.
    CHECK(wowee::core::coords::TILE_SIZE ==
          Catch::Approx(1600.0f / 3.0f).epsilon(1e-6));
}

TEST_CASE("a chunk is a sixteenth of a tile and a quad an eighth of that",
          "[coordinates]") {
    // The mesh generator derives its own constants from the tile size rather
    // than spelling any of them out, so pinning the tile pins the chain: 16
    // chunks to a tile, 8 quads to a chunk. A chunk is 33.33 yards and a quad
    // 4.17, which are the numbers the terrain comments quote.
    const float tile = wowee::core::coords::TILE_SIZE;
    CHECK(tile / 16.0f == Catch::Approx(33.3333f).epsilon(1e-4));
    CHECK(tile / 16.0f / 8.0f == Catch::Approx(4.16666f).epsilon(1e-4));
}

TEST_CASE("the world is sixty-four tiles across", "[coordinates]") {
    // 34133.33 in each axis, and the origin sits at the centre, which is what
    // ZEROPOINT names.
    CHECK(wowee::core::coords::ZEROPOINT ==
          Catch::Approx(32.0f * (1600.0f / 3.0f)).epsilon(1e-6));
    CHECK(64.0f * wowee::core::coords::TILE_SIZE ==
          Catch::Approx(2.0f * wowee::core::coords::ZEROPOINT).epsilon(1e-6));
}

TEST_CASE("the truncated spelling is measurably different", "[coordinates]") {
    // 533.333 was in two files. This is what it costs across the map: a fifth
    // of a millimetre per tile, two centimetres end to end. Harmless on its
    // own and the reason a second spelling survives unnoticed.
    const float truncated = 533.333f;
    CHECK(truncated != wowee::core::coords::TILE_SIZE);
    const float driftAcrossMap =
        std::abs(64.0f * (wowee::core::coords::TILE_SIZE - truncated));
    CHECK(driftAcrossMap > 0.001f);
    CHECK(driftAcrossMap < 0.05f);
}
