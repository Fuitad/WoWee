// Where the first key of an M2 colour track is.
//
// An M2Color is a vec3 colour track followed by an alpha track. The loader
// read the alpha and stepped over the colour, so every tinted batch came out
// the colour of its texture - and glow cards are painted white, which is why
// Orgrimmar's bonfire burned white when its colour track says (1.0, 0.329,
// 0.0).
//
// Reading the colour is version-dependent in a way that cannot be guessed at,
// because getting it wrong yields a plausible colour rather than a failure:
// three floats out of whatever the offset landed on, clamped into range and
// used. WotLK nests an array per sequence behind the key array; vanilla and
// TBC point straight at the values.
//
// The oracle is the two on-disk layouts, laid out here as the files hold them
// and read back through the same function the loader uses.
#include <catch_amalgamated.hpp>

#include <cstring>
#include <vector>

#include "pipeline/m2_color_track.hpp"

using wowee::pipeline::m2ColorAlphaTrackOffset;
using wowee::pipeline::m2ColorRecordSize;
using wowee::pipeline::m2ColorTrackFirstKeyOffset;
using wowee::pipeline::m2ColorTrackKeysOffset;

namespace {

struct Buffer {
    std::vector<uint8_t> bytes;

    explicit Buffer(size_t size) : bytes(size, 0) {}

    void u32(size_t at, uint32_t v) {
        std::memcpy(&bytes[at], &v, sizeof(v));
    }
    void f32(size_t at, float v) {
        std::memcpy(&bytes[at], &v, sizeof(v));
    }
    uint32_t readU32(uint32_t at) const {
        uint32_t v = 0;
        std::memcpy(&v, &bytes[at], sizeof(v));
        return v;
    }
};

}  // namespace

TEST_CASE("the record and track offsets differ by version", "[m2-color]") {
    // WotLK dropped the interpolation-range array, which shortens the track
    // from 28 bytes to 20 and moves everything after it.
    CHECK(m2ColorTrackKeysOffset(true) == 12);
    CHECK(m2ColorTrackKeysOffset(false) == 20);
    CHECK(m2ColorAlphaTrackOffset(true) == 20);
    CHECK(m2ColorAlphaTrackOffset(false) == 28);
    CHECK(m2ColorRecordSize(true) == 40);
    CHECK(m2ColorRecordSize(false) == 56);
}

TEST_CASE("a WotLK colour is two hops away", "[m2-color]") {
    // track.keys -> an M2Array per sequence -> that sequence's values.
    Buffer buf(256);
    const uint32_t track = 0;
    const uint32_t seqArray = 64;   // the per-sequence M2Array lives here
    const uint32_t values = 128;    // and points here

    buf.u32(track + 12, 1);          // nKeys: one sequence
    buf.u32(track + 16, seqArray);   // ofsKeys
    buf.u32(seqArray + 0, 1);        // that sequence has one value
    buf.u32(seqArray + 4, values);
    buf.f32(values + 0, 1.0f);
    buf.f32(values + 4, 0.329f);
    buf.f32(values + 8, 0.0f);

    uint32_t at = 0;
    REQUIRE(m2ColorTrackFirstKeyOffset(buf.bytes.size(), track, true,
                                       [&](uint32_t o) { return buf.readU32(o); }, at));
    CHECK(at == values);

    float r = 0.0f;
    std::memcpy(&r, &buf.bytes[at], sizeof(r));
    CHECK(r == Catch::Approx(1.0f));
}

TEST_CASE("a vanilla colour is one hop away", "[m2-color]") {
    // The key array is the values. Taking the WotLK path here would read a
    // float's bit pattern as a file offset.
    Buffer buf(256);
    const uint32_t track = 0;
    const uint32_t values = 96;

    buf.u32(track + 20, 3);        // nKeys
    buf.u32(track + 24, values);   // ofsKeys, straight at the vec3s
    buf.f32(values + 0, 0.25f);
    buf.f32(values + 4, 0.5f);
    buf.f32(values + 8, 0.75f);

    uint32_t at = 0;
    REQUIRE(m2ColorTrackFirstKeyOffset(buf.bytes.size(), track, false,
                                       [&](uint32_t o) { return buf.readU32(o); }, at));
    CHECK(at == values);

    float g = 0.0f;
    std::memcpy(&g, &buf.bytes[at + 4], sizeof(g));
    CHECK(g == Catch::Approx(0.5f));
}

TEST_CASE("reading a vanilla track as WotLK finds the wrong place",
          "[m2-color]") {
    // The reason this is a named function and not four lines inline. The same
    // buffer read the other way lands somewhere else entirely, and every value
    // it finds is a float that clamps into a colour.
    Buffer buf(256);
    const uint32_t track = 0;
    const uint32_t values = 96;
    buf.u32(track + 20, 3);
    buf.u32(track + 24, values);
    buf.f32(values + 0, 0.25f);

    uint32_t vanillaAt = 0;
    REQUIRE(m2ColorTrackFirstKeyOffset(buf.bytes.size(), track, false,
                                       [&](uint32_t o) { return buf.readU32(o); }, vanillaAt));

    uint32_t wotlkAt = 0;
    const bool asWotlk = m2ColorTrackFirstKeyOffset(
        buf.bytes.size(), track, true,
        [&](uint32_t o) { return buf.readU32(o); }, wotlkAt);
    // Either it fails outright or it points somewhere that is not the colour.
    if (asWotlk) CHECK(wotlkAt != vanillaAt);
}

TEST_CASE("a track with no keys has no colour", "[m2-color]") {
    Buffer buf(128);
    uint32_t at = 0xFFFFFFFF;
    // nKeys is zero, so nothing to read.
    CHECK_FALSE(m2ColorTrackFirstKeyOffset(buf.bytes.size(), 0, true,
                                           [&](uint32_t o) { return buf.readU32(o); }, at));
    CHECK_FALSE(m2ColorTrackFirstKeyOffset(buf.bytes.size(), 0, false,
                                           [&](uint32_t o) { return buf.readU32(o); }, at));
    CHECK(at == 0xFFFFFFFF);  // untouched
}

TEST_CASE("an offset past the end of the file is refused", "[m2-color]") {
    // A truncated or hostile file must not walk off the buffer. Three floats
    // have to fit, not just the offset.
    Buffer buf(128);
    buf.u32(20, 1);
    buf.u32(24, 120);  // 120 + 12 > 128
    uint32_t at = 0;
    CHECK_FALSE(m2ColorTrackFirstKeyOffset(buf.bytes.size(), 0, false,
                                           [&](uint32_t o) { return buf.readU32(o); }, at));

    // And the same for the second hop on WotLK.
    Buffer w(128);
    w.u32(12, 1);
    w.u32(16, 64);
    w.u32(64, 1);
    w.u32(68, 124);  // 124 + 12 > 128
    CHECK_FALSE(m2ColorTrackFirstKeyOffset(w.bytes.size(), 0, true,
                                           [&](uint32_t o) { return w.readU32(o); }, at));
}

TEST_CASE("the track record does not read past a short file", "[m2-color]") {
    Buffer tiny(8);
    uint32_t at = 0;
    CHECK_FALSE(m2ColorTrackFirstKeyOffset(tiny.bytes.size(), 0, true,
                                           [&](uint32_t o) { return tiny.readU32(o); }, at));
    CHECK_FALSE(m2ColorTrackFirstKeyOffset(tiny.bytes.size(), 0, false,
                                           [&](uint32_t o) { return tiny.readU32(o); }, at));
}
