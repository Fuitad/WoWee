#include <catch_amalgamated.hpp>

#include "rendering/m2_model_classifier.hpp"

#include <string>

using wowee::rendering::classifyBatchTexture;

namespace {
bool isStarLayer(const std::string& path) {
    return classifyBatchTexture(path).starPointLayer;
}
}

TEST_CASE("the sky models' star layers are recognised", "[m2][sky][stars]") {
    REQUIRE(isStarLayer("environment\\stars\\stars3.blp"));
    REQUIRE(isStarLayer("environment\\stars\\hellfirestars2.blp"));
    REQUIRE(isStarLayer("environment\\stars\\hellfirestars.blp"));
    REQUIRE(isStarLayer("environment\\stars\\nexusraid_starsa.blp"));
    REQUIRE(isStarLayer("environment\\stars\\rubysanctumstars01.blp"));
}

TEST_CASE("the other layers of the same dome are left alone", "[m2][sky][stars]") {
    // Suppressing these would delete the sky itself, not its stars.
    REQUIRE_FALSE(isStarLayer("environment\\stars\\auchindoun_clouds01.blp"));
    REQUIRE_FALSE(isStarLayer("environment\\stars\\auchindounplanet01.blp"));
    REQUIRE_FALSE(isStarLayer("environment\\stars\\auchindoun_galaxy_01.blp"));
    REQUIRE_FALSE(isStarLayer("environment\\stars\\starrynight.blp"));

    // Both layers in one texture: taking it removes the clouds too.
    REQUIRE_FALSE(isStarLayer("environment\\stars\\starsandclouds.blp"));

    // A brightening card rather than the field.
    REQUIRE_FALSE(isStarLayer("environment\\stars\\starbrightenerlarge.blp"));
}

TEST_CASE("a floor doodad named for stars is not a sky", "[m2][sky][stars]") {
    // The name alone matches; the directory is what settles it.
    REQUIRE_FALSE(isStarLayer("world\\expansion02\\doodads\\ulduar\\ul_sky_floor_stars.blp"));
    REQUIRE_FALSE(isStarLayer("world\\expansion02\\doodads\\ulduar\\ul_sky_floor_stars02.blp"));
}
