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
using wowee::ui::caretStepLeft;
using wowee::ui::caretStepRight;

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


// ── The caret walks what is drawn ───────────────────────────────────────────
//
// Only reachable since links became clickable: shift-clicking one puts the
// whole "|Hitem:3299|h[Fractured Canine]|h" into the edit box, and the box
// draws its display text. A caret stepping one byte at a time would sit still
// for forty keypresses crossing the payload and then jump a word.
TEST_CASE("the caret steps by drawn characters", "[markup]") {
    const std::string line = "hi |Hitem:1|h[AB]|h x";

    SECTION("right from the start crosses the plain text one at a time") {
        REQUIRE(caretStepRight(line, 0) == 1);
        REQUIRE(caretStepRight(line, 1) == 2);
    }

    SECTION("a link is one step whole, so the caret never rests inside it") {
        // "hi " is three characters and the link runs from 3 to the end. One
        // step from 3 clears all of it — payload, display text and closing
        // marker — because a caret inside a link is a caret that can leave
        // half an escape behind when something is erased.
        REQUIRE(caretStepRight(line, 3) == 19);
        REQUIRE(line.substr(19) == " x");
    }

    SECTION("left is the inverse of right, everywhere along the line") {
        for (size_t p = 0; p < line.size();) {
            const size_t next = caretStepRight(line, p);
            if (next >= line.size()) break;
            REQUIRE(caretStepLeft(line, next) == p);
            p = next;
        }
    }

    SECTION("neither runs past an end") {
        REQUIRE(caretStepRight(line, line.size()) == line.size());
        REQUIRE(caretStepLeft(line, 0) == 0);
    }

    SECTION("a colour escape draws nothing, so the caret does not stop in it") {
        // "a|cffff0000b|rc": the escape is ten bytes drawing nothing, so a
        // step from after 'a' crosses it and 'b' together.
        const std::string coloured = "a|cffff0000b|rc";
        REQUIRE(coloured.substr(11, 1) == "b");
        REQUIRE(caretStepRight(coloured, 1) == 12);
    }
}


// ── Erasing uses the same step, so the two cannot disagree ─────────────────
//
// Backspace over a link has to take the whole link. Removing a byte leaves
// half an escape behind, which the parser then reads as whatever the wreckage
// resembles — and the text is the player's own message, so the damage is
// visible and unrecoverable by pressing the key again.
namespace {

/// Backspace, as the edit box performs it: erase back to the previous caret
/// position.
std::string backspaceAt(std::string s, size_t& at) {
    const size_t from = caretStepLeft(s, at);
    s.erase(from, at - from);
    at = from;
    return s;
}

}  // namespace

TEST_CASE("erasing removes what draws as one character", "[markup]") {
    SECTION("backspace over a link takes all of it") {
        std::string s = "hi |Hitem:1|h[AB]|h";
        size_t at = s.size();
        // One press: the link is a single unit.
        s = backspaceAt(s, at);
        REQUIRE(s == "hi ");
        REQUIRE(at == 3);
    }

    SECTION("and never leaves half an escape") {
        std::string s = "a|cffff0000b|rc";
        size_t at = s.size();
        while (at > 0) {
            s = backspaceAt(s, at);
            // Whatever is left must still parse to something sane: every bar
            // in it is either doubled or opens an escape that closes.
            REQUIRE(parseMarkup(s).size() <= 3);
        }
        REQUIRE(s.empty());
    }
}
