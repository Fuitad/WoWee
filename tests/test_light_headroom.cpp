// Keeping a surface's total light inside what a colour can hold.
//
// Every lit shader adds the two the same way: ambient + diffuse * N-dot-L. On
// ground facing the sun that is very nearly ambient + diffuse, and over the
// 844 LightParams rows the client ships, 82% exceed 1.0 in some channel at
// noon. The worst reaches 2.00.
//
// What that costs is colour rather than brightness. Clipping is per channel,
// so the ones that overflow stop at 1.0 while the rest keep their value and
// the hue slides toward whatever saturated. Mulgore's grass clipped red and
// green and left blue behind, reading as searing yellow rather than bright
// green; Teldrassil's canopies went magenta.
//
// The oracle is the real rows: the values below are what the light tables hold
// for those zones at noon.
#include <catch_amalgamated.hpp>

#include "rendering/light_headroom.hpp"

using wowee::rendering::applyLightHeadroom;
using wowee::rendering::lightHeadroomScale;

TEST_CASE("light already inside the range is untouched", "[light]") {
    // The 18% of rows that never needed this must not be dimmed by it.
    glm::vec3 ambient(0.30f, 0.35f, 0.40f);
    glm::vec3 diffuse(0.40f, 0.42f, 0.45f);
    const glm::vec3 a0 = ambient, d0 = diffuse;

    CHECK(lightHeadroomScale(ambient, diffuse) == 1.0f);
    applyLightHeadroom(ambient, diffuse);
    CHECK(ambient == a0);
    CHECK(diffuse == d0);
}

TEST_CASE("a sum over one is brought to exactly one", "[light]") {
    // Teldrassil at noon: LightParams 208, ambient and diffuse as stored.
    glm::vec3 ambient(0.663f, 0.878f, 1.0f);
    glm::vec3 diffuse(0.510f, 0.282f, 0.490f);
    REQUIRE((ambient + diffuse).b > 1.0f);

    applyLightHeadroom(ambient, diffuse);
    const glm::vec3 sum = ambient + diffuse;
    CHECK(std::max({sum.r, sum.g, sum.b}) == Catch::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("the hue survives, which is the whole point", "[light]") {
    // Clipping each channel independently is what turns green grass yellow.
    // Scaling keeps every ratio between the channels.
    glm::vec3 ambient(0.663f, 0.878f, 1.0f);
    glm::vec3 diffuse(0.510f, 0.282f, 0.490f);
    const glm::vec3 before = ambient + diffuse;

    applyLightHeadroom(ambient, diffuse);
    const glm::vec3 after = ambient + diffuse;

    CHECK(after.r / after.g == Catch::Approx(before.r / before.g).epsilon(1e-4));
    CHECK(after.b / after.g == Catch::Approx(before.b / before.g).epsilon(1e-4));

    // What clipping would have done to the same colour: the two channels that
    // overflow land on each other and the ratio between them is gone.
    const glm::vec3 clipped = glm::min(before, glm::vec3(1.0f));
    CHECK(clipped.r == 1.0f);
    CHECK(clipped.g == 1.0f);
    CHECK(clipped.r / clipped.g != Catch::Approx(before.r / before.g).epsilon(1e-4));
}

TEST_CASE("the two terms keep their share of the light", "[light]") {
    // Scaling both by one factor means the sun still contributes the same
    // fraction it did. Scaling only one would flatten or harden the shading.
    glm::vec3 ambient(0.663f, 0.878f, 1.0f);
    glm::vec3 diffuse(0.510f, 0.282f, 0.490f);
    const float shareBefore = diffuse.g / (ambient.g + diffuse.g);

    applyLightHeadroom(ambient, diffuse);
    const float shareAfter = diffuse.g / (ambient.g + diffuse.g);
    CHECK(shareAfter == Catch::Approx(shareBefore).epsilon(1e-5));
}

TEST_CASE("the worst row in the file lands in range", "[light]") {
    // 2.00 is the largest channel sum across all 844 rows.
    glm::vec3 ambient(1.0f, 0.8f, 0.5f);
    glm::vec3 diffuse(1.0f, 0.6f, 0.3f);
    applyLightHeadroom(ambient, diffuse);
    const glm::vec3 sum = ambient + diffuse;
    CHECK(sum.r == Catch::Approx(1.0f).epsilon(1e-5));
    CHECK(sum.g <= 1.0f);
    CHECK(sum.b <= 1.0f);
}

TEST_CASE("it only ever darkens", "[light]") {
    // A scale above 1 would brighten a zone that was already correct, which is
    // not this function's business.
    for (float v : {0.05f, 0.2f, 0.5f, 0.9f}) {
        glm::vec3 ambient(v), diffuse(v);
        const glm::vec3 a0 = ambient;
        applyLightHeadroom(ambient, diffuse);
        INFO("v = " << v);
        CHECK(ambient.r <= a0.r + 1e-6f);
    }
}
