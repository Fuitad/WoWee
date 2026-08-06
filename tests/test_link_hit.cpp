// A hyperlink click landing where the link was drawn.
//
// Two passes meet here and they disagree about y. The draw pass works in
// screen pixels measured from the top; the widget tree works in interface
// units measured from the bottom. A rect filed in the first space and tested
// in the second misses by the interface scale AND by the height of the screen,
// and nothing says so — both sides compile, both run, every click lands on
// nothing. That is exactly what shipped, and it is why both halves of the
// conversion live in one header.
//
// So the invariant here is not either conversion on its own. It is the round
// trip: text drawn at a place on screen must be found by a click at that same
// place on screen.
#include "catch_amalgamated.hpp"
#include "ui/link_hit.hpp"

using wowee::ui::LinkRect;
using wowee::ui::linkRectFromDraw;
using wowee::ui::mouseToTreeSpace;

namespace {

/// Does a click at this window pixel land on the rect?
bool clickHits(const LinkRect& r, float px, float py, float screenH, float scale) {
    mouseToTreeSpace(px, py, screenH, scale);
    return px >= r.x0 && px <= r.x1 && py >= r.y0 && py <= r.y1;
}

}  // namespace

TEST_CASE("a click lands where the link was drawn", "[linkhit]") {
    constexpr float kScreenH = 1080.0f;

    SECTION("at scale 1, anywhere inside the drawn glyphs") {
        // Drawn 100 px from the left, 200 px down from the top, 80 wide, 14 tall.
        const LinkRect r = linkRectFromDraw(7, "item:3299", "[Fractured Canine]",
                                            100.0f, 200.0f, 80.0f, 14.0f,
                                            kScreenH, 1.0f);
        REQUIRE(clickHits(r, 101.0f, 201.0f, kScreenH, 1.0f));
        REQUIRE(clickHits(r, 179.0f, 213.0f, kScreenH, 1.0f));
        REQUIRE(clickHits(r, 140.0f, 207.0f, kScreenH, 1.0f));
    }

    SECTION("and not outside them") {
        const LinkRect r = linkRectFromDraw(7, "item:3299", "[Fractured Canine]",
                                            100.0f, 200.0f, 80.0f, 14.0f,
                                            kScreenH, 1.0f);
        REQUIRE_FALSE(clickHits(r,  99.0f, 207.0f, kScreenH, 1.0f));  // left
        REQUIRE_FALSE(clickHits(r, 181.0f, 207.0f, kScreenH, 1.0f));  // right
        REQUIRE_FALSE(clickHits(r, 140.0f, 199.0f, kScreenH, 1.0f));  // above
        REQUIRE_FALSE(clickHits(r, 140.0f, 215.0f, kScreenH, 1.0f));  // below
    }

    SECTION("the round trip survives an interface scale") {
        // The scale divides both sides. Getting it wrong on one of them is the
        // half of the original fault that a same-scale test would not show.
        for (float scale : {0.64f, 0.85f, 1.0f, 1.25f}) {
            const LinkRect r = linkRectFromDraw(7, "spell:133", "[Fireball]",
                                                300.0f, 500.0f, 60.0f, 16.0f,
                                                kScreenH, scale);
            REQUIRE(clickHits(r, 330.0f, 508.0f, kScreenH, scale));
            REQUIRE_FALSE(clickHits(r, 330.0f, 480.0f, kScreenH, scale));
        }
    }

    SECTION("a link at the top of the screen is not confused with one at the bottom") {
        // The whole shape of the original fault: an unflipped rect near the top
        // answers clicks near the bottom.
        const LinkRect top = linkRectFromDraw(1, "item:1", "[A]",
                                              10.0f, 5.0f, 40.0f, 14.0f,
                                              kScreenH, 1.0f);
        REQUIRE(clickHits(top, 20.0f, 10.0f, kScreenH, 1.0f));
        REQUIRE_FALSE(clickHits(top, 20.0f, kScreenH - 10.0f, kScreenH, 1.0f));
    }
}
