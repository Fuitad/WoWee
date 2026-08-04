#pragma once

#include <cstdint>
#include <cstddef>

namespace wowee {
namespace game {

/**
 * Battleground score presentation, keyed by map.
 *
 * The server sends world states as bare key/value pairs — SMSG_INIT_WORLD_STATES
 * carries no labels — so which key means "Alliance flags" and what it counts up
 * to is client knowledge. This table is that knowledge.
 *
 * It lives here rather than beside a renderer because two surfaces present the
 * same scores: this client's own heads-up display, and FrameXML's
 * WorldStateAlwaysUpFrame through GetWorldStateUIInfo. Keeping one table means
 * a new battleground is described once.
 */
struct BgScoreDef {
    uint32_t    mapId;
    const char* name;
    uint32_t    allianceKey;   // world state key for the Alliance value
    uint32_t    hordeKey;      // world state key for the Horde value
    uint32_t    maxKey;        // world state key for the maximum (0 = use hardcodedMax)
    uint32_t    hardcodedMax;  // used when maxKey is 0
    const char* unit;          // suffix label ("flags", "resources"); empty = no maximum shown
};

inline constexpr BgScoreDef kBgScoreDefs[] = {
    // Warsong Gulch: 3 flag captures wins
    { 489, "Warsong Gulch",          1581, 1582, 0,    3, "flags" },
    // Arathi Basin: 1600 resources wins
    { 529, "Arathi Basin",           1218, 1219, 0, 1600, "resources" },
    // Alterac Valley: reinforcements count down from 600 / 800 etc.
    {  30, "Alterac Valley",         1322, 1323, 0,  600, "reinforcements" },
    // Eye of the Storm: 1600 resources wins
    { 566, "Eye of the Storm",       2757, 2758, 0, 1600, "resources" },
    // Strand of the Ancients (WotLK)
    { 607, "Strand of the Ancients", 3476, 3477, 0,    4, "" },
    // Isle of Conquest (WotLK): reinforcements (300 default)
    { 628, "Isle of Conquest",       4221, 4222, 0,  300, "reinforcements" },
};

/// The definition for a map, or nullptr when the map is not a scored battleground.
inline const BgScoreDef* findBgScoreDef(uint32_t mapId) {
    for (const auto& d : kBgScoreDefs) {
        if (d.mapId == mapId) return &d;
    }
    return nullptr;
}

}  // namespace game
}  // namespace wowee
