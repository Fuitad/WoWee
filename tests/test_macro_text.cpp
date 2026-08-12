// Reading a macro body.
//
// The action bar and the chat panel each had their own copy of the line walk,
// one to pick a macro's icon and one to run it. Neither had a test, and both
// fail quietly: a dropped line is a macro that half works, and a comment
// treated as a command is a macro whose text goes out as a say to everyone
// nearby.
#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

#include "ui/macro_text.hpp"

using wowee::ui::macroCommandLines;
using wowee::ui::macroShowtooltipArg;

TEST_CASE("every non-comment line is a command", "[macrotext]") {
    const std::string macro = "/cast Frostbolt\n/say Incoming\n";
    CHECK(macroCommandLines(macro) ==
          std::vector<std::string>{"/cast Frostbolt", "/say Incoming"});
}

TEST_CASE("the last line counts even with no newline after it", "[macrotext]") {
    // A macro body arrives without a trailing newline, so a reader that only
    // acts on a newline loses the final command. That is the last line of the
    // macro: the one the player is most likely to have just typed.
    CHECK(macroCommandLines("/cast Frostbolt") ==
          std::vector<std::string>{"/cast Frostbolt"});
    CHECK(macroCommandLines("/cast A\n/cast B") ==
          std::vector<std::string>{"/cast A", "/cast B"});
}

TEST_CASE("comments and blank lines are not commands", "[macrotext]") {
    const std::string macro =
        "#showtooltip\n"
        "\n"
        "   \n"
        "# a note to self\n"
        "/cast Frostbolt\n";
    CHECK(macroCommandLines(macro) == std::vector<std::string>{"/cast Frostbolt"});
}

TEST_CASE("carriage returns are stripped", "[macrotext]") {
    // A macro edited on Windows carries these. "/cast Frostbolt\r" matches no
    // spell, and the failure is a macro that does nothing with no message.
    CHECK(macroCommandLines("/cast Frostbolt\r\n/say Hi\r\n") ==
          std::vector<std::string>{"/cast Frostbolt", "/say Hi"});
    CHECK(macroCommandLines("/cast Frostbolt\r") ==
          std::vector<std::string>{"/cast Frostbolt"});
}

TEST_CASE("leading whitespace goes and trailing stays", "[macrotext]") {
    // Trailing space is kept because a slash command's argument may end in
    // one, and trimming it changes what the command is given.
    CHECK(macroCommandLines("   /cast Frostbolt") ==
          std::vector<std::string>{"/cast Frostbolt"});
    CHECK(macroCommandLines("\t/cast Frostbolt ") ==
          std::vector<std::string>{"/cast Frostbolt "});
}

TEST_CASE("an empty body has no commands", "[macrotext]") {
    CHECK(macroCommandLines("").empty());
    CHECK(macroCommandLines("\n\n\n").empty());
}

TEST_CASE("showtooltip with an argument names it", "[macrotext]") {
    CHECK(macroShowtooltipArg("#showtooltip Frostbolt\n/cast Frostbolt") ==
          "Frostbolt");
    CHECK(macroShowtooltipArg("#showtooltip   Frostbolt   \n") == "Frostbolt");
    // The short form is accepted too.
    CHECK(macroShowtooltipArg("#show Frostbolt\n") == "Frostbolt");
}

TEST_CASE("showtooltip with no argument asks the icon to follow the macro",
          "[macrotext]") {
    // Distinct from having no directive at all, and the two must not collapse
    // into each other: this one tracks whatever the macro would cast, and no
    // directive leaves the macro's own icon alone.
    CHECK(macroShowtooltipArg("#showtooltip\n/cast Frostbolt") == "__auto__");
    CHECK(macroShowtooltipArg("#showtooltip   \n/cast Frostbolt") == "__auto__");
    CHECK(macroShowtooltipArg("/cast Frostbolt").empty());
    CHECK(macroShowtooltipArg("# just a comment\n/cast Frostbolt").empty());
}

TEST_CASE("the first showtooltip wins", "[macrotext]") {
    CHECK(macroShowtooltipArg("#showtooltip Fireball\n#showtooltip Frostbolt\n") ==
          "Fireball");
}

TEST_CASE("showtooltip is found wherever it sits", "[macrotext]") {
    // Conventionally the first line, but not required to be, and it is read
    // with the same trimming as any other line.
    CHECK(macroShowtooltipArg("/cast Frostbolt\n  #showtooltip Fireball\r\n") ==
          "Fireball");
}
