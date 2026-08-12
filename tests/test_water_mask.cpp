// Which of a water chunk's sub-tiles get drawn.
//
// This rule has been got wrong repeatedly and every failure is scenery rather
// than a crash: water outside a pond's banks, a gap in open sea, an ocean drawn
// over a dry chunk. Nothing raises and no other test notices, so the cases are
// written out here.

#include <catch_amalgamated.hpp>

#include "rendering/water_mask.hpp"

#include <vector>

using wowee::rendering::chunkWaterMask;

namespace {

// The canonical chunk-wide form: bit = row * 8 + col, LSB first.
std::vector<uint8_t> maskWithTiles(std::initializer_list<int> tiles) {
    std::vector<uint8_t> m(8, 0);
    for (int t : tiles) m[t / 8] |= static_cast<uint8_t>(1 << (t % 8));
    return m;
}

bool has(uint64_t mask, int row, int col) {
    return (mask & (uint64_t{1} << (row * 8 + col))) != 0;
}

// An ocean chunk fills completely, whatever its own sub-rect says. Honouring a
// sparse ocean bitmap leaves holes in open sea, which is what the tile-wide
// seed this replaced was really compensating for.
TEST_CASE("an ocean chunk fills completely") {
    const uint64_t m = chunkWaterMask(maskWithTiles({0}), /*x=*/2, /*y=*/3,
                                      /*width=*/2, /*height=*/2, /*isOcean=*/true);
    REQUIRE(m == ~uint64_t{0});
}

// ...and only its own chunk. The caller places these 64 bits into one chunk's
// 8x8 block of the merged tile; nothing here can reach a neighbour. This is the
// Caverns of Time case: a dry chunk contributes no layer, so it never reaches
// this function and receives no bits at all.
TEST_CASE("an ocean chunk fills only itself") {
    const uint64_t m = chunkWaterMask({}, 0, 0, 8, 8, /*isOcean=*/true);
    int set = 0;
    for (int i = 0; i < 64; ++i) set += (m >> i) & 1;
    REQUIRE(set == 64);
}

// A pond draws where its bitmap says and nowhere else - no mirroring of the bit
// order, no dilation into the neighbouring tile. Both were once compensation
// for a transposition and both painted water past the bank.
TEST_CASE("a sparse bitmap is honoured exactly") {
    // A 2x2 sub-rect at x=2, y=3 with only its first and last tiles present.
    const int first = 3 * 8 + 2;   // row 3, col 2
    const int last  = 4 * 8 + 3;   // row 4, col 3
    const uint64_t m = chunkWaterMask(maskWithTiles({first, last}), 2, 3, 2, 2,
                                      /*isOcean=*/false);
    REQUIRE(has(m, 3, 2));
    REQUIRE(has(m, 4, 3));
    REQUIRE(!has(m, 3, 3));
    REQUIRE(!has(m, 4, 2));
    int set = 0;
    for (int i = 0; i < 64; ++i) set += (m >> i) & 1;
    REQUIRE(set == 2);
}

// Nothing outside the sub-rect, however the bits inside fall.
TEST_CASE("nothing outside the sub-rect") {
    std::vector<uint8_t> all(8, 0xFF);
    const uint64_t m = chunkWaterMask(all, /*x=*/1, /*y=*/1, /*width=*/2,
                                      /*height=*/2, /*isOcean=*/false);
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
            REQUIRE(has(m, row, col) ==
                    ((row >= 1 && row <= 2) && (col >= 1 && col <= 2)));
}

// A layer with no exists bitmap is solid across its sub-rect - that is what an
// absent bitmap means in both MH2O and MCLQ.
TEST_CASE("an absent bitmap means solid") {
    const uint64_t m = chunkWaterMask({}, 0, 0, 8, 8, /*isOcean=*/false);
    REQUIRE(m == ~uint64_t{0});
}

// Row is the MH2O y axis and column the x. Getting these the wrong way round
// transposes every pond on the map, which is the failure the axis notes exist
// for; a non-square sub-rect is what tells them apart.
TEST_CASE("row is the y axis and column the x") {
    // x=0, y=0, one column wide and three rows tall.
    const uint64_t m = chunkWaterMask({}, /*x=*/0, /*y=*/0, /*width=*/1,
                                      /*height=*/3, /*isOcean=*/false);
    REQUIRE(has(m, 0, 0));
    REQUIRE(has(m, 1, 0));
    REQUIRE(has(m, 2, 0));
    REQUIRE(!has(m, 0, 1));   // would be set if width and height were swapped
    REQUIRE(!has(m, 3, 0));
}

}  // namespace

