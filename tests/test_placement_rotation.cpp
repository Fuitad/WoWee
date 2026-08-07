// The order the two placement chunks are composed in.
//
// ADT stores doodads in MDDF and buildings in MODF, and the rotation field is
// the same field in both — the format says so, and both readers in
// terrain_manager build the same triple out of it:
//
//     (-rot[2], -rot[0], rot[1] + 180)   in radians
//
// WoW composes that as Ry(B)*Rx(A)*Rz(C), which in this space is
// Rz(B)*Ry(-A)*Rx(-C) — so the stored triple is applied Z, then Y, then X.
// The building path has always done that and the buildings are right. The
// doodad path applied the same triple in the reverse order.
//
// What makes this worth a test rather than a look: with no pitch and no roll,
// the two orders are *the same rotation*. Nearly every doodad in the world is
// upright, so nearly every doodad looked correct under either one and only a
// genuinely tilted placement came out wrong. Three attempts to fix this by
// permuting axes were reverted, because a permutation that satisfies the
// upright case says nothing at all.
//
// So both halves are pinned: that the orders agree where they must, and that
// they differ where the bug lived.

#include <catch_amalgamated.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

/// Z, then Y, then X — what WMOInstance::updateModelMatrix does, and now what
/// M2Instance::updateModelMatrix does.
glm::mat4 composeZYX(const glm::vec3& r) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, r.z, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::rotate(m, r.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, r.x, glm::vec3(1.0f, 0.0f, 0.0f));
    return m;
}

/// X, then Y, then Z — what the doodad path used to do.
glm::mat4 composeXYZ(const glm::vec3& r) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, r.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, r.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, r.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return m;
}

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

/// The triple both readers build from a placement record's three degrees.
glm::vec3 placementEuler(float rotX, float rotY, float rotZ) {
    return glm::vec3(-rotZ * kDegToRad,
                     -rotX * kDegToRad,
                     (rotY + 180.0f) * kDegToRad);
}

bool nearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(a[c][r] - b[c][r]) > eps) return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("An upright placement composes the same either way", "[placement][rotation]") {
    // This is why the fault survived three attempts and a lot of looking: with
    // only a yaw, the order cannot be observed. Any permutation "works" here.
    for (float yaw : {0.0f, 37.5f, 90.0f, 180.0f, 271.25f}) {
        const glm::vec3 e = placementEuler(0.0f, yaw, 0.0f);
        INFO("yaw " << yaw);
        CHECK(nearlyEqual(composeZYX(e), composeXYZ(e)));
    }
}

TEST_CASE("A tilted placement does not, which is where the fault was",
          "[placement][rotation]") {
    // Pitch, roll, and both together. If these ever agree, the test above has
    // stopped being able to tell the two orders apart and this file is no
    // longer checking anything.
    const glm::vec3 pitched = placementEuler(25.0f, 40.0f, 0.0f);
    const glm::vec3 rolled  = placementEuler(0.0f, 40.0f, 25.0f);
    const glm::vec3 both    = placementEuler(25.0f, 40.0f, -15.0f);

    CHECK_FALSE(nearlyEqual(composeZYX(pitched), composeXYZ(pitched)));
    CHECK_FALSE(nearlyEqual(composeZYX(rolled), composeXYZ(rolled)));
    CHECK_FALSE(nearlyEqual(composeZYX(both), composeXYZ(both)));
}

TEST_CASE("The composed order is Rz(B) * Ry(-A) * Rx(-C)", "[placement][rotation]") {
    // Stated as the matrix product rather than as three calls, so the claim in
    // the comment is checked against arithmetic and not against itself.
    const float A = 25.0f, B = 40.0f, C = -15.0f;   // the record's three degrees
    const glm::mat4 expected =
        glm::rotate(glm::mat4(1.0f), (B + 180.0f) * kDegToRad, glm::vec3(0, 0, 1)) *
        glm::rotate(glm::mat4(1.0f), -A * kDegToRad,           glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1.0f), -C * kDegToRad,           glm::vec3(1, 0, 0));

    CHECK(nearlyEqual(composeZYX(placementEuler(A, B, C)), expected));
}
