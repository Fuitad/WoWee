#pragma once

/**
 * item_text.hpp — the words an item's and a spell's numbers are displayed with.
 *
 * Kept apart from world_packets.hpp so a tooltip can ask for a stat's name
 * without pulling in every packet structure in the game. Four things render item
 * tooltips — this client's bag, the chat item link, the one built for FrameXML,
 * and a shim written in Lua — and each of them had its own copy of these tables
 * until the copies were found to disagree.
 */

#include <cstdint>
#include <string>

namespace wowee {
namespace game {

/// The word an item's spell line starts with — "Use", "Equip", "Chance on Hit".
///
/// Three tooltips render this and their tables disagreed. The chat item link
/// called trigger 5 "Teaches" where the other two call it "Use", and WoW writes
/// a recipe as "Use: Teaches you how to make..." — the word is Use and the
/// teaching is in the spell's own description. The chat link also had no entry
/// for triggers 4 and 6 and skipped the line entirely, so an item whose spell
/// fires on use with no delay showed no spell at all there.
///
/// Null for a trigger with nothing to say. The values are the same in Classic,
/// TBC and WotLK.
inline const char* itemSpellTriggerText(uint32_t spellTrigger) {
    switch (spellTrigger) {
        case 0: return "Use";            // on use
        case 1: return "Equip";          // on equip
        case 2: return "Chance on Hit";  // proc on melee hit
        case 4: return "Use";            // soulstone, still a use
        case 5: return "Use";            // on use, no delay
        case 6: return "Use";            // learn spell — recipe or pattern
        default: return nullptr;
    }
}

/// What a spell's powerType is called, or null for one with no name.
///
/// Four places wrote this out: the combat log twice, the spellbook, and the
/// tooltip built for FrameXML. The spellbook's copy had Focus at 4, which is
/// Happiness — so a spell costing Focus fell through to "Mana" and one costing
/// Happiness said "Focus".
///
/// 5 is the death knight's runes, which are drawn as runes rather than written
/// as a number, so it has no word here. 5 and 6 exist only from WotLK; naming
/// them costs the earlier expansions nothing, because their servers never send
/// them.
inline const char* powerTypeName(uint32_t powerType) {
    switch (powerType) {
        case 0: return "Mana";
        case 1: return "Rage";
        case 2: return "Focus";
        case 3: return "Energy";
        case 4: return "Happiness";
        case 6: return "Runic Power";
        default: return nullptr;
    }
}

/// What an item's bindType says, or null for one that binds to nobody.
///
/// Four places render this line — this client's own bag tooltip, the chat item
/// link, the tooltip built for FrameXML, and a fourth copy written in Lua inside
/// the tooltip shim. The three in C++ share this now. They did not all agree:
/// two knew that 4 is a quest item and one did not, so a quest item's tooltip
/// said nothing about being one, depending on which tooltip you were looking at.
///
/// The colour stays with each renderer. They genuinely differ — one draws this
/// line gold and one draws it dimmed — and that is a decision about a tooltip,
/// not about what bindType 4 means.
inline const char* itemBindText(uint32_t bindType) {
    switch (bindType) {
        case 1: return "Binds when picked up";
        case 2: return "Binds when equipped";
        case 3: return "Binds when used";
        case 4: return "Quest Item";
        default: return nullptr;
    }
}

/// The name of an ItemQueryResponseData::ExtraStat type, or null for one there
/// is nothing to say about.
///
/// Shared because two tooltips read it — this client's own bag tooltip and the
/// one FrameXML asks for through GameTooltip:SetBagItem — and a second copy of
/// this table would drift the first time a rating was added to one of them.
/// Several ids map to the same words on purpose: 16, 17, 18 and 31 are melee,
/// ranged, spell and generic hit, and WoW writes all four as "Hit Rating".
inline const char* itemStatName(uint32_t statType) {
    switch (statType) {
        case 0:  return "Mana";
        case 1:  return "Health";
        case 12: return "Defense Rating";
        case 13: return "Dodge Rating";
        case 14: return "Parry Rating";
        case 15: return "Block Rating";
        case 16: case 17: case 18: case 31: return "Hit Rating";
        case 19: case 20: case 21: case 32: return "Crit Rating";
        case 28: case 29: case 30: case 36: return "Haste Rating";
        case 35: return "Resilience";
        case 37: return "Expertise Rating";
        case 38: return "Attack Power";
        case 39: return "Ranged Attack Power";
        case 41: return "Healing Power";
        case 42: return "Spell Damage";
        case 43: return "Mana per 5 sec";
        case 44: return "Armor Penetration";
        case 45: return "Spell Power";
        case 46: return "Health per 5 sec";
        case 47: return "Spell Penetration";
        case 48: return "Block Value";
        default: return nullptr;
    }
}


/// An item's quality colour as an eight-digit hex string, alpha first.
///
/// The same eight colours the interface uses, and the form a link's |c escape
/// wants. Two tables carried these — one with the alpha prefix and one without
/// — and they had already disagreed once: heirloom was 00ccff in the second,
/// which is a later expansion's token colour and not a quality 3.3.5 has, so
/// an heirloom link came out cyan.
inline const char* itemQualityColorHex(uint32_t quality) {
    static constexpr const char* kByQuality[] = {
        "ff9d9d9d",  // poor
        "ffffffff",  // common
        "ff1eff00",  // uncommon
        "ff0070dd",  // rare
        "ffa335ee",  // epic
        "ffff8000",  // legendary
        "ffe6cc80",  // artifact
        "ffe6cc80",  // heirloom — the same gold as an artifact
    };
    return quality < 8 ? kByQuality[quality] : "ffffffff";
}

/// A chat hyperlink for an item, as 3.3.5a writes one.
///
/// Nine fields after "item:": the id, then enchant, four gems, suffix, unique
/// id and level. Six places built this by hand and they did not agree — the
/// three on the Lua side wrote eight, one short, so a link handed to an addon
/// by GetContainerItemLink had a different shape from one produced by
/// shift-clicking a quest reward.
///
/// Nothing visibly breaks today: this client's own two parsers read the id and
/// stop at the first colon, and FrameXML hands the whole link back to
/// SetHyperlink rather than splitting it. It is a difference waiting for the
/// first thing that does split it.
inline std::string itemChatLink(uint32_t itemId, uint32_t quality, const std::string& name) {
    return std::string("|c") + itemQualityColorHex(quality) + "|Hitem:" +
           std::to_string(itemId) + ":0:0:0:0:0:0:0:0|h[" + name + "]|h|r";
}

}  // namespace game
}  // namespace wowee
