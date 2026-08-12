// Which LightParams row and channel a light band belongs to, and where its
// fields are.
//
// LightIntBand and LightFloatBand hold one row per channel per LightParams
// row: eighteen colour channels and six float channels. Two things about that
// were wrong, and either alone leaves a zone with no colours of its own.
//
// The block mapping was `id / channels` and `id % channels`. Both the band ids
// and the LightParams ids start at one, so band 1 - the first colour channel
// of LightParams 1 - came out as LightParams 0, channel 1. LightParams 0 does
// not exist, so the first seventeen channels of every row were dropped, and
// the rest landed one slot over.
//
// The field indices were shifted by one too. LightIntBand.dbc is 34 fields:
// the id, a count, sixteen times, sixteen values. The layout declared the
// count at field 2, which is the first time key, so 12024 of the file's 15300
// bands read as having zero keyframes.
//
// The oracle is the files. The counts settle the mapping - 15300 rows is
// 850 x 18 and the ids run to 16506, which is 917 x 18 - and the values in
// field 2 settle the field shift: 1440, 2160, 360, 720 are times of day in
// half-minutes, not counts of anything.
#include <catch_amalgamated.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "rendering/light_band_block.hpp"

using wowee::rendering::lightBandSlot;
using wowee::rendering::LIGHT_FLOAT_CHANNELS;
using wowee::rendering::LIGHT_INT_CHANNELS;

TEST_CASE("the first band is the first channel of the first row", "[light]") {
    const auto slot = lightBandSlot(1, LIGHT_INT_CHANNELS);
    REQUIRE(slot.valid);
    CHECK(slot.lightParamsId == 1);
    CHECK(slot.channel == 0);
}

TEST_CASE("a row's eighteen channels are consecutive ids", "[light]") {
    // Ids 1..18 are LightParams 1's channels 0..17, and 19 starts the next
    // row. Dividing without subtracting first splits that block in the middle.
    for (uint32_t channel = 0; channel < LIGHT_INT_CHANNELS; ++channel) {
        const auto slot = lightBandSlot(1 + channel, LIGHT_INT_CHANNELS);
        INFO("band id " << (1 + channel));
        REQUIRE(slot.valid);
        CHECK(slot.lightParamsId == 1);
        CHECK(slot.channel == channel);
    }
    const auto next = lightBandSlot(19, LIGHT_INT_CHANNELS);
    CHECK(next.lightParamsId == 2);
    CHECK(next.channel == 0);
}

TEST_CASE("the last id in the file is the last channel of the last row",
          "[light]") {
    // LightIntBand ids run to 16506 and LightParams ids to 917. 16506 is
    // 917 x 18, so the highest band has to be that row's last channel - which
    // is the check that the mapping is not off by one at the far end either.
    const auto slot = lightBandSlot(16506, LIGHT_INT_CHANNELS);
    REQUIRE(slot.valid);
    CHECK(slot.lightParamsId == 917);
    CHECK(slot.channel == 17);

    // The float file is the same shape with six channels: 5502 is 917 x 6.
    const auto f = lightBandSlot(5502, LIGHT_FLOAT_CHANNELS);
    REQUIRE(f.valid);
    CHECK(f.lightParamsId == 917);
    CHECK(f.channel == 5);
}

TEST_CASE("the float file's six channels map the same way", "[light]") {
    for (uint32_t channel = 0; channel < LIGHT_FLOAT_CHANNELS; ++channel) {
        const auto slot = lightBandSlot(1 + channel, LIGHT_FLOAT_CHANNELS);
        INFO("band id " << (1 + channel));
        REQUIRE(slot.valid);
        CHECK(slot.lightParamsId == 1);
        CHECK(slot.channel == channel);
    }
    CHECK(lightBandSlot(7, LIGHT_FLOAT_CHANNELS).lightParamsId == 2);
    CHECK(lightBandSlot(7, LIGHT_FLOAT_CHANNELS).channel == 0);
}

TEST_CASE("zero is not a band", "[light]") {
    // The ids are one-based, so a zero is a missing value rather than the
    // first row. Treating it as a row is how LightParams 0 got written to.
    CHECK_FALSE(lightBandSlot(0, LIGHT_INT_CHANNELS).valid);
    CHECK_FALSE(lightBandSlot(0, LIGHT_FLOAT_CHANNELS).valid);
    CHECK_FALSE(lightBandSlot(5, 0).valid);
}

namespace {

#ifdef WOWEE_SOURCE_DIR
const std::string kRoot = std::string(WOWEE_SOURCE_DIR) + "/";
#else
const std::string kRoot;
#endif

std::string slurp(const std::string& relative) {
    std::ifstream in(kRoot + relative);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("every expansion declares the band fields where they are",
          "[light]") {
    // Both files are the id, a count, sixteen times and sixteen values: 34
    // fields. Declaring the count at 2 reads the first time key, and 1440 -
    // noon - is not a keyframe count.
    for (const char* expansion : {"classic", "tbc", "wotlk", "turtle"}) {
        const std::string json =
            slurp(std::string("Data/expansions/") + expansion +
                  "/dbc_layouts.json");
        INFO(expansion);
        REQUIRE(json.size() > 100);
        for (const char* band : {"LightIntBand", "LightFloatBand"}) {
            const size_t at = json.find(band);
            INFO(band);
            REQUIRE(at != std::string::npos);
            const std::string body = json.substr(at, 220);
            CHECK(body.find("\"BlockIndex\": 0") != std::string::npos);
            CHECK(body.find("\"NumKeyframes\": 1") != std::string::npos);
            CHECK(body.find("\"TimeKey0\": 2") != std::string::npos);
            CHECK(body.find("\"Value0\": 18") != std::string::npos);
        }
    }
}
