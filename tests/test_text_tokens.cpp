// The $-tokens WoW's server text is written with, and what fills them in.
//
// The server sends a quest, a gossip line or a monster's say with the player
// left as a blank - "$N, you have done well" - and the client writes the name
// in. Until this was wired into the chat handler, every scripted NPC in the
// game addressed the player as "$N".
#include <catch_amalgamated.hpp>

#include "game/text_tokens.hpp"

using namespace wowee;

namespace {
game::TextSubject nightElfHuntress() {
    game::TextSubject s;
    s.name = "Shandris";
    s.className = "Hunter";
    s.raceName = "Night Elf";
    s.gender = game::Gender::FEMALE;
    return s;
}
}  // namespace

TEST_CASE("the player's own details", "[text][tokens]") {
    const auto s = nightElfHuntress();
    CHECK(game::resolveTextTokens("$N, you have done well.", s) ==
          "Shandris, you have done well.");
    CHECK(game::resolveTextTokens("Well met, $n.", s) == "Well met, Shandris.");
    CHECK(game::resolveTextTokens("A $r $c stands before me.", s) ==
          "A Night Elf Hunter stands before me.");

    SECTION("a line with no token comes back untouched") {
        CHECK(game::resolveTextTokens("Hail, traveller.", s) == "Hail, traveller.");
    }

    SECTION("$b is a line break") {
        CHECK(game::resolveTextTokens("One.$bTwo.", s) == "One.\nTwo.");
    }
}

TEST_CASE("$g chooses by gender", "[text][tokens]") {
    const auto her = nightElfHuntress();
    game::TextSubject him = her;
    him.name = "Jarod";
    him.gender = game::Gender::MALE;

    CHECK(game::resolveTextTokens("Well met, $gsir:madam;.", him) == "Well met, sir.");
    CHECK(game::resolveTextTokens("Well met, $gsir:madam;.", her) == "Well met, madam.");

    SECTION("a third branch is taken when the text offers one") {
        game::TextSubject them = her;
        them.gender = game::Gender::NONBINARY;
        CHECK(game::resolveTextTokens("$gsir:madam:friend;", them) == "friend");
    }

    SECTION("with only two branches, neither of which fits, the shorter is used") {
        game::TextSubject them = her;
        them.gender = game::Gender::NONBINARY;
        CHECK(game::resolveTextTokens("$gbrother:sister;", them) == "sister");
    }

    SECTION("an unterminated $g is left alone rather than eating the rest") {
        // No semicolon: the switch runs to the end of the line, and swallowing
        // it would delete the sentence.
        const std::string open = "Well met, $gsir:madam";
        CHECK(game::resolveTextTokens(open, her) == open);
    }
}

TEST_CASE("pronouns follow the same gender", "[text][tokens]") {
    const auto her = nightElfHuntress();
    CHECK(game::resolveTextTokens("$p went home.", her) == "she went home.");
    CHECK(game::resolveTextTokens("I saw $o.", her) == "I saw her.");
    CHECK(game::resolveTextTokens("That is $s sword.", her) == "That is her sword.");
}

TEST_CASE("a character we know nothing about still reads as a sentence", "[text][tokens]") {
    // Before the character list arrives there is no name to write in, and the
    // line still has to be printable.
    const game::TextSubject unknown;
    CHECK(game::resolveTextTokens("Greetings, $N.", unknown) == "Greetings, Adventurer.");
}

TEST_CASE("a dollar that means nothing in particular is kept", "[text][tokens]") {
    const auto s = nightElfHuntress();
    CHECK(game::resolveTextTokens("It costs $5 and change.", s) == "It costs $5 and change.");
    CHECK(game::resolveTextTokens("Ends with a $", s) == "Ends with a $");
}
