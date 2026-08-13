#include "game/shapeshift_forms.hpp"

#include "game/protocol_constants.hpp"

namespace wowee::game {
namespace {

// The order is the order the bar shows them, which is the order the client
// has always shown them and the order every index refers to.
//
// Dire Bear is deliberately absent. It replaces Bear Form on the same button
// rather than adding one, so a druid who has learned it has two spells for one
// slot; the bar shows Bear and casting it casts whichever the server accepts.
const ShapeshiftForm kDruid[] = {
    {SPELL_BEAR_FORM,    1,  "Bear Form",         "Interface\\Icons\\Ability_Racial_BearForm"},
    {SPELL_AQUATIC_FORM, 2,  "Aquatic Form",      "Interface\\Icons\\Ability_Druid_AquaticForm"},
    {SPELL_CAT_FORM,     3,  "Cat Form",          "Interface\\Icons\\Ability_Druid_CatForm"},
    {SPELL_TRAVEL_FORM,  4,  "Travel Form",       "Interface\\Icons\\Ability_Druid_TravelForm"},
    {SPELL_MOONKIN_FORM, 31, "Moonkin Form",      "Interface\\Icons\\Spell_Nature_ForceOfNature"},
    {SPELL_TREE_OF_LIFE, 36, "Tree of Life",      "Interface\\Icons\\Ability_Druid_TreeofLife"},
    {SPELL_FLIGHT_FORM,  29, "Flight Form",       "Interface\\Icons\\Ability_Druid_FlightForm"},
    {SPELL_SWIFT_FLIGHT, 27, "Swift Flight Form", "Interface\\Icons\\Ability_Druid_FlightForm"},
};

const ShapeshiftForm kWarrior[] = {
    {SPELL_BATTLE_STANCE,    17, "Battle Stance",    "Interface\\Icons\\Ability_Warrior_OffensiveStance"},
    {SPELL_DEFENSIVE_STANCE, 18, "Defensive Stance", "Interface\\Icons\\Ability_Warrior_DefensiveStance"},
    {SPELL_BERSERKER_STANCE, 19, "Berserker Stance", "Interface\\Icons\\Ability_Racial_Avatar"},
};

const ShapeshiftForm kDeathKnight[] = {
    {SPELL_BLOOD_PRESENCE,  32, "Blood Presence",  "Interface\\Icons\\Spell_Deathknight_BloodPresence"},
    {SPELL_FROST_PRESENCE,  33, "Frost Presence",  "Interface\\Icons\\Spell_Deathknight_FrostPresence"},
    {SPELL_UNHOLY_PRESENCE, 34, "Unholy Presence", "Interface\\Icons\\Spell_Deathknight_UnholyPresence"},
};

const ShapeshiftForm kRogue[] = {
    {SPELL_STEALTH, 30, "Stealth", "Interface\\Icons\\Ability_Stealth"},
};

const ShapeshiftForm kPriest[] = {
    {SPELL_SHADOWFORM, 28, "Shadowform", "Interface\\Icons\\Spell_Shadow_Shadowform"},
};

}  // namespace

std::vector<ShapeshiftForm> allShapeshiftForms(uint8_t classId) {
    switch (classId) {
        case 1:  return {std::begin(kWarrior), std::end(kWarrior)};
        case 4:  return {std::begin(kRogue), std::end(kRogue)};
        case 5:  return {std::begin(kPriest), std::end(kPriest)};
        case 6:  return {std::begin(kDeathKnight), std::end(kDeathKnight)};
        case 11: return {std::begin(kDruid), std::end(kDruid)};
        default: return {};
    }
}

}  // namespace wowee::game
