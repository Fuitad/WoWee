#pragma once

/// The stance, form and presence spells a class switches between.
///
/// Five classes have a stance bar, and two places need the same answer: the
/// bar itself, and the Ctrl+1..Ctrl+8 bindings that press its buttons. Both
/// used to carry their own copy of all five tables and the class switch, with
/// a comment in one of them saying it had to keep "the same slot ordering as
/// renderStanceBar". A reorder or an added form in one copy moves what
/// Ctrl+N does without touching the bar it is supposed to be pressing.
///
/// The order is display order, which is also the order the bindings count in.

#include <cstdint>
#include <iterator>

#include "game/character.hpp"

namespace wowee::game {

/// A class's stance spells, in display order. `count` is zero for the classes
/// that have no stance bar.
struct StanceSpells {
    const uint32_t* spells = nullptr;
    int count = 0;
};

namespace detail {

inline constexpr uint32_t kWarriorStances[] = {
    2457,   // Battle Stance
    71,     // Defensive Stance
    2458,   // Berserker Stance
};

inline constexpr uint32_t kDeathKnightPresences[] = {
    48266,  // Blood Presence
    48263,  // Frost Presence
    48265,  // Unholy Presence
};

inline constexpr uint32_t kDruidForms[] = {
    5487,   // Bear Form
    9634,   // Dire Bear Form
    768,    // Cat Form
    783,    // Travel Form
    1066,   // Aquatic Form
    24858,  // Moonkin Form
    33891,  // Tree of Life
    33943,  // Flight Form
    40120,  // Swift Flight Form
};

inline constexpr uint32_t kRogueForms[] = {
    1784,   // Stealth
};

inline constexpr uint32_t kPriestForms[] = {
    15473,  // Shadowform
};

}  // namespace detail

/// The stance spells for a class id as it arrives from the server.
///
/// The count comes from the table rather than being written beside it, so
/// adding a form is one edit rather than two that have to agree.
inline StanceSpells stanceSpellsForClass(uint8_t playerClass) {
    switch (static_cast<Class>(playerClass)) {
        case Class::WARRIOR:
            return {detail::kWarriorStances,
                    static_cast<int>(std::size(detail::kWarriorStances))};
        case Class::DEATH_KNIGHT:
            return {detail::kDeathKnightPresences,
                    static_cast<int>(std::size(detail::kDeathKnightPresences))};
        case Class::DRUID:
            return {detail::kDruidForms,
                    static_cast<int>(std::size(detail::kDruidForms))};
        case Class::ROGUE:
            return {detail::kRogueForms,
                    static_cast<int>(std::size(detail::kRogueForms))};
        case Class::PRIEST:
            return {detail::kPriestForms,
                    static_cast<int>(std::size(detail::kPriestForms))};
        default:
            return {};
    }
}

}  // namespace wowee::game
