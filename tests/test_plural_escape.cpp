// WoW's |4singular:plural; escape.
//
// The interface writes a counted thing as one string and leaves the ending to
// the client: SECONDS is "|4Second:Seconds;" and CAMP_TIMER is
// "%d %s until logout". Nothing resolved it, so the logout prompt showed
// "5 |4Second:Seconds; until logout" exactly as written. Seventy-five strings
// in this interface use it, so the same was true of every countdown, every
// "N players", every "N quests".
//
// The rule is inferred rather than documented here, which is why it is pinned:
// the ending is chosen by the last number *before* the escape.

#include <catch_amalgamated.hpp>

#include <string>

#include "ui/plural_escape.hpp"

using wowee::ui::resolvePluralEscapes;

TEST_CASE("one takes the singular, anything else the plural", "[plural]") {
    CHECK(resolvePluralEscapes("1 |4second:seconds;") == "1 second");
    CHECK(resolvePluralEscapes("2 |4second:seconds;") == "2 seconds");
    CHECK(resolvePluralEscapes("0 |4second:seconds;") == "0 seconds");
    CHECK(resolvePluralEscapes("21 |4second:seconds;") == "21 seconds");
}

TEST_CASE("the number is the one before the escape, not the first", "[plural]") {
    // "You have 1 of 5 |4quest:quests;" — the count that governs is the 5.
    // Words may sit between the number and the escape, which the interface's
    // own strings do constantly: "%d more daily |4quest:quests;".
    CHECK(resolvePluralEscapes("1 of 5 |4quest:quests;") == "1 of 5 quests");
    CHECK(resolvePluralEscapes("5 of 1 |4quest:quests;") == "5 of 1 quest");
}

TEST_CASE("two escapes in one string are decided separately", "[plural]") {
    CHECK(resolvePluralEscapes("1 |4day:days; and 3 |4hour:hours;") ==
          "1 day and 3 hours");
}

TEST_CASE("no number before it takes the plural", "[plural]") {
    // A bare SECONDS is a column heading rather than a count, and the plural
    // is what a heading wants.
    CHECK(resolvePluralEscapes("|4Second:Seconds;") == "Seconds");
    CHECK(resolvePluralEscapes("Time: |4Second:Seconds;") == "Time: Seconds");
}

TEST_CASE("the whole string around it survives", "[plural]") {
    CHECK(resolvePluralEscapes("You will be logged out in 20 |4second:seconds;.") ==
          "You will be logged out in 20 seconds.");
    CHECK(resolvePluralEscapes("no escape here") == "no escape here");
    CHECK(resolvePluralEscapes("") == "");
}

TEST_CASE("a malformed escape is left as it was written", "[plural]") {
    // Eating it would lose text the string meant to show, and a half-written
    // escape is a data fault worth seeing rather than hiding.
    CHECK(resolvePluralEscapes("5 |4second") == "5 |4second");
    CHECK(resolvePluralEscapes("5 |4second;") == "5 |4second;");
    CHECK(resolvePluralEscapes("|4") == "|4");
}

TEST_CASE("an empty half is honoured rather than skipped", "[plural]") {
    // "1 |4:s;" is how a string says "add nothing, or an s".
    CHECK(resolvePluralEscapes("1 item|4:s;") == "1 item");
    CHECK(resolvePluralEscapes("4 item|4:s;") == "4 items");
}

TEST_CASE("digits touching the escape still count", "[plural]") {
    // No space between the number and the escape.
    CHECK(resolvePluralEscapes("1|4 second: seconds;") == "1 second");
    // And words in between, which is the commoner shape in the real strings.
    CHECK(resolvePluralEscapes("1 more daily |4quest:quests;") ==
          "1 more daily quest");
    CHECK(resolvePluralEscapes("3 more daily |4quest:quests;") ==
          "3 more daily quests");
}

TEST_CASE("other bar escapes are left alone", "[plural]") {
    // |1, |2 and |3 are declension and gender forms the localised builds need
    // and the English data never emits. Guessing at them would be inventing
    // grammar this text does not carry.
    CHECK(resolvePluralEscapes("|1a:b;") == "|1a:b;");
    CHECK(resolvePluralEscapes("|cffffffff1 |4gem:gems;|r") ==
          "|cffffffff1 gem|r");
}
