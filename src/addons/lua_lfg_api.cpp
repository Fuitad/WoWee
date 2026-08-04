// lua_lfg_api.cpp — the dungeon finder.
//
// Split out rather than added to a neighbour because it is a whole panel's
// worth of surface: lfdframe.lua, lfgframe.lua and lfrframe.lua between them
// called forty-one globals and not one of them was bound. The LFD micro button
// sits on the main bar, which is handed over by default, so clicking it raised
// on the first line of LFDParentFrame's OnShow.
//
// What is real here and what is not:
//
//   * The dungeon list is real, read from LFGDungeons.dbc through
//     GameHandler::getLfgDungeons(). Names, levels, groups, difficulty and
//     faction all come from the file.
//   * The queue is real — joining, leaving, roles and the queued list go
//     through the LFG verbs this client has had all along.
//   * Locks, rewards, the proposal's per-member detail, the boot vote's
//     detail, party backfill and the raid browser's search are NOT modelled.
//     They answer empty, and each one says why where it is bound.
//
// An empty answer is chosen deliberately over a plausible one. Every caller
// here guards, and a fabricated reward or lock reads as fact.
#include "addons/lua_api_helpers.hpp"
#include "addons/lua_engine.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::addons {

namespace {

/// FrameXML numbers a category header with the negative of its group id, and
/// keeps headers and dungeons in one key space — LFGIsIDHeader is `id < 0`,
/// while LFGListUpdateHeaderEnabledAndLockedStates indexes the same lists by
/// the groupID it read out of the info table. So the groupID reported for a
/// dungeon has to be the header's id, not the raw number from the DBC.
int headerIdFor(uint32_t groupId) { return -static_cast<int>(groupId); }

/// LFGDungeons.dbc has no name for a category. The real client carries these
/// itself; they are the standard names for the groups the file actually uses,
/// and group 10 is absent from the data rather than omitted here.
const char* groupName(uint32_t groupId) {
    switch (groupId) {
        case 1:  return "Classic Dungeons";
        case 2:  return "Burning Crusade Dungeons";
        case 3:  return "Burning Crusade Heroic Dungeons";
        case 4:  return "Wrath of the Lich King Dungeons";
        case 5:  return "Wrath of the Lich King Heroic Dungeons";
        case 6:  return "Classic Raids";
        case 7:  return "Burning Crusade Raids";
        case 8:  return "Wrath of the Lich King Raids (10)";
        case 9:  return "Wrath of the Lich King Raids (25)";
        case 11: return "Holiday Dungeons";
        default: return "Dungeons";
    }
}

/// How many the group is built for. The file does not say, so it comes from
/// what the group is: five for a dungeon, and the two Wrath raid groups are
/// split ten from twenty-five, which is the whole reason they are two groups.
int maxPlayersFor(const game::LfgDungeon& d) {
    if (d.typeId == static_cast<uint32_t>(game::LfgTypeId::Raid)) {
        return d.groupId == 9 ? 25 : 10;
    }
    return 5;
}

/// The dungeon finder lists dungeons and the raid browser lists raids, so each
/// asks for its own half of the same file.
bool isDungeonSide(const game::LfgDungeon& d) {
    return d.typeId == static_cast<uint32_t>(game::LfgTypeId::Dungeon) ||
           d.typeId == static_cast<uint32_t>(game::LfgTypeId::Heroic) ||
           d.typeId == static_cast<uint32_t>(game::LfgTypeId::Random);
}
bool isRaidSide(const game::LfgDungeon& d) {
    return d.typeId == static_cast<uint32_t>(game::LfgTypeId::Raid);
}

/// Which dungeons the player has ticked, and which headers are folded up.
/// Both are the panel's own state — FrameXML says so in a comment, "we
/// maintain this list in Lua" — and the only reason they are here is that it
/// asks the client for the starting value.
std::unordered_map<int, bool>& enabledDungeons() {
    static std::unordered_map<int, bool> enabled;
    return enabled;
}
std::unordered_map<int, bool>& collapsedHeaders() {
    static std::unordered_map<int, bool> collapsed;
    return collapsed;
}

int luaReturnTrue(lua_State* L)    { lua_pushboolean(L, 1); return 1; }
int luaReturnNothing(lua_State* L) { (void)L; return 0; }

/// The roles the player has offered. lfgSetRoles sends them and keeps nothing,
/// so the panel reading its own checkboxes back has to read them from here.
uint8_t& offeredRoles() {
    static uint8_t roles = 0x01;   // "player" is always set
    return roles;
}

/// Build the ordered id list one side of the panel shows: a header, then the
/// dungeons under it, then the next header.
void pushChoiceOrder(lua_State* L, game::GameHandler* gh, bool dungeonSide) {
    lua_newtable(L);
    if (!gh) return;
    int n = 0;
    uint32_t lastGroup = 0;
    for (const auto& d : gh->getLfgDungeons()) {
        // groupId 0 is a row that belongs under no header — the old
        // per-zone entries — and LFDList_DefaultFilterFunction drops them
        // anyway. Leaving them out here keeps the list honest either way.
        if (d.groupId == 0) continue;
        if (dungeonSide ? !isDungeonSide(d) : !isRaidSide(d)) continue;
        if (d.groupId != lastGroup) {
            lastGroup = d.groupId;
            lua_pushinteger(L, headerIdFor(d.groupId));
            lua_rawseti(L, -2, ++n);
        }
        lua_pushinteger(L, static_cast<lua_Integer>(d.id));
        lua_rawseti(L, -2, ++n);
    }
}

/// One row of the info table: the twelve values LFG_RETURN_VALUES names, in
/// its order. Getting this order wrong is invisible — every read is by index
/// through that table — so it is written out here field by field.
void pushInfoRow(lua_State* L, const char* name, int typeId, int minLevel,
                 int maxLevel, int recLevel, int minRecLevel, int maxRecLevel,
                 int expansion, int groupId, const char* texture,
                 int difficulty, int maxPlayers) {
    lua_newtable(L);
    auto set = [&](int idx, auto push) { push(); lua_rawseti(L, -2, idx); };
    set(1,  [&]{ lua_pushstring(L, name); });
    set(2,  [&]{ lua_pushinteger(L, typeId); });
    set(3,  [&]{ lua_pushinteger(L, minLevel); });
    set(4,  [&]{ lua_pushinteger(L, maxLevel); });
    set(5,  [&]{ lua_pushinteger(L, recLevel); });
    set(6,  [&]{ lua_pushinteger(L, minRecLevel); });
    set(7,  [&]{ lua_pushinteger(L, maxRecLevel); });
    set(8,  [&]{ lua_pushinteger(L, expansion); });
    set(9,  [&]{ lua_pushinteger(L, groupId); });
    set(10, [&]{ lua_pushstring(L, texture); });
    set(11, [&]{ lua_pushinteger(L, difficulty); });
    set(12, [&]{ lua_pushinteger(L, maxPlayers); });
}

}  // namespace

void registerLfgLuaAPI(lua_State* L) {
    static const std::pair<const char*, lua_CFunction> api[] = {

    // ---- The catalogue ----

    // GetLFDChoiceInfo(t) / GetLFRChoiceInfo — every listable row keyed by id,
    // headers included. FrameXML calls this once and keeps the result: "this
    // will never change (without a patch)".
    {"GetLFDChoiceInfo", [](lua_State* L) -> int {
        auto* gh = getGameHandler(L);
        lua_newtable(L);
        if (!gh) return 1;
        std::vector<uint32_t> groupsSeen;
        for (const auto& d : gh->getLfgDungeons()) {
            if (d.groupId == 0) continue;
            if (std::find(groupsSeen.begin(), groupsSeen.end(), d.groupId) == groupsSeen.end()) {
                groupsSeen.push_back(d.groupId);
                // The header's own row. Its groupID is itself, which is how
                // LFDList_SetHeaderCollapsed matches children to the header
                // that was clicked.
                pushInfoRow(L, groupName(d.groupId), 0, 0, 0, 0, 0, 0,
                            static_cast<int>(d.expansion), headerIdFor(d.groupId),
                            "", 0, 0);
                lua_rawseti(L, -2, headerIdFor(d.groupId));
            }
            pushInfoRow(L, d.name.c_str(), static_cast<int>(d.typeId),
                        static_cast<int>(d.minLevel), static_cast<int>(d.maxLevel),
                        static_cast<int>(d.recLevel), static_cast<int>(d.minRecLevel),
                        static_cast<int>(d.maxRecLevel), static_cast<int>(d.expansion),
                        headerIdFor(d.groupId), d.texture.c_str(),
                        static_cast<int>(d.difficulty), maxPlayersFor(d));
            lua_rawseti(L, -2, static_cast<lua_Integer>(d.id));
        }
        return 1;
    }},

    {"GetLFDChoiceOrder", [](lua_State* L) -> int {
        pushChoiceOrder(L, getGameHandler(L), /*dungeonSide=*/true);
        return 1;
    }},
    {"GetLFRChoiceOrder", [](lua_State* L) -> int {
        pushChoiceOrder(L, getGameHandler(L), /*dungeonSide=*/false);
        return 1;
    }},

    // ---- The three state tables the panel keeps ----
    //
    // Returned as tables keyed by id. FrameXML maintains them itself after the
    // first read; these are the starting values.

    {"GetLFDChoiceEnabledState", [](lua_State* L) -> int {
        lua_newtable(L);
        for (const auto& [id, on] : enabledDungeons()) {
            lua_pushboolean(L, on ? 1 : 0);
            lua_rawseti(L, -2, id);
        }
        return 1;
    }},
    {"GetLFDChoiceCollapseState", [](lua_State* L) -> int {
        lua_newtable(L);
        for (const auto& [id, folded] : collapsedHeaders()) {
            lua_pushboolean(L, folded ? 1 : 0);
            lua_rawseti(L, -2, id);
        }
        return 1;
    }},
    // Which dungeons the server will not let this character queue for, and
    // why. The server sends that in SMSG_LFG_PLAYER_INFO and this client does
    // not keep it, so nothing is locked. The list is read as
    // `lockList[dungeonID]` and an absent entry is "not locked", which is the
    // permissive answer — the filter's own comment allows for it: "if the
    // server tells us we can join, who are we to complain".
    {"GetLFDChoiceLockedState", [](lua_State* L) -> int { lua_newtable(L); return 1; }},
    {"GetLFDLockInfo",          [](lua_State* L) -> int { return luaReturnNil(L); }},

    {"SetLFGDungeonEnabled", [](lua_State* L) -> int {
        const int id = static_cast<int>(luaL_optinteger(L, 1, 0));
        if (id != 0) enabledDungeons()[id] = lua_toboolean(L, 2) != 0;
        return 0;
    }},
    {"SetLFGHeaderCollapsed", [](lua_State* L) -> int {
        const int id = static_cast<int>(luaL_optinteger(L, 1, 0));
        if (id != 0) collapsedHeaders()[id] = lua_toboolean(L, 2) != 0;
        return 0;
    }},
    {"ClearAllLFGDungeons", [](lua_State* L) -> int {
        (void)L;
        enabledDungeons().clear();
        return 0;
    }},
    // Picking the single dungeon a "specific" queue is for.
    {"SetLFGDungeon", [](lua_State* L) -> int {
        const int id = static_cast<int>(luaL_optinteger(L, 1, 0));
        enabledDungeons().clear();
        if (id != 0) enabledDungeons()[id] = true;
        return 0;
    }},

    // ---- Roles ----

    // GetLFGRoles() → leader, tank, healer, damage. The client keeps the three
    // role flags; nobody here is a leader of an LFG group that does not exist.
    {"GetLFGRoles", [](lua_State* L) -> int {
        const uint8_t roles = offeredRoles();
        lua_pushboolean(L, 0);
        lua_pushboolean(L, (roles & 0x02) ? 1 : 0);   // tank
        lua_pushboolean(L, (roles & 0x04) ? 1 : 0);   // healer
        lua_pushboolean(L, (roles & 0x08) ? 1 : 0);   // damage
        return 4;
    }},
    {"SetLFGRoles", [](lua_State* L) -> int {
        auto* gh = getGameHandler(L);
        if (!gh) return 0;
        // arg 1 is leader, which the server does not take here.
        uint8_t roles = 0x01;                                   // always "player"
        if (lua_toboolean(L, 2)) roles |= 0x02;
        if (lua_toboolean(L, 3)) roles |= 0x04;
        if (lua_toboolean(L, 4)) roles |= 0x08;
        offeredRoles() = roles;
        gh->lfgSetRoles(roles);
        return 0;
    }},
    // Which roles this character may offer. Every class can deal damage; tank
    // and healer are gated by class rather than by spec, because this client
    // does not read the active tree here and offering too much is a queue that
    // is declined rather than a lie the player cannot see.
    {"GetAvailableRoles", [](lua_State* L) -> int {
        auto* gh = getGameHandler(L);
        const uint8_t cls = gh ? gh->getPlayerClass() : 0;
        // WARRIOR 1, PALADIN 2, PRIEST 5, SHAMAN 7, DRUID 11, DEATH KNIGHT 6
        const bool tank   = cls == 1 || cls == 2 || cls == 11 || cls == 6;
        const bool healer = cls == 2 || cls == 5 || cls == 7 || cls == 11;
        lua_pushboolean(L, tank ? 1 : 0);
        lua_pushboolean(L, healer ? 1 : 0);
        lua_pushboolean(L, 1);
        return 3;
    }},
    // The role check a party leader starts. Answering it is CMSG_LFG_SET_ROLES,
    // which SetLFGRoles above already sends, so this is the confirmation and
    // has nothing left to send.
    {"CompleteLFGRoleCheck",  luaReturnNothing},
    {"GetLFGRoleUpdateSlot",  [](lua_State* L) -> int { return luaReturnNil(L); }},

    // ---- The queue ----

    {"JoinLFG", [](lua_State* L) -> int {
        auto* gh = getGameHandler(L);
        if (!gh) return 0;
        // Whatever is ticked. The server takes one dungeon per join here, so
        // the first ticked entry is the one queued for — a multi-select queue
        // needs CMSG_LFG_JOIN to carry a list, which lfgJoin does not.
        for (const auto& [id, on] : enabledDungeons()) {
            if (on && id > 0) {
                gh->lfgJoin(static_cast<uint32_t>(id), offeredRoles());
                break;
            }
        }
        return 0;
    }},
    {"GetLFGQueuedList", [](lua_State* L) -> int {
        auto* gh = getGameHandler(L);
        lua_newtable(L);
        if (gh && gh->isLfgQueued()) {
            const uint32_t id = gh->getLfgDungeonId();
            if (id != 0) {
                lua_pushboolean(L, 1);
                lua_rawseti(L, -2, static_cast<lua_Integer>(id));
            }
        }
        return 1;
    }},
    // Whether a row can be queued for at all. Nothing is locked here — see
    // GetLFDChoiceLockedState — so the answer is yes and the server decides.
    {"IsLFGDungeonJoinable", luaReturnTrue},
    // How long the queue has been running and how it is filling. The client
    // keeps the time in queue and nothing else, and the average waits are what
    // the panel prints beside each role; a made-up wait is worse than a blank.
    {"GetLFGQueueStats", [](lua_State* L) -> int { return luaReturnNil(L); }},

    // ---- Detail this client does not keep ----
    //
    // Each of these is a real feature answered empty, not a feature that does
    // not exist. Named separately so a later pass can see what is left.

    // The proposal's per-member and per-encounter breakdown. The client tracks
    // the proposal id and answers it; who else is in it, and which bosses are
    // already down, arrive in parts of SMSG_LFG_PROPOSAL_UPDATE it does not
    // parse.
    {"GetLFGProposalMember",    [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"GetLFGProposalEncounter", [](lua_State* L) -> int { return luaReturnNil(L); }},
    // The boot vote. getLfgBootVotes and friends have the counts; the name of
    // who is being voted on and the reason given are not parsed.
    {"GetLFGBootProposal",      [](lua_State* L) -> int { return luaReturnNil(L); }},
    // What a random dungeon pays out the first time each day. Needs the
    // reward block of SMSG_LFG_PLAYER_INFO, which is not read.
    {"GetLFGDungeonRewards",     [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"GetLFGDungeonRewardInfo",  [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"GetLFGRandomDungeonInfo",  [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"GetRandomDungeonBestChoice", luaReturnZero},
    // GetLFGDungeonInfo(id) — the same twelve values as a row of the info
    // table, asked one at a time. Answered from the same catalogue.
    {"GetLFGDungeonInfo", [](lua_State* L) -> int {
        auto* gh = getGameHandler(L);
        const uint32_t id = static_cast<uint32_t>(luaL_optinteger(L, 1, 0));
        if (!gh || id == 0) return luaReturnNil(L);
        for (const auto& d : gh->getLfgDungeons()) {
            if (d.id != id) continue;
            lua_pushstring(L, d.name.c_str());
            lua_pushinteger(L, static_cast<lua_Integer>(d.typeId));
            lua_pushinteger(L, static_cast<lua_Integer>(d.minLevel));
            lua_pushinteger(L, static_cast<lua_Integer>(d.maxLevel));
            lua_pushinteger(L, static_cast<lua_Integer>(d.recLevel));
            lua_pushinteger(L, static_cast<lua_Integer>(d.minRecLevel));
            lua_pushinteger(L, static_cast<lua_Integer>(d.maxRecLevel));
            lua_pushinteger(L, static_cast<lua_Integer>(d.expansion));
            lua_pushinteger(L, headerIdFor(d.groupId));
            lua_pushstring(L, d.texture.c_str());
            lua_pushinteger(L, static_cast<lua_Integer>(d.difficulty));
            lua_pushinteger(L, maxPlayersFor(d));
            return 12;
        }
        return luaReturnNil(L);
    }},
    // Per-boss lockouts inside a raid the character is saved to. The saved
    // instance list has no encounter breakdown in it.
    {"GetInstanceLockTimeRemainingEncounter", [](lua_State* L) -> int { return luaReturnNil(L); }},
    // Whether the character is sitting out a deserter or random-dungeon
    // cooldown. Both are auras; this client does not check for them, and
    // answering yes would grey out a queue the server would have allowed.
    {"UnitHasLFGDeserter",       luaReturnFalse},
    {"UnitHasLFGRandomCooldown", luaReturnFalse},
    // Filling an empty slot in a party already inside. Needs the party's own
    // queue state, which is not tracked.
    {"CanPartyLFGBackfill",      luaReturnFalse},
    {"GetPartyLFGBackfillInfo",  [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"PartyLFGStartBackfill",    luaReturnNothing},
    // A note shown beside the queue. Nothing carries it.
    {"SetLFGComment",            luaReturnNothing},

    // ---- The raid browser ----
    //
    // A separate system from the queue: it searches for groups already forming
    // rather than building one. None of it is implemented, and the counts
    // answer zero so every loop over results runs zero times.
    {"SearchLFGGetNumResults",     luaReturnZero},
    {"SearchLFGGetResults",        [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"SearchLFGGetPartyResults",   [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"SearchLFGGetEncounterResults", [](lua_State* L) -> int { return luaReturnNil(L); }},
    {"SearchLFGGetJoinedID",       luaReturnZero},
    {"SearchLFGJoin",              luaReturnNothing},
    {"SearchLFGLeave",             luaReturnNothing},
    };

    for (const auto& [name, func] : api) {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
    }
}

}  // namespace wowee::addons
