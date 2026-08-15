// Which Combat Text panel checkbox covers each floating line.
//
// Six of those checkboxes named a CVar that nothing read, so the panel
// remembered a choice the screen ignored. These cases pin the mapping that
// replaced that, including the two that are easy to get backwards: a pet's
// swing must not be answered by the player's own damage row, and effects are
// read against the unit they landed on rather than who caused them.
#include <catch_amalgamated.hpp>
#include "game/combat_text_filter.hpp"

using namespace wowee::game;
using T = CombatTextEntry;

namespace {
constexpr uint64_t kPlayer = 0x1000;
constexpr uint64_t kPet    = 0x2000;
constexpr uint64_t kTarget = 0x3000;
constexpr uint64_t kOther  = 0x4000;

CombatTextFilterRule ruleFor(CombatTextEntry::Type type, bool playerSource,
                             uint64_t src, uint64_t dst) {
    return combatTextFilterFor(type, playerSource, src, dst, kPet, kTarget);
}
}  // namespace

TEST_CASE("damage the player deals answers to the damage row", "[combattext]") {
    const auto r = ruleFor(T::MELEE_DAMAGE, true, kPlayer, kTarget);
    REQUIRE(r.cvar != nullptr);
    CHECK(std::string(r.cvar) == "CombatDamage");
    CHECK(std::string(r.fallback) == "1");
}

TEST_CASE("a pet's swing answers to the pet row, not the player's", "[combattext]") {
    const auto r = ruleFor(T::MELEE_DAMAGE, false, kPet, kTarget);
    REQUIRE(r.cvar != nullptr);
    CHECK(std::string(r.cvar) == "PetMeleeDamage");
}

TEST_CASE("damage arriving at the player is not filtered here", "[combattext]") {
    // "Target Damage" does not mean "damage taken"; incoming hits have their
    // own rows elsewhere in the panel and must keep showing.
    CHECK(ruleFor(T::MELEE_DAMAGE, false, kOther, kPlayer).cvar == nullptr);
    CHECK(ruleFor(T::CRIT_DAMAGE, false, kOther, kPlayer).cvar == nullptr);
    CHECK(ruleFor(T::HEAL, false, kOther, kPlayer).cvar == nullptr);
}

TEST_CASE("periodic and healing have their own rows", "[combattext]") {
    CHECK(std::string(ruleFor(T::PERIODIC_DAMAGE, true, kPlayer, kTarget).cvar)
          == "CombatLogPeriodicSpells");
    CHECK(std::string(ruleFor(T::PERIODIC_HEAL, true, kPlayer, kPlayer).cvar)
          == "CombatLogPeriodicSpells");
    CHECK(std::string(ruleFor(T::HEAL, true, kPlayer, kPlayer).cvar) == "CombatHealing");
    CHECK(std::string(ruleFor(T::CRIT_HEAL, true, kPlayer, kPlayer).cvar) == "CombatHealing");
}

TEST_CASE("effects are read against the unit they landed on", "[combattext]") {
    // On your target, and on anything else, are two different rows - and the
    // second is off in the real client, which is the whole reason the fallback
    // travels with the name.
    const auto onTarget = ruleFor(T::IMMUNE, true, kPlayer, kTarget);
    REQUIRE(onTarget.cvar != nullptr);
    CHECK(std::string(onTarget.cvar) == "fctSpellMechanics");
    CHECK(std::string(onTarget.fallback) == "1");

    const auto elsewhere = ruleFor(T::IMMUNE, true, kPlayer, kOther);
    REQUIRE(elsewhere.cvar != nullptr);
    CHECK(std::string(elsewhere.cvar) == "fctSpellMechanicsOther");
    CHECK(std::string(elsewhere.fallback) == "0");

    // Who caused it does not enter into it.
    CHECK(std::string(ruleFor(T::RESIST, false, kOther, kTarget).cvar) == "fctSpellMechanics");
}

TEST_CASE("lines these six rows do not describe are left alone", "[combattext]") {
    CHECK(ruleFor(T::XP_GAIN, true, kPlayer, kPlayer).cvar == nullptr);
    CHECK(ruleFor(T::HONOR_GAIN, true, kPlayer, kPlayer).cvar == nullptr);
    CHECK(ruleFor(T::ENERGIZE, true, kPlayer, kPlayer).cvar == nullptr);
    CHECK(ruleFor(T::MISS, true, kPlayer, kTarget).cvar == nullptr);
}
