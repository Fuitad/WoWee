// The model matrix a placement becomes.
//
// The doodad and building renderers composed this separately and disagreed
// about the order for a long time - X, Y, Z against Z, Y, X. Nothing on flat
// ground can tell the two apart, which is why it survived four attempts to fix
// it: with no pitch and no roll every order is the same rotation.
//
// The expected matrices below are built from explicit trigonometry rather than
// from glm::rotate, so this checks the composition rather than restating it.
#include <catch_amalgamated.hpp>

#include <cmath>

#include <glm/glm.hpp>

#include "rendering/placement_transform.hpp"

using wowee::rendering::placementModelMatrix;

namespace {

/// Rotation about X, Y and Z built by hand, as the reference to check against.
glm::mat3 rotX(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return glm::mat3(1, 0, 0, 0, c, s, 0, -s, c);
}
glm::mat3 rotY(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return glm::mat3(c, 0, -s, 0, 1, 0, s, 0, c);
}
glm::mat3 rotZ(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return glm::mat3(c, s, 0, -s, c, 0, 0, 0, 1);
}

void requireClose(const glm::mat4& got, const glm::mat4& want) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            INFO("column " << col << " row " << row);
            CHECK(got[col][row] == Catch::Approx(want[col][row]).margin(1e-4));
        }
    }
}

}  // namespace

TEST_CASE("a placement with no rotation is a translate and a scale", "[placement]") {
    const glm::mat4 m = placementModelMatrix({10.0f, -4.0f, 2.5f}, {0, 0, 0}, 3.0f);
    CHECK(m[3][0] == Catch::Approx(10.0f));
    CHECK(m[3][1] == Catch::Approx(-4.0f));
    CHECK(m[3][2] == Catch::Approx(2.5f));
    CHECK(m[0][0] == Catch::Approx(3.0f));
    CHECK(m[1][1] == Catch::Approx(3.0f));
    CHECK(m[2][2] == Catch::Approx(3.0f));
}

TEST_CASE("the rotation composes X then Y then Z", "[placement]") {
    // A triple with all three nonzero and all three different, because that is
    // the only shape that tells the six possible orders apart. Every other
    // case - one angle, or two equal ones - is where this went unnoticed.
    const glm::vec3 euler(0.30f, -0.70f, 1.10f);
    const glm::mat3 expected = rotX(euler.x) * rotY(euler.y) * rotZ(euler.z);

    const glm::mat4 got = placementModelMatrix({0, 0, 0}, euler, 1.0f);
    glm::mat4 want(expected);
    requireClose(got, want);

    SECTION("and it is not the other way round") {
        // Z, Y, X is what the buildings used, and this is the assertion that
        // would have caught the difference.
        const glm::mat3 reversed = rotZ(euler.z) * rotY(euler.y) * rotX(euler.x);
        CHECK_FALSE(glm::mat3(got) == reversed);
    }
}

TEST_CASE("scale is applied inside the rotation, not after it", "[placement]") {
    // Uniform scale commutes with rotation, so this cannot be caught by
    // comparing a rotated point. What it can be caught by is the translation:
    // scaling last would leave it untouched, and scaling the whole matrix
    // afterwards would multiply it.
    const glm::vec3 pos(7.0f, 0.0f, 0.0f);
    const glm::mat4 m = placementModelMatrix(pos, {0.2f, 0.3f, 0.4f}, 5.0f);
    CHECK(m[3][0] == Catch::Approx(7.0f));
    CHECK(m[3][1] == Catch::Approx(0.0f).margin(1e-6));
    CHECK(m[3][2] == Catch::Approx(0.0f).margin(1e-6));
}

TEST_CASE("a point lands where composing the three rotations puts it", "[placement]") {
    // The end-to-end statement: an offset from the placement origin, scaled,
    // rotated and translated, against the same thing done by hand.
    const glm::vec3 pos(100.0f, 20.0f, -5.0f);
    const glm::vec3 euler(-0.45f, 0.85f, 0.15f);
    const float scale = 2.0f;
    const glm::vec3 local(1.0f, 2.0f, 3.0f);

    const glm::vec3 got = glm::vec3(
        placementModelMatrix(pos, euler, scale) * glm::vec4(local, 1.0f));
    const glm::vec3 want =
        pos + rotX(euler.x) * rotY(euler.y) * rotZ(euler.z) * (local * scale);

    CHECK(got.x == Catch::Approx(want.x).margin(1e-4));
    CHECK(got.y == Catch::Approx(want.y).margin(1e-4));
    CHECK(got.z == Catch::Approx(want.z).margin(1e-4));
}
