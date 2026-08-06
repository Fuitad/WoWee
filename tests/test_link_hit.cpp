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
#include "ui/text_markup.hpp"

using wowee::ui::LinkRect;
using wowee::ui::linkRectFromDraw;
using wowee::ui::mouseToTreeSpace;
using wowee::ui::parseMarkup;
using wowee::ui::WrapRun;

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


// ── The whole chain, which is where the faults actually were ────────────────
//
// Each layer was verified on its own and each was wrong at a seam: the parser
// dropped the display text, the rect was filed in the draw pass's coordinate
// space and tested in the tree's, and the dispatch was written against font
// strings when a chat line is drawn by a message frame. Testing the layers
// separately is what let all three through, so this walks a chat line the way
// the client does — parse it, lay the runs out, file the link, click where the
// text appears — and asks what the click found.
namespace {

/// One unit per character, so a width reads as a count. The renderer measures
/// with a font; the arithmetic between the two is identical.
float runWidth(const std::string& s) { return static_cast<float>(s.size()); }

/// What a click at a window pixel lands on, given a line drawn at (atX, atY).
/// Mirrors drawMarkupText's advance: runs are laid left to right, each one
/// starting where the last ended.
std::string linkUnderClick(const std::string& line, float atX, float atY,
                           float lineH, float clickX, float clickY,
                           float screenH, float scale) {
    std::vector<wowee::ui::LinkRect> filed;
    float x = atX;
    for (const WrapRun& run : parseMarkup(line)) {
        const float w = runWidth(run.text);
        if (!run.link.empty()) {
            filed.push_back(wowee::ui::linkRectFromDraw(
                1, run.link, run.text, x, atY, w, lineH, screenH, scale));
        }
        x += w;
    }
    mouseToTreeSpace(clickX, clickY, screenH, scale);
    for (auto it = filed.rbegin(); it != filed.rend(); ++it) {
        if (clickX >= it->x0 && clickX <= it->x1 &&
            clickY >= it->y0 && clickY <= it->y1) {
            return it->link;
        }
    }
    return "";
}

}  // namespace

TEST_CASE("a chat line's link is found by a click on its text", "[linkhit]") {
    constexpr float kScreenH = 1080.0f;
    constexpr float kLineH = 14.0f;
    // "You receive loot: " is eighteen characters, so the link's display text
    // "[Fractured Canine]" runs from x=18 to x=36 on a line drawn at x=0.
    const std::string line =
        "You receive loot: |cff9d9d9d|Hitem:3299|h[Fractured Canine]|h|r.";

    SECTION("a click on the item name finds the item") {
        REQUIRE(linkUnderClick(line, 0.0f, 200.0f, kLineH,
                               25.0f, 205.0f, kScreenH, 1.0f) == "item:3299");
    }

    SECTION("a click on the words before it finds nothing") {
        REQUIRE(linkUnderClick(line, 0.0f, 200.0f, kLineH,
                               5.0f, 205.0f, kScreenH, 1.0f).empty());
    }

    SECTION("a click on the full stop after it finds nothing") {
        REQUIRE(linkUnderClick(line, 0.0f, 200.0f, kLineH,
                               36.5f, 205.0f, kScreenH, 1.0f).empty());
    }

    SECTION("a click on the line above finds nothing") {
        // The shape of the coordinate fault: an unflipped rect answers clicks
        // a screen away from where the text is.
        REQUIRE(linkUnderClick(line, 0.0f, 200.0f, kLineH,
                               25.0f, 100.0f, kScreenH, 1.0f).empty());
    }

    SECTION("two links in one line are told apart") {
        const std::string two = "|Hitem:1|h[AAAA]|h |Hspell:2|h[BBBB]|h";
        REQUIRE(linkUnderClick(two, 0.0f, 300.0f, kLineH,
                               3.0f, 305.0f, kScreenH, 1.0f) == "item:1");
        REQUIRE(linkUnderClick(two, 0.0f, 300.0f, kLineH,
                               10.0f, 305.0f, kScreenH, 1.0f) == "spell:2");
    }

    SECTION("and at an interface scale") {
        REQUIRE(linkUnderClick(line, 0.0f, 200.0f, kLineH,
                               25.0f, 205.0f, kScreenH, 0.8f) == "item:3299");
    }
}
