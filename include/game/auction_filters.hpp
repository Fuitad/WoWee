// auction_filters.hpp — the auction house's category tree, in one place.
//
// Both interfaces ask the same question of the same server field, so they ask
// it from the same table. This client's own auction window had these three
// arrays inside the function that drew them; FrameXML needs the identical
// lists, because GetAuctionItemClasses is what fills its filter column and the
// index it hands back to QueryAuctionItems has to mean the same thing on the
// way in as it did on the way out. Two copies of a list whose *positions* are
// the protocol is the shape that goes wrong quietly: the second category would
// search for the first one's items and nothing would say so.
//
// Row zero of every list is "All", and that is load-bearing rather than
// decorative. FrameXML's filter indices are 1-based and it passes nil when
// nothing is picked, which arrives as zero — so an unselected filter lands on
// row zero and reads as "any" without a special case. Before this, nil arrived
// as zero and went to the wire as zero, which is a real item class: searching
// the auction house by name alone returned consumables and nothing else.
//
// The labels are English literals, as they were in the window. ItemClass.dbc
// field 3 and ItemSubClass.dbc field 10 carry the localised names for the same
// ids (verified: class 0 is Consumable, class 1 subclass 2 is Herb Bag), and
// are where a localisation pass should read them from.
#pragma once

#include <cstdint>

namespace wowee {
namespace game {

/// 0xFFFFFFFF is what CMSG_AUCTION_LIST_ITEMS means by "do not filter on this".
/// AzerothCore's searcher compares `proto->Class != itemClass` for anything
/// else, so any other value is a filter — including zero.
inline constexpr uint32_t kAuctionAny = 0xFFFFFFFFu;

struct AuctionClassFilter { const char* label; uint32_t classId; };
struct AuctionSubFilter   { const char* label; uint32_t subId; };
struct AuctionSlotFilter  { const char* label; uint32_t invType; };

// WoW 3.3.5a item class ids: 0=Consumable, 1=Container, 2=Weapon, 3=Gem,
// 4=Armor, 7=Trade Goods, 9=Recipe, 11=Quiver, 15=Miscellaneous.
inline constexpr AuctionClassFilter kAuctionClasses[] = {
    {"All",           kAuctionAny},
    {"Weapon",        2},
    {"Armor",         4},
    {"Container",     1},
    {"Consumable",    0},
    {"Trade Goods",   7},
    {"Gem",           3},
    {"Recipe",        9},
    {"Quiver",        11},
    {"Miscellaneous", 15},
};
inline constexpr int kNumAuctionClasses = 10;

inline constexpr AuctionSubFilter kAuctionWeaponSubs[] = {
    {"All", kAuctionAny}, {"Axe (1H)", 0}, {"Axe (2H)", 1}, {"Bow", 2},
    {"Gun", 3}, {"Mace (1H)", 4}, {"Mace (2H)", 5}, {"Polearm", 6},
    {"Sword (1H)", 7}, {"Sword (2H)", 8}, {"Staff", 10},
    {"Fist Weapon", 13}, {"Dagger", 15}, {"Thrown", 16},
    {"Crossbow", 18}, {"Wand", 19},
};
inline constexpr int kNumAuctionWeaponSubs = 16;

inline constexpr AuctionSubFilter kAuctionArmorSubs[] = {
    {"All", kAuctionAny}, {"Cloth", 1}, {"Leather", 2}, {"Mail", 3},
    {"Plate", 4}, {"Shield", 6}, {"Miscellaneous", 0},
};
inline constexpr int kNumAuctionArmorSubs = 7;

/// Equipment-slot ids, carried in the auctionSlotID field of the same request.
inline constexpr AuctionSlotFilter kAuctionSlots[] = {
    {"All Slots", kAuctionAny}, {"Head", 1}, {"Neck", 2}, {"Shoulder", 3},
    {"Chest", 5}, {"Waist", 6}, {"Legs", 7}, {"Feet", 8}, {"Wrist", 9},
    {"Hands", 10}, {"Finger", 11}, {"Trinket", 12}, {"Back", 16},
    {"One-Hand", 13}, {"Two-Hand", 17}, {"Main Hand", 21}, {"Off Hand", 22},
    {"Ranged", 26}, {"Shield", 14}, {"Held Off-hand", 23}, {"Relic", 28},
};
inline constexpr int kNumAuctionSlots = 21;

/// The subclass list for a class id, or null when that class has none offered.
/// Only weapons and armour are divided here, which is what the window has
/// always shown; every other class searches whole.
inline const AuctionSubFilter* auctionSubsFor(uint32_t classId, int& count) {
    if (classId == 2) { count = kNumAuctionWeaponSubs; return kAuctionWeaponSubs; }
    if (classId == 4) { count = kNumAuctionArmorSubs;  return kAuctionArmorSubs;  }
    count = 0;
    return nullptr;
}

/// The three lengths an auction may run for, in minutes, which is what the
/// wire field carries: AzerothCore multiplies it by sixty and then accepts
/// only 1, 2 or 4 times MIN_AUCTION_TIME (12 hours), rejecting the request
/// outright otherwise. FrameXML's duration dropdown does not deal in minutes —
/// its values are 1, 2 and 3 for the same three lengths — so a posting made
/// through it asked for one, two or three minutes and the server dropped it
/// without a word.
inline constexpr uint32_t kAuctionDurationMinutes[3] = {720, 1440, 2880};

/// Minutes for a duration written either way: the dropdown's 1..3, or minutes
/// already. Anything else falls to twelve hours, which is the shortest the
/// server will take and so the least costly guess.
inline constexpr uint32_t auctionDurationMinutes(uint32_t d) {
    if (d >= 1 && d <= 3) return kAuctionDurationMinutes[d - 1];
    for (uint32_t m : kAuctionDurationMinutes) if (d == m) return d;
    return kAuctionDurationMinutes[0];
}

} // namespace game
} // namespace wowee
