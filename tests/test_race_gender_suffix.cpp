// The two-letter race code an item's per-race model is suffixed with.
//
// Helm_Plate_B_01_HuM.m2 is the human male cut of a helm; the shoulders use
// the same suffix. Both were written out as their own ten-entry map - the
// helmet resolver and the shoulder attachment in the spawner - so a race added
// or corrected in one would leave the other asking the archive for a name that
// is not there. That failure is silent by construction: the resolvers treat a
// missing file as "this piece has no per-race cut" and fall back to the base
// model, so the wrong suffix and no suffix look the same from outside.
//
// Ten races, not twelve. 9 and 22 are goblin and worgen, which arrive in
// Cataclysm and have no art in a 3.3.5 install, and an entry for either would
// be a suffix nothing can satisfy.
#include <catch_amalgamated.hpp>

#include <string>

#include "core/helm_visual.hpp"

using wowee::core::raceGenderSuffix;

TEST_CASE("each playable race has its two-letter code", "[appearance]") {
    // Gender 0 is male. The pairs are read straight off the file names in the
    // archive rather than from a race enum, which is why Scourge is "Sc" and
    // not "Un" - the art uses the internal name, not the displayed one.
    CHECK(raceGenderSuffix(1, 0) == "_HuM");   // human
    CHECK(raceGenderSuffix(2, 0) == "_OrM");   // orc
    CHECK(raceGenderSuffix(3, 0) == "_DwM");   // dwarf
    CHECK(raceGenderSuffix(4, 0) == "_NiM");   // night elf
    CHECK(raceGenderSuffix(5, 0) == "_ScM");   // undead, "Scourge" in the art
    CHECK(raceGenderSuffix(6, 0) == "_TaM");   // tauren
    CHECK(raceGenderSuffix(7, 0) == "_GnM");   // gnome
    CHECK(raceGenderSuffix(8, 0) == "_TrM");   // troll
    CHECK(raceGenderSuffix(10, 0) == "_BeM");  // blood elf
    CHECK(raceGenderSuffix(11, 0) == "_DrM");  // draenei
}

TEST_CASE("gender is the last letter and only F or M", "[appearance]") {
    CHECK(raceGenderSuffix(1, 1) == "_HuF");
    CHECK(raceGenderSuffix(11, 1) == "_DrF");

    SECTION("anything that is not male is female, as the field only holds two") {
        CHECK(raceGenderSuffix(1, 2) == "_HuF");
    }
}

TEST_CASE("a race with no per-race cut answers empty", "[appearance]") {
    // Empty means "use the base model", which is a real answer rather than a
    // failure. Race 9 and 22 are goblin and worgen: no 3.3.5 art exists, and
    // inventing a code for them would ask for a file that cannot be there.
    CHECK(raceGenderSuffix(9, 0).empty());
    CHECK(raceGenderSuffix(22, 0).empty());
    CHECK(raceGenderSuffix(0, 0).empty());
    CHECK(raceGenderSuffix(255, 1).empty());
}

TEST_CASE("no two races share a code", "[appearance]") {
    // A collision would put one race in another's armour, and the codes are
    // close enough to each other that a typo produces one.
    std::string seen;
    for (uint8_t race = 0; race < 32; ++race) {
        const std::string suffix = raceGenderSuffix(race, 0);
        if (suffix.empty()) continue;
        INFO("race " << static_cast<int>(race) << " -> " << suffix);
        CHECK(seen.find(suffix) == std::string::npos);
        seen += suffix + ";";
    }
}
