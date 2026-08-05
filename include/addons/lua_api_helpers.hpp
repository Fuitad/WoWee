// lua_api_helpers.hpp — Shared helpers, lookup tables, and utility functions
// used by all lua_*_api.cpp domain files.
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#pragma once

#include <string>
#include <chrono>
#include <cstring>
#include <algorithm>

#include "addons/lua_services.hpp"
#include "game/game_handler.hpp"
#include "game/entity.hpp"
#include "game/update_field_table.hpp"
#include "core/logger.hpp"
#include "core/app_clock.hpp"
#include "ui/widget_tree.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace wowee::addons {

// ---- String helper ----
inline void toLowerInPlace(std::string& s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// ---- Lua return helpers — used 200+ times as guard/fallback returns ----
inline int luaReturnNil(lua_State* L)  { lua_pushnil(L); return 1; }
inline int luaReturnZero(lua_State* L) { lua_pushnumber(L, 0); return 1; }
inline int luaReturnFalse(lua_State* L){ lua_pushboolean(L, 0); return 1; }

// ---- Shared GetTime() epoch ----
//
// The application's clock, not one of its own. Both are steady_clock seconds
// and both fixed their origin on first call, so they differed by however long
// separated those two calls — the app's at startup, this one at the first Lua
// GetTime, which is after the addon system comes up.
//
// That gap landed on every cooldown. GetActionCooldown reports a start time on
// this clock and the widget renderer draws the sweep as
// appTimeSeconds() - start, so the difference was added to every elapsed: a
// sweep ran ahead of itself, and one shorter than the gap was already over
// before it was drawn. Anything else comparing a GetTime value against a
// widget's stored time had the same offset.
inline double luaGetTimeNow() {
    return wowee::core::appTimeSeconds();
}

// ---- Shared WoW class/race/power name tables (indexed by ID, element 0 = unknown) ----
inline constexpr const char* kLuaClasses[] = {
    "","Warrior","Paladin","Hunter","Rogue","Priest",
    "Death Knight","Shaman","Mage","Warlock","","Druid"
};
/// What the cursor is carrying, if anything.
///
/// Shared because two sides need it: the bindings that pick an item up and put
/// it down, and the ones that describe a slot to the interface. A slot whose
/// item is on the cursor is "locked", which is what makes it draw greyed while
/// it is being dragged — and without somewhere common to ask, the binding that
/// answers that question had no way to know.
struct CursorItemSlot {
    int bag = -1;    ///< 0 for the backpack, 1-4 for a worn bag, -1 for none
    int slot = 0;    ///< 1-based within the bag, or the equipment slot
    bool equipped = false;
};
inline CursorItemSlot& cursorItemSlot() {
    static CursorItemSlot held;
    return held;
}

/// The uppercase tokens WoW returns second from UnitClass, and the key every
/// class-indexed table in FrameXML uses — CLASS_ICON_TCOORDS for the class
/// portrait, RAID_CLASS_COLORS for a name's colour. Written out rather than
/// derived from the display names above, because the token for Death Knight
/// has no space in it.
inline constexpr const char* kLuaClassTokens[] = {
    "","WARRIOR","PALADIN","HUNTER","ROGUE","PRIEST",
    "DEATHKNIGHT","SHAMAN","MAGE","WARLOCK","","DRUID"
};
inline constexpr const char* kLuaRaces[] = {
    "","Human","Orc","Dwarf","Night Elf","Undead",
    "Tauren","Gnome","Troll","","Blood Elf","Draenei"
};
inline constexpr const char* kLuaPowerNames[] = {
    "MANA","RAGE","FOCUS","ENERGY","HAPPINESS","","RUNIC_POWER"
};

// ---- Quality hex strings ----
// No alpha prefix — for item links
inline constexpr const char* kQualHexNoAlpha[] = {
    "9d9d9d","ffffff","1eff00","0070dd","a335ee","ff8000","e6cc80","e6cc80"
};
// With ff alpha prefix — for Lua color returns
// Heirloom is e6cc80, the same gold as an artifact — the table beside this one
// has always said so and this one said 00ccff, which is a later expansion's
// token colour and not a quality 3.3.5 has. An item link for an heirloom came
// out cyan.
inline constexpr const char* kQualHexAlpha[] = {
    "ff9d9d9d","ffffffff","ff1eff00","ff0070dd","ffa335ee","ffff8000","ffe6cc80","ffe6cc80"
};

// ---- Retrieve GameHandler pointer stored in Lua registry ----
inline game::GameHandler* getGameHandler(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_game_handler");
    auto* gh = static_cast<game::GameHandler*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return gh;
}

// ---- Retrieve the widget tree that backs CreateFrame/CreateTexture ----
// Null before the UI is up, which the bindings treat as "record nothing and
// carry on" so an addon loaded early cannot crash on a missing tree.
inline wowee::ui::WidgetTree* getWidgetTree(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_widget_tree");
    auto* t = static_cast<wowee::ui::WidgetTree*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return t;
}

// ---- Retrieve LuaServices pointer stored in Lua registry ----
inline LuaServices* getLuaServices(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_lua_services");
    auto* svc = static_cast<LuaServices*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return svc;
}

// ---- Unit resolution helpers ----

// Read UNIT_FIELD_TARGET_LO/HI from an entity's update fields to get what it's targeting
inline uint64_t getEntityTargetGuid(game::GameHandler* gh, uint64_t guid) {
    if (guid == 0) return 0;
    // If asking for the player's target, use direct accessor
    if (guid == gh->getPlayerGuid()) return gh->getTargetGuid();
    auto entity = gh->getEntityManager().getEntity(guid);
    if (!entity) return 0;
    const auto& fields = entity->getFields();
    auto loIt = fields.find(game::fieldIndex(game::UF::UNIT_FIELD_TARGET_LO));
    if (loIt == fields.end()) return 0;
    uint64_t targetGuid = loIt->second;
    auto hiIt = fields.find(game::fieldIndex(game::UF::UNIT_FIELD_TARGET_HI));
    if (hiIt != fields.end())
        targetGuid |= (static_cast<uint64_t>(hiIt->second) << 32);
    return targetGuid;
}

// Resolve WoW unit IDs to GUID
inline uint64_t resolveUnitGuid(game::GameHandler* gh, const std::string& uid) {
    if (uid == "player")      return gh->getPlayerGuid();
    if (uid == "target")      return gh->getTargetGuid();
    if (uid == "focus")       return gh->getFocusGuid();
    if (uid == "mouseover")   return gh->getMouseoverGuid();
    if (uid == "pet")         return gh->getPetGuid();
    // Compound unit IDs: targettarget, focustarget, pettarget, mouseovertarget
    if (uid == "targettarget")    return getEntityTargetGuid(gh, gh->getTargetGuid());
    if (uid == "focustarget")     return getEntityTargetGuid(gh, gh->getFocusGuid());
    if (uid == "pettarget")       return getEntityTargetGuid(gh, gh->getPetGuid());
    if (uid == "mouseovertarget") return getEntityTargetGuid(gh, gh->getMouseoverGuid());
    // party1-party4, raid1-raid40
    if (uid.rfind("party", 0) == 0 && uid.size() > 5) {
        int idx = 0;
        try { idx = std::stoi(uid.substr(5)); } catch (...) { return 0; }
        if (idx < 1 || idx > 4) return 0;
        const auto& pd = gh->getPartyData();
        // party members exclude self; index 1-based
        int found = 0;
        for (const auto& m : pd.members) {
            if (m.guid == gh->getPlayerGuid()) continue;
            if (++found == idx) return m.guid;
        }
        return 0;
    }
    // boss1-boss5, the encounter frames. Filled by
    // SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT; without this every UnitExists("boss1")
    // answered nil and the frames stayed hidden through the whole fight.
    if (uid.rfind("boss", 0) == 0 && uid.size() > 4) {
        int idx = 0;
        try { idx = std::stoi(uid.substr(4)); } catch (...) { return 0; }
        if (idx < 1 || idx > static_cast<int>(game::GameHandler::kMaxEncounterSlots)) return 0;
        return gh->getEncounterUnitGuid(static_cast<uint32_t>(idx - 1));
    }
    if (uid.rfind("raid", 0) == 0 && uid.size() > 4 && uid[4] != 'p') {
        int idx = 0;
        try { idx = std::stoi(uid.substr(4)); } catch (...) { return 0; }
        if (idx < 1 || idx > 40) return 0;
        const auto& pd = gh->getPartyData();
        if (idx <= static_cast<int>(pd.members.size()))
            return pd.members[idx - 1].guid;
        return 0;
    }
    return 0;
}

// Resolve unit IDs (player, target, focus, mouseover, pet, targettarget, etc.) to entity
inline game::Unit* resolveUnit(lua_State* L, const char* unitId) {
    auto* gh = getGameHandler(L);
    if (!gh || !unitId) return nullptr;
    std::string uid(unitId);
    toLowerInPlace(uid);

    uint64_t guid = resolveUnitGuid(gh, uid);
    if (guid == 0) return nullptr;
    auto entity = gh->getEntityManager().getEntity(guid);
    if (!entity) return nullptr;
    return dynamic_cast<game::Unit*>(entity.get());
}

// Resolve a unit ID to a targeted game object, or null when it names anything else
//
// A GameObject and a Unit are siblings under Entity, so the dynamic_cast in
// resolveUnit answers null for every object — and this client lets objects be
// targeted, which the right-click path above relies on. Its own target frame
// drew whatever was selected and coloured anything that was not a unit grey,
// so a targeted mailbox had a frame; FrameXML's asks UnitExists first, and that
// was false for all of them.
inline game::GameObject* resolveGameObject(lua_State* L, const char* unitId) {
    auto* gh = getGameHandler(L);
    if (!gh || !unitId) return nullptr;
    std::string uid(unitId);
    toLowerInPlace(uid);
    const uint64_t guid = resolveUnitGuid(gh, uid);
    if (guid == 0) return nullptr;
    auto entity = gh->getEntityManager().getEntity(guid);
    if (!entity) return nullptr;
    return dynamic_cast<game::GameObject*>(entity.get());
}

// Finish an armed item-target use on the item in a slot, if one is armed
//
// A sharpening stone, a weapon oil, an enchanting scroll and a disenchant all
// park the use and wait for the player to pick the item it applies to. Every
// button that can name an item has to be able to be that pick, and until now
// the only one that could was this client's own bag window — which is handed
// over, so nothing could finish one at all.
//
// True means the click was the target and is therefore not also a use or a
// pickup. An empty slot is not a target: the click is still eaten, because the
// cursor stays armed and dropping out of targeting on a miss would be worse
// than doing nothing.
inline bool completedItemTarget(lua_State* L, uint64_t targetGuid) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isAwaitingItemTarget()) return false;
    if (targetGuid != 0) gh->completeItemUseOnItem(targetGuid);
    return true;
}

// The GUID of the item in a FrameXML container slot: bag 0 is the backpack,
// 1-4 are the worn bags, and both are 1-based on the slot.
inline uint64_t containerSlotGuid(game::GameHandler* gh, int bag, int slot) {
    if (!gh || slot < 1) return 0;
    if (bag == 0) return gh->getBackpackItemGuid(slot - 1);
    if (bag >= 1 && bag <= 4) return gh->getBagItemGuid(bag - 1, slot - 1);
    return 0;
}

/// An item's cooldown, which is its on-use spell's.
///
/// The client tracks cooldowns per spell, and an item on cooldown is one whose
/// use spell is — the same relationship dispatchUseItem walks to decide what
/// using it casts. Only the remaining time is kept, not the original duration,
/// so the sweep is reported as starting now and lasting what is left: that
/// draws exactly the remaining arc, which is the part anyone reads.
///
/// False when the item has no use spell or that spell is ready.
inline bool itemUseCooldown(game::GameHandler* gh, uint32_t itemId,
                            double& start, double& duration) {
    if (!gh || itemId == 0) return false;
    const auto* info = gh->getItemInfo(itemId);
    if (!info || !info->valid) return false;
    for (const auto& sp : info->spells) {
        if (sp.spellId == 0 || (sp.spellTrigger != 0 && sp.spellTrigger != 5)) continue;
        const auto& cds = gh->getSpellCooldowns();
        auto it = cds.find(sp.spellId);
        if (it == cds.end() || it->second <= 0.0f) continue;
        start = luaGetTimeNow();
        duration = it->second;
        return true;
    }
    return false;
}

// Find GroupMember data for a GUID (for party members out of entity range)
inline const game::GroupMember* findPartyMember(game::GameHandler* gh, uint64_t guid) {
    if (!gh || guid == 0) return nullptr;
    for (const auto& m : gh->getPartyData().members) {
        if (m.guid == guid && m.hasPartyStats) return &m;
    }
    return nullptr;
}

/// The talent at a position in the tree, by the tab and index FrameXML counts
/// in. Defined in lua_quest_api.cpp, which is where everything else about
/// talents lives.
///
/// Declared here because the tooltip setters are in lua_engine.cpp and need the
/// same lookup. Copying it there would mean two answers to "which talent is
/// this", and the pair would agree only for as long as nobody touched either —
/// the tab ordering and the row/column sort both have to match exactly or the
/// tooltip describes a different talent from the one under the cursor.
const game::TalentEntry* talentAt(game::GameHandler* gh, int tabIndex, int talentIndex);

/// The item id the cursor is carrying, or zero for anything else.
///
/// The cursor state lives in lua_action_api.cpp, where everything that picks
/// something up and puts it down again is. This is the one thing another file
/// needs from it: CursorCanGoInSlot has to know what is being dragged before
/// it can say which paperdoll slot should light up.
uint32_t cursorItemId();

/// The paperdoll slot an item on the cursor was picked up from, or zero when it
/// came from a bag or nothing is held. One-based, as FrameXML numbers slots.
int cursorEquipSlot();

/// Put a vendor's list entry on the cursor, which is what a left-click in
/// FrameXML's merchant window does. Buying happens when it is dropped.
void pickupMerchantItem(lua_State* L, int index);

/// Buy whatever vendor entry the cursor is holding and put the cursor down.
/// False when it is holding something else, so a caller can fall through to
/// its normal handling.
bool boughtHeldMerchantItem(lua_State* L);

} // namespace wowee::addons
