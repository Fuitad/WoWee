// Particle density against the floor that holds a flame together.
//
// A fire is only visible because a scattering of its particles reaches open
// air, so thinning the emission does not make it smaller - below a threshold it
// disappears. m2_renderer_particles.cpp floors the rate of flame-like emitters
// for exactly that reason, and the density setting has to be applied *before*
// that floor or it takes every candle in the world out with it.
#include <catch_amalgamated.hpp>

#include <algorithm>

namespace {

constexpr float kMinLiveParticles = 15.0f;

/// The emission rate an emitter ends up with, in the order the renderer applies
/// the two: the player's density first, the flame floor second.
float rateAfter(float authoredRate, float lifespan, float density, bool flameLike) {
    float rate = authoredRate * density;
    if (rate > 0.0f && lifespan > 0.0f && flameLike) {
        rate = std::max(rate, kMinLiveParticles / std::max(lifespan, 0.1f));
    }
    return rate;
}

/// How many are alive at once, which is what a flame is judged by.
float livePopulation(float rate, float lifespan) { return rate * lifespan; }

} // namespace

TEST_CASE("Turning density down thins ordinary particles", "[particles][density]") {
    const float full = rateAfter(40.0f, 0.5f, 1.0f, /*flameLike=*/false);
    const float low  = rateAfter(40.0f, 0.5f, 0.1f, /*flameLike=*/false);
    CHECK(low < full);
    CHECK(low == Catch::Approx(4.0f));
}

TEST_CASE("A candle survives the lowest density setting", "[particles][density]") {
    // CHANDELIER01 asks for 1/s over six seconds - a single speck per candle.
    // At a tenth of that it would be nothing at all; the floor is what stops it.
    const float rate = rateAfter(1.0f, 6.0f, 0.1f, /*flameLike=*/true);
    CHECK(livePopulation(rate, 6.0f) >= kMinLiveParticles);
}

TEST_CASE("A bright flame is still allowed to thin", "[particles][density]") {
    // Above the floor the setting has room to work, so a fire that emits far
    // more than the minimum does get quieter - it just cannot go below it.
    const float full = rateAfter(400.0f, 0.5f, 1.0f, /*flameLike=*/true);
    const float low  = rateAfter(400.0f, 0.5f, 0.25f, /*flameLike=*/true);
    CHECK(low < full);
    CHECK(livePopulation(low, 0.5f) >= kMinLiveParticles);
}

TEST_CASE("The floor is what makes the order matter", "[particles][density]") {
    // Flooring first and thinning second - the other way round - puts the
    // candle back below the threshold, which is the bug this order avoids.
    const float wrongWayRound =
        std::max(1.0f, kMinLiveParticles / 6.0f) * 0.1f;
    CHECK(livePopulation(wrongWayRound, 6.0f) < kMinLiveParticles);
}
