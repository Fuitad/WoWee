#include <catch_amalgamated.hpp>

#include <cmath>

#include "rendering/m2_view_distance.hpp"

using wowee::rendering::m2InstanceMaxDistSq;

namespace {
constexpr float kGameObjectFloor = 600.0f;
float distanceOf(float distSq) { return std::sqrt(distSq); }
}

TEST_CASE("an ordinary doodad draws to the scene distance", "[m2][viewdist]") {
    const float base = 1000.0f * 1000.0f;
    REQUIRE(distanceOf(m2InstanceMaxDistSq(base, 1.0f, false, kGameObjectFloor, 2400.0f))
            == Catch::Approx(1000.0f));
}

TEST_CASE("a large model's widening factor stops at the view distance", "[m2][viewdist]") {
    // The factor is the reason distant trees stood on nothing: it multiplies a
    // squared distance, so a cathedral-sized doodad asked for several times the
    // scene's range while the terrain under it stopped at the slider.
    const float base = 1000.0f * 1000.0f;
    const float factor = 16.0f;  // 4x the distance, unclamped
    REQUIRE(distanceOf(m2InstanceMaxDistSq(base, factor, false, kGameObjectFloor, 4000.0f))
            == Catch::Approx(4000.0f));
    REQUIRE(distanceOf(m2InstanceMaxDistSq(base, factor, false, kGameObjectFloor, 1200.0f))
            == Catch::Approx(1200.0f));
}

TEST_CASE("the game object floor cannot reach past the ground either", "[m2][viewdist]") {
    // The floor is 600 yards, above the 400-yard minimum view distance: a
    // mailbox is meant to stay findable, not to hang in the air.
    const float base = 100.0f * 100.0f;
    REQUIRE(distanceOf(m2InstanceMaxDistSq(base, 1.0f, true, kGameObjectFloor, 2400.0f))
            == Catch::Approx(kGameObjectFloor));
    REQUIRE(distanceOf(m2InstanceMaxDistSq(base, 1.0f, true, kGameObjectFloor, 400.0f))
            == Catch::Approx(400.0f));
}

TEST_CASE("a non-game-object ignores the floor", "[m2][viewdist]") {
    const float base = 100.0f * 100.0f;
    REQUIRE(distanceOf(m2InstanceMaxDistSq(base, 1.0f, false, kGameObjectFloor, 2400.0f))
            == Catch::Approx(100.0f));
}

TEST_CASE("raising the view distance never shortens an instance's range", "[m2][viewdist]") {
    const float base = 1000.0f * 1000.0f;
    float previous = 0.0f;
    for (float viewDistance = 400.0f; viewDistance <= 2400.0f; viewDistance += 100.0f) {
        const float d = m2InstanceMaxDistSq(base, 3.0f, true, kGameObjectFloor, viewDistance);
        REQUIRE(d >= previous);
        previous = d;
    }
}
