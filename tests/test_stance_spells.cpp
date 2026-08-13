// The stance bar's spells, and the order they are in.
//
// Two places used to hold this: renderStanceBar, and the Ctrl+1..Ctrl+8
// handler that presses its buttons. The second carried a comment saying it
// kept "the same slot ordering as renderStanceBar", which is the whole
// hazard written down - the ordering is a shared fact and nothing enforced
// it. Reorder the bar and Ctrl+3 goes on pressing whatever used to be third.
//
// Order is what most of this pins, because a wrong order is the failure that
// looks like nothing: every button still works and every one is the wrong
// form.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <set>
#include <vector>

#include "game/stance_spells.hpp"

using wowee::game::Class;
using wowee::game::stanceSpellsForClass;

namespace {

std::vector<uint32_t> spellsFor(Class c) {
    const auto s = stanceSpellsForClass(static_cast<uint8_t>(c));
    return std::vector<uint32_t>(s.spells, s.spells + s.count);
}

}  // namespace

TEST_CASE("each stance class has its forms in display order", "[stance]") {
    CHECK(spellsFor(Class::WARRIOR) == std::vector<uint32_t>{2457, 71, 2458});
    CHECK(spellsFor(Class::DEATH_KNIGHT) ==
          std::vector<uint32_t>{48266, 48263, 48265});
    CHECK(spellsFor(Class::DRUID) ==
          std::vector<uint32_t>{5487, 9634, 768, 783, 1066, 24858, 33891, 33943,
                                40120});
    CHECK(spellsFor(Class::ROGUE) == std::vector<uint32_t>{1784});
    CHECK(spellsFor(Class::PRIEST) == std::vector<uint32_t>{15473});
}

TEST_CASE("a class with no stance bar gets nothing", "[stance]") {
    // The bar draws nothing and the bindings do nothing, which is the same
    // answer both call sites reached separately.
    for (const Class c : {Class::PALADIN, Class::HUNTER, Class::SHAMAN,
                          Class::MAGE, Class::WARLOCK}) {
        const auto s = stanceSpellsForClass(static_cast<uint8_t>(c));
        INFO("class " << static_cast<int>(c));
        CHECK(s.count == 0);
        CHECK(s.spells == nullptr);
    }
}

TEST_CASE("an unknown class id is not a stance class", "[stance]") {
    // 10 is unused in 3.3.5a, and the id arrives from the server.
    for (const uint8_t id : {uint8_t{0}, uint8_t{10}, uint8_t{12}, uint8_t{255}}) {
        INFO("class id " << static_cast<int>(id));
        CHECK(stanceSpellsForClass(id).count == 0);
    }
}

TEST_CASE("the count matches the table it describes", "[stance]") {
    // The count used to be written at the switch, separately from the array,
    // so adding a form meant two edits that had to agree. A count that is one
    // short hides the last form; one too long reads off the end.
    CHECK(stanceSpellsForClass(static_cast<uint8_t>(Class::DRUID)).count == 9);
    CHECK(stanceSpellsForClass(static_cast<uint8_t>(Class::WARRIOR)).count == 3);
    CHECK(stanceSpellsForClass(static_cast<uint8_t>(Class::DEATH_KNIGHT)).count == 3);
}

TEST_CASE("no form is listed twice for one class", "[stance]") {
    // A repeat would give two bar buttons that do the same thing and shift
    // every binding after it.
    for (const Class c : {Class::WARRIOR, Class::DEATH_KNIGHT, Class::DRUID,
                          Class::ROGUE, Class::PRIEST}) {
        const auto spells = spellsFor(c);
        const std::set<uint32_t> unique(spells.begin(), spells.end());
        INFO("class " << static_cast<int>(c));
        CHECK(unique.size() == spells.size());
        for (const uint32_t id : spells) CHECK(id != 0);
    }
}
