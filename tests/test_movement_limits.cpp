#include <catch_amalgamated.hpp>
#include "rendering/movement_limits.hpp"
#include "core/coordinates.hpp"

#include <cmath>

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

// Facing crosses two representations: the renderer holds the character's yaw in
// degrees, the game side holds canonical yaw in radians, and the frame loop
// converts render → game every frame. Both directions of that conversion were
// hand-written at four call sites, two of them inverting the other two from
// memory. If they ever disagree, facing a target writes one value and the next
// frame reads back another — which is how a cast could be accepted and then
// fail the server's arc check a second and a half later.
TEST_CASE("character yaw and canonical yaw convert back to each other") {
    using namespace wowee::core::coords;

    for (float deg = -720.0f; deg <= 720.0f; deg += 7.5f) {
        const float canonical = characterYawDegToCanonical(deg);
        const float back = canonicalToCharacterYawDeg(canonical);
        // Round-trips to the same heading, allowing for full turns.
        float delta = std::fmod(std::fabs(back - deg), 360.0f);
        if (delta > 180.0f) delta = 360.0f - delta;
        INFO("degrees: " << deg << " canonical: " << canonical << " back: " << back);
        CHECK(delta < 0.01f);
        CHECK(canonical >= -PI - 0.001f);
        CHECK(canonical <= PI + 0.001f);
    }

    // The headings the game actually names: canonical north is 0.
    CHECK(characterYawDegToCanonical(180.0f) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(canonicalToCharacterYawDeg(0.0f) == Catch::Approx(180.0f).margin(1e-5));
}
