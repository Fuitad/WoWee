#include <catch_amalgamated.hpp>

#include "rendering/m2_model_classifier.hpp"

#include <string>

using wowee::rendering::isHelperCreatureModel;

TEST_CASE("the invisible helper creatures are recognised", "[creature][trigger]") {
    REQUIRE(isHelperCreatureModel("creature\\invisiblestalker\\invisiblestalker.m2"));
    REQUIRE(isHelperCreatureModel("creature\\invisiblestalker\\invisiblestalkerground.m2"));
    REQUIRE(isHelperCreatureModel("creature\\invisiblestalker\\invisiblestalker_noanims.m2"));
    REQUIRE(isHelperCreatureModel("creature\\invisibleman\\invisibleman.m2"));
}

TEST_CASE("the measuring boxes are helpers too", "[creature][trigger]") {
    // Their names are only numbers, so the directory is what identifies them.
    REQUIRE(isHelperCreatureModel("world\\scale\\1000x1000.m2"));
    REQUIRE(isHelperCreatureModel("world\\scale\\200yardradiusdisc.m2"));
    REQUIRE(isHelperCreatureModel("world/scale/100x100.m2"));
}

TEST_CASE("creatures that are meant to be seen are left alone", "[creature][trigger]") {
    REQUIRE_FALSE(isHelperCreatureModel("creature\\rabbit\\rabbit.m2"));
    REQUIRE_FALSE(isHelperCreatureModel("creature\\dragonkite\\dragonkite.m2"));
    REQUIRE_FALSE(isHelperCreatureModel("creature\\murloc\\murloc.m2"));
    // Named for a man and for scale, but neither is the helper.
    REQUIRE_FALSE(isHelperCreatureModel("creature\\humanmale\\humanmale.m2"));
    REQUIRE_FALSE(isHelperCreatureModel("world\\azeroth\\elwynn\\scaleladder.m2"));
}
