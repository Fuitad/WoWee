// Keeping nameplates off one another when overlap is turned off.
//
// The cases that matter are the ones a glance at the code agrees with and the
// screen does not: a plate lifted onto a third plate, and a lift that clears
// by so little the two bars still read as touching.
#include <catch_amalgamated.hpp>
#include "ui/nameplate_stacking.hpp"

using namespace wowee::ui;

namespace {
constexpr float kBarH = 8.0f;
constexpr float kTop = kBarH + 24.0f;

// The box a plate occupies once its bar top is known.
PlateBox boxFor(float x0, float x1, float y) {
    return PlateBox{x0, y - kTop, x1, y + kBarH};
}
}  // namespace

TEST_CASE("a plate with nothing in the way does not move", "[nameplates]") {
    std::vector<PlateBox> placed;
    CHECK(plateTopClearOf(placed, 100.0f, 180.0f, 400.0f, kBarH, kTop) == 400.0f);
}

TEST_CASE("a plate that would land on another is lifted clear", "[nameplates]") {
    std::vector<PlateBox> placed{boxFor(100.0f, 180.0f, 400.0f)};
    const float y = plateTopClearOf(placed, 100.0f, 180.0f, 400.0f, kBarH, kTop);
    CHECK(y < 400.0f);
    // Clear means clear: the lifted bar's bottom must sit above the other
    // plate's top, not merely at it.
    CHECK(y + kBarH < placed[0].y0);
}

TEST_CASE("plates that do not share screen space are left alone", "[nameplates]") {
    std::vector<PlateBox> placed{boxFor(100.0f, 180.0f, 400.0f)};
    // Well to the right, same height.
    CHECK(plateTopClearOf(placed, 400.0f, 480.0f, 400.0f, kBarH, kTop) == 400.0f);
    // Same column, far enough above that the boxes never meet.
    CHECK(plateTopClearOf(placed, 100.0f, 180.0f, 200.0f, kBarH, kTop) == 200.0f);
}

TEST_CASE("a plate lifted onto a third is lifted again", "[nameplates]") {
    // The case a single pass gets wrong: the space above the first plate is
    // already taken, so one lift is not enough.
    std::vector<PlateBox> placed{
        boxFor(100.0f, 180.0f, 400.0f),
        boxFor(100.0f, 180.0f, 360.0f),
    };
    auto clearsAll = [&](float y) {
        for (const auto& r : placed) {
            if (!((y + kBarH < r.y0) || (y - kTop > r.y1))) return false;
        }
        return true;
    };
    CHECK(clearsAll(plateTopClearOf(placed, 100.0f, 180.0f, 400.0f, kBarH, kTop)));

    // And that this case is really the one being tested: allowed a single
    // lift, the plate lands on the second box and stays there. Without this
    // the check above passes for a version that only ever lifts once.
    CHECK_FALSE(clearsAll(
        plateTopClearOf(placed, 100.0f, 180.0f, 400.0f, kBarH, kTop, 1)));
}

TEST_CASE("the lift gives up rather than spinning", "[nameplates]") {
    // Forty plates on one spot is not a real frame, but the loop must still
    // return: the guard bounds the work, and every lift moves upward.
    std::vector<PlateBox> placed;
    for (int i = 0; i < 40; ++i) placed.push_back(boxFor(100.0f, 180.0f, 400.0f));
    const float y = plateTopClearOf(placed, 100.0f, 180.0f, 400.0f, kBarH, kTop);
    CHECK(y <= 400.0f);
}
