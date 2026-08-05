// lua_quest_api.cpp — Quest log, skills, talents, glyphs, and achievements Lua API bindings.
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#include "addons/lua_api_helpers.hpp"
#include "addons/lua_engine.hpp"
#include "game/game_utils.hpp"
#include "game/packed_time.hpp"

#include <algorithm>
#include <vector>

namespace wowee::addons {

static int lua_GetNumQuestLogEntries(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    const auto& ql = gh->getQuestLog();
    lua_pushnumber(L, ql.size());  // numEntries
    lua_pushnumber(L, 0);          // numQuests (headers not tracked)
    return 2;
}

// GetQuestLogTitle(index) → title, level, suggestedGroup, isHeader, isCollapsed, isComplete, frequency, questID
// ---- The quest info panel's reward block ----
//
// Shared by the quest giver and the quest log, so a raise here takes both down.
// None of these appeared in a scan of either frame's own file, because the
// panel that draws the rewards is a third file they both pull in.

// GetQuestLogTimeLeft() → seconds left on the selected quest, or nil.
// Backed by the same PLAYER_QUEST_LOG expiry the tracker's timers read.
static int lua_GetQuestLogTimeLeft(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return luaReturnNil(L);
    const int sel = gh->getSelectedQuestLogIndex();
    const auto& ql = gh->getQuestLog();
    if (sel < 1 || sel > static_cast<int>(ql.size())) return luaReturnNil(L);
    const uint32_t questId = ql[static_cast<size_t>(sel) - 1].questId;
    for (const auto& t : gh->getQuestTimers()) {
        if (t.first == questId) { lua_pushnumber(L, t.second); return 1; }
    }
    return luaReturnNil(L);   // not a timed quest
}

// Whether the quest being looked at has been failed. Failure is not tracked —
// GetQuestLogTitle reports complete or not and nothing else — so no quest reads
// as failed rather than every quest reading as one.
static int lua_IsCurrentQuestFailed(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// The spell, title and faction rewards a quest can carry. The query response
// this client parses parses none of them, and each is asked behind `if ( ... )`
// before its block is drawn — so nil leaves the block out rather than drawing
// an empty one.
static int lua_GetQuestRewardSpell(lua_State* L) { (void)L; return luaReturnNil(L); }
static int lua_GetQuestRewardTitle(lua_State* L) { (void)L; return luaReturnNil(L); }
static int lua_ProcessQuestLogRewardFactions(lua_State* L) { (void)L; return 0; }
static int lua_GetQuestLogRewardFactionInfo(lua_State* L) { (void)L; return luaReturnNil(L); }

// GetFactionInfoByID(id) → name, description, standingId, barMin, barMax, barValue
//
// The same answer GetFactionInfo gives by position, found by faction id
// instead. The reputation list carries the id already.
static int lua_GetFactionInfoByID(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
    if (!gh || id == 0) return luaReturnNil(L);
    for (const auto& r : gh->getReputationList()) {
        if (r.factionId != id) continue;
        lua_pushstring(L, r.name.c_str());   // 1: name
        lua_pushnil(L);                      // 2: description
        lua_pushnil(L);                      // 3: standingId
        lua_pushnil(L);                      // 4: barMin
        lua_pushnil(L);                      // 5: barMax
        lua_pushnil(L);                      // 6: barValue
        return 6;
    }
    return luaReturnNil(L);
}

// ---- Quest watch ordering ----
//
// The watch list here is a set of quest ids, so its order is the quest log's
// order rather than one of its own. That is what these three say.

// GetQuestWatchIndex(questLogIndex) → where that quest sits in the watch list.
static int lua_GetQuestWatchIndex(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int idx = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || idx < 1) return luaReturnNil(L);
    const auto& ql = gh->getQuestLog();
    if (idx > static_cast<int>(ql.size())) return luaReturnNil(L);
    const uint32_t wanted = ql[static_cast<size_t>(idx) - 1].questId;
    int position = 0;
    for (const auto& q : ql) {
        if (!gh->isQuestTracked(q.questId)) continue;
        ++position;
        if (q.questId == wanted) { lua_pushnumber(L, position); return 1; }
    }
    return luaReturnNil(L);   // not watched
}

// SortQuestWatches() → whether the order changed.
//
// False, and that is the truthful answer rather than a shrug: the watch order
// follows the quest log, so there is never a separate order to sort. The
// caller reads it as "did anything move" and rebuilds the tracker when it did.
static int lua_SortQuestWatches(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// ShiftQuestWatches(from, to) — reorder the list by hand. Nothing is stored to
// reorder, so dragging a tracker entry leaves it where the log puts it.
static int lua_ShiftQuestWatches(lua_State* L) { (void)L; return 0; }

// GetQuestSortIndex(questLogIndex) → the header the quest sits under.
//
// Nil: this quest log has no headers — GetQuestLogTitle answers false for
// isHeader on every row — so there is no header index to give and nothing for
// the caller to expand.
static int lua_GetQuestSortIndex(lua_State* L) { (void)L; return luaReturnNil(L); }

// ---- Quest log special items ----
//
// Whether the quest item's target is close enough to use it on. Nothing here
// knows an item's range, and nil is the answer that hides the range indicator
// rather than colouring it wrongly — WatchFrameItem_OnUpdate takes the third
// branch and hides the count text.
static int lua_IsQuestLogSpecialItemInRange(lua_State* L) { (void)L; return luaReturnNil(L); }

// UseQuestLogSpecialItem(questLogIndex) — clicking that button.
//
// By slot rather than by item id, for the same reason UseContainerItem is:
// searching by id can find a different stack of the same thing.
static int lua_UseQuestLogSpecialItem(lua_State* L);

// GetQuestLogSpecialItemCooldown(index) → start, duration, enable.
// Enable is one, not zero: zero means the cooldown swipe is switched off, and
// the caller feeds all three straight to CooldownFrame_SetTimer.
static int lua_GetQuestLogSpecialItemCooldown(lua_State* L) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1);
    return 3;
}

// GetQuestTimers() — the seconds left on each timed quest, as separate values.
//
// QuestTimerFrame counts them with select("#", ...) and reads them with
// select(i, ...), so the count is the return count. Returning nothing is the
// honest answer for a log with no timed quest in it, and the frame hides
// itself — which it could not do while this was missing, because the OnEvent
// that calls it runs on every QUEST_LOG_UPDATE.
static int lua_GetQuestTimers(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    const auto timers = gh->getQuestTimers();
    for (const auto& t : timers) lua_pushnumber(L, t.second);
    return static_cast<int>(timers.size());
}

// GetQuestIndexForTimer(i) — the quest log index the i-th timer belongs to,
// so clicking a timer row selects its quest.
static int lua_GetQuestIndexForTimer(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int idx = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || idx < 1) return luaReturnNil(L);
    const auto timers = gh->getQuestTimers();
    if (idx > static_cast<int>(timers.size())) return luaReturnNil(L);
    const uint32_t questId = timers[static_cast<size_t>(idx) - 1].first;
    const auto& ql = gh->getQuestLog();
    for (size_t i = 0; i < ql.size(); ++i) {
        if (ql[i].questId == questId) { lua_pushnumber(L, static_cast<double>(i + 1)); return 1; }
    }
    return luaReturnNil(L);
}

static int lua_GetQuestLogTitle(lua_State* L) {
    auto* gh = getGameHandler(L);
    // optnumber, not checknumber: FrameXML walks the quest log with an index
    // that can be nil before anything has been selected, and raising there
    // loses whatever asked rather than answering that there is no such quest.
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || index < 1) { return luaReturnNil(L); }
    const auto& ql = gh->getQuestLog();
    if (index > static_cast<int>(ql.size())) { return luaReturnNil(L); }
    const auto& q = ql[index - 1];  // 1-based
    // The client's ten, in its order:
    //
    //   title, level, questTag, suggestedGroup, isHeader, isCollapsed,
    //   isComplete, isDaily, questID, displayQuestID
    //
    // Eight were returned, with questTag and isDaily absent, so everything
    // from the third value on landed one or two places early. isComplete
    // received a zero and so no quest ever showed as complete; isDaily
    // received the quest id, which is a large number and therefore true, so
    // every quest in the log was marked daily; and questID arrived nil.
    lua_pushstring(L, q.title.c_str());  // 1: title
    // The level is tracked — the query response carries it — and was being
    // answered as a flat zero beside a comment saying it was not.
    lua_pushnumber(L, q.level);          // 2: level
    lua_pushnil(L);                      // 3: questTag ("Elite", "PvP", …)
    lua_pushnumber(L, 0);                // 4: suggestedGroup
    lua_pushboolean(L, 0);               // 5: isHeader
    lua_pushboolean(L, 0);               // 6: isCollapsed
    // A number, not a boolean: 1 for complete, -1 for failed, nil otherwise.
    // watchframe.lua writes `if ( isComplete and isComplete < 0 )` to tell a
    // failed quest from a finished one, and comparing a boolean with a number
    // raises. Correcting the *position* of this value without correcting its
    // type turned a quiet wrong answer into an error on the quest tracker.
    // Failure is not tracked here, so a quest is either complete or not.
    if (q.complete) lua_pushnumber(L, 1); else lua_pushnil(L);  // 7: isComplete
    lua_pushboolean(L, 0);               // 8: isDaily
    lua_pushnumber(L, q.questId);        // 9: questID
    lua_pushnumber(L, q.questId);        // 10: displayQuestID
    return 10;
}

// GetQuestLogQuestText(index) → description, objectives
/// The quest log index an argument-less call means: the one the log has
/// selected. WoW's quest log functions take the index only when asking about
/// some other entry, and demanding it raised a Lua error on every bare call.
static int questLogIndexOrSelected(lua_State* L, int arg) {
    if (!lua_isnoneornil(L, arg)) return static_cast<int>(luaL_checknumber(L, arg));
    auto* gh = getGameHandler(L);
    return gh ? gh->getSelectedQuestLogIndex() : 0;
}

static int lua_GetQuestLogQuestText(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = questLogIndexOrSelected(L, 1);
    if (!gh || index < 1) { return luaReturnNil(L); }
    const auto& ql = gh->getQuestLog();
    if (index > static_cast<int>(ql.size())) { return luaReturnNil(L); }
    const auto& q = ql[index - 1];
    lua_pushstring(L, "");                    // description (not stored)
    lua_pushstring(L, q.objectives.c_str());  // objectives
    return 2;
}

// IsQuestComplete(questID) → boolean
static int lua_IsQuestComplete(lua_State* L) {
    auto* gh = getGameHandler(L);
    uint32_t questId = static_cast<uint32_t>(luaL_checknumber(L, 1));
    if (!gh) { return luaReturnFalse(L); }
    for (const auto& q : gh->getQuestLog()) {
        if (q.questId == questId) {
            lua_pushboolean(L, q.complete);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// SelectQuestLogEntry(index) — select a quest in the quest log
static int lua_SelectQuestLogEntry(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (gh) gh->setSelectedQuestLogIndex(index);
    return 0;
}

// GetQuestLogSelection() → index
static int lua_GetQuestLogSelection(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? gh->getSelectedQuestLogIndex() : 0);
    return 1;
}

/// The quest the log has selected, or null if none is.
static const game::QuestHandler::QuestLogEntry* selectedQuest(game::GameHandler* gh) {
    if (!gh) return nullptr;
    const int index = gh->getSelectedQuestLogIndex();
    const auto& log = gh->getQuestLog();
    if (index < 1 || index > static_cast<int>(log.size())) return nullptr;
    return &log[static_cast<size_t>(index - 1)];
}

// GetQuestLogPushable() → whether the selected quest may be offered to the party.
//
// Yes for any real selection. Which quests the server will actually share is a
// flag on the quest, and no packet this client parses carries it — so the
// choice is between offering the attempt and letting the server refuse, or
// never offering it at all. The button is disabled without this, and sharing
// works, so silence would be the more misleading answer of the two.
static int lua_GetQuestLogPushable(lua_State* L) {
    lua_pushboolean(L, selectedQuest(getGameHandler(L)) != nullptr ? 1 : 0);
    return 1;
}

// QuestLogPushQuest() — offer the selected quest to the party.
static int lua_QuestLogPushQuest(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (const auto* quest = selectedQuest(gh)) {
        gh->shareQuestWithParty(quest->questId);
    }
    return 0;
}

// GetNumQuestWatches() → count
static int lua_GetNumQuestWatches(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? gh->getTrackedQuestIds().size() : 0);
    return 1;
}

// GetQuestIndexForWatch(watchIndex) → questLogIndex
// Maps the Nth watched quest to its quest log index (1-based)
static int lua_GetQuestIndexForWatch(lua_State* L) {
    auto* gh = getGameHandler(L);
    int watchIdx = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || watchIdx < 1) { return luaReturnNil(L); }
    const auto& ql = gh->getQuestLog();
    const auto& tracked = gh->getTrackedQuestIds();
    int found = 0;
    for (size_t i = 0; i < ql.size(); ++i) {
        if (tracked.count(ql[i].questId)) {
            found++;
            if (found == watchIdx) {
                lua_pushnumber(L, static_cast<int>(i) + 1); // 1-based
                return 1;
            }
        }
    }
    lua_pushnil(L);
    return 1;
}

// AddQuestWatch(questLogIndex) — add a quest to the watch list
static int lua_AddQuestWatch(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) return 0;
    const auto& ql = gh->getQuestLog();
    if (index <= static_cast<int>(ql.size())) {
        gh->setQuestTracked(ql[index - 1].questId, true);
    }
    return 0;
}

// RemoveQuestWatch(questLogIndex) — remove a quest from the watch list
static int lua_RemoveQuestWatch(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) return 0;
    const auto& ql = gh->getQuestLog();
    if (index <= static_cast<int>(ql.size())) {
        gh->setQuestTracked(ql[index - 1].questId, false);
    }
    return 0;
}

// IsQuestWatched(questLogIndex) → boolean
static int lua_IsQuestWatched(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) { return luaReturnFalse(L); }
    const auto& ql = gh->getQuestLog();
    if (index <= static_cast<int>(ql.size())) {
        lua_pushboolean(L, gh->isQuestTracked(ql[index - 1].questId) ? 1 : 0);
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

// GetQuestLink(questLogIndex) → "|cff...|Hquest:id:level|h[title]|h|r"
static int lua_GetQuestLink(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) { return luaReturnNil(L); }
    const auto& ql = gh->getQuestLog();
    if (index > static_cast<int>(ql.size())) { return luaReturnNil(L); }
    const auto& q = ql[index - 1];
    // Yellow quest link format matching WoW
    std::string link = "|cff808000|Hquest:" + std::to_string(q.questId) +
                       ":0|h[" + q.title + "]|h|r";
    lua_pushstring(L, link.c_str());
    return 1;
}

// GetNumQuestLeaderBoards(questLogIndex) → count of objectives
static int lua_GetNumQuestLeaderBoards(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = questLogIndexOrSelected(L, 1);
    if (!gh || index < 1) { return luaReturnZero(L); }
    const auto& ql = gh->getQuestLog();
    if (index > static_cast<int>(ql.size())) { return luaReturnZero(L); }
    const auto& q = ql[index - 1];
    int count = 0;
    for (const auto& ko : q.killObjectives) {
        if (ko.npcOrGoId != 0 || ko.required > 0) ++count;
    }
    for (const auto& io : q.itemObjectives) {
        if (io.itemId != 0 || io.required > 0) ++count;
    }
    lua_pushnumber(L, count);
    return 1;
}

// GetQuestLogLeaderBoard(objIndex, questLogIndex) → text, type, finished
// objIndex is 1-based within the quest's objectives

static int lua_GetQuestLogLeaderBoard(lua_State* L);

// ── Quest points of interest ───────────────────────────────────────────────
//
// The map's quest markers. The server sends these as SMSG_QUEST_POI and this
// client already keeps them — it draws its own markers from the same list — so
// FrameXML's world map can read the real thing rather than an empty one.
//
// WoW numbers them by "visible index", which is a position in the list of
// quests that have a POI on the map now, not a quest log index. The two differ
// as soon as one quest in the log has no marker.

/// The quest ids that have a marker, in the order the server sent them, each
/// appearing once. Built on demand: the list is short and changes whenever the
/// server sends a new one.
static std::vector<uint32_t> questsWithPois(game::GameHandler* gh) {
    std::vector<uint32_t> out;
    if (!gh) return out;
    for (const auto& poi : gh->getGossipPois()) {
        // -2 is an ordinary gossip marker rather than a quest one.
        if (poi.questObjectiveIndex == -2 || poi.data == 0) continue;
        if (std::find(out.begin(), out.end(), poi.data) == out.end()) out.push_back(poi.data);
    }
    return out;
}

/// QuestMapUpdateAllQuests() → how many quests have a marker on the map.
///
/// Both a verb and a question in the real client: it refreshes the POI set and
/// answers how many there are. There is nothing to refresh here — the list is
/// whatever the server last sent — so this is the answer alone.
///
/// It was not bound at all, and WatchFrame_GetCurrentMapQuests reads it
/// straight into `for i = 1, numQuests`. A nil limit there is not an empty
/// loop but an error, so the tracker's map-quest table was never built and the
/// handler around it died on the way. The count it needs was already sitting
/// in the same list QuestPOIGetQuestIDByVisibleIndex indexes.
static int lua_QuestMapUpdateAllQuests(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(questsWithPois(getGameHandler(L)).size()));
    return 1;
}

/// QuestPOIGetQuestIDByVisibleIndex(i) → questId, questLogIndex.
///
/// Both, because the world map uses the second to reach everything else about
/// the quest: `if ( questLogIndex and questLogIndex > 0 )` gates the whole
/// block that builds the map's quest list, and with only one value returned
/// that gate never opened — so the list was always empty, quietly, with no
/// error to say why.
static int lua_QuestPOIGetQuestIDByVisibleIndex(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    const auto ids = questsWithPois(gh);
    if (index < 1 || index > static_cast<int>(ids.size())) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 2;
    }
    const uint32_t questId = ids[static_cast<size_t>(index - 1)];
    lua_pushnumber(L, questId);
    // Where that quest sits in the log, counted as Lua counts. A marker can
    // outlive the log entry — the server sends POIs separately — so a quest
    // that is no longer held answers zero rather than a stale position.
    int logIndex = 0;
    if (gh) {
        const auto& ql = gh->getQuestLog();
        for (size_t i = 0; i < ql.size(); ++i) {
            if (ql[i].questId == questId) { logIndex = static_cast<int>(i) + 1; break; }
        }
    }
    lua_pushnumber(L, logIndex);
    return 2;
}

/// QuestPOIGetIconInfo(questId) → completed, x, y.
///
/// The endpoint marker is the one the map draws for a quest, so that is the
/// one reported: objective index -1 identifies it. Completion comes from the
/// quest log rather than the marker, which does not carry it.
static int lua_QuestPOIGetIconInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const uint32_t questId = static_cast<uint32_t>(luaL_checknumber(L, 1));
    if (!gh || questId == 0) { return luaReturnNil(L); }

    const game::GossipPoi* best = nullptr;
    for (const auto& poi : gh->getGossipPois()) {
        if (poi.data != questId || poi.questObjectiveIndex == -2) continue;
        if (!best || poi.questObjectiveIndex == -1) best = &poi;
        if (poi.questObjectiveIndex == -1) break;
    }
    if (!best) { return luaReturnNil(L); }

    bool complete = false;
    for (const auto& q : gh->getQuestLog()) {
        if (q.questId == questId) { complete = q.complete; break; }
    }
    lua_pushboolean(L, complete);
    lua_pushnumber(L, best->x);
    lua_pushnumber(L, best->y);
    return 3;
}

/// The markers arrive with the server's own updates, so there is nothing to
/// refresh on demand — but the map asks before drawing and expects the call to
/// exist.
static int lua_QuestPOIUpdateIcons(lua_State* L) { (void)L; return 0; }

/// GetQuestPOILeaderBoard(objectiveIndex, questId) → the objective's text and
/// counts, the same as the quest log's version — except that this one is given
/// a quest id where that one takes a log index. Aliasing the two would look
/// right and read the wrong quest, so the id is turned into an index here.
static int lua_GetQuestPOILeaderBoard(lua_State* L) {
    auto* gh = getGameHandler(L);
    const uint32_t questId = static_cast<uint32_t>(luaL_optnumber(L, 2, 0));
    if (!gh || questId == 0) { return luaReturnNil(L); }
    const auto& ql = gh->getQuestLog();
    int index = 0;
    for (size_t i = 0; i < ql.size(); ++i) {
        if (ql[i].questId == questId) { index = static_cast<int>(i) + 1; break; }
    }
    if (index == 0) { return luaReturnNil(L); }
    lua_pushvalue(L, 1);            // objective index, unchanged
    lua_pushnumber(L, index);       // the log index the other one wants
    lua_replace(L, 2);
    lua_replace(L, 1);
    return lua_GetQuestLogLeaderBoard(L);
}

static int lua_GetQuestLogLeaderBoard(lua_State* L) {
    auto* gh = getGameHandler(L);
    int objIdx = static_cast<int>(luaL_checknumber(L, 1));
    int questIdx = static_cast<int>(luaL_optnumber(L, 2,
        gh ? gh->getSelectedQuestLogIndex() : 0));
    if (!gh || questIdx < 1 || objIdx < 1) { return luaReturnNil(L); }
    const auto& ql = gh->getQuestLog();
    if (questIdx > static_cast<int>(ql.size())) { return luaReturnNil(L); }
    const auto& q = ql[questIdx - 1];

    // Build ordered list: kill objectives first, then item objectives
    int cur = 0;
    for (int i = 0; i < 4; ++i) {
        if (q.killObjectives[i].npcOrGoId == 0 && q.killObjectives[i].required == 0) continue;
        ++cur;
        if (cur == objIdx) {
            // Get current count from killCounts map (keyed by abs(npcOrGoId))
            uint32_t key = static_cast<uint32_t>(std::abs(q.killObjectives[i].npcOrGoId));
            uint32_t current = 0;
            auto it = q.killCounts.find(key);
            if (it != q.killCounts.end()) current = it->second.first;
            uint32_t required = q.killObjectives[i].required;
            bool finished = (current >= required);
            // Build display text like "Kobold Vermin slain: 3/8"
            std::string text = (q.killObjectives[i].npcOrGoId < 0 ? "Object" : "Creature")
                + std::string(" slain: ") + std::to_string(current) + "/" + std::to_string(required);
            lua_pushstring(L, text.c_str());
            lua_pushstring(L, q.killObjectives[i].npcOrGoId < 0 ? "object" : "monster");
            lua_pushboolean(L, finished ? 1 : 0);
            return 3;
        }
    }
    for (int i = 0; i < 6; ++i) {
        if (q.itemObjectives[i].itemId == 0 && q.itemObjectives[i].required == 0) continue;
        ++cur;
        if (cur == objIdx) {
            uint32_t current = 0;
            auto it = q.itemCounts.find(q.itemObjectives[i].itemId);
            if (it != q.itemCounts.end()) current = it->second;
            uint32_t required = q.itemObjectives[i].required;
            bool finished = (current >= required);
            // Get item name if available
            std::string itemName;
            const auto* info = gh->getItemInfo(q.itemObjectives[i].itemId);
            if (info && !info->name.empty()) itemName = info->name;
            else itemName = "Item #" + std::to_string(q.itemObjectives[i].itemId);
            std::string text = itemName + ": " + std::to_string(current) + "/" + std::to_string(required);
            lua_pushstring(L, text.c_str());
            lua_pushstring(L, "item");
            lua_pushboolean(L, finished ? 1 : 0);
            return 3;
        }
    }
    lua_pushnil(L);
    return 1;
}

// ExpandQuestHeader / CollapseQuestHeader — no-ops (flat quest list, no headers)
static int lua_ExpandQuestHeader(lua_State* L) { (void)L; return 0; }
static int lua_CollapseQuestHeader(lua_State* L) { (void)L; return 0; }

// GetQuestLogSpecialItemInfo(questLogIndex) -> link, texture, charges
//
// Answering nil meant no button was ever built, so a quest that gives you
// something to use looked like one that does not.
static int lua_GetQuestLogSpecialItemInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const QuestSpecialItem item = questSpecialItemAt(gh, index);
    if (!item.itemId) { return luaReturnNil(L); }

    const auto* info = gh->getItemInfo(item.itemId);
    // The name is what goes in the link, and without it there is no link to
    // return. The query was sent when the quest was read; until it answers,
    // there is honestly nothing to draw.
    if (!info || info->name.empty()) { return luaReturnNil(L); }

    const uint32_t quality = info->quality < 8 ? info->quality : 1u;
    char link[256];
    snprintf(link, sizeof(link), "|cff%s|Hitem:%u:0:0:0:0:0:0:0|h[%s]|h|r",
             kQualHexNoAlpha[quality], item.itemId, info->name.c_str());
    lua_pushstring(L, link);                                        // 1: link

    // A nil texture is an empty slot to the interface, and the button draws
    // its background art instead of the item.
    const std::string icon = info->displayInfoId
        ? gh->getItemIconPath(info->displayInfoId) : std::string();
    lua_pushstring(L, icon.empty() ? "Interface\\Icons\\INV_Misc_QuestionMark"
                                   : icon.c_str());                 // 2: texture
    // WatchFrameItem_OnUpdate compares this against the count it drew with and
    // rebuilds the whole frame when it changes, so it has to be stable.
    lua_pushnumber(L, item.count > 0 ? item.count : 1);             // 3: charges
    return 3;
}

static int lua_UseQuestLogSpecialItem(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const QuestSpecialItem item = questSpecialItemAt(gh, index);
    if (!item.itemId) return 0;
    if (item.bag == 0) gh->useItemBySlot(item.slot - 1);
    else               gh->useItemInBag(item.bag - 1, item.slot - 1);
    return 0;
}

/// The player's skills in the order the list shows them: by name.
///
/// They live in an unordered_map, and the skill list used to be read out of it
/// by counting to the asked-for position. That order is arbitrary — the list
/// came out in no order a player could recognise — and worse, it is not fixed:
/// learning a skill inserts, inserting can rehash, and rehashing reorders a
/// list already drawn under a selection held as an index. The skill being
/// looked at would quietly become a different one.
///
/// By name, with the id breaking ties, so the answer is the same every time it
/// is asked and reads like a list rather than a spill. Ties matter: two skills
/// can share a name before the name cache has resolved either.
static std::vector<uint32_t> skillOrder(game::GameHandler* gh) {
    std::vector<uint32_t> ids;
    if (!gh) return ids;
    const auto& skills = gh->getPlayerSkills();
    ids.reserve(skills.size());
    for (const auto& [id, skill] : skills) ids.push_back(id);
    std::sort(ids.begin(), ids.end(), [gh](uint32_t a, uint32_t b) {
        const std::string na = gh->getSkillName(a), nb = gh->getSkillName(b);
        if (na != nb) return na < nb;
        return a < b;
    });
    return ids;
}

// --- The stable ---
//
// Everything here was already tracked: the pets, their levels, how many slots
// the player has bought, and the three commands that list, store and retrieve.
// Only the questions the original interface asks were missing, so its stable
// opened on nothing however many pets were in it.
//
// Slot 0 is the pet that is out, and 1 upwards are the ones stabled. That is
// the interface's numbering, not this client's — the pets arrive in one list
// with a flag saying which is active.
namespace {

/// The stabled pets, in the order they arrived, with the active one left out.
std::vector<const game::GameHandler::StabledPet*> stabledOnly(game::GameHandler* gh) {
    std::vector<const game::GameHandler::StabledPet*> out;
    if (!gh) return out;
    for (const auto& p : gh->getStabledPets()) {
        if (!p.isActive) out.push_back(&p);
    }
    return out;
}

/// The pet that is currently out, or null if none is.
const game::GameHandler::StabledPet* activePet(game::GameHandler* gh) {
    if (!gh) return nullptr;
    for (const auto& p : gh->getStabledPets()) {
        if (p.isActive) return &p;
    }
    return nullptr;
}

/// Which slot the player has clicked. Held here because it is a fact about the
/// window rather than about the character, and the server is never told.
int& selectedStableSlot() {
    static int slot = 0;
    return slot;
}

} // namespace

// GetStablePetInfo(slot) → icon, name, level, family, talent
//
// The icon is what says a slot is occupied: the frame tests it before deciding
// whether the slot reads as a pet or as an empty box, so an occupied slot must
// answer something and an empty one must answer nil.
//
// Family and talent tree are blank. They come from the creature's family, and
// the stable packet carries the creature entry without it — this client's own
// stable window shows a name and a level for the same reason.
static int lua_GetStablePetInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int slot = static_cast<int>(luaL_optnumber(L, 1, -1));
    if (!gh || slot < 0) { return luaReturnNil(L); }

    const game::GameHandler::StabledPet* pet = nullptr;
    if (slot == 0) {
        pet = activePet(gh);
    } else {
        const auto stabled = stabledOnly(gh);
        if (slot <= static_cast<int>(stabled.size())) {
            pet = stabled[static_cast<size_t>(slot - 1)];
        }
    }
    if (!pet) { return luaReturnNil(L); }

    lua_pushstring(L, "Interface\\Icons\\Ability_Hunter_BeastTaming");
    lua_pushstring(L, pet->name.empty()
                          ? ("Pet #" + std::to_string(pet->petNumber)).c_str()
                          : pet->name.c_str());
    lua_pushnumber(L, pet->level);
    lua_pushstring(L, "");   // family
    lua_pushstring(L, "");   // pet talent tree
    return 5;
}

static int lua_GetNumStablePets(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(stabledOnly(getGameHandler(L)).size()));
    return 1;
}

static int lua_GetNumStableSlots(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? gh->getStableSlots() : 0);
    return 1;
}

static int lua_GetSelectedStablePet(lua_State* L) {
    lua_pushnumber(L, selectedStableSlot());
    return 1;
}

static int lua_ClickStablePet(lua_State* L) {
    selectedStableSlot() = static_cast<int>(luaL_optnumber(L, 1, 0));
    // The model preview shows whichever pet is selected, so selecting a
    // different one is exactly when it has to be redrawn. Nothing else changes
    // it, and the frame will not redraw on its own.
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_lua_engine");
    auto* engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (engine) engine->fireEvent("PET_STABLE_UPDATE_PAPERDOLL", {});
    return 0;
}

// ---- What a pet eats, and what it trains into ----
//
// Both come from the creature's family. This client learns a family from
// SMSG_CREATURE_QUERY_RESPONSE, which only ever arrives for creatures it has
// seen — and a stabled pet is by definition not in the world, so its family is
// never known. Mapping a family to a diet would need CreatureFamily.dbc on top
// of that, whose field layout does not read cleanly enough to trust.
//
// So these answer absent, and the frame is built for that: GetPetIcon and
// GetPetFoodTypes are both tested before use, and the talent tree is taken as
// `GetPetTalentTree() or ""`.
static int lua_GetPetIcon(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !activePet(gh)) { return luaReturnNil(L); }
    lua_pushstring(L, "Interface\\Icons\\Ability_Hunter_BeastTaming");
    return 1;
}
static int lua_GetPetTalentTree(lua_State* L) { return luaReturnNil(L); }
static int lua_GetPetFoodTypes(lua_State* L)  { return luaReturnNil(L); }

/// SetPetStablePaperdoll(model) — put the selected pet in the preview frame.
///
/// Defined and does nothing, which is deliberate and not the same as done. A
/// model frame here shows an image this client renders for it, and it renders
/// one for a player character; there is no path that puts an arbitrary
/// creature in one. Leaving the name undefined would be worse than a blank
/// preview — the call is unguarded, so it would take the stable window down
/// on the click that selects a pet.
static int lua_SetPetStablePaperdoll(lua_State* L) { (void)L; return 0; }

/// PickupStablePet(slot) — start dragging a pet between stable slots.
///
/// Also a deliberate no-op. Moving a pet by dragging needs a cursor that can
/// hold one, and this client's cursor holds items and spells; the buttons the
/// stable frame offers for the same moves go through stablePet and
/// unstablePet, which do work. So the frame loses the drag and keeps the
/// operation.
static int lua_PickupStablePet(lua_State* L) { (void)L; return 0; }

/// GetStablePetFoodTypes(slot) — and the one that cannot answer nil.
///
/// Its result goes straight into format(PET_DIET_TEMPLATE,
/// BuildListString(...)) with no test in between. BuildListString hands nil
/// back for nil, and string.format raises on a nil where it wants a string, so
/// answering honestly there takes the stable window down as it opens. An empty
/// string is the one value that says "not known" without doing that: the diet
/// line comes out blank instead of wrong.
static int lua_GetStablePetFoodTypes(lua_State* L) {
    (void)L;
    lua_pushstring(L, "");
    return 1;
}

static int lua_ClosePetStables(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->closeStableWindow();
    return 0;
}

static int lua_IsAtStableMaster(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushboolean(L, gh && gh->isStableWindowOpen() ? 1 : 0);
    return 1;
}

// GetNextStableSlotCost() → what the next slot costs, in copper.
//
// Zero, because the server never says. It reaches a money frame, which divides
// it into gold and silver the moment the window opens, so it has to be a number
// — and a made-up price shown as though the server had quoted it is worse than
// a visible nothing.
static int lua_GetNextStableSlotCost(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

static int lua_GetNumSkillLines(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return luaReturnZero(L); }
    lua_pushnumber(L, gh->getPlayerSkills().size());
    return 1;
}

// GetSkillLineInfo(index) → skillName, isHeader, isExpanded, skillRank, numTempPoints, skillModifier, skillMaxRank, isAbandonable, stepCost, rankCost, minLevel, skillCostType
static int lua_GetSkillLineInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    // optnumber, not checknumber: a nil index is a question this can answer
    // with nil, and raising instead takes down whatever file asked. SkillFrame
    // calls SkillDetailFrame_SetStatusBar with no selection during its own
    // load, which is exactly that question.
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    // An index with no skill behind it answers as an empty skill rather than a
    // single nil. SkillFrame_UpdateSkills passes GetSelectedSkill() straight in
    // and adds two of the results together on the next line, and before the
    // server has sent any skills there is nothing selected — so a bare nil
    // there is arithmetic on nothing and the file is lost. An empty row is the
    // truthful answer and it costs no more than a blank line in the list.
    // Short-circuit rather than a ternary: binding a reference across both arms
    // would copy the whole skill map on every call.
    if (!gh || index < 1 ||
        index > static_cast<int>(gh->getPlayerSkills().size())) {
        lua_pushstring(L, "");                          // 1: skillName
        lua_pushboolean(L, 0);                          // 2: isHeader
        lua_pushboolean(L, 1);                          // 3: isExpanded
        for (int i = 4; i <= 7; ++i) lua_pushnumber(L, 0);   // rank, temp, mod, max
        lua_pushboolean(L, 0);                          // 8: isAbandonable
        // stepCost and rankCost nil for the same reason they are nil below:
        // zero is true in Lua, so a zero here sends the empty row down the
        // "Learn <skill>" branch. The fix went in on the real path and this
        // fallback kept its zeros, which is how a fix half-lands.
        lua_pushnil(L);                                 // 9: stepCost
        lua_pushnil(L);                                 // 10: rankCost
        lua_pushnumber(L, 0);                           // 11: minLevel
        lua_pushnumber(L, 0);                           // 12: skillCostType
        lua_pushstring(L, "");                          // 13: skillDescription
        return 13;
    }
    const auto order = skillOrder(gh);
    const auto& skills = gh->getPlayerSkills();
    const auto found = skills.find(order[static_cast<size_t>(index - 1)]);
    if (found == skills.end()) { return luaReturnNil(L); }
    const auto& skill = found->second;
    std::string name = gh->getSkillName(skill.skillId);
    if (name.empty()) name = "Skill " + std::to_string(skill.skillId);

    lua_pushstring(L, name.c_str());                    // 1: skillName
    lua_pushboolean(L, 0);                              // 2: isHeader (false — flat list)
    lua_pushboolean(L, 1);                              // 3: isExpanded
    lua_pushnumber(L, skill.effectiveValue());           // 4: skillRank
    lua_pushnumber(L, skill.bonusTemp);                  // 5: numTempPoints
    lua_pushnumber(L, skill.bonusPerm);                  // 6: skillModifier
    lua_pushnumber(L, skill.maxValue);                   // 7: skillMaxRank
    lua_pushboolean(L, 0);                              // 8: isAbandonable
    // Nil, not zero, and this is the whole of the "Learn Mounts" mystery.
    //
    // SkillFrame_SetStatusBar branches on these three in order:
    //
    //     if ( stepCost ) then          -- a skill that must be bought
    //         statusBarName:SetFormattedText(LEARN_SKILL_TEMPLATE, skillName)
    //     elseif ( rankCost or numTempPoints > 0 ) then   -- trainable
    //     else                                            -- an ordinary skill
    //
    // Zero is *true* in Lua, so the first branch always won and every row in
    // the skills window was titled "Learn <skill>" — First Aid, Axes, Cooking,
    // everything — as though none of them were known.
    //
    // No purchase cost is tracked here, and nil is how the client says a skill
    // has none. With both nil and no temporary points, the third branch runs
    // and the row is simply the skill's name, which is what it should have
    // been reading all along.
    lua_pushnil(L);                                     // 9: stepCost
    lua_pushnil(L);                                     // 10: rankCost
    lua_pushnumber(L, 0);                               // 11: minLevel
    lua_pushnumber(L, 0);                               // 12: skillCostType
    // The sentence the detail panel prints under the selected skill.
    //
    // Returning twelve values left it nil, and SkillDetailFrame_SetStatusBar
    // feeds it straight into SetFormattedText(SKILL_DESCRIPTION, type, desc).
    // string.format raises on a nil %s, so the guarded SetFormattedText fell
    // back to writing the format string itself — the lower half of the skills
    // window showed "%s %s" where the description belongs.
    lua_pushstring(L, gh->getSkillDescription(skill.skillId).c_str());  // 13
    return 13;
}

// --- Friends/Ignore API ---


/// Whose talents a talent binding is being asked about.
///
/// Every talent binding takes an `inspect` flag that this client ignored, so
/// the inspect talent tab enumerated the viewer's own class tabs and read the
/// viewer's own ranks — the wrong tree under the target's name, rather than an
/// empty one. Zero means the inspect result has no class yet, in which case
/// falling back to the player's is the only thing left to do.
static uint8_t talentClassId(game::GameHandler* gh, bool inspect) {
    if (inspect && gh) {
        if (const auto* r = gh->getInspectResult()) {
            if (r->classId) return r->classId;
        }
    }
    return gh ? gh->getPlayerClass() : 0;
}

/// The rank a talent is at, for whoever is being asked about.
static int talentRankFor(game::GameHandler* gh, bool inspect, uint32_t talentId) {
    if (!gh) return 0;
    if (inspect) {
        if (const auto* r = gh->getInspectResult()) {
            auto it = r->talentRanks.find(talentId);
            return (it != r->talentRanks.end()) ? it->second : 0;
        }
        return 0;
    }
    return gh->getTalentRank(talentId);
}

static int lua_GetNumTalentTabs(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return luaReturnZero(L); }
    // Count tabs matching the class in question
    uint8_t classId = talentClassId(gh, lua_toboolean(L, 1) != 0);
    uint32_t classMask = (classId > 0) ? (1u << (classId - 1)) : 0;
    int count = 0;
    for (const auto& [tabId, tab] : gh->getAllTalentTabs()) {
        if (tab.classMask & classMask) count++;
    }
    lua_pushnumber(L, count);
    return 1;
}

/// Points staged in the talent preview but not yet learned, defined below with
/// the functions that change it.
static std::unordered_map<uint32_t, int>& previewPoints();

// GetTalentTabInfo(tabIndex) → name, iconTexture, pointsSpent, background
static int lua_GetTalentTabInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    int tabIndex = static_cast<int>(luaL_checknumber(L, 1)); // 1-indexed
    if (!gh || tabIndex < 1) {
        return luaReturnNil(L);
    }
    const bool inspect = lua_toboolean(L, 2) != 0;
    uint8_t classId = talentClassId(gh, inspect);
    uint32_t classMask = (classId > 0) ? (1u << (classId - 1)) : 0;
    // Find the Nth tab for this class (sorted by orderIndex)
    std::vector<const game::GameHandler::TalentTabEntry*> classTabs;
    for (const auto& [tabId, tab] : gh->getAllTalentTabs()) {
        if (tab.classMask & classMask) classTabs.push_back(&tab);
    }
    std::sort(classTabs.begin(), classTabs.end(),
        [](const auto* a, const auto* b) { return a->orderIndex < b->orderIndex; });
    if (tabIndex > static_cast<int>(classTabs.size())) {
        return luaReturnNil(L);
    }
    const auto* tab = classTabs[tabIndex - 1];
    // Count points spent in this tab
    int pointsSpent = 0;
    static const std::unordered_map<uint32_t, uint8_t> kNoTalents;
    const auto* inspectResult = inspect ? gh->getInspectResult() : nullptr;
    const auto& learned = inspect
        ? (inspectResult ? inspectResult->talentRanks : kNoTalents)
        : gh->getLearnedTalents();
    for (const auto& [talentId, rank] : learned) {
        const auto* entry = gh->getTalentEntry(talentId);
        if (entry && entry->tabId == tab->tabId) pointsSpent += rank;
    }
    // Points staged in the preview but not yet learned, for this tab. The
    // talent frame adds this to the spent count without checking it —
    //     local displayPointsSpent = pointsSpent + previewPointsSpent;
    // — in a loop over every tab, so leaving it out took the whole frame down
    // as it opened rather than merely showing the wrong total.
    // Staged points are the viewer part-way through spending their own; they
    // have no meaning on someone else's tree.
    int previewSpent = 0;
    if (!inspect) {
        for (const auto& [talentId, staged] : previewPoints()) {
            const auto* entry = gh->getTalentEntry(talentId);
            if (entry && entry->tabId == tab->tabId) previewSpent += staged;
        }
    }

    lua_pushstring(L, tab->name.c_str());              // 1: name
    lua_pushnil(L);                                     // 2: iconTexture (not resolved)
    lua_pushnumber(L, pointsSpent);                     // 3: pointsSpent
    lua_pushstring(L, tab->backgroundFile.c_str());     // 4: background
    lua_pushnumber(L, previewSpent);                    // 5: previewPointsSpent
    return 5;
}

// GetNumTalents(tabIndex) → count
static int lua_GetNumTalents(lua_State* L) {
    auto* gh = getGameHandler(L);
    int tabIndex = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || tabIndex < 1) { return luaReturnZero(L); }
    uint8_t classId = talentClassId(gh, lua_toboolean(L, 2) != 0);
    uint32_t classMask = (classId > 0) ? (1u << (classId - 1)) : 0;
    std::vector<const game::GameHandler::TalentTabEntry*> classTabs;
    for (const auto& [tabId, tab] : gh->getAllTalentTabs()) {
        if (tab.classMask & classMask) classTabs.push_back(&tab);
    }
    std::sort(classTabs.begin(), classTabs.end(),
        [](const auto* a, const auto* b) { return a->orderIndex < b->orderIndex; });
    if (tabIndex > static_cast<int>(classTabs.size())) {
        return luaReturnZero(L);
    }
    uint32_t targetTabId = classTabs[tabIndex - 1]->tabId;
    int count = 0;
    for (const auto& [talentId, entry] : gh->getAllTalents()) {
        if (entry.tabId == targetTabId) count++;
    }
    lua_pushnumber(L, count);
    return 1;
}

// GetTalentInfo(tabIndex, talentIndex) → name, iconTexture, tier, column, rank, maxRank, isExceptional, available
/// Which quest the confirmation is about. Set when the log asks to abandon one
/// and read back by the popup that confirms it, because the two are separate
/// calls with the player's answer in between.
static uint32_t& pendingAbandonQuest() {
    static uint32_t questId = 0;
    return questId;
}

/// The talent at a tab and index, by the ordering every talent function has to
/// agree on: the player's own tabs in their order index, then the tab's talents
/// by row and column.
///
/// Shared rather than repeated, because two copies that drift disagree about
/// which talent is fourth — and the prerequisite lines are drawn between
/// positions, so a disagreement points an arrow at the wrong button rather than
/// failing outright.
// Not static: the tooltip setters in lua_engine.cpp ask the same question, and
// two copies of this would have to keep the same tab ordering and the same
// row/column sort forever or the tooltip would describe a different talent from
// the one under the cursor. Declared in lua_api_helpers.hpp.

const game::TalentEntry* talentAt(game::GameHandler* gh,
                                  int tabIndex, int talentIndex,
                                  uint8_t classIdOverride) {
    if (!gh || tabIndex < 1 || talentIndex < 1) return nullptr;
    const uint8_t classId = classIdOverride ? classIdOverride : gh->getPlayerClass();
    const uint32_t classMask = (classId > 0) ? (1u << (classId - 1)) : 0;

    std::vector<const game::GameHandler::TalentTabEntry*> classTabs;
    for (const auto& [tabId, tab] : gh->getAllTalentTabs()) {
        if (tab.classMask & classMask) classTabs.push_back(&tab);
    }
    std::sort(classTabs.begin(), classTabs.end(),
        [](const auto* a, const auto* b) { return a->orderIndex < b->orderIndex; });
    if (tabIndex > static_cast<int>(classTabs.size())) return nullptr;

    const uint32_t targetTabId = classTabs[tabIndex - 1]->tabId;
    std::vector<const game::GameHandler::TalentEntry*> tabTalents;
    for (const auto& [talentId, entry] : gh->getAllTalents()) {
        if (entry.tabId == targetTabId) tabTalents.push_back(&entry);
    }
    std::sort(tabTalents.begin(), tabTalents.end(),
        [](const auto* a, const auto* b) {
            return (a->row != b->row) ? a->row < b->row : a->column < b->column;
        });
    if (talentIndex > static_cast<int>(tabTalents.size())) return nullptr;
    return tabTalents[talentIndex - 1];
}


/// Whether every prerequisite of a talent is satisfied, optionally counting
/// points staged in the preview but not yet learned.
static bool talentPrereqsMet(game::GameHandler* gh,
                             const game::GameHandler::TalentEntry* talent,
                             bool withPreview) {
    if (!gh || !talent) return false;
    for (int p = 0; p < 3; ++p) {
        const uint32_t prereqId = talent->prereqTalent[p];
        if (prereqId == 0) continue;
        // Counted from zero in the DBC, so the rank asked for is one more.
        const int needed = static_cast<int>(talent->prereqRank[p]) + 1;
        int have = gh->getTalentRank(prereqId);
        if (withPreview) {
            const auto staged = previewPoints().find(prereqId);
            if (staged != previewPoints().end()) have += staged->second;
        }
        if (have < needed) return false;
    }
    return true;
}

static int lua_GetTalentInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int tabIndex = static_cast<int>(luaL_checknumber(L, 1));
    const int talentIndex = static_cast<int>(luaL_checknumber(L, 2));
    const bool inspect = lua_toboolean(L, 3) != 0;
    const auto* talent = talentAt(gh, tabIndex, talentIndex, talentClassId(gh, inspect));
    // Ten values, not eight. The frame reads previewRank into the rank it
    // displays and then compares it against maxRank — as nil that is an error
    // rather than a blank, and it happens the moment points are staged, which
    // is how talents are spent at all.
    if (!talent) {
        for (int i = 0; i < 10; i++) lua_pushnil(L);
        return 10;
    }
    const int rank = talentRankFor(gh, inspect, talent->talentId);
    const auto staged = previewPoints().find(talent->talentId);
    const int previewRank = inspect
        ? rank
        : rank + (staged == previewPoints().end() ? 0 : staged->second);

    std::string name = gh->getSpellName(talent->rankSpells[0]);
    if (name.empty()) name = "Talent " + std::to_string(talent->talentId);
    // A nil texture is an empty slot to the interface, so every talent button
    // was drawn blank.
    const std::string icon = gh->getSpellIconPath(talent->rankSpells[0]);

    lua_pushstring(L, name.c_str());          // 1: name
    if (icon.empty()) lua_pushnil(L);          // 2: iconTexture
    else lua_pushstring(L, icon.c_str());
    lua_pushnumber(L, talent->row + 1);        // 3: tier (1-indexed)
    lua_pushnumber(L, talent->column + 1);     // 4: column (1-indexed)
    lua_pushnumber(L, rank);                   // 5: rank
    lua_pushnumber(L, talent->maxRank);        // 6: maxRank
    lua_pushboolean(L, 0);                     // 7: isExceptional
    // Was hardcoded true, which drew every talent as learnable however deep in
    // a chain it sat.
    lua_pushboolean(L, talentPrereqsMet(gh, talent, /*withPreview=*/false) ? 1 : 0);
    lua_pushnumber(L, previewRank);            // 9: previewRank
    lua_pushboolean(L, talentPrereqsMet(gh, talent, /*withPreview=*/true) ? 1 : 0);
    return 10;
}

/// The trainer panel's own selection and filters. The client has no opinion
/// about either — they are what the player last clicked — so they live here
/// rather than being invented on every read.
static int& tradeSkillSelection() {
    static int selected = 0;
    return selected;
}
static int& trainerSelection() {
    static int selected = 0;
    return selected;
}
static std::unordered_map<std::string, bool>& trainerFilters() {
    static std::unordered_map<std::string, bool> filters;
    return filters;
}

/// (tabIndex, talentIndex) → talent id, or 0.
///
/// The same ordering GetTalentInfo reports by — tabs in orderIndex, talents by
/// row then column — because the interface identifies a talent by its position
/// in that listing and nothing else.
static uint32_t resolveTalentId(game::GameHandler* gh, int tabIndex, int talentIndex) {
    if (!gh || tabIndex < 1 || talentIndex < 1) return 0;
    const uint8_t classId = gh->getPlayerClass();
    const uint32_t classMask = (classId > 0) ? (1u << (classId - 1)) : 0;
    std::vector<const game::GameHandler::TalentTabEntry*> classTabs;
    for (const auto& [tabId, tab] : gh->getAllTalentTabs()) {
        if (tab.classMask & classMask) classTabs.push_back(&tab);
    }
    std::sort(classTabs.begin(), classTabs.end(),
              [](const auto* a, const auto* b) { return a->orderIndex < b->orderIndex; });
    if (tabIndex > static_cast<int>(classTabs.size())) return 0;

    std::vector<const game::GameHandler::TalentEntry*> tabTalents;
    for (const auto& [talentId, entry] : gh->getAllTalents()) {
        if (entry.tabId == classTabs[tabIndex - 1]->tabId) tabTalents.push_back(&entry);
    }
    std::sort(tabTalents.begin(), tabTalents.end(),
              [](const auto* a, const auto* b) {
                  return (a->row != b->row) ? a->row < b->row : a->column < b->column;
              });
    if (talentIndex > static_cast<int>(tabTalents.size())) return 0;
    return tabTalents[talentIndex - 1]->talentId;
}

/// The points staged but not yet sent, per talent.
///
/// WotLK's talent frame does not spend a point when one is clicked: it adds to
/// a preview, redraws from the preview, and sends the lot when Learn is
/// pressed. Without somewhere to hold that, clicking a talent did nothing at
/// all. Keyed by talent id so it survives a tab change, and cleared whenever
/// the server confirms what was learned.
static std::unordered_map<uint32_t, int>& previewPoints() {
    static std::unordered_map<uint32_t, int> points;
    return points;
}

// AddPreviewTalentPoints(tabIndex, talentIndex, delta, pet, group)
static int lua_AddPreviewTalentPoints(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int tab   = static_cast<int>(luaL_optnumber(L, 1, 0));
    const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
    const int delta = static_cast<int>(luaL_optnumber(L, 3, 1));
    const uint32_t id = resolveTalentId(gh, tab, index);
    if (!gh || id == 0) return 0;

    const uint8_t have = gh->getTalentRank(id);
    uint8_t maxRank = 0;
    for (const auto& [talentId, entry] : gh->getAllTalents()) {
        if (talentId == id) { maxRank = entry.maxRank; break; }
    }
    int& staged = previewPoints()[id];
    // Bounded by what is already learned and the talent's own cap: a preview
    // that goes past either is one the server will refuse, and the frame draws
    // straight from these numbers.
    const int wanted = std::clamp(staged + delta, 0, static_cast<int>(maxRank) - have);
    staged = wanted;
    if (staged == 0) previewPoints().erase(id);
    // What makes a staged point appear: the talent frame refreshes on this
    // event, and reads the rank back through GetTalentInfo's preview value.
    gh->fireAddonEvent("PREVIEW_TALENT_POINTS_CHANGED", {});
    return 0;
}

// GetTalentPrereqs(tab, index) → tier, column, isLearnable, isPreviewLearnable
//                                 repeated once per prerequisite
//
// The frame draws a line from each prerequisite to the talent that needs it, so
// the positions here have to be the same ones GetTalentInfo reports — 1-indexed
// row and column, from the same ordering.
static int lua_GetTalentPrereqs(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int tabIndex = static_cast<int>(luaL_checknumber(L, 1));
    const int talentIndex = static_cast<int>(luaL_checknumber(L, 2));
    const auto* talent = talentAt(gh, tabIndex, talentIndex);
    if (!talent) return 0;

    int pushed = 0;
    for (int p = 0; p < 3; ++p) {
        const uint32_t prereqId = talent->prereqTalent[p];
        if (prereqId == 0) continue;
        const auto* prereq = gh->getTalentEntry(prereqId);
        if (!prereq) continue;

        // The DBC counts ranks from zero, so the rank a prerequisite asks for
        // is one more than the number stored beside it.
        const int needed = static_cast<int>(talent->prereqRank[p]) + 1;
        const int have = gh->getTalentRank(prereqId);
        const auto staged = previewPoints().find(prereqId);
        const int previewHave = have + (staged == previewPoints().end() ? 0 : staged->second);

        lua_pushnumber(L, prereq->row + 1);
        lua_pushnumber(L, prereq->column + 1);
        lua_pushboolean(L, have >= needed ? 1 : 0);
        lua_pushboolean(L, previewHave >= needed ? 1 : 0);
        pushed += 4;
    }
    return pushed;
}

// GetGroupPreviewTalentPointsSpent(pet, group) → points staged
static int lua_GetGroupPreviewTalentPointsSpent(lua_State* L) {
    int total = 0;
    for (const auto& [id, n] : previewPoints()) { (void)id; total += n; }
    lua_pushnumber(L, total);
    return 1;
}

// ResetGroupPreviewTalentPoints(pet, group)
static int lua_ResetGroupPreviewTalentPoints(lua_State* L) {
    previewPoints().clear();
    // The frame redraws on this and on nothing else, so clearing the staged
    // points without it leaves them on screen after they are gone.
    if (auto* gh = getGameHandler(L)) {
        gh->fireAddonEvent("PREVIEW_TALENT_POINTS_CHANGED", {});
    }
    return 0;
}

// LearnPreviewTalents(pet) — commit the staged points
static int lua_LearnPreviewTalents(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    // A copy, because learnTalent goes to the server and what comes back
    // clears this map — iterating it while that happens is not safe.
    const auto staged = previewPoints();
    for (const auto& [id, n] : staged) {
        const uint8_t have = gh->getTalentRank(id);
        // One request per rank: the server counts them, and asking for the
        // final rank in one call is not how the protocol reads it.
        for (int r = 1; r <= n; ++r) gh->learnTalent(id, have + r);
    }
    previewPoints().clear();
    return 0;
}

// LearnTalent(tabIndex, talentIndex, pet, group) — spend one point directly
static int lua_LearnTalent(lua_State* L) {
    auto* gh = getGameHandler(L);
    const uint32_t id = resolveTalentId(gh, static_cast<int>(luaL_optnumber(L, 1, 0)),
                                            static_cast<int>(luaL_optnumber(L, 2, 0)));
    if (gh && id != 0) gh->learnTalent(id, gh->getTalentRank(id) + 1);
    return 0;
}

// GetUnspentTalentPoints(inspect, pet, group) → points
static int lua_GetUnspentTalentPoints(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushnumber(L, 0); return 1; }
    const int group = static_cast<int>(luaL_optnumber(L, 3, 0));
    const uint8_t spec = (group >= 1 && group <= 2) ? static_cast<uint8_t>(group - 1)
                                                   : gh->getActiveTalentSpec();
    int points = gh->getUnspentTalentPoints(spec);
    // Less whatever is staged, or the frame shows points that are already
    // committed in the preview and lets them be spent twice.
    for (const auto& [id, n] : previewPoints()) { (void)id; points -= n; }
    lua_pushnumber(L, points > 0 ? points : 0);
    return 1;
}

// GetNumTalentGroups(inspect, pet) → how many specs the player has
static int lua_GetNumTalentGroups(lua_State* L) {
    auto* gh = getGameHandler(L);
    // Two only once the second is actually bought: the frame draws a spec tab
    // per group, and reporting two unconditionally offers one that is not there.
    int groups = 1;
    if (gh && lua_toboolean(L, 1)) {
        // The inspected player's spec count comes with their talents; the
        // viewer's own says nothing about how many specs the target bought.
        if (const auto* r = gh->getInspectResult()) {
            if (r->talentGroups > 0) groups = r->talentGroups;
        }
        lua_pushnumber(L, groups);
        return 1;
    }
    if (gh && gh->getUnspentTalentPoints(1) > 0) groups = 2;
    if (gh && !gh->getLearnedTalents(1).empty()) groups = 2;
    lua_pushnumber(L, groups);
    return 1;
}

// SetActiveTalentGroup(group)
static int lua_SetActiveTalentGroup(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int group = static_cast<int>(luaL_optnumber(L, 1, 1));
    if (gh && group >= 1 && group <= 2) {
        gh->switchTalentSpec(static_cast<uint8_t>(group - 1));
    }
    return 0;
}

// GetTalentLink(tabIndex, talentIndex, inspect, group) → hyperlink
static int lua_GetTalentLink(lua_State* L) {
    auto* gh = getGameHandler(L);
    const uint32_t id = resolveTalentId(gh, static_cast<int>(luaL_optnumber(L, 1, 0)),
                                            static_cast<int>(luaL_optnumber(L, 2, 0)));
    if (!gh || id == 0) { lua_pushnil(L); return 1; }
    uint32_t rank1 = 0;
    for (const auto& [talentId, entry] : gh->getAllTalents()) {
        if (talentId == id) { rank1 = entry.rankSpells[0]; break; }
    }
    std::string name = gh->getSpellName(rank1);
    if (name.empty()) name = "Talent";
    const std::string link = "|cff4e96f7|Htalent:" + std::to_string(id) + ":" +
                             std::to_string(gh->getTalentRank(id)) + "|h[" + name +
                             "]|h|r";
    lua_pushstring(L, link.c_str());
    return 1;
}

static int lua_GetActiveTalentGroup(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh && lua_toboolean(L, 1)) {
        // Which of the target's specs the talents just read belong to. The
        // inspect frame keeps this as its talentGroup and passes it back into
        // every other talent call.
        const auto* r = gh->getInspectResult();
        lua_pushnumber(L, r ? (r->activeTalentGroup + 1) : 1);
        return 1;
    }
    lua_pushnumber(L, gh ? (gh->getActiveTalentSpec() + 1) : 1);
    return 1;
}


namespace {
/// A template because the dialog carries its rewards in a vector and the quest
/// log in a fixed array, and both are padded the same way.
template <typename Container>
int countRewards(const Container* v) {
    if (!v) return 0;
    int n = 0;
    for (const auto& r : *v) if (r.itemId != 0) ++n;
    return n;
}
}  // namespace

// --- The quest log's own reward panel ---
//
// QuestInfo draws rewards for the quest log as well as for the quest giver, and
// asks a different set of functions for each. The giver's were written earlier;
// these read the same fields out of the log entry the player has selected.

/// The quest log entry the panel is showing, or null.
static const game::GameHandler::QuestLogEntry* selectedLogEntry(game::GameHandler* gh) {
    if (!gh) return nullptr;
    const int idx = gh->getSelectedQuestLogIndex();
    const auto& log = gh->getQuestLog();
    if (idx < 1 || idx > static_cast<int>(log.size())) return nullptr;
    return &log[idx - 1];
}

/// One reward, described the way every reward button reads it.
template <typename Container>
static int pushRewardAt(lua_State* L, game::GameHandler* gh,
                        const Container& list, int index) {
    if (!gh || index < 1) { return luaReturnNil(L); }
    int seen = 0;
    for (const auto& r : list) {
        if (r.itemId == 0) continue;
        if (++seen != index) continue;
        const auto* info = gh->getItemInfo(r.itemId);
        lua_pushstring(L, info ? info->name.c_str() : "");
        lua_pushstring(L, gh->getItemIconPath(
            info && info->displayInfoId ? info->displayInfoId : r.displayInfoId).c_str());
        lua_pushnumber(L, r.count);
        lua_pushnumber(L, info ? info->quality : 1);
        lua_pushboolean(L, 1);
        return 5;
    }
    return luaReturnNil(L);
}

// GetSuggestedGroupNum() / GetQuestLogGroupNum() → how many players a quest
// suggests bringing
//
// QuestInfo assigns one of these and then tests `groupNum > 0`, so an absent
// answer is an error rather than a quest that suggests nothing — and QuestInfo
// draws for the quest giver and the quest log both.
//
// The giver's packet carries the number. The log's does not, and zero is what
// stops the line being drawn at all.
// GetDailyQuestsCompleted() / GetMaxDailyQuests() → the daily allowance
//
// The quest log tests the first against zero before drawing the line at all, so
// an absent answer is an error rather than a hidden line. Nothing here counts
// dailies, and zero completed is what keeps the line hidden — which is the same
// thing the count would do if it were counting and found none.
static int lua_GetDailyQuestsCompleted(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// Twenty-five is the allowance in this era. Only ever shown beside the count
// above, which stays at zero, so it is a label rather than a limit here.
static int lua_GetMaxDailyQuests(lua_State* L) {
    lua_pushnumber(L, 25);
    return 1;
}

static int lua_GetSuggestedGroupNum(lua_State* L) {
    auto* gh = getGameHandler(L);
    const bool open = gh && gh->isQuestDetailsOpen();
    lua_pushnumber(L, open ? gh->getQuestDetails().suggestedPlayers : 0);
    return 1;
}

static int lua_GetQuestLogGroupNum(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

static int lua_GetNumQuestLogRewards(lua_State* L) {
    const auto* q = selectedLogEntry(getGameHandler(L));
    lua_pushnumber(L, q ? countRewards(&q->rewardItems) : 0);
    return 1;
}

static int lua_GetNumQuestLogChoices(lua_State* L) {
    const auto* q = selectedLogEntry(getGameHandler(L));
    lua_pushnumber(L, q ? countRewards(&q->rewardChoiceItems) : 0);
    return 1;
}

static int lua_GetQuestLogRewardInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto* q = selectedLogEntry(gh);
    if (!q) { return luaReturnNil(L); }
    return pushRewardAt(L, gh, q->rewardItems,
                        static_cast<int>(luaL_optnumber(L, 1, 0)));
}

static int lua_GetQuestLogChoiceInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto* q = selectedLogEntry(gh);
    if (!q) { return luaReturnNil(L); }
    return pushRewardAt(L, gh, q->rewardChoiceItems,
                        static_cast<int>(luaL_optnumber(L, 1, 0)));
}

// GetQuestLogItemLink(type, index) → a reward of the selected quest, as a link.
//
// The quest log's own version of GetQuestItemLink, over the same two lists
// GetQuestLogRewardInfo and GetQuestLogChoiceInfo read. Shift-clicking a
// reward in the log put nothing in chat before.
static int lua_GetQuestLogItemLink(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto* q = selectedLogEntry(gh);
    const char* type = luaL_optstring(L, 1, "reward");
    const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
    if (!gh || !q || index < 1) { return luaReturnNil(L); }

    // The two lists are fixed arrays of different lengths, so they are walked
    // by a template rather than bound to one reference.
    const auto linkAt = [&](const auto& list) -> int {
        int seen = 0;
        for (const auto& r : list) {
            if (r.itemId == 0) continue;
            if (++seen != index) continue;
            const auto* info = gh->getItemInfo(r.itemId);
            const std::string name = info ? info->name : "";
            if (name.empty()) break;
            lua_pushstring(L, game::buildItemLink(r.itemId,
                                                  info ? info->quality : 1u,
                                                  name).c_str());
            return 1;
        }
        return luaReturnNil(L);
    };
    return (std::string(type) == "choice") ? linkAt(q->rewardChoiceItems)
                                           : linkAt(q->rewardItems);
}

// Nil for the same reason GetQuestSpellLink is: nothing this client parses
// says a quest gives a spell, so there is no spell to name.
static int lua_GetQuestLogSpellLink(lua_State* L) {
    return luaReturnNil(L);
}

static int lua_GetQuestLogRewardMoney(lua_State* L) {
    const auto* q = selectedLogEntry(getGameHandler(L));
    lua_pushnumber(L, q ? q->rewardMoney : 0);
    return 1;
}

// --- The quest giver's own dialog ---
//
// WoW's quest-giver functions do not name which quest they mean: GetTitleText
// and the reward accessors serve the dialog and the quest log both, and answer
// for whichever is in front. A dialog is in front while it is open — the
// reward panel over the progress panel over the offer, in the order the server
// walks a player through them — and the log's selection answers otherwise.

namespace {

struct QuestSource {
    const std::string* title = nullptr;
    const std::vector<game::QuestRewardItem>* rewards = nullptr;
    const std::vector<game::QuestRewardItem>* choices = nullptr;
    uint32_t money = 0;
    uint32_t xp = 0;
};

QuestSource currentQuestSource(game::GameHandler* gh) {
    QuestSource s;
    if (!gh) return s;
    if (gh->isQuestOfferRewardOpen()) {
        const auto& d = gh->getQuestOfferReward();
        s = {&d.title, &d.fixedRewards, &d.choiceRewards, d.rewardMoney, d.rewardXp};
    } else if (gh->isQuestRequestItemsOpen()) {
        const auto& d = gh->getQuestRequestItems();
        s.title = &d.title;
    } else if (gh->isQuestDetailsOpen()) {
        const auto& d = gh->getQuestDetails();
        s = {&d.title, &d.rewardItems, &d.rewardChoiceItems, d.rewardMoney, d.rewardXp};
    }
    return s;
}

/// Counts only the filled slots: the server pads a reward list to a fixed
/// width, and a row drawn for an empty one is a blank button the player can
/// click.

int pushQuestText(lua_State* L, const std::string* s) {
    lua_pushstring(L, s ? s->c_str() : "");
    return 1;
}

}  // namespace

// GetTitleText() → the quest's name, whichever panel is showing it
static int lua_GetTitleText(lua_State* L) {
    return pushQuestText(L, currentQuestSource(getGameHandler(L)).title);
}

// GetQuestText() → what the quest giver says when offering it
static int lua_GetQuestText(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isQuestDetailsOpen()) { lua_pushstring(L, ""); return 1; }
    return pushQuestText(L, &gh->getQuestDetails().details);
}

// GetObjectiveText() → what the quest asks for
static int lua_GetObjectiveText(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isQuestDetailsOpen()) { lua_pushstring(L, ""); return 1; }
    return pushQuestText(L, &gh->getQuestDetails().objectives);
}

// GetProgressText() → what is said while the quest is still unfinished
static int lua_GetProgressText(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isQuestRequestItemsOpen()) { lua_pushstring(L, ""); return 1; }
    return pushQuestText(L, &gh->getQuestRequestItems().completionText);
}

// GetRewardText() → what is said on handing it in
static int lua_GetRewardText(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isQuestOfferRewardOpen()) { lua_pushstring(L, ""); return 1; }
    return pushQuestText(L, &gh->getQuestOfferReward().rewardText);
}

// GetGossipText() → the body of the gossip window
static int lua_GetGossipText(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isGossipWindowOpen()) { lua_pushstring(L, ""); return 1; }
    return pushQuestText(L, &gh->getNpcText(gh->getCurrentGossip().titleTextId));
}

// GetRewardMoney() → coin the quest pays out
static int lua_GetRewardMoney(lua_State* L) {
    lua_pushnumber(L, currentQuestSource(getGameHandler(L)).money);
    return 1;
}

// GetRewardXP() → experience the quest pays out
static int lua_GetRewardXP(lua_State* L) {
    lua_pushnumber(L, currentQuestSource(getGameHandler(L)).xp);
    return 1;
}

// --- Rewards this client is not told about ---
//
// Honour, arena points and talent points are not in any quest packet this
// client parses, and the quest log is built from SMSG_QUEST_QUERY_RESPONSE,
// which does not carry experience either — in 3.3.5a the client derives that
// from a QuestXP.dbc row index and the quest's level, which nothing here reads.
//
// They answer zero rather than staying absent because the reward panel adds
// and compares them without checking first:
//
//     local totalRewards = numQuestRewards + numQuestChoices + numQuestSpellRewards;
//     if ( numQuestRewards > 0 or money > 0 or honor > 0 or ... )
//
// `or` short-circuits, so `honor > 0` is only reached when there is no fixed
// item reward and no money — which is most quests. Comparing nil against a
// number raises, so the panel died on opening for them. Zero is also the true
// answer for nearly every quest: honour, arena points and talent points are
// rare rewards. A quest that does pay them shows one line short, which is a
// far smaller wrong than a reward panel that will not open.
//
// The title is deliberately left nil. It is read as `not playerTitle`, where
// nil means "no title" and is exactly right.
static int lua_GetZeroReward(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// GetQuestMoneyToGet() → coin the player has to *hand over*, which is the
// opposite of the reward and a different field entirely. Some quests ask for
// money; showing the reward here would tell the player they are being paid
// when they are being charged.
static int lua_GetQuestMoneyToGet(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isQuestRequestItemsOpen()) { lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, gh->getQuestRequestItems().requiredMoney);
    return 1;
}

// IsQuestCompletable() → whether the turn-in button should work
static int lua_IsQuestCompletable(lua_State* L) {
    auto* gh = getGameHandler(L);
    const bool ok = gh && gh->isQuestRequestItemsOpen() &&
                    gh->getQuestRequestItems().isCompletable();
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// GetQuestReward(choiceIndex) → takes the reward and closes the dialog
static int lua_GetQuestReward(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    // One-based, and zero means the quest offered nothing to choose between.
    const int choice = static_cast<int>(luaL_optnumber(L, 1, 0));
    gh->chooseQuestReward(choice > 0 ? static_cast<uint32_t>(choice - 1) : 0);
    return 0;
}

// CloseQuest() → puts the dialog away without answering it
static int lua_CloseQuest(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    if (gh->isQuestOfferRewardOpen())      gh->closeQuestOfferReward();
    else if (gh->isQuestRequestItemsOpen()) gh->closeQuestRequestItems();
    else                                    gh->declineQuest();
    return 0;
}

// GetQuestItemInfo(type, index) → name, texture, count, quality, isUsable
//
// type is "choice" for the rewards a player picks between and "reward" for the
// ones always given.
static int lua_GetQuestItemInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const char* type = luaL_optstring(L, 1, "reward");
    const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
    const QuestSource s = currentQuestSource(gh);

    // Three lists, not two. "required" is what the *progress* page asks for —
    // the items a quest wants handed in — and it was falling through to the
    // reward list, so turning in a quest showed what it would pay rather than
    // what it wanted.
    const std::string want(type);
    const std::vector<game::QuestRewardItem>* list = nullptr;
    if (want == "choice")        list = s.choices;
    else if (want == "required") list = gh ? &gh->getQuestRequestItems().requiredItems : nullptr;
    else                         list = s.rewards;
    if (!gh || !list || index < 1) { return luaReturnNil(L); }

    int seen = 0;
    for (const auto& r : *list) {
        if (r.itemId == 0) continue;
        if (++seen != index) continue;
        const auto* info = gh->getItemInfo(r.itemId);
        lua_pushstring(L, info ? info->name.c_str() : "");
        lua_pushstring(L, gh->getItemIconPath(
            info && info->displayInfoId ? info->displayInfoId : r.displayInfoId).c_str());
        lua_pushnumber(L, r.count);
        lua_pushnumber(L, info ? info->quality : 1);
        lua_pushboolean(L, 1);
        return 5;
    }
    return luaReturnNil(L);
}

// GetQuestItemLink(type, index) → the reward as a link, for shift-clicking it
// into chat.
//
// The same two lists GetQuestItemInfo walks, answered as a link rather than as
// pieces. Nil when there is no such reward, which is what the click handler
// checks before doing anything with it.
static int lua_GetQuestItemLink(lua_State* L) {
    auto* gh = getGameHandler(L);
    const char* type = luaL_optstring(L, 1, "reward");
    const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
    const QuestSource s = currentQuestSource(gh);

    const auto* list = (std::string(type) == "choice") ? s.choices : s.rewards;
    if (!gh || !list || index < 1) { return luaReturnNil(L); }

    int seen = 0;
    for (const auto& r : *list) {
        if (r.itemId == 0) continue;
        if (++seen != index) continue;
        const auto* info = gh->getItemInfo(r.itemId);
        const std::string name = info ? info->name : "";
        if (name.empty()) { return luaReturnNil(L); }
        lua_pushstring(L, game::buildItemLink(r.itemId,
                                              info ? info->quality : 1u,
                                              name).c_str());
        return 1;
    }
    return luaReturnNil(L);
}

// GetQuestSpellLink(...) — the spell a quest gives, as a link.
//
// Nil, and deliberately: no quest packet this client parses carries a reward
// spell, so there is nothing to name. The click handler passes whatever it
// gets to HandleModifiedItemClick, which does nothing with nil — where a
// made-up link would put a spell in someone's chat that the quest does not
// give.
static int lua_GetQuestSpellLink(lua_State* L) {
    return luaReturnNil(L);
}

void registerQuestLuaAPI(lua_State* L) {
    static const struct { const char* name; lua_CFunction func; } api[] = {
                {"GetNumQuestLogEntries",   lua_GetNumQuestLogEntries},
                {"GetQuestLogTimeLeft",     lua_GetQuestLogTimeLeft},
                {"IsCurrentQuestFailed",    lua_IsCurrentQuestFailed},
                {"GetQuestLogRewardSpell",  lua_GetQuestRewardSpell},
                {"GetRewardSpell",          lua_GetQuestRewardSpell},
                {"GetRewardTitle",          lua_GetQuestRewardTitle},
                {"GetQuestLogRewardTitle",  lua_GetQuestRewardTitle},
                {"ProcessQuestLogRewardFactions", lua_ProcessQuestLogRewardFactions},
                {"GetQuestLogRewardFactionInfo",  lua_GetQuestLogRewardFactionInfo},
                {"GetFactionInfoByID",      lua_GetFactionInfoByID},
                {"GetQuestWatchIndex",      lua_GetQuestWatchIndex},
                {"SortQuestWatches",        lua_SortQuestWatches},
                {"ShiftQuestWatches",       lua_ShiftQuestWatches},
                {"GetQuestSortIndex",       lua_GetQuestSortIndex},
                {"IsQuestLogSpecialItemInRange", lua_IsQuestLogSpecialItemInRange},
                {"UseQuestLogSpecialItem",  lua_UseQuestLogSpecialItem},
                {"GetQuestLogSpecialItemCooldown", lua_GetQuestLogSpecialItemCooldown},
                {"GetQuestTimers",          lua_GetQuestTimers},
                {"GetQuestIndexForTimer",   lua_GetQuestIndexForTimer},
                {"GetQuestLogTitle",        lua_GetQuestLogTitle},
                // IsUnitOnQuest(questIndex, unit) — whether that unit is also
                // on the quest, which the log prints as "[2]" beside an entry
                // to say how many group mates share it.
                //
                // Only answerable for the player: the server does not tell a
                // client what its party members' quest logs hold, and nothing
                // here tracks them. False for everyone else is the truthful
                // answer and is what leaves the counter hidden, rather than
                // claiming a number nobody can stand behind.
                {"IsUnitOnQuest", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
            const char* uid = luaL_optstring(L, 2, "player");
            std::string u(uid);
            for (char& c : u) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!gh || u != "player" || index < 1) { lua_pushboolean(L, 0); return 1; }
            lua_pushboolean(L, index <= static_cast<int>(gh->getQuestLog().size()) ? 1 : 0);
            return 1;
        }},
                {"GetQuestLogQuestText",    lua_GetQuestLogQuestText},
                // The line a quest shows once its objectives are done. Not in
                // anything this client parses — the quest log is built from
                // SMSG_QUEST_QUERY_RESPONSE, which does not carry it.
                //
                // An empty string rather than nothing, because the world map
                // hovers a finished quest's pin and writes
                // AddLine("- "..GetQuestLogCompletionText(i)) without checking.
                // Concatenating nil raises, so the pin would have taken the map
                // down; concatenating an empty string draws a bare dash.
                {"GetQuestLogCompletionText", [](lua_State* L) -> int {
            lua_pushstring(L, "");
            return 1;
        }},
                // The words on a book or a plaque, which this client parses out
                // of SMSG_ITEM_TEXT_QUERY_RESPONSE and kept to itself. This
                // answered with an empty string on the reasoning that nothing
                // opened the window anyway; ITEM_TEXT_READY is fired now, so it
                // does open, and empty is the difference between a book and a
                // blank page.
                //
                // Still a string and never nil: the page is drawn as
                // "\n"..ItemTextGetText()..creator, and a nil there takes the
                // whole window down rather than leaving it empty.
                {"ItemTextGetText", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushstring(L, gh ? gh->getItemText().c_str() : "");
            return 1;
        }},
                // What the wire does not carry. The frame guards all three —
                // a nil material becomes "Parchment", a nil creator drops the
                // "from" line, and SetText takes a nil as no text — so absent
                // is both honest and safe, where a made-up title or author
                // would be neither.
                {"ItemTextGetItem",     [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"ItemTextGetCreator",  [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"ItemTextGetMaterial", [](lua_State* L) -> int { return luaReturnNil(L); }},
                // One page. The response carries a single body of text with no
                // pagination, so the turn-page buttons have nowhere to go and
                // the frame hides them when told there is no next page.
                {"ItemTextGetPage", [](lua_State* L) -> int {
            lua_pushnumber(L, 1);
            return 1;
        }},
                {"ItemTextHasNextPage", [](lua_State* L) -> int {
            lua_pushboolean(L, 0);
            return 1;
        }},
                {"ItemTextPrevPage", [](lua_State*) -> int { return 0; }},
                {"ItemTextNextPage", [](lua_State*) -> int { return 0; }},
                // Closing is a state change this client owns, and the frame
                // calls it on hide as well as from its close button.
                {"CloseItemText", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->closeItemText();
            return 0;
        }},
                {"IsQuestComplete",         lua_IsQuestComplete},
                {"SelectQuestLogEntry",     lua_SelectQuestLogEntry},
                {"GetQuestLogSelection",    lua_GetQuestLogSelection},
                {"GetQuestLogPushable",     lua_GetQuestLogPushable},
                {"QuestLogPushQuest",       lua_QuestLogPushQuest},
                {"GetNumQuestWatches",      lua_GetNumQuestWatches},
                {"GetQuestIndexForWatch",   lua_GetQuestIndexForWatch},
                {"AddQuestWatch",           lua_AddQuestWatch},
                {"RemoveQuestWatch",        lua_RemoveQuestWatch},
                {"IsQuestWatched",          lua_IsQuestWatched},
                {"GetQuestLink",            lua_GetQuestLink},
                {"GetNumQuestLeaderBoards", lua_GetNumQuestLeaderBoards},
                {"GetQuestLogLeaderBoard",  lua_GetQuestLogLeaderBoard},
                {"QuestMapUpdateAllQuests", lua_QuestMapUpdateAllQuests},
                {"QuestPOIGetQuestIDByVisibleIndex", lua_QuestPOIGetQuestIDByVisibleIndex},
                {"QuestPOIGetIconInfo",     lua_QuestPOIGetIconInfo},
                {"QuestPOIUpdateIcons",     lua_QuestPOIUpdateIcons},
                {"GetQuestPOILeaderBoard",  lua_GetQuestPOILeaderBoard},
                {"ExpandQuestHeader",       lua_ExpandQuestHeader},
                {"CollapseQuestHeader",     lua_CollapseQuestHeader},
                {"GetQuestLogSpecialItemInfo", lua_GetQuestLogSpecialItemInfo},
                {"GetNumSkillLines",        lua_GetNumSkillLines},
                {"GetStablePetInfo",       lua_GetStablePetInfo},
                {"GetNumStablePets",       lua_GetNumStablePets},
                {"GetNumStableSlots",      lua_GetNumStableSlots},
                {"GetSelectedStablePet",   lua_GetSelectedStablePet},
                {"ClickStablePet",         lua_ClickStablePet},
                {"GetPetIcon",             lua_GetPetIcon},
                {"GetPetTalentTree",       lua_GetPetTalentTree},
                {"GetPetFoodTypes",        lua_GetPetFoodTypes},
                {"GetStablePetFoodTypes",  lua_GetStablePetFoodTypes},
                {"SetPetStablePaperdoll",  lua_SetPetStablePaperdoll},
                {"PickupStablePet",        lua_PickupStablePet},
                {"ClosePetStables",        lua_ClosePetStables},
                {"IsAtStableMaster",       lua_IsAtStableMaster},
                {"GetNextStableSlotCost",  lua_GetNextStableSlotCost},
                {"GetSkillLineInfo",        lua_GetSkillLineInfo},
                // GetSkillLineInfo reports isHeader false for every row, so the
                // skills list has no headers to open or close. Bound rather
                // than left out because the click that calls them is on the
                // row label template, which the tab builds for whatever the
                // data gives it — the guard is in the data, not in the frame.
                {"ExpandSkillHeader",       [](lua_State* L) -> int { (void)L; return 0; }},
                {"CollapseSkillHeader",     [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetNumTalentTabs",        lua_GetNumTalentTabs},
                {"GetTalentTabInfo",        lua_GetTalentTabInfo},
                {"GetNumTalents",           lua_GetNumTalents},
                {"GetTalentInfo",           lua_GetTalentInfo},
                {"GetTalentPrereqs",        lua_GetTalentPrereqs},
                {"AddPreviewTalentPoints",  lua_AddPreviewTalentPoints},
                {"GetGroupPreviewTalentPointsSpent", lua_GetGroupPreviewTalentPointsSpent},
                {"ResetGroupPreviewTalentPoints",    lua_ResetGroupPreviewTalentPoints},
                {"LearnPreviewTalents",     lua_LearnPreviewTalents},
                {"LearnTalent",             lua_LearnTalent},
                {"GetUnspentTalentPoints",  lua_GetUnspentTalentPoints},
                {"GetNumTalentGroups",      lua_GetNumTalentGroups},
                {"SetActiveTalentGroup",    lua_SetActiveTalentGroup},
                {"GetTalentLink",           lua_GetTalentLink},
                {"GetActiveTalentGroup",    lua_GetActiveTalentGroup},
                {"AcceptQuest", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->acceptQuest();
            return 0;
        }},
                {"DeclineQuest", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->declineQuest();
            return 0;
        }},
                {"CompleteQuest", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->completeQuest();
            return 0;
        }},
                // Abandoning is two steps, not one: the quest log marks which
                // quest it means, a confirmation shows its name, and only then
                // is it abandoned. AbandonQuest therefore takes no argument in
                // the interface, and requiring one raised a Lua error on every
                // attempt — the id is accepted when given so anything already
                // passing one keeps working.
                {"SetAbandonQuest", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const int idx = gh->getSelectedQuestLogIndex();
            const auto& log = gh->getQuestLog();
            pendingAbandonQuest() =
                (idx >= 1 && idx <= static_cast<int>(log.size())) ? log[idx - 1].questId : 0;
            return 0;
        }},
                {"GetAbandonQuestName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = pendingAbandonQuest();
            if (gh && id != 0) {
                for (const auto& q : gh->getQuestLog()) {
                    if (q.questId == id) { lua_pushstring(L, q.title.c_str()); return 1; }
                }
            }
            lua_pushstring(L, "");
            return 1;
        }},
                // What is destroyed along with the quest. Nothing here knows
                // which items a quest would take back, and an invented list
                // would warn about items the player keeps.
                {"GetAbandonQuestItems", [](lua_State* L) -> int {
            lua_pushstring(L, "");
            return 1;
        }},
                {"AbandonQuest", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const uint32_t questId = lua_isnoneornil(L, 1)
                ? pendingAbandonQuest()
                : static_cast<uint32_t>(luaL_checknumber(L, 1));
            if (questId != 0) gh->abandonQuest(questId);
            pendingAbandonQuest() = 0;
            return 0;
        }},
                // Both of these answer for the quest giver's dialog while one
                // is open and for the log's selection otherwise, which is the
                // same rule the text accessors follow.
                {"GetNumQuestRewards", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { return luaReturnZero(L); }
            if (const auto* v = currentQuestSource(gh).rewards) {
                lua_pushnumber(L, countRewards(v));
                return 1;
            }
            int idx = gh->getSelectedQuestLogIndex();
            if (idx < 1) { return luaReturnZero(L); }
            const auto& ql = gh->getQuestLog();
            if (idx > static_cast<int>(ql.size())) { return luaReturnZero(L); }
            lua_pushnumber(L, countRewards(&ql[idx-1].rewardItems));
            return 1;
        }},
                {"GetNumQuestChoices", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { return luaReturnZero(L); }
            if (const auto* v = currentQuestSource(gh).choices) {
                lua_pushnumber(L, countRewards(v));
                return 1;
            }
            int idx = gh->getSelectedQuestLogIndex();
            if (idx < 1) { return luaReturnZero(L); }
            const auto& ql = gh->getQuestLog();
            if (idx > static_cast<int>(ql.size())) { return luaReturnZero(L); }
            lua_pushnumber(L, countRewards(&ql[idx-1].rewardChoiceItems));
            return 1;
        }},
                {"GetSuggestedGroupNum", lua_GetSuggestedGroupNum},
                {"GetDailyQuestsCompleted", lua_GetDailyQuestsCompleted},
                {"GetMaxDailyQuests",    lua_GetMaxDailyQuests},
                {"GetQuestLogGroupNum",  lua_GetQuestLogGroupNum},
                {"GetNumQuestLogRewards", lua_GetNumQuestLogRewards},
                {"GetNumQuestLogChoices", lua_GetNumQuestLogChoices},
                {"GetQuestLogRewardInfo", lua_GetQuestLogRewardInfo},
                {"GetQuestLogChoiceInfo", lua_GetQuestLogChoiceInfo},
                {"GetQuestLogItemLink",     lua_GetQuestLogItemLink},
                {"GetQuestLogSpellLink",    lua_GetQuestLogSpellLink},
                {"GetQuestLogRewardMoney", lua_GetQuestLogRewardMoney},
                {"GetTitleText",         lua_GetTitleText},
                {"GetQuestText",         lua_GetQuestText},
                {"GetObjectiveText",     lua_GetObjectiveText},
                {"GetProgressText",      lua_GetProgressText},
                {"GetRewardText",        lua_GetRewardText},
                {"GetGossipText",        lua_GetGossipText},
                {"GetQuestItemInfo",     lua_GetQuestItemInfo},
                // How many items a quest wants handed in.
                // QuestFrameProgressItems_Update reads it straight into
                // `numRequiredItems > 0`, and comparing nil against a number
                // raises — so opening the progress page of any quest that
                // takes items took the page down.
                {"GetNumQuestItems", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnumber(L, 0); return 1; }
            const auto& req = gh->getQuestRequestItems().requiredItems;
            int n = 0;
            for (const auto& r : req) if (r.itemId != 0) ++n;
            lua_pushnumber(L, n);
            return 1;
        }},
                // The parchment behind the quest text. The caller substitutes
                // "Parchment" for a nil, which is the only material this
                // client has art for, so nil is both honest and correct.
                {"GetQuestBackgroundMaterial", [](lua_State* L) -> int { return luaReturnNil(L); }},
                // Whether the quest is flagged PvP, and whether the giver
                // opened it without being asked. Neither is parsed from the
                // quest packets here, and false is what the interface does
                // with an absent answer anyway.
                {"QuestFlagsPVP",      [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"QuestGetAutoAccept", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                // Shown when a reward is confirmed without one being picked.
                // The message belongs to the server in the real client; there
                // is nothing to say here, and the call is made for its effect
                // rather than its answer.
                {"QuestChooseRewardError", [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetQuestItemLink",        lua_GetQuestItemLink},
                {"GetQuestSpellLink",       lua_GetQuestSpellLink},
                {"GetQuestMoneyToGet",   lua_GetQuestMoneyToGet},
                {"GetRewardMoney",       lua_GetRewardMoney},
                {"GetRewardXP",          lua_GetRewardXP},
                {"GetRewardHonor",              lua_GetZeroReward},
                {"GetRewardArenaPoints",        lua_GetZeroReward},
                {"GetRewardTalents",            lua_GetZeroReward},
                {"GetQuestLogRewardHonor",       lua_GetZeroReward},
                {"GetQuestLogRewardArenaPoints", lua_GetZeroReward},
                {"GetQuestLogRewardTalents",     lua_GetZeroReward},
                {"GetQuestLogRewardXP",          lua_GetZeroReward},
                {"IsQuestCompletable",   lua_IsQuestCompletable},
                {"GetQuestReward",       lua_GetQuestReward},
                {"CloseQuest",           lua_CloseQuest},
                {"GetNumGlyphSockets", [](lua_State* L) -> int {
            lua_pushnumber(L, game::GameHandler::MAX_GLYPH_SLOTS);
            return 1;
        }},
                {"GetGlyphSocketInfo", [](lua_State* L) -> int {
            // GetGlyphSocketInfo(index [, talentGroup]) → enabled, glyphType, glyphSpellID, icon
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            int spec = static_cast<int>(luaL_optnumber(L, 2, 0));
            if (!gh || index < 1 || index > game::GameHandler::MAX_GLYPH_SLOTS) {
                lua_pushboolean(L, 0); lua_pushnumber(L, 0); lua_pushnil(L); lua_pushnil(L);
                return 4;
            }
            const auto& glyphs = (spec >= 1 && spec <= 2)
                ? gh->getGlyphs(static_cast<uint8_t>(spec - 1)) : gh->getGlyphs();
            uint16_t glyphId = glyphs[index - 1];
            // Glyph type: slots 1,2,3 = major (1), slots 4,5,6 = minor (2)
            int glyphType = (index <= 3) ? 1 : 2;
            lua_pushboolean(L, 1);              // enabled
            lua_pushnumber(L, glyphType);       // glyphType (1=major, 2=minor)
            if (glyphId != 0) {
                lua_pushnumber(L, glyphId);     // glyphSpellID
                lua_pushstring(L, "Interface\\Icons\\INV_Glyph_MajorWarrior"); // placeholder icon
            } else {
                lua_pushnil(L);
                lua_pushnil(L);
            }
            return 4;
        }},
                // ---- Trade skills ---------------------------------------
                //
                // The recipe list comes from GameHandler so this panel and the
                // client's own crafting window agree on every number.
                {"GetNumTradeSkills", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? static_cast<double>(
                gh->getCraftingRecipes().size()) : 0.0);
            return 1;
        }},
                // GetTradeSkillInfo(i) → name, type, numAvailable, isExpanded,
                //                        altVerb, numSkillUps
                {"GetTradeSkillInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh) return luaReturnNil(L);
            const auto recipes = gh->getCraftingRecipes();
            if (i < 1 || i > static_cast<int>(recipes.size())) return luaReturnNil(L);
            const auto& r = recipes[i - 1];
            static const char* kBands[4] = {"optimal", "medium", "easy", "trivial"};
            lua_pushstring(L, r.name.c_str());
            lua_pushstring(L, kBands[r.difficulty < 0 || r.difficulty > 3
                                         ? 0 : r.difficulty]);
            lua_pushnumber(L, r.canMake);
            lua_pushboolean(L, 1);      // expanded: this list has no headers
            lua_pushnil(L);             // altVerb
            lua_pushnumber(L, 1);       // numSkillUps
            return 6;
        }},
                {"GetTradeSkillIcon", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh) return luaReturnNil(L);
            const auto recipes = gh->getCraftingRecipes();
            if (i < 1 || i > static_cast<int>(recipes.size())) return luaReturnNil(L);
            const std::string icon = gh->getSpellIconPath(recipes[i - 1].spellId);
            lua_pushstring(L, icon.empty()
                ? "Interface\\Icons\\INV_Misc_QuestionMark" : icon.c_str());
            return 1;
        }},
                {"GetTradeSkillDescription", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh) return luaReturnNil(L);
            const auto recipes = gh->getCraftingRecipes();
            if (i < 1 || i > static_cast<int>(recipes.size())) return luaReturnNil(L);
            const uint32_t id = recipes[i - 1].spellId;
            lua_pushstring(L, gh->formatSpellDescription(
                id, gh->getSpellDescription(id)).c_str());
            return 1;
        }},
                {"GetTradeSkillNumReagents", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            int n = 0;
            if (gh) {
                const auto recipes = gh->getCraftingRecipes();
                if (i >= 1 && i <= static_cast<int>(recipes.size())) {
                    auto it = gh->spellNameCacheRef().find(recipes[i - 1].spellId);
                    if (it != gh->spellNameCacheRef().end()) {
                        for (const auto& r : it->second.reagents) {
                            if (r.itemId != 0) ++n;
                        }
                    }
                }
            }
            lua_pushnumber(L, n);
            return 1;
        }},
                // GetTradeSkillReagentInfo(i, n) → name, texture, needed, have
                {"GetTradeSkillReagentInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const int n = static_cast<int>(luaL_optnumber(L, 2, 1));
            if (!gh) return luaReturnNil(L);
            const auto recipes = gh->getCraftingRecipes();
            if (i < 1 || i > static_cast<int>(recipes.size())) return luaReturnNil(L);
            auto it = gh->spellNameCacheRef().find(recipes[i - 1].spellId);
            if (it == gh->spellNameCacheRef().end()) return luaReturnNil(L);
            int seen = 0;
            for (const auto& r : it->second.reagents) {
                if (r.itemId == 0) continue;
                if (++seen != n) continue;
                gh->ensureItemInfo(r.itemId);
                const auto* info = gh->getItemInfo(r.itemId);
                lua_pushstring(L, info ? info->name.c_str() : "Reagent");
                const std::string ricon =
                    info ? gh->getItemIconPath(info->displayInfoId) : std::string();
                lua_pushstring(L, ricon.empty()
                    ? "Interface\\Icons\\INV_Misc_QuestionMark" : ricon.c_str());
                lua_pushnumber(L, r.count);
                lua_pushnumber(L, gh->countItemInBags(r.itemId));
                return 4;
            }
            return luaReturnNil(L);
        }},
                {"GetTradeSkillNumMade", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            int lo = 1, hi = 1;
            if (gh) {
                const auto recipes = gh->getCraftingRecipes();
                if (i >= 1 && i <= static_cast<int>(recipes.size())) {
                    auto it = gh->spellNameCacheRef().find(recipes[i - 1].spellId);
                    if (it != gh->spellNameCacheRef().end()) {
                        // The count is not in what this client parses; one is
                        // right for the great majority of recipes and wrong
                        // only in the amount, never in the item.
                        (void)it;
                    }
                }
            }
            lua_pushnumber(L, lo);
            lua_pushnumber(L, hi);
            return 2;
        }},
                // GetTradeSkillLine() → name, rank, maxRank
                {"GetTradeSkillLine", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return luaReturnNil(L);
            const uint32_t line = gh->getCraftingSkillLine();
            std::string name = gh->getSkillLineName(line);
            if (name.empty()) name = "Trade Skill";
            uint32_t rank = 0, maxRank = 0;
            auto it = gh->getPlayerSkills().find(line);
            if (it != gh->getPlayerSkills().end()) {
                rank    = it->second.effectiveValue();
                maxRank = it->second.maxValue;
            }
            lua_pushstring(L, name.c_str());
            lua_pushnumber(L, rank);
            lua_pushnumber(L, maxRank);
            return 3;
        }},
                {"DoTradeSkill", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh) return 0;
            const auto recipes = gh->getCraftingRecipes();
            if (i < 1 || i > static_cast<int>(recipes.size())) return 0;
            gh->castSpell(recipes[i - 1].spellId);
            return 0;
        }},
                {"CloseTradeSkill", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->closeCraftingWindow();
            return 0;
        }},
                {"SelectTradeSkill", [](lua_State* L) -> int {
            tradeSkillSelection() = static_cast<int>(luaL_optnumber(L, 1, 0));
            return 0;
        }},
                {"GetTradeSkillSelectionIndex", [](lua_State* L) -> int {
            lua_pushnumber(L, tradeSkillSelection());
            return 1;
        }},
                {"GetFirstTradeSkill", [](lua_State* L) -> int {
            // No headers in this list, so the first recipe is the first row.
            lua_pushnumber(L, 1);
            return 1;
        }},
                {"GetTradeSkillCooldown", [](lua_State* L) -> int {
            return luaReturnNil(L);   // nil means "not on cooldown"
        }},
                {"GetTradeSkillTools", [](lua_State* L) -> int {
            // The tool requirement is not in what this client parses, and
            // claiming a tool is missing would grey out recipes that work.
            return luaReturnNil(L);
        }},
                // Links, which need an item this client does not resolve for a
                // recipe, and the play-time limits that only Chinese realms set.
                {"GetTradeSkillItemLink",     [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"GetTradeSkillRecipeLink",   [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"GetTradeSkillReagentItemLink", [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"GetTradeSkillListLink",     [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"IsTradeSkillLinked",        [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"NoPlayTime",                [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"PartialPlayTime",           [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"GetBillingTimeRested",      [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
                // Filters and sub-classes: panel state, kept so it reads back.
                {"GetTradeSkillSubClasses",   [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetTradeSkillInvSlots",     [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetTradeSkillSubClassFilter", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
                {"GetTradeSkillInvSlotFilter",  [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
                {"SetTradeSkillSubClassFilter", [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetTradeSkillInvSlotFilter",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetTradeSkillItemNameFilter", [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetTradeSkillItemLevelFilter",[](lua_State* L) -> int { (void)L; return 0; }},
                {"TradeSkillOnlyShowMakeable",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"CollapseTradeSkillSubClass",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"ExpandTradeSkillSubClass",    [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetTradeskillRepeatCount",    [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
                {"StopTradeSkillRepeat",        [](lua_State* L) -> int { (void)L; return 0; }},
                // ---- Trainer -------------------------------------------
                //
                // The client has parsed the trainer list all along — spell,
                // cost, required level and skill, and whether it is already
                // known — and fires TRAINER_SHOW when it arrives. None of it
                // had a way into the interface, so the panel opened blank at
                // every trainer in the game.
                {"GetNumTrainerServices", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? static_cast<double>(
                gh->getTrainerSpells().spells.size()) : 0.0);
            return 1;
        }},
                // GetTrainerServiceInfo(i) → name, rank, category, expanded
                {"GetTrainerServiceInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh) return luaReturnNil(L);
            const auto& list = gh->getTrainerSpells().spells;
            if (i < 1 || i > static_cast<int>(list.size())) return luaReturnNil(L);
            const auto& sp = list[i - 1];
            std::string name = gh->getSpellName(sp.spellId);
            if (name.empty()) name = "Spell " + std::to_string(sp.spellId);
            lua_pushstring(L, name.c_str());
            lua_pushstring(L, "");   // rank, which this list does not carry
            // The three words the panel colours by: green, grey, and already
            // trained.
            lua_pushstring(L, sp.state == 0   ? "available"
                            : sp.state == 2   ? "used"
                                              : "unavailable");
            lua_pushboolean(L, 1);   // expanded: the list here is flat
            return 4;
        }},
                // Three costs: coin, talent points, and a profession slot.
                // The trainer reads the third bare — `if ( cpCost2 > 0 )` — so
                // one value made selecting anything a trainer offers an error.
                //
                // Only the coin is known here. Nothing in the trainer list says
                // which service is a profession, so the other two are zero,
                // which is right for the spells and recipes that make up nearly
                // all of it; the consequence is that learning a profession asks
                // for money without the extra confirmation about slots.
                {"GetTrainerServiceCost", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            const bool have = list && i >= 1 && i <= static_cast<int>(list->size());
            lua_pushnumber(L, have ? (*list)[i - 1].spellCost : 0);
            lua_pushnumber(L, 0);
            lua_pushnumber(L, 0);
            return 3;
        }},
                {"GetTrainerServiceLevelReq", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            if (!list || i < 1 || i > static_cast<int>(list->size())) {
                lua_pushnumber(L, 0);
                return 1;
            }
            lua_pushnumber(L, (*list)[i - 1].reqLevel);
            return 1;
        }},
                // GetTrainerServiceSkillReq(i) → skill name, required value
                {"GetTrainerServiceSkillReq", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            if (!list || i < 1 || i > static_cast<int>(list->size())) {
                return luaReturnNil(L);
            }
            const auto& sp = (*list)[i - 1];
            if (sp.reqSkill == 0) return luaReturnNil(L);
            const std::string skill = gh->getSkillLineName(sp.reqSkill);
            lua_pushstring(L, skill.empty() ? "Skill" : skill.c_str());
            lua_pushnumber(L, sp.reqSkillValue);
            // Whether the player already meets it. The trainer window picks
            // between TRAINER_REQ_SKILL_RANK and its _RED twin on this, so
            // leaving it nil painted every requirement red — including the
            // ones already satisfied, next to a spell the player could train.
            const auto& skills = gh->getPlayerSkills();
            const auto it = skills.find(sp.reqSkill);
            const bool met = it != skills.end() &&
                             it->second.effectiveValue() >= sp.reqSkillValue;
            lua_pushboolean(L, met ? 1 : 0);
            return 3;
        }},
                {"GetTrainerServiceIcon", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            if (!list || i < 1 || i > static_cast<int>(list->size())) {
                return luaReturnNil(L);
            }
            const std::string icon = gh->getSpellIconPath((*list)[i - 1].spellId);
            lua_pushstring(L, icon.empty()
                ? "Interface\\Icons\\INV_Misc_QuestionMark" : icon.c_str());
            return 1;
        }},
                {"GetTrainerServiceDescription", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            if (!list || i < 1 || i > static_cast<int>(list->size())) {
                return luaReturnNil(L);
            }
            const uint32_t id = (*list)[i - 1].spellId;
            const std::string desc =
                gh->formatSpellDescription(id, gh->getSpellDescription(id));
            lua_pushstring(L, desc.c_str());
            return 1;
        }},
                {"GetTrainerServiceSkillLine", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            if (!list || i < 1 || i > static_cast<int>(list->size())) {
                return luaReturnNil(L);
            }
            const std::string skill = gh->getSkillLineName((*list)[i - 1].reqSkill);
            lua_pushstring(L, skill.empty() ? "" : skill.c_str());
            return 1;
        }},
                // The prerequisite chain, which this list carries as up to
                // three spell ids.
                {"GetTrainerServiceNumAbilityReq", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            if (!list || i < 1 || i > static_cast<int>(list->size())) {
                lua_pushnumber(L, 0);
                return 1;
            }
            const auto& sp = (*list)[i - 1];
            int n = 0;
            if (sp.chainNode1) ++n;
            if (sp.chainNode2) ++n;
            if (sp.chainNode3) ++n;
            lua_pushnumber(L, n);
            return 1;
        }},
                // GetTrainerServiceAbilityReq(i, n) → name, hasIt
                {"GetTrainerServiceAbilityReq", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            const int n = static_cast<int>(luaL_optnumber(L, 2, 1));
            const auto* list = gh ? &gh->getTrainerSpells().spells : nullptr;
            if (!list || i < 1 || i > static_cast<int>(list->size())) {
                return luaReturnNil(L);
            }
            const auto& sp = (*list)[i - 1];
            const uint32_t chain[3] = {sp.chainNode1, sp.chainNode2, sp.chainNode3};
            if (n < 1 || n > 3 || chain[n - 1] == 0) return luaReturnNil(L);
            const uint32_t req = chain[n - 1];
            std::string name = gh->getSpellName(req);
            if (name.empty()) name = "Ability";
            lua_pushstring(L, name.c_str());
            lua_pushboolean(L, gh->getKnownSpells().count(req) ? 1 : 0);
            return 2;
        }},
                // GetTrainerServiceStepReq(i) → step, met.
                //
                // Nil, not zero. The trainer window does `if ( step ) then`
                // and zero is truthy in Lua, so answering 0 claimed every
                // service had a step requirement — and with `met` missing it
                // took the red branch, printing a bogus unmet requirement of
                // "0" beside every spell on the list.
                //
                // No step requirement is tracked here, and saying so is both
                // true and what removes the line.
                {"GetTrainerServiceStepReq", [](lua_State* L) -> int {
            return luaReturnNil(L);
        }},
                {"GetTrainerServiceItemLink", [](lua_State* L) -> int {
            return luaReturnNil(L);
        }},
                {"GetTrainerGreetingText", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const std::string& g = gh ? gh->getTrainerSpells().greeting
                                      : std::string();
            lua_pushstring(L, g.c_str());
            return 1;
        }},
                {"IsTradeskillTrainer", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushboolean(L, gh && gh->getTrainerSpells().trainerType == 2);
            return 1;
        }},
                {"BuyTrainerService", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh) return 0;
            const auto& list = gh->getTrainerSpells().spells;
            if (i < 1 || i > static_cast<int>(list.size())) return 0;
            gh->trainSpell(list[i - 1].spellId);
            return 0;
        }},
                {"CloseTrainer", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->closeTrainer();
            return 0;
        }},
                // Selection and the type filter are the panel's own state; the
                // client has no opinion about either, so they are kept here
                // rather than pretended away — the panel reads back what it set.
                {"SelectTrainerService", [](lua_State* L) -> int {
            trainerSelection() = static_cast<int>(luaL_optnumber(L, 1, 0));
            return 0;
        }},
                {"GetTrainerSelectionIndex", [](lua_State* L) -> int {
            lua_pushnumber(L, trainerSelection());
            return 1;
        }},
                {"SetTrainerServiceTypeFilter", [](lua_State* L) -> int {
            const char* which = luaL_optstring(L, 1, "");
            trainerFilters()[which] = lua_toboolean(L, 2) != 0;
            return 0;
        }},
                {"GetTrainerServiceTypeFilter", [](lua_State* L) -> int {
            const char* which = luaL_optstring(L, 1, "");
            auto it = trainerFilters().find(which);
            // Unset means showing, which is how a freshly opened panel looks.
            lua_pushboolean(L, it == trainerFilters().end() ? 1 : (it->second ? 1 : 0));
            return 1;
        }},
                // The list this client builds is flat, so there is no header to
                // fold; the panel calls these when its own headers are clicked
                // and expects nothing back.
                {"CollapseTrainerSkillLine", [](lua_State* L) -> int { (void)L; return 0; }},
                {"ExpandTrainerSkillLine",   [](lua_State* L) -> int { (void)L; return 0; }},
                // GetGlyphLink(socket [, talentGroup]) → hyperlink, or nil
                {"GetGlyphLink", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
            const int spec  = static_cast<int>(luaL_optnumber(L, 2, 0));
            if (!gh || index < 1 || index > game::GameHandler::MAX_GLYPH_SLOTS) {
                lua_pushnil(L);
                return 1;
            }
            const auto& glyphs = (spec >= 1 && spec <= 2)
                ? gh->getGlyphs(static_cast<uint8_t>(spec - 1)) : gh->getGlyphs();
            const uint16_t glyphId = glyphs[index - 1];
            if (glyphId == 0) { lua_pushnil(L); return 1; }
            std::string name = gh->getSpellName(glyphId);
            if (name.empty()) name = "Glyph";
            const std::string link = "|cff66bbff|Hglyph:" + std::to_string(index) +
                                     ":" + std::to_string(glyphId) + "|h[" + name +
                                     "]|h|r";
            lua_pushstring(L, link.c_str());
            return 1;
        }},
                // GlyphMatchesSocket(socket) → whether what is on the cursor
                // fits. Always false: this client does not track a glyph on the
                // cursor, and answering yes would light every empty socket as a
                // place to drop something that cannot be dropped.
                {"GlyphMatchesSocket", [](lua_State* L) -> int {
            lua_pushboolean(L, 0);
            return 1;
        }},
                // PlaceGlyphInSocket(socket). Socketing needs a packet this
                // client does not send, so this says so once rather than
                // failing quietly — a button that looks live and does nothing
                // is worse than one that explains itself.
                {"PlaceGlyphInSocket", [](lua_State* L) -> int {
            static bool said = false;
            if (!said) {
                said = true;
                LOG_WARNING("PlaceGlyphInSocket: this client cannot apply "
                            "glyphs — the glyph panel is read-only");
            }
            (void)L;
            return 0;
        }},
                // SetCursor(art) — the pointer's own image, which this client
                // does not change.
                {"SetCursor", [](lua_State* L) -> int { (void)L; return 0; }},
                // How long until the daily quests reset, which the quest log
                // prints in a tooltip through SecondsToTime. A nil there is
                // arithmetic on nothing rather than a blank line.
                //
                // Counted to the next midnight in the server's own day, which
                // is where a stock realm puts it. Not the true reset — that is
                // a realm setting nothing sends — but the right shape and the
                // right order of magnitude, where zero would read as "the
                // reset is now" every time the tooltip was opened.
                {"GetQuestResetTime", [](lua_State* L) -> int {
            const time_t now = time(nullptr);
            struct tm* t = localtime(&now);
            const int secondsIntoDay = t ? (t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec) : 0;
            lua_pushnumber(L, 86400 - secondsIntoDay);
            return 1;
        }},
                // The cursor changes at a vendor: the hand that sells, and the
                // hammer that repairs. This client draws its own cursor and
                // has no art switched by name, so these do nothing — which is
                // what they did before, silently, as unknown globals.
                //
                // Bound rather than left to the fallback because the fallback
                // is what makes a typo look like a working call, and because a
                // window that calls nothing undefined is the measure of
                // whether it can be handed over.
                {"ShowMerchantSellCursor", [](lua_State* L) -> int { (void)L; return 0; }},
                {"ShowRepairCursor",       [](lua_State* L) -> int { (void)L; return 0; }},
                {"HideRepairCursor",       [](lua_State* L) -> int { (void)L; return 0; }},
                // GetNumCompletedAchievements() → total, completed.
                //
                // Two values, and the total comes first. This returned one —
                // the *earned* count in the total's place — so the summary bar
                // was scaled to the number earned, and `completed` was nil.
                // AchievementFrameSummaryCategoriesStatusBar_Update then does
                // SetText(completed.."/"..total), and concatenating nil raises,
                // so opening the achievements panel took its own update down.
                {"GetNumCompletedAchievements", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
            // The name cache is one entry per achievement in the DBC, which is
            // the only count of "all of them" this client has. Asking for it
            // is what loads it.
            gh->ensureAchievementNamesLoaded();
            const size_t total = gh->achievementNameCacheRef().size();
            const size_t earned = gh->getEarnedAchievements().size();
            // Never fewer total than earned: if the DBC did not load, saying
            // "3 of 0" is worse than saying the total is what we have seen.
            lua_pushnumber(L, static_cast<lua_Number>(total ? total : earned));
            lua_pushnumber(L, static_cast<lua_Number>(earned));
            return 2;
        }},
                // GetCategoryList() → every achievement category id, as a table.
                {"GetCategoryList", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_newtable(L);
            if (!gh) return 1;
            gh->ensureAchievementCategoriesLoaded();
            int n = 0;
            for (uint32_t id : gh->getAchievementCategoryOrder()) {
                lua_pushnumber(L, ++n);
                lua_pushnumber(L, id);
                lua_settable(L, -3);
            }
            return 1;
        }},
                // GetCategoryInfo(id) → name, parentCategoryID, flags
                {"GetCategoryInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh) return luaReturnNil(L);
            gh->ensureAchievementCategoriesLoaded();
            const auto* info = gh->getAchievementCategoryInfo(id);
            if (!info) return luaReturnNil(L);
            lua_pushstring(L, info->name.c_str());
            lua_pushnumber(L, info->parentId);
            lua_pushnumber(L, 0);
            return 3;
        }},
                // GetCategoryNumAchievements(id) → total, completed, incomplete
                {"GetCategoryNumAchievements", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 3; }
            gh->ensureAchievementCategoriesLoaded();
            const auto& ids = gh->getCategoryAchievements(id);
            const auto& earned = gh->getEarnedAchievements();
            uint32_t done = 0;
            for (uint32_t a : ids) if (earned.count(a)) ++done;
            lua_pushnumber(L, static_cast<lua_Number>(ids.size()));
            lua_pushnumber(L, done);
            lua_pushnumber(L, static_cast<lua_Number>(ids.size() - done));
            return 3;
        }},
                {"GetAchievementCategory", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh) return luaReturnNil(L);
            gh->ensureAchievementCategoriesLoaded();
            lua_pushnumber(L, gh->getAchievementCategory(id));
            return 1;
        }},
                {"GetAchievementNumCriteria", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh) { lua_pushnumber(L, 0); return 1; }
            gh->ensureAchievementCriteriaLoaded();
            lua_pushnumber(L, static_cast<lua_Number>(gh->getAchievementCriteria(id).size()));
            return 1;
        }},
                // GetAchievementCriteriaInfo(achievementID, index) →
                //   description, criteriaType, completed, quantity,
                //   reqQuantity, charName, flags, assetID, quantityString,
                //   criteriaID
                {"GetAchievementCriteriaInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id  = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            const int  idx = static_cast<int>(luaL_optnumber(L, 2, 0));
            if (!gh || idx < 1) return luaReturnNil(L);
            gh->ensureAchievementCriteriaLoaded();
            const auto& list = gh->getAchievementCriteria(id);
            if (idx > static_cast<int>(list.size())) return luaReturnNil(L);
            const auto& c = list[static_cast<size_t>(idx) - 1];
            // Per-criterion progress is tracked after all: SMSG_ALL_ACHIEVEMENT_DATA
            // carries a counter per criterion id, and this reported all-or-none
            // from the achievement's earned flag while that sat unread beside
            // it. A half-finished achievement now shows which of its criteria
            // are done and how far the rest have got.
            const auto& progress = gh->getCriteriaProgress();
            const auto pit = progress.find(c.id);
            const uint32_t have = (pit != progress.end())
                ? static_cast<uint32_t>(pit->second) : 0u;
            const bool earned = gh->getEarnedAchievements().count(id) > 0;
            // Earned wins over the counter: the server stops counting once an
            // achievement is complete, so a finished one can carry a criterion
            // still short of its quantity.
            const bool done = earned || (c.quantity > 0 && have >= c.quantity);
            const uint32_t shown = earned ? c.quantity : have;
            lua_pushstring(L, c.description.c_str());          // 1: description
            lua_pushnumber(L, c.type);                         // 2: criteriaType
            lua_pushboolean(L, done ? 1 : 0);                  // 3: completed
            lua_pushnumber(L, shown);                          // 4: quantity
            lua_pushnumber(L, c.quantity);                     // 5: reqQuantity
            lua_pushnil(L);                                    // 6: charName
            lua_pushnumber(L, 0);                              // 7: flags
            lua_pushnumber(L, c.assetId);                      // 8: assetID
            lua_pushstring(L, std::to_string(shown).c_str());  // 9: quantityString
            lua_pushnumber(L, c.id);                           // 10: criteriaID
            return 10;
        }},
                // GetLatestCompletedAchievements() → the most recent earned
                // ids, newest first. The summary page fills its top rows from
                // these. The earn date is a WoW PackedTime — year in the low
                // sixteen bits, then day, then month — so it does not sort as
                // a plain integer and has to be unpacked first.
                {"GetLatestCompletedAchievements", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            std::vector<std::pair<uint32_t, uint32_t>> byDate;  // sortKey, id
            for (uint32_t id : gh->getEarnedAchievements()) {
                const game::WowDate d =
                    game::unpackWowPackedTime(gh->getAchievementDate(id));
                byDate.emplace_back(
                    (static_cast<uint32_t>(d.yearSince2000) << 9) |
                    (static_cast<uint32_t>(d.month) << 5) |
                    static_cast<uint32_t>(d.day), id);
            }
            std::sort(byDate.begin(), byDate.end(),
                      [](const auto& a, const auto& b) {
                          if (a.first != b.first) return a.first > b.first;
                          return a.second > b.second;  // stable, and a total order
                      });
            const size_t n = std::min<size_t>(byDate.size(), 5);
            for (size_t i = 0; i < n; ++i) lua_pushnumber(L, byDate[i].second);
            return static_cast<int>(n);
        }},
                // GetAchievementInfoFromCriteria(criteriaID) → the achievement
                // that criterion belongs to, in GetAchievementInfo's shape.
                {"GetAchievementInfoFromCriteria", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto critId = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh || critId == 0) return luaReturnNil(L);
            gh->ensureAchievementCriteriaLoaded();
            // Nothing indexes criteria by their own id — the panel asks this
            // once per link clicked, not per frame, so the walk is cheaper
            // than a second map kept in step with the first.
            for (const auto& [achId, list] : gh->getAchievementCriteriaMap()) {
                for (const auto& c : list) {
                    if (c.id != critId) continue;
                    // Ten, the same shape as GetAchievementInfo: the panel
                    // unpacks it into the same ten names and puts the last one
                    // on a button as its texture.
                    const bool done = gh->getEarnedAchievements().count(achId) > 0;
                    const uint32_t date = gh->getAchievementDate(achId);
                    lua_pushnumber(L, achId);                                    // 1: id
                    lua_pushstring(L, gh->getAchievementName(achId).c_str());    // 2: name
                    lua_pushnumber(L, gh->getAchievementPoints(achId));          // 3: points
                    lua_pushboolean(L, done ? 1 : 0);                            // 4: completed
                    const game::WowDate on = game::unpackWowPackedTime(date);
                    lua_pushnumber(L, done ? on.month : 0);                      // 5: month
                    lua_pushnumber(L, done ? on.day : 0);                        // 6: day
                    // SHORTDATE formats the year "%02d", so it wants the short
                    // form rather than a four-digit one.
                    lua_pushnumber(L, done ? on.yearSince2000 : 0);              // 7: year
                    lua_pushstring(L, gh->getAchievementDescription(achId).c_str()); // 8
                    lua_pushnumber(L, 0);                                        // 9: flags
                    lua_pushnumber(L, gh->getAchievementIconId(achId));            // 10: icon
                    return 10;
                }
            }
            return luaReturnNil(L);
        }},
                // The chain an achievement belongs to. Achievement.dbc carries
                // it in Supercedes, which is not loaded — so no chain, and the
                // panel simply draws none rather than a wrong one.
                {"GetNextAchievement",     [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"GetPreviousAchievement", [](lua_State* L) -> int { return luaReturnNil(L); }},
                // GetStatistic(id) → the statistic's value, as a string.
                // Statistics count criteria progress, and only whether an
                // achievement was earned is tracked here, so this reports the
                // dash WoW itself shows for a statistic with no value.
                {"GetStatistic", [](lua_State* L) -> int { lua_pushstring(L, "--"); return 1; }},
                // Comparing achievements against an inspected player needs the
                // server to send theirs, which is never asked for here.
                {"SetAchievementComparisonUnit",   [](lua_State* L) -> int { (void)L; return 0; }},
                {"ClearAchievementComparisonUnit", [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetAchievementComparisonInfo",   [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"GetComparisonStatistic",         [](lua_State* L) -> int { lua_pushstring(L, "--"); return 1; }},
                {"GetComparisonAchievementPoints", [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
                {"GetTotalAchievementPoints", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getTotalAchievementPoints() : 0);
            return 1;
        }},
                {"GetAchievementLink", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh || id == 0) return luaReturnNil(L);
            gh->ensureAchievementNamesLoaded();
            const std::string& name = gh->getAchievementName(id);
            if (name.empty()) return luaReturnNil(L);
            std::string link = "|cffffff00|Hachievement:" + std::to_string(id) +
                               ":0:0:0:0:0:0:0:0:0|h[" + name + "]|h|r";
            lua_pushstring(L, link.c_str());
            return 1;
        }},
                // ---- Achievement tracking (client-side, like the quest tracker)
                {"IsTrackedAchievement", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            lua_pushboolean(L, gh && gh->getTrackedAchievements().count(id) ? 1 : 0);
            return 1;
        }},
                {"AddTrackedAchievement", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (gh && id) { gh->setAchievementTracked(id, true); gh->fireAddonEvent("TRACKED_ACHIEVEMENT_UPDATE", {std::to_string(id)}); }
            return 0;
        }},
                {"RemoveTrackedAchievement", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (gh && id) { gh->setAchievementTracked(id, false); gh->fireAddonEvent("TRACKED_ACHIEVEMENT_UPDATE", {std::to_string(id)}); }
            return 0;
        }},
                {"GetNumTrackedAchievements", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? static_cast<lua_Number>(gh->getTrackedAchievements().size()) : 0);
            return 1;
        }},
                {"GetAchievementInfo", [](lua_State* L) -> int {
            // GetAchievementInfo(id) → id, name, points, completed, month, day, year, description, flags, icon, rewardText, isGuildAch
            auto* gh = getGameHandler(L);
            uint32_t id = static_cast<uint32_t>(luaL_checknumber(L, 1));
            if (!gh) { return luaReturnNil(L); }
            // The other form the panel uses: GetAchievementInfo(category,
            // index) walks a category's achievements by position. Only the
            // by-id form existed, so every row the achievements panel asked
            // for was read as an achievement id and answered with the wrong
            // achievement, or with nothing.
            if (!lua_isnoneornil(L, 2)) {
                const int idx = static_cast<int>(luaL_optnumber(L, 2, 0));
                gh->ensureAchievementCategoriesLoaded();
                const auto& ids = gh->getCategoryAchievements(id);
                if (idx < 1 || idx > static_cast<int>(ids.size())) return luaReturnNil(L);
                id = ids[static_cast<size_t>(idx) - 1];
            }
            const std::string& name = gh->getAchievementName(id);
            if (name.empty()) { return luaReturnNil(L); }
            bool completed = gh->getEarnedAchievements().count(id) > 0;
            uint32_t date = gh->getAchievementDate(id);
            uint32_t points = gh->getAchievementPoints(id);
            const std::string& desc = gh->getAchievementDescription(id);
            const game::WowDate earned = game::unpackWowPackedTime(date);
            int month = completed ? earned.month : 0;
            int day   = completed ? earned.day : 0;
            // Short form: SHORTDATE is "%2$d/%1$02d/%3$02d".
            int year  = completed ? earned.yearSince2000 : 0;
            lua_pushnumber(L, id);                 // 1: id
            lua_pushstring(L, name.c_str());       // 2: name
            lua_pushnumber(L, points);             // 3: points
            lua_pushboolean(L, completed ? 1 : 0); // 4: completed
            lua_pushnumber(L, month);              // 5: month
            lua_pushnumber(L, day);                // 6: day
            lua_pushnumber(L, year);               // 7: year
            lua_pushstring(L, desc.c_str());       // 8: description
            lua_pushnumber(L, 0);                  // 9: flags
            lua_pushstring(L, "Interface\\Icons\\Achievement_General"); // 10: icon
            lua_pushstring(L, "");                 // 11: rewardText
            lua_pushboolean(L, 0);                 // 12: isGuildAchievement
            return 12;
        }},
    };
    for (const auto& [name, func] : api) {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
    }
}

} // namespace wowee::addons
