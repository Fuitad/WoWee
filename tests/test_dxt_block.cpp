// The colour half of a DXT block, which DXT1, DXT3 and DXT5 all share.
//
// blp_loader.cpp decoded the same eight bytes three times and none of the
// three had a test: the BLP tests covered header validation and stopped there.
// A decode that is wrong does not fail, it draws: every texture in the game
// comes through here, and the failure is a tint, a banded gradient, or foliage
// with its cut-outs filled in.
//
// The expected values below are worked out from the S3TC rules rather than
// recorded from the implementation. RGB565 expands by scaling each channel
// over its own maximum (31, 63, 31), and the two interpolated entries are the
// thirds points of the endpoints, truncated.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <vector>

#include "pipeline/dxt_block.hpp"

using wowee::pipeline::decodeDxtColorBlock;
using wowee::pipeline::DxtColorBlock;

namespace {

/// The eight colour bytes: two little-endian RGB565 endpoints and an index
/// word.
std::vector<uint8_t> colorBlock(uint16_t c0, uint16_t c1, uint32_t indices) {
    return {static_cast<uint8_t>(c0 & 0xFF), static_cast<uint8_t>(c0 >> 8),
            static_cast<uint8_t>(c1 & 0xFF), static_cast<uint8_t>(c1 >> 8),
            static_cast<uint8_t>(indices & 0xFF),
            static_cast<uint8_t>((indices >> 8) & 0xFF),
            static_cast<uint8_t>((indices >> 16) & 0xFF),
            static_cast<uint8_t>((indices >> 24) & 0xFF)};
}

constexpr uint16_t kRed = 0xF800;    // 31, 0, 0
constexpr uint16_t kGreen = 0x07E0;  // 0, 63, 0
constexpr uint16_t kBlue = 0x001F;   // 0, 0, 31

}  // namespace

TEST_CASE("RGB565 expands each channel over its own maximum", "[dxt]") {
    // The trap is the green channel: six bits over 63, not five over 31.
    // Reading it as five loses the low bit of every green in the game.
    const auto redBlock = colorBlock(kRed, kGreen, 0);
    const DxtColorBlock decoded = decodeDxtColorBlock(redBlock.data(), false);

    CHECK(decoded.rgb[0][0] == 255);
    CHECK(decoded.rgb[0][1] == 0);
    CHECK(decoded.rgb[0][2] == 0);
    CHECK(decoded.rgb[1][0] == 0);
    CHECK(decoded.rgb[1][1] == 255);
    CHECK(decoded.rgb[1][2] == 0);
}

TEST_CASE("a mid grey keeps all three channels near each other", "[dxt]") {
    // 12, 25, 12 is as close to a neutral grey as RGB565 gets. A channel
    // scaled by the wrong maximum shows up here as a colour cast on what
    // should be grey stone.
    const uint16_t grey = static_cast<uint16_t>((12 << 11) | (25 << 5) | 12);
    const auto block = colorBlock(grey, grey, 0);
    const DxtColorBlock decoded = decodeDxtColorBlock(block.data(), false);

    CHECK(decoded.rgb[0][0] == 98);
    CHECK(decoded.rgb[0][1] == 101);
    CHECK(decoded.rgb[0][2] == 98);
}

TEST_CASE("four-colour mode interpolates at the thirds", "[dxt]") {
    // c0 red, c1 blue, so index 2 is two thirds red and index 3 two thirds
    // blue. Getting these the wrong way round reverses every gradient.
    const auto block = colorBlock(kRed, kBlue, 0);
    const DxtColorBlock decoded = decodeDxtColorBlock(block.data(), true);

    REQUIRE_FALSE(decoded.index3IsTransparent);
    CHECK(decoded.rgb[2][0] == 170);
    CHECK(decoded.rgb[2][2] == 85);
    CHECK(decoded.rgb[3][0] == 85);
    CHECK(decoded.rgb[3][2] == 170);
}

TEST_CASE("DXT1 reads the endpoint order as a mode flag", "[dxt]") {
    // c0 below c1 is three colours plus a transparent index: the halfway
    // point, then nothing. This is how a cut-out is encoded, so treating it as
    // four colours fills the holes in every grate and fence in the game.
    const auto block = colorBlock(kBlue, kRed, 0);
    const DxtColorBlock decoded = decodeDxtColorBlock(block.data(), true);

    REQUIRE(decoded.index3IsTransparent);
    CHECK(decoded.rgb[2][0] == 127);
    CHECK(decoded.rgb[2][2] == 127);
}

TEST_CASE("DXT3 and DXT5 never punch through", "[dxt]") {
    // The same bytes, read for a format that carries alpha separately: four
    // colours whatever the endpoint order says. Honouring the flag here would
    // make a quarter of the pixels of some blocks transparent in a texture
    // whose alpha is fully specified elsewhere.
    const auto block = colorBlock(kBlue, kRed, 0);
    const DxtColorBlock decoded = decodeDxtColorBlock(block.data(), false);

    REQUIRE_FALSE(decoded.index3IsTransparent);
    CHECK(decoded.rgb[2][2] == 170);   // two thirds blue
    CHECK(decoded.rgb[3][2] == 85);
}

TEST_CASE("equal endpoints are four-colour whichever way they are read",
          "[dxt]") {
    // c0 == c1 is not greater than, so DXT1 takes the three-colour branch, and
    // every interpolated entry is the same colour anyway. Worth stating
    // because the boundary is `>` and a `>=` would change which branch a flat
    // block takes, and with it whether its index 3 is transparent.
    const auto block = colorBlock(kRed, kRed, 0);
    const DxtColorBlock decoded = decodeDxtColorBlock(block.data(), true);

    CHECK(decoded.index3IsTransparent);
    CHECK(decoded.rgb[2][0] == 255);
}

TEST_CASE("indices are two bits per pixel in row-major order", "[dxt]") {
    // Pixel (px, py) is at bit (py * 4 + px) * 2. A transposed read mirrors
    // every block about its diagonal, which on a brick wall is invisible and
    // on lettering is not.
    const uint32_t indices = 0b11'10'01'00;   // pixels 0..3 of row 0
    const auto block = colorBlock(kRed, kBlue, indices);
    const DxtColorBlock decoded = decodeDxtColorBlock(block.data(), true);

    CHECK(decoded.indexAt(0, 0) == 0);
    CHECK(decoded.indexAt(1, 0) == 1);
    CHECK(decoded.indexAt(2, 0) == 2);
    CHECK(decoded.indexAt(3, 0) == 3);
    CHECK(decoded.indexAt(0, 1) == 0);
}

TEST_CASE("the index word is read little-endian", "[dxt]") {
    // Byte 7 holds the last row. Reading the word big-endian swaps the top and
    // bottom rows of every block.
    const auto block = colorBlock(kRed, kBlue, 0xC0000000u);
    const DxtColorBlock decoded = decodeDxtColorBlock(block.data(), true);

    CHECK(decoded.indexAt(3, 3) == 3);
    CHECK(decoded.indexAt(0, 0) == 0);
}
