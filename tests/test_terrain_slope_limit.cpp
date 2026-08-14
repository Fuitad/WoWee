// The slope limit as it applies to the ground itself.
//
// kMinWalkableNormalZ existed and governed WMO and M2 floors. The terrain query
// answers a height and no normal, so it was never applied there at all - the
// constant was only ever used as the fallback limit for those other two - and a
// mountain of any steepness was walkable, held back by nothing but the per-step
// height budget, which a smooth heightfield never trips.
#include <catch_amalgamated.hpp>

#include "rendering/movement_limits.hpp"

#include <cmath>
#include <optional>

using namespace wowee::rendering::movement;

namespace {

/// A plane rising at a given angle along +x.
auto ramp(float degrees) {
    const float slope = std::tan(degrees * 3.14159265f / 180.0f);
    return [slope](float x, float) -> std::optional<float> { return x * slope; };
}

float normalZOf(float degrees) {
    return heightfieldNormalZ(ramp(degrees), 10.0f, 10.0f, 0.35f);
}

} // namespace

TEST_CASE("Flat and gentle ground is walkable", "[movement][slope]") {
    CHECK(isWalkableNormal(normalZOf(0.0f)));
    CHECK(isWalkableNormal(normalZOf(15.0f)));
    CHECK(isWalkableNormal(normalZOf(45.0f)));
}

TEST_CASE("Ground past the limit is not", "[movement][slope]") {
    CHECK_FALSE(isWalkableNormal(normalZOf(55.0f)));
    CHECK_FALSE(isWalkableNormal(normalZOf(70.0f)));
    CHECK_FALSE(isWalkableNormal(normalZOf(89.0f)));
}

TEST_CASE("The limit sits at 50 degrees", "[movement][slope]") {
    // Either side of it, since the constant is cos(50) and the test should fail
    // if that ever silently becomes a different angle.
    CHECK(isWalkableNormal(normalZOf(49.0f)));
    CHECK_FALSE(isWalkableNormal(normalZOf(51.0f)));
}

TEST_CASE("Slope is measured the same in every direction", "[movement][slope]") {
    // Down +y rather than +x, and diagonally: a limit that only held on one
    // axis would let a player walk up the corner of a mountain.
    const float slope = std::tan(60.0f * 3.14159265f / 180.0f);
    auto northRamp = [slope](float, float y) -> std::optional<float> { return y * slope; };
    auto diagRamp  = [slope](float x, float y) -> std::optional<float> {
        return (x + y) * slope * 0.70710678f;
    };
    CHECK_FALSE(isWalkableNormal(heightfieldNormalZ(northRamp, 5.0f, 5.0f, 0.35f)));
    CHECK_FALSE(isWalkableNormal(heightfieldNormalZ(diagRamp, 5.0f, 5.0f, 0.35f)));
}

TEST_CASE("Ground that is not there does not read as a cliff", "[movement][slope]") {
    // At a hole, or off the loaded tiles, a neighbour is missing. Refusing on
    // that would stop the player at every tile edge, so it passes instead.
    auto gap = [](float x, float) -> std::optional<float> {
        if (x > 10.0f) return std::nullopt;
        return 0.0f;
    };
    CHECK(isWalkableNormal(heightfieldNormalZ(gap, 10.0f, 5.0f, 0.35f)));
}
