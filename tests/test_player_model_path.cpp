// Which body a character wears.
//
// Gender alone does not answer it. A nonbinary character picks one of the two
// models and that choice lives on the character, so every path that loads a
// body or its skins has to be told about it.
//
// Three did not agree. The character screen's preview passed the choice, the
// world spawn took the default, and the skin textures were chosen from gender
// alone - so a nonbinary character who picked the female body saw it in the
// paper doll, a male body in the world, and would have had male skins painted
// on a female mesh. Nothing raises for any of that; it is only visible by
// looking at the character.
#include <catch_amalgamated.hpp>

#include <string>

#include "game/character.hpp"

using wowee::game::Gender;
using wowee::game::getPlayerModelPath;
using wowee::game::Race;

namespace {

bool isFemalePath(const std::string& path) {
    return path.find("\\Female\\") != std::string::npos;
}

}  // namespace

TEST_CASE("male and female take their own body whatever the flag says",
          "[model-path]") {
    // The flag only speaks for the nonbinary case; it must not override a
    // gender that has already answered.
    CHECK_FALSE(isFemalePath(getPlayerModelPath(Race::HUMAN, Gender::MALE, false)));
    CHECK_FALSE(isFemalePath(getPlayerModelPath(Race::HUMAN, Gender::MALE, true)));
    CHECK(isFemalePath(getPlayerModelPath(Race::HUMAN, Gender::FEMALE, false)));
    CHECK(isFemalePath(getPlayerModelPath(Race::HUMAN, Gender::FEMALE, true)));
}

TEST_CASE("a nonbinary character wears the body they chose", "[model-path]") {
    CHECK(isFemalePath(getPlayerModelPath(Race::HUMAN, Gender::NONBINARY, true)));
    CHECK_FALSE(isFemalePath(getPlayerModelPath(Race::HUMAN, Gender::NONBINARY, false)));
}

TEST_CASE("the choice holds for every race", "[model-path]") {
    // A race that ignored it would put one character in the wrong body, which
    // is harder to notice than all of them being wrong.
    for (const Race race : {Race::HUMAN, Race::ORC, Race::DWARF, Race::NIGHT_ELF,
                            Race::UNDEAD, Race::TAUREN, Race::GNOME, Race::TROLL,
                            Race::BLOOD_ELF, Race::DRAENEI}) {
        INFO("race " << static_cast<int>(race));
        const std::string chose = getPlayerModelPath(race, Gender::NONBINARY, true);
        const std::string other = getPlayerModelPath(race, Gender::NONBINARY, false);
        REQUIRE_FALSE(chose.empty());
        REQUIRE_FALSE(other.empty());
        CHECK(isFemalePath(chose));
        CHECK_FALSE(isFemalePath(other));
        CHECK(chose != other);
    }
}

TEST_CASE("the path names the race it was asked for", "[model-path]") {
    CHECK(getPlayerModelPath(Race::ORC, Gender::MALE).find("Orc") != std::string::npos);
    CHECK(getPlayerModelPath(Race::TAUREN, Gender::FEMALE).find("Tauren") != std::string::npos);
}

TEST_CASE("a character carries its own answer", "[model-path]") {
    // The overload the callers with a Character in hand use; it must read the
    // same flag rather than defaulting like the two-argument form.
    wowee::game::Character ch;
    ch.race = Race::HUMAN;
    ch.gender = Gender::NONBINARY;
    ch.useFemaleModel = true;
    CHECK(isFemalePath(getPlayerModelPath(ch)));
    ch.useFemaleModel = false;
    CHECK_FALSE(isFemalePath(getPlayerModelPath(ch)));
}
