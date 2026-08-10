// Choosing a geoset when the model does not have the one asked for.
//
// The rule is one line and it was written five times, as a local lambda in each
// place that needed it, and only some of them knew the exception. That produced
// three faults in one evening: a clean-shaven NPC with a beard, a character
// wearing an untextured cloak it did not own, and a player with no feet.
#include <catch_amalgamated.hpp>
#include "core/geoset_rules.hpp"

using namespace wowee::core;

TEST_CASE("a geoset id is a group and a variant", "[geoset]") {
    CHECK(geosetGroup(501) == 5);
    CHECK(geosetVariant(501) == 1);
    CHECK(geosetGroup(2001) == 20);
    CHECK(geosetVariant(2001) == 1);
    CHECK(geosetGroup(1506) == 15);
    CHECK(geosetVariant(1506) == 6);
}

TEST_CASE("none has two spellings and both are none", "[geoset]") {
    // The geoset tables say variant 1: bare feet, no cloak, no beard.
    CHECK(geosetMeansNone(501));
    CHECK(geosetMeansNone(1501));
    CHECK(geosetMeansNone(101));
    // A DBC that stores the variant directly says 0, and it arrives as x00.
    CHECK(geosetMeansNone(200));
    CHECK(geosetMeansNone(300));
    // Anything else is a thing the character has.
    CHECK_FALSE(geosetMeansNone(1502));
    CHECK_FALSE(geosetMeansNone(505));
    CHECK_FALSE(geosetMeansNone(202));
}

TEST_CASE("resolving against what a model actually carries", "[geoset]") {
    SECTION("the exact geoset wins whenever the model has it") {
        std::unordered_set<uint16_t> model{501, 502, 503};
        CHECK(resolveGeoset(502, model) == 502);
        CHECK(resolveGeoset(501, model) == 501);
    }

    SECTION("a missing variant falls back within its group") {
        // Five kinds of boot, and the one asked for is not among them.
        std::unordered_set<uint16_t> model{502, 503, 505};
        CHECK(resolveGeoset(504, model) == 502);
    }

    SECTION("none never falls back — this is the whole point") {
        // The HD models carry no 1501, the "no cloak" panel. Falling back
        // inside group 15 hands a cloak to a character wearing none, and with
        // no cloak texture bound, a white sheet.
        std::unordered_set<uint16_t> hdModel{1502, 1503, 1504, 1505, 1506};
        CHECK(resolveGeoset(1501, hdModel) == 0);

        // CharFacialHairStyles stores 0 for a character with no beard, which
        // arrives as group*100. Every other member of group 2 is facial hair.
        std::unordered_set<uint16_t> bearded{201, 202, 203};
        CHECK(resolveGeoset(200, bearded) == 0);
    }

    SECTION("a group the model has nothing in draws nothing") {
        std::unordered_set<uint16_t> model{0, 1, 2};
        CHECK(resolveGeoset(1802, model) == 0);
    }

    SECTION("an unknown model is not guessed at") {
        // No geoset list means the model has not been read yet. Answering 0
        // there would hide a part of a character for want of information.
        std::unordered_set<uint16_t> unknown;
        CHECK(resolveGeoset(1501, unknown) == 1501);
        CHECK(resolveGeoset(504, unknown) == 504);
    }

    SECTION("the feet, which is where this was found") {
        // Group 20 is split out of the body on the HD models and they do not
        // agree on which member to use: an HD human female carries 2001 and an
        // HD human male 2002. Asking for either resolves to the one present.
        std::unordered_set<uint16_t> female{2001};
        std::unordered_set<uint16_t> male{2002};
        // 2002 is variant 2, so it is a thing rather than the absence of one,
        // and substituting the 2001 the model does carry is exactly the fix:
        // ask for feet, get the feet this model spells.
        CHECK(resolveGeoset(2002, female) == 2001);
        CHECK(resolveGeoset(2001, female) == 2001);
        CHECK(resolveGeoset(2002, male) == 2002);
        // 2001 is variant 1 and so reads as none, which is why the caller names
        // both rather than relying on one to find the other.
        CHECK(resolveGeoset(2001, male) == 0);
        // A stock model has no group 20 at all — the feet are part of the body.
        std::unordered_set<uint16_t> stock{0, 1, 501, 1501};
        CHECK(resolveGeoset(2001, stock) == 0);
        CHECK(resolveGeoset(2002, stock) == 0);
    }
}
