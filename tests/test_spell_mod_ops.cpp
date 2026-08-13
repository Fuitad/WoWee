// The spell modifier op numbers, against the ones the server sends.
//
// SMSG_SET_FLAT_SPELL_MODIFIER and its percentage twin carry an op number, a
// group and a value. The op says which property of a spell a talent changes.
// Read it against the wrong table and a talent that shortens a cooldown is
// applied to something else, silently, because every op is a small integer and
// they are all valid.
//
// Ops 0 to 19 were right. From 20 the list was the modern one - Efficiency,
// MultipleValue, ResistPushback and the rest are Cataclysm names - grafted
// onto a 3.3.5 head, so op 22 was called ResistDispelChance when the server
// means a periodic effect by it, and dispel resistance is 28.
//
// Nothing consumed those ops, so nothing was visibly wrong. The trap was for
// whoever read one next.
//
// The oracle is SPELLMOD_* in AzerothCore's SpellDefines.h.
#include <catch_amalgamated.hpp>

#include "game/game_handler.hpp"

using Op = wowee::game::GameHandler::SpellModOp;

namespace {
constexpr int n(Op o) { return static_cast<int>(o); }
}  // namespace

TEST_CASE("the ops the client reads are where the server puts them",
          "[spell-mods]") {
    // The two the spellbook actually applies. Both were already right, and
    // both are worth pinning because a spell's displayed cost and cast time
    // come from them.
    CHECK(n(Op::CastingTime) == 10);
    CHECK(n(Op::Cost) == 14);
}

TEST_CASE("the whole table matches the server's", "[spell-mods]") {
    CHECK(n(Op::Damage) == 0);
    CHECK(n(Op::Duration) == 1);
    CHECK(n(Op::Threat) == 2);
    CHECK(n(Op::Effect1) == 3);
    CHECK(n(Op::Charges) == 4);
    CHECK(n(Op::Range) == 5);
    CHECK(n(Op::Radius) == 6);
    CHECK(n(Op::CritChance) == 7);
    CHECK(n(Op::AllEffects) == 8);
    CHECK(n(Op::NotLoseCastingTime) == 9);
    CHECK(n(Op::Cooldown) == 11);
    CHECK(n(Op::Effect2) == 12);
    CHECK(n(Op::IgnoreArmor) == 13);
    CHECK(n(Op::CritDamageBonus) == 15);
    CHECK(n(Op::ResistMissChance) == 16);
    CHECK(n(Op::JumpTargets) == 17);
    CHECK(n(Op::ChanceOfSuccess) == 18);
    CHECK(n(Op::ActivationTime) == 19);
}

TEST_CASE("the tail is the shipped version's, not a later one",
          "[spell-mods]") {
    // Where it had drifted. Each of these was a different number before.
    CHECK(n(Op::DamageMultiplier) == 20);
    CHECK(n(Op::GlobalCooldown) == 21);
    CHECK(n(Op::Dot) == 22);
    CHECK(n(Op::Effect3) == 23);
    CHECK(n(Op::BonusMultiplier) == 24);
    CHECK(n(Op::ProcPerMinute) == 26);
    CHECK(n(Op::ValueMultiplier) == 27);
    CHECK(n(Op::ResistDispelChance) == 28);
    CHECK(n(Op::CritDamageBonus2) == 29);
    CHECK(n(Op::SpellCostRefundOnFail) == 30);

    // 22 is a periodic effect, not dispel resistance. Naming them the other
    // way round is what the tail did.
    CHECK(n(Op::Dot) != n(Op::ResistDispelChance));
    CHECK(n(Op::ResistDispelChance) == 28);
}

TEST_CASE("the modifier array holds every op", "[spell-mods]") {
    // Modifiers are accumulated into a fixed array indexed by op, so an op at
    // or past the end writes outside it.
    CHECK(n(Op::SpellCostRefundOnFail) <
          wowee::game::GameHandler::SPELL_MOD_OP_COUNT);
    CHECK(wowee::game::GameHandler::SPELL_MOD_OP_COUNT == 32);
}
