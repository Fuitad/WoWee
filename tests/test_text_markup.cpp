// The inline markup a label carries: colour, links, line breaks, textures.
//
// The case that made this a file of its own: |Hitem:3299|h[Fractured Canine]|h
// drew as nothing at all. The parser skipped from the opening marker to the
// next bar — which is the |h *before* the display text — and then skipped
// again from there to the |h after it, so the name between them went with the
// payload. "You receive loot: [Fractured Canine]." rendered as "You receive
// loot: ." on every FrameXML surface that draws a link, which is the loot
// stream, quest rewards, achievement text and every tooltip carrying one.
//
// It went unnoticed because the function lived inside widget_renderer.cpp with
// ImGui, where nothing could reach it.
#include "catch_amalgamated.hpp"
#include "ui/text_markup.hpp"

#include <string>

using wowee::ui::parseMarkup;
using wowee::ui::WrapRun;

namespace {
/// Everything the runs would draw, which is what the reader sees.
std::string drawn(const std::string& in) {
    std::string out;
    for (const WrapRun& r : parseMarkup(in)) out += r.text;
    return out;
}
}  // namespace

TEST_CASE("a link keeps its display text", "[markup]") {
    SECTION("the name between the markers survives") {
        REQUIRE(drawn("|Hitem:6948:0:0:0:0:0:0:0|h[Hearthstone]|h") == "[Hearthstone]");
    }

    SECTION("and the text around it") {
        REQUIRE(drawn("You receive loot: |cff9d9d9d|Hitem:3299|h[Fractured Canine]|h|r.")
                == "You receive loot: [Fractured Canine].");
    }

    SECTION("two links in one line stay separate runs") {
        const auto runs = parseMarkup("|Hitem:1|h[A]|h and |Hspell:2|h[B]|h");
        std::string first, second;
        for (const WrapRun& r : runs) {
            if (r.link == "item:1")  first  += r.text;
            if (r.link == "spell:2") second += r.text;
        }
        REQUIRE(first == "[A]");
        REQUIRE(second == "[B]");
    }

    SECTION("the payload is carried, so a click can name what it hit") {
        const auto runs = parseMarkup("|Hquest:1234:60|h[Kill Ten Rats]|h");
        bool found = false;
        for (const WrapRun& r : runs) {
            if (r.text == "[Kill Ten Rats]") {
                REQUIRE(r.link == "quest:1234:60");
                found = true;
            }
        }
        REQUIRE(found);
    }

    SECTION("text after the closing marker is not part of the link") {
        for (const WrapRun& r : parseMarkup("|Hitem:1|h[A]|h tail")) {
            if (r.text.find("tail") != std::string::npos) REQUIRE(r.link.empty());
        }
    }
}

TEST_CASE("the other escapes still behave", "[markup]") {
    SECTION("a colour escape sets the run's colour and draws nothing itself") {
        const auto runs = parseMarkup("|cffff0000red|r");
        REQUIRE(drawn("|cffff0000red|r") == "red");
        REQUIRE(runs.front().hasColor);
    }
    SECTION("|n is a line break") { REQUIRE(drawn("a|nb") == "a\nb"); }
    SECTION("|| is a literal bar") { REQUIRE(drawn("a||b") == "a|b"); }
    SECTION("an inline texture draws nothing") {
        REQUIRE(drawn("a|TInterface\\Icons\\X:16|tb") == "ab");
    }
    SECTION("an unterminated link does not run off the end") {
        REQUIRE(drawn("|Hitem:1") == "");
    }
}
