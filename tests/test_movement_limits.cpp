#include <catch_amalgamated.hpp>
#include "rendering/movement_limits.hpp"

TEST_CASE("stock hill climbing limits are shared by all surfaces") {
    using namespace wowee::rendering::movement;
    REQUIRE(kMaxWalkableSlopeDegrees == 50.0f);
    REQUIRE(isWalkableNormal(kMinWalkableNormalZ));
    REQUIRE_FALSE(isWalkableNormal(kMinWalkableNormalZ - 0.001f));
    REQUIRE(isReachableStep(kMaxStepUp));
    REQUIRE_FALSE(isReachableStep(kMaxStepUp + 0.001f));
}

// A walkable slope can rise faster than the step-up budget allows for, which is
// why grounding cannot rely on the budget alone. At the steepest walkable angle
// a mounted player crosses more ground per frame than kMaxStepUp covers as soon
// as the frame runs long — and the floor selection rejects any surface above
// feet + budget as unreachable, so the terrain under a climbing player stops
// counting as ground and they sink into the hill.
TEST_CASE("a walkable slope out-climbs the step-up budget in a long frame") {
    using namespace wowee::rendering::movement;

    // tan(50 degrees), the rise per unit travelled along the steepest slope a
    // player may walk up.
    constexpr float kSteepestRisePerYard = 1.19175f;

    auto riseOverFrame = [](float speedYardsPerSec, float frameSeconds) {
        return speedYardsPerSec * frameSeconds * kSteepestRisePerYard;
    };

    // A smooth frame stays well inside the budget at every travel speed.
    CHECK(riseOverFrame(7.0f, 1.0f / 60.0f) < kMaxStepUp);   // running
    CHECK(riseOverFrame(14.0f, 1.0f / 60.0f) < kMaxStepUp);  // epic mount

    // A slow frame does not. This is the case that put the player inside the
    // hill, so grounding has to recover from penetration rather than assume it
    // cannot happen.
    CHECK(riseOverFrame(14.0f, 1.0f / 20.0f) > kMaxStepUp);
}
