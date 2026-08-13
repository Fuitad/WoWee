// The names the footstep sounds are stored under.
//
// Six families, none derivable from another, and a name that stops matching
// the archive produces no sound and no error. Water is the case that already
// happened: it does not follow the solid-surface naming - there is no
// mFootMediumLargeWater in the data - so the water surface loaded an empty
// clip set and walking through shallows was silent.
//
// The expected strings below are written out in full rather than rebuilt from
// the same pieces the functions use, so this checks the names rather than
// restating how they are assembled.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "audio/footstep_paths.hpp"

using namespace wowee::audio;

TEST_CASE("a solid surface offers twelve candidates, A to L", "[footstep]") {
    // Twelve names probed, not twelve files: the loader tests each against the
    // archive and skips what is absent. An extracted 3.3.5a install has ten
    // for stone, nine for dirt, eight for wood and five each for grass and
    // snow, so narrowing this to any one material's count loses the others.
    const auto paths = classicFootstepPaths("Stone");
    REQUIRE(paths.size() == 12);
    CHECK(paths.front() == "Sound\\Character\\Footsteps\\mFootMediumLargeStoneA.wav");
    CHECK(paths.back() == "Sound\\Character\\Footsteps\\mFootMediumLargeStoneL.wav");

    SECTION("and the material is the only thing that varies") {
        const auto grass = classicFootstepPaths("Grass");
        REQUIRE(grass.size() == paths.size());
        CHECK(grass.front() == "Sound\\Character\\Footsteps\\mFootMediumLargeGrassA.wav");
    }
}

TEST_CASE("a numbered surface is zero padded to two digits", "[footstep]") {
    // 01 rather than 1: the archive pads them, and a set built without the
    // padding asks for eight files that are not there.
    // The arguments the metal surface is actually loaded with, and all eight
    // of these are present in an extracted install.
    const auto paths =
        altFootstepPaths("MediumLargeMetalFootsteps", "MediumLargeFootstepMetal");
    REQUIRE(paths.size() == 8);
    CHECK(paths.front() == "Sound\\Character\\Footsteps\\MediumLargeMetalFootsteps\\"
                           "MediumLargeFootstepMetal_01.wav");
    CHECK(paths.back() == "Sound\\Character\\Footsteps\\MediumLargeMetalFootsteps\\"
                          "MediumLargeFootstepMetal_08.wav");
}

TEST_CASE("hooves live under Creature, not Character", "[footstep]") {
    const auto paths = horseFootstepPaths("Dirt");
    REQUIRE(paths.size() == 5);
    CHECK(paths.front() == "Sound\\Creature\\Horse\\mFootstepsHorseDirt01.wav");
    CHECK(paths.back() == "Sound\\Creature\\Horse\\mFootstepsHorseDirt05.wav");
}

TEST_CASE("a large race has its own stem and five clips", "[footstep]") {
    // Not twelve, and not the mFootMediumLarge stem: reusing either would ask
    // for a mix of files that exist and files that do not.
    const auto paths = hugeFootstepPaths("Stone");
    REQUIRE(paths.size() == 5);
    CHECK(paths.front() == "Sound\\Character\\Footsteps\\mFootHugeStoneA.wav");
    CHECK(paths.back() == "Sound\\Character\\Footsteps\\mFootHugeStoneE.wav");
}

TEST_CASE("water follows none of the other conventions", "[footstep]") {
    // The one that was already wrong once. Its own folder, its own stem, and
    // a capital S in FootSteps where the solid surfaces have none.
    const auto paths = waterFootstepPaths();
    REQUIRE(paths.size() == 5);
    CHECK(paths.front() ==
          "Sound\\Character\\Footsteps\\WaterSplash\\FootStepsMediumWaterA.wav");
    CHECK(paths.back() ==
          "Sound\\Character\\Footsteps\\WaterSplash\\FootStepsMediumWaterE.wav");

    SECTION("and the large-race water is not in that folder") {
        // The two water sets differ by more than their stem, which is the
        // kind of thing a tidy-up would flatten.
        const auto huge = hugeWaterFootstepPaths();
        REQUIRE(huge.size() == 5);
        CHECK(huge.front() ==
              "Sound\\Character\\Footsteps\\FootstepsHugeWaterA.wav");
        CHECK(huge.front().find("WaterSplash") == std::string::npos);
    }
}

TEST_CASE("no two families produce the same name", "[footstep]") {
    // The whole reason these are six functions rather than one with arguments.
    std::vector<std::string> all;
    for (const auto& set : {classicFootstepPaths("Stone"), hugeFootstepPaths("Stone"),
                            waterFootstepPaths(), hugeWaterFootstepPaths(),
                            horseFootstepPaths("Stone")}) {
        all.insert(all.end(), set.begin(), set.end());
    }
    const size_t total = all.size();
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    CHECK(all.size() == total);
}
