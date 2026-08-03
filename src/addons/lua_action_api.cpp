// lua_action_api.cpp — Action bar, cursor/pickup, keyboard input, key bindings, and pet actions Lua API bindings.
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#include "addons/lua_api_helpers.hpp"
#include "ui/framexml_takeover.hpp"
#include "ui/keybinding_manager.hpp"
#include "core/config_paths.hpp"
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include "game/pet_action.hpp"
#include "imgui.h"
#include <optional>
#include <SDL2/SDL_keyboard.h>

namespace wowee::addons {

// What the interface is holding.
//
// This is the original interface's cursor, and it is not the only one: this
// client's inventory screen keeps a held item of its own. They do not need
// merging, because whichever interface is drawing the bags is the one the
// player can pick anything up from, and that is the cursor in use — with the
// bags owned, as they are by default, this is it.
//
// What that does mean is that CursorHasItem answers for this cursor alone. An
// addon asking while the player is dragging in this client's own bags is told
// no, which is true of the cursor it can see and not of the player's hand.
enum class CursorType { NONE, SPELL, ITEM, ACTION, MACRO };
static CursorType s_cursorType = CursorType::NONE;
static uint32_t   s_cursorId   = 0;    // spellId, itemId, or action slot
static int        s_cursorSlot = 0;    // source slot for placement
static int        s_cursorBag  = -1;   // source bag for container items

static int lua_HasAction(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return luaReturnFalse(L); }
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1; // WoW uses 1-indexed slots
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size())) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, !bar[slot].isEmpty());
    return 1;
}

// GetActionTexture(slot) → texturePath or nil
static int lua_GetActionTexture(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return luaReturnNil(L); }
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1;
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) {
        lua_pushnil(L);
        return 1;
    }
    const auto& action = bar[slot];
    if (action.type == game::ActionBarSlot::SPELL) {
        std::string icon = gh->getSpellIconPath(action.id);
        if (!icon.empty()) {
            lua_pushstring(L, icon.c_str());
            return 1;
        }
    } else if (action.type == game::ActionBarSlot::ITEM && action.id != 0) {
        const auto* info = gh->getItemInfo(action.id);
        if (info && info->displayInfoId != 0) {
            std::string icon = gh->getItemIconPath(info->displayInfoId);
            if (!icon.empty()) {
                lua_pushstring(L, icon.c_str());
                return 1;
            }
        }
    }
    lua_pushnil(L);
    return 1;
}

// IsCurrentAction(slot) → boolean
static int lua_IsCurrentAction(lua_State* L) {
    // Currently no "active action" tracking; return false
    (void)L;
    lua_pushboolean(L, 0);
    return 1;
}

// IsUsableAction(slot) → usable, notEnoughMana
static int lua_IsUsableAction(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushboolean(L, 0); lua_pushboolean(L, 0); return 2; }
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1;
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) {
        lua_pushboolean(L, 0);
        lua_pushboolean(L, 0);
        return 2;
    }
    const auto& action = bar[slot];
    bool usable = action.isReady();
    bool noMana = false;
    if (action.type == game::ActionBarSlot::SPELL) {
        usable = usable && gh->getKnownSpells().count(action.id);
        // Check power cost
        if (usable && action.id != 0) {
            auto spellData = gh->getSpellData(action.id);
            if (spellData.manaCost > 0) {
                auto pe = gh->getEntityManager().getEntity(gh->getPlayerGuid());
                if (pe) {
                    auto* unit = dynamic_cast<game::Unit*>(pe.get());
                    if (unit && unit->getPower() < spellData.manaCost) {
                        noMana = true;
                        usable = false;
                    }
                }
            }
        }
    }
    lua_pushboolean(L, usable ? 1 : 0);
    lua_pushboolean(L, noMana ? 1 : 0);
    return 2;
}

// IsActionInRange(slot) → 1 if in range, 0 if out, nil if no range check applicable
static int lua_IsActionInRange(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return luaReturnNil(L); }
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1;
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) {
        lua_pushnil(L);
        return 1;
    }
    const auto& action = bar[slot];
    uint32_t spellId = 0;
    if (action.type == game::ActionBarSlot::SPELL) {
        spellId = action.id;
    } else {
        // Items/macros: no range check for now
        lua_pushnil(L);
        return 1;
    }
    if (spellId == 0) { return luaReturnNil(L); }

    auto data = gh->getSpellData(spellId);
    if (data.maxRange <= 0.0f) {
        // Melee or self-cast spells: no range indicator
        lua_pushnil(L);
        return 1;
    }

    // Need a target to check range against
    uint64_t targetGuid = gh->getTargetGuid();
    if (targetGuid == 0) { return luaReturnNil(L); }
    auto targetEnt = gh->getEntityManager().getEntity(targetGuid);
    auto playerEnt = gh->getEntityManager().getEntity(gh->getPlayerGuid());
    if (!targetEnt || !playerEnt) { return luaReturnNil(L); }

    float dx = playerEnt->getX() - targetEnt->getX();
    float dy = playerEnt->getY() - targetEnt->getY();
    float dz = playerEnt->getZ() - targetEnt->getZ();
    float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    lua_pushnumber(L, dist <= data.maxRange ? 1 : 0);
    return 1;
}

// GetActionInfo(slot) → actionType, id, subType
static int lua_GetActionInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return 0; }
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1;
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) {
        return 0;
    }
    const auto& action = bar[slot];
    switch (action.type) {
        case game::ActionBarSlot::SPELL:
            lua_pushstring(L, "spell");
            lua_pushnumber(L, action.id);
            lua_pushstring(L, "spell");
            return 3;
        case game::ActionBarSlot::ITEM:
            lua_pushstring(L, "item");
            lua_pushnumber(L, action.id);
            lua_pushstring(L, "item");
            return 3;
        case game::ActionBarSlot::MACRO:
            lua_pushstring(L, "macro");
            lua_pushnumber(L, action.id);
            lua_pushstring(L, "macro");
            return 3;
        default:
            return 0;
    }
}

// GetActionCount(slot) → count (item stack count or 0)
static int lua_GetActionCount(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return luaReturnZero(L); }
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1;
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) {
        lua_pushnumber(L, 0);
        return 1;
    }
    const auto& action = bar[slot];
    if (action.type == game::ActionBarSlot::ITEM && action.id != 0) {
        // Count items across backpack + bags
        uint32_t count = 0;
        const auto& inv = gh->getInventory();
        for (int i = 0; i < inv.getBackpackSize(); ++i) {
            const auto& s = inv.getBackpackSlot(i);
            if (!s.empty() && s.item.itemId == action.id)
                count += (s.item.stackCount > 0 ? s.item.stackCount : 1);
        }
        for (int b = 0; b < game::Inventory::NUM_BAG_SLOTS; ++b) {
            int bagSize = inv.getBagSize(b);
            for (int i = 0; i < bagSize; ++i) {
                const auto& s = inv.getBagSlot(b, i);
                if (!s.empty() && s.item.itemId == action.id)
                    count += (s.item.stackCount > 0 ? s.item.stackCount : 1);
            }
        }
        lua_pushnumber(L, count);
    } else {
        lua_pushnumber(L, 0);
    }
    return 1;
}

// GetActionCooldown(slot) → start, duration, enable
static int lua_GetActionCooldown(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 1); return 3; }
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1;
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 1);
        return 3;
    }
    const auto& action = bar[slot];
    if (action.cooldownRemaining > 0.0f) {
        // WoW returns GetTime()-based start time; approximate
        double now = 0;
        lua_getglobal(L, "GetTime");
        if (lua_isfunction(L, -1)) {
            lua_call(L, 0, 1);
            now = lua_tonumber(L, -1);
            lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
        }
        double start = now - (action.cooldownTotal - action.cooldownRemaining);
        lua_pushnumber(L, start);
        lua_pushnumber(L, action.cooldownTotal);
        lua_pushnumber(L, 1);
    } else if (action.type == game::ActionBarSlot::SPELL && gh->isGCDActive()) {
        // No individual cooldown but GCD is active — show GCD sweep
        float gcdRem = gh->getGCDRemaining();
        float gcdTotal = gh->getGCDTotal();
        double now = 0;
        lua_getglobal(L, "GetTime");
        if (lua_isfunction(L, -1)) { lua_call(L, 0, 1); now = lua_tonumber(L, -1); lua_pop(L, 1); }
        else lua_pop(L, 1);
        double elapsed = gcdTotal - gcdRem;
        lua_pushnumber(L, now - elapsed);
        lua_pushnumber(L, gcdTotal);
        lua_pushnumber(L, 1);
    } else {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 1);
    }
    return 3;
}

// UseAction(slot, checkCursor, onSelf) — activate action bar slot (1-indexed)
static int lua_UseAction(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    int slot = static_cast<int>(luaL_checknumber(L, 1)) - 1;
    const auto& bar = gh->getActionBar();
    if (slot < 0 || slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) return 0;
    const auto& action = bar[slot];
    // The end of the chain a click travels, and the last place it can stop
    // without saying so: an empty slot, a cooldown, or a type nothing acts on
    // all look the same from the button.
    LOG_WARNING("UseAction: slot ", slot + 1, " type=", static_cast<int>(action.type),
                " id=", action.id, " ready=", action.isReady() ? 1 : 0);
    if (action.type == game::ActionBarSlot::SPELL && action.isReady()) {
        uint64_t target = gh->hasTarget() ? gh->getTargetGuid() : 0;
        gh->castSpell(action.id, target);
    } else if (action.type == game::ActionBarSlot::ITEM && action.id != 0) {
        gh->useItemById(action.id);
    }
    // Macro execution requires GameScreen context; not available from pure Lua API
    return 0;
}

// --- Cursor / Drag-Drop System ---
// Tracks what the player is "holding" on the cursor (spell, item, action).


static int lua_ClearCursor(lua_State* L) {
    (void)L;
    s_cursorType = CursorType::NONE;
    s_cursorId = 0;
    s_cursorSlot = 0;
    s_cursorBag = -1;
    return 0;
}

static int lua_GetCursorInfo(lua_State* L) {
    switch (s_cursorType) {
        case CursorType::SPELL:
            lua_pushstring(L, "spell");
            lua_pushnumber(L, 0);          // bookSlotIndex
            lua_pushstring(L, "spell");    // bookType
            lua_pushnumber(L, s_cursorId); // spellId
            return 4;
        case CursorType::ITEM:
            lua_pushstring(L, "item");
            lua_pushnumber(L, s_cursorId);
            return 2;
        case CursorType::ACTION:
            lua_pushstring(L, "action");
            lua_pushnumber(L, s_cursorSlot);
            return 2;
        case CursorType::MACRO:
            lua_pushstring(L, "macro");
            lua_pushnumber(L, s_cursorId);
            return 2;
        default:
            return 0;
    }
}

static int lua_CursorHasItem(lua_State* L) {
    lua_pushboolean(L, s_cursorType == CursorType::ITEM ? 1 : 0);
    return 1;
}

static int lua_CursorHasSpell(lua_State* L) {
    lua_pushboolean(L, s_cursorType == CursorType::SPELL ? 1 : 0);
    return 1;
}

// PickupAction(slot) — picks up an action from the action bar
static int lua_PickupAction(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    int slot = static_cast<int>(luaL_checknumber(L, 1));
    const auto& bar = gh->getActionBar();
    if (slot < 1 || slot > static_cast<int>(bar.size())) return 0;
    const auto& action = bar[slot - 1];
    if (action.isEmpty()) {
        // Empty slot — if cursor has something, place it
        if (s_cursorType == CursorType::SPELL && s_cursorId != 0) {
            gh->setActionBarSlot(slot - 1, game::ActionBarSlot::SPELL, s_cursorId);
            s_cursorType = CursorType::NONE;
            s_cursorId = 0;
        }
    } else {
        // Pick up existing action
        s_cursorType = (action.type == game::ActionBarSlot::SPELL) ? CursorType::SPELL :
                       (action.type == game::ActionBarSlot::ITEM)  ? CursorType::ITEM :
                       CursorType::ACTION;
        s_cursorId = action.id;
        s_cursorSlot = slot;
    }
    return 0;
}

// PlaceAction(slot) — places cursor content into an action bar slot
static int lua_PlaceAction(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    int slot = static_cast<int>(luaL_checknumber(L, 1));
    if (slot < 1 || slot > static_cast<int>(gh->getActionBar().size())) return 0;
    if (s_cursorType == CursorType::SPELL && s_cursorId != 0) {
        gh->setActionBarSlot(slot - 1, game::ActionBarSlot::SPELL, s_cursorId);
    } else if (s_cursorType == CursorType::ITEM && s_cursorId != 0) {
        gh->setActionBarSlot(slot - 1, game::ActionBarSlot::ITEM, s_cursorId);
    } else if (s_cursorType == CursorType::MACRO && s_cursorId != 0) {
        gh->setActionBarSlot(slot - 1, game::ActionBarSlot::MACRO, s_cursorId);
    }
    s_cursorType = CursorType::NONE;
    s_cursorId = 0;
    return 0;
}

// PickupSpell(bookSlot, bookType) — picks up a spell from the spellbook
static int lua_PickupSpell(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    int slot = static_cast<int>(luaL_checknumber(L, 1));
    const auto& tabs = gh->getSpellBookTabs();
    int idx = slot;
    for (const auto& tab : tabs) {
        if (idx <= static_cast<int>(tab.spellIds.size())) {
            s_cursorType = CursorType::SPELL;
            s_cursorId = tab.spellIds[idx - 1];
            return 0;
        }
        idx -= static_cast<int>(tab.spellIds.size());
    }
    return 0;
}

// PickupSpellBookItem(bookSlot, bookType) — alias for PickupSpell
static int lua_PickupSpellBookItem(lua_State* L) {
    return lua_PickupSpell(L);
}

// PickupContainerItem(bag, slot) — picks up an item from a bag

/// Where the item on the cursor lives, in the numbering the server uses.
///
/// The server addresses one flat space: equipment is slots 0 to 22, the
/// backpack follows at 23, and a bag's contents are addressed by that bag's own
/// equipment slot together with an index inside it. FrameXML counts bags 0 to 4
/// with 1-based slots instead, so the two have to be translated — the same
/// translation this client's own bag window does before sending a swap.
static bool cursorWireSlot(uint8_t& bag, uint8_t& slot) {
    if (s_cursorType != CursorType::ITEM) return false;
    if (s_cursorBag < 0) {                    // an equipped item
        bag = 0xFF;
        slot = static_cast<uint8_t>(s_cursorSlot - 1);
    } else if (s_cursorBag == 0) {            // the backpack
        bag = 0xFF;
        slot = static_cast<uint8_t>(23 + s_cursorSlot - 1);
    } else {                                   // one of the four worn bags
        bag = static_cast<uint8_t>(19 + s_cursorBag - 1);
        slot = static_cast<uint8_t>(s_cursorSlot - 1);
    }
    return true;
}

/// Put the cursor down.
static void clearCursorItem() {
    s_cursorType = CursorType::NONE;
    s_cursorId = 0;
    s_cursorSlot = 0;
    s_cursorBag = -1;
    wowee::ui::frameXmlSetCursorItem(std::string());
    cursorItemSlot() = {};
}

static int lua_PickupContainerItem(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    int bag = static_cast<int>(luaL_checknumber(L, 1));
    int slot = static_cast<int>(luaL_checknumber(L, 2));

    // Already carrying something, so this is the drop rather than the pickup.
    // One function does both halves of a drag in WoW, and without this half a
    // dragged item was picked up and never put down anywhere.
    uint8_t srcBag = 0, srcSlot = 0;
    if (cursorWireSlot(srcBag, srcSlot)) {
        uint8_t dstBag, dstSlot;
        if (bag == 0) {
            dstBag = 0xFF;
            dstSlot = static_cast<uint8_t>(23 + slot - 1);
        } else {
            dstBag = static_cast<uint8_t>(19 + bag - 1);
            dstSlot = static_cast<uint8_t>(slot - 1);
        }
        // At warning level because it is the outcome of a drag and happens
        // once per drop, and because the log carries nothing below warning —
        // which is why "did the move go out" could not be answered at all.
        LOG_WARNING("FrameXML drop: bag ", s_cursorBag, " slot ", s_cursorSlot,
                    " (wire ", (int)srcBag, "/", (int)srcSlot, ") -> bag ", bag,
                    " slot ", slot, " (wire ", (int)dstBag, "/", (int)dstSlot, ")");
        // Back where it came from: put it down rather than asking the server to
        // swap a slot with itself.
        if (s_cursorBag == bag && s_cursorSlot == slot) {
            const int sameBag = bag, sameSlot = slot;
            clearCursorItem();
            gh->fireAddonEvent("ITEM_LOCK_CHANGED",
                               {std::to_string(sameBag), std::to_string(sameSlot)});
            return 0;
        }

        const int wasBag = s_cursorBag, wasSlot = s_cursorSlot;
        gh->swapContainerItems(srcBag, srcSlot, dstBag, dstSlot);
        clearCursorItem();
        gh->fireAddonEvent("ITEM_LOCK_CHANGED",
                           {std::to_string(wasBag), std::to_string(wasSlot)});
        gh->fireAddonEvent("ITEM_LOCK_CHANGED",
                           {std::to_string(bag), std::to_string(slot)});
        return 0;
    }

    const auto& inv = gh->getInventory();
    const game::ItemSlot* itemSlot = nullptr;
    if (bag == 0 && slot >= 1 && slot <= inv.getBackpackSize()) {
        itemSlot = &inv.getBackpackSlot(slot - 1);
    } else if (bag >= 1 && bag <= 4) {
        int bagSize = inv.getBagSize(bag - 1);
        if (slot >= 1 && slot <= bagSize) {
            itemSlot = &inv.getBagSlot(bag - 1, slot - 1);
        }
    }
    if (itemSlot && !itemSlot->empty()) {
        s_cursorType = CursorType::ITEM;
        s_cursorId = itemSlot->item.itemId;
        s_cursorBag = bag;
        s_cursorSlot = slot;
        uint32_t displayId = itemSlot->item.displayInfoId;
        if (displayId == 0) {
            if (const auto* info = gh->getItemInfo(itemSlot->item.itemId)) {
                displayId = info->displayInfoId;
            }
        }
        wowee::ui::frameXmlSetCursorItem(
            displayId ? gh->getItemIconPath(displayId) : std::string());
        cursorItemSlot() = {bag, slot, false};
        // The slot draws greyed while its item is on the cursor, which is how a
        // bag shows that something has been picked up out of it.
        gh->fireAddonEvent("ITEM_LOCK_CHANGED",
                           {std::to_string(bag), std::to_string(slot)});
        LOG_WARNING("FrameXML pickup: bag ", bag, " slot ", slot,
                    " item ", itemSlot->item.itemId);
    } else {
        LOG_WARNING("FrameXML pickup: bag ", bag, " slot ", slot,
                    " — nothing there to pick up");
    }
    return 0;
}

// PickupInventoryItem(slot) — picks up an equipped item
static int lua_PickupInventoryItem(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    int slot = static_cast<int>(luaL_checknumber(L, 1));
    if (slot < 1 || slot > 23) return 0;

    // Carrying something: equip it here, which is the other half of the drag.
    uint8_t srcBag = 0, srcSlot = 0;
    if (cursorWireSlot(srcBag, srcSlot)) {
        gh->swapContainerItems(srcBag, srcSlot, 0xFF,
                               static_cast<uint8_t>(slot - 1));
        clearCursorItem();
        return 0;
    }

    const auto& inv = gh->getInventory();
    const auto& eq = inv.getEquipSlot(static_cast<game::EquipSlot>(slot - 1));
    if (!eq.empty()) {
        s_cursorType = CursorType::ITEM;
        s_cursorId = eq.item.itemId;
        s_cursorSlot = slot;
        s_cursorBag = -1;
        // The same three things a container pickup does, which this had none
        // of: remember where it came from, put its icon on the pointer, and say
        // the slot is locked. Without them dragging off the character sheet
        // carried nothing visible and left the slot looking untouched.
        cursorItemSlot() = {-1, slot, true};
        uint32_t displayId = eq.item.displayInfoId;
        if (displayId == 0) {
            if (const auto* info = gh->getItemInfo(eq.item.itemId)) {
                displayId = info->displayInfoId;
            }
        }
        wowee::ui::frameXmlSetCursorItem(
            displayId ? gh->getItemIconPath(displayId) : std::string());
        // One argument, not two: the paperdoll's handler is
        // `if ( not arg2 and arg1 == self:GetID() )`, so an equipment lock is
        // the slot alone. A second argument makes that test fail and the square
        // never greys.
        gh->fireAddonEvent("ITEM_LOCK_CHANGED", {std::to_string(slot)});
        LOG_WARNING("FrameXML pickup: equipment slot ", slot,
                    " item ", eq.item.itemId);
    } else {
        LOG_WARNING("FrameXML pickup: equipment slot ", slot,
                    " — nothing equipped there");
    }
    return 0;
}

// DeleteCursorItem() — destroys the item on cursor
static int lua_DeleteCursorItem(lua_State* L) {
    (void)L;
    s_cursorType = CursorType::NONE;
    s_cursorId = 0;
    return 0;
}

// AutoEquipCursorItem() — equip item from cursor
static int lua_AutoEquipCursorItem(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh && s_cursorType == CursorType::ITEM && s_cursorId != 0) {
        gh->useItemById(s_cursorId);
    }
    s_cursorType = CursorType::NONE;
    s_cursorId = 0;
    return 0;
}

// --- Frame System ---
// Minimal WoW-compatible frame objects with RegisterEvent/SetScript/GetScript.
// Frames are Lua tables with a metatable that provides methods.

// Frame method: frame:RegisterEvent("EVENT")

// The modifier state comes from the keyboard rather than from ImGui, which
// learns it from events this client does not always route there once FrameXML
// owns the mouse. lua_system_api registers the same three names against
// SDL_GetModState; these agree with it now instead of racing it, and the
// duplicate Is*KeyDown bindings that used to live here are gone with them.
static bool shiftHeld() { return (SDL_GetModState() & KMOD_SHIFT) != 0; }
static bool ctrlHeld()  { return (SDL_GetModState() & KMOD_CTRL)  != 0; }
static bool altHeld()   { return (SDL_GetModState() & KMOD_ALT)   != 0; }


// IsModifiedClick(action) → boolean
// Checks if a modifier key combo matches a named click action.
// Common actions: "CHATLINK" (shift-click), "DRESSUP" (ctrl-click),
//                 "SPLITSTACK" (shift-click), "SELFCAST" (alt-click)
static int lua_IsModifiedClick(lua_State* L) {
    const char* action = luaL_optstring(L, 1, "");
    std::string act(action);
    for (char& c : act) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    bool result = false;
    if (act.empty()) {
        // No action named means "is this click modified at all", which is how
        // the paperdoll and the bags branch: IsModifiedClick() picks between
        // OnModifiedClick and the plain OnClick. Answering with shift alone
        // sent every ctrl-click down the plain path, so ctrl-clicking a worn
        // item picked it up instead of putting it in the dressing room.
        result = shiftHeld() || ctrlHeld() || altHeld();
    }
    else if (act == "CHATLINK" || act == "SPLITSTACK")
        result = shiftHeld();
    else if (act == "DRESSUP" || act == "COMPAREITEMS")
        result = ctrlHeld();
    else if (act == "SELFCAST" || act == "FOCUSCAST")
        result = altHeld();
    else if (act == "STICKYCAMERA")
        result = ctrlHeld();
    else
        result = shiftHeld(); // Default: shift for unknown actions
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// GetModifiedClick(action) → key name ("SHIFT", "CTRL", "ALT", "NONE")
static int lua_GetModifiedClick(lua_State* L) {
    const char* action = luaL_optstring(L, 1, "");
    std::string act(action);
    for (char& c : act) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (act == "CHATLINK" || act == "SPLITSTACK")
        lua_pushstring(L, "SHIFT");
    else if (act == "DRESSUP" || act == "COMPAREITEMS")
        lua_pushstring(L, "CTRL");
    else if (act == "SELFCAST" || act == "FOCUSCAST")
        lua_pushstring(L, "ALT");
    else
        lua_pushstring(L, "SHIFT");
    return 1;
}
static int lua_SetModifiedClick(lua_State* L) { (void)L; return 0; }


// --- Trading ---
//
// Placing an item is the only part that needs the cursor, which is why these
// live here. The peer's side is read-only: a trade slot of theirs can be looked
// at and not touched, so clicking one does nothing rather than pretending.

static int pushTradeSlot(lua_State* L, game::GameHandler* gh,
                         const game::GameHandler::TradeSlot& slot) {
    if (!slot.occupied || slot.itemId == 0) { return luaReturnNil(L); }
    const auto* info = gh->getItemInfo(slot.itemId);
    lua_pushstring(L, info ? info->name.c_str() : "");
    lua_pushstring(L, gh->getItemIconPath(
        info && info->displayInfoId ? info->displayInfoId : slot.displayId).c_str());
    lua_pushnumber(L, slot.stackCount);
    lua_pushboolean(L, 1);       // isUsable
    lua_pushnil(L);              // enchantment: not carried by the trade packet
    return 5;
}

static int lua_GetTradePlayerItemInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || i < 1 || i > game::GameHandler::TRADE_SLOT_COUNT) { return luaReturnNil(L); }
    return pushTradeSlot(L, gh, gh->getMyTradeSlots()[i - 1]);
}

static int lua_GetTradeTargetItemInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || i < 1 || i > game::GameHandler::TRADE_SLOT_COUNT) { return luaReturnNil(L); }
    return pushTradeSlot(L, gh, gh->getPeerTradeSlots()[i - 1]);
}

static int pushTradeLink(lua_State* L, game::GameHandler* gh,
                         const game::GameHandler::TradeSlot& slot) {
    const auto* info = (gh && slot.occupied) ? gh->getItemInfo(slot.itemId) : nullptr;
    if (!info || info->name.empty()) { return luaReturnNil(L); }
    const char* ch = (info->quality < 8) ? kQualHexAlpha[info->quality] : "ffffffff";
    char link[256];
    snprintf(link, sizeof(link), "|c%s|Hitem:%u:0:0:0:0:0:0:0|h[%s]|h|r",
             ch, slot.itemId, info->name.c_str());
    lua_pushstring(L, link);
    return 1;
}

static int lua_GetTradePlayerItemLink(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || i < 1 || i > game::GameHandler::TRADE_SLOT_COUNT) { return luaReturnNil(L); }
    return pushTradeLink(L, gh, gh->getMyTradeSlots()[i - 1]);
}

static int lua_GetTradeTargetItemLink(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || i < 1 || i > game::GameHandler::TRADE_SLOT_COUNT) { return luaReturnNil(L); }
    return pushTradeLink(L, gh, gh->getPeerTradeSlots()[i - 1]);
}

// ClickTradeButton(slot) — put what is held into the slot, or take back what is
// already there when nothing is held
static int lua_ClickTradeButton(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int i = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || i < 1 || i > game::GameHandler::TRADE_SLOT_COUNT) return 0;
    if (s_cursorType == CursorType::ITEM && s_cursorBag >= 0) {
        gh->setTradeItem(static_cast<uint8_t>(i - 1),
                         static_cast<uint8_t>(s_cursorBag),
                         static_cast<uint8_t>(s_cursorSlot));
        s_cursorType = CursorType::NONE;
        s_cursorId = 0;
        s_cursorBag = -1;
    } else {
        gh->clearTradeItem(static_cast<uint8_t>(i - 1));
    }
    return 0;
}

// The other side's slots belong to the other player.
static int lua_ClickTargetTradeButton(lua_State* L) { (void)L; return 0; }

static int lua_SetTradeMoney(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) gh->setTradeGold(static_cast<uint64_t>(luaL_optnumber(L, 1, 0)));
    return 0;
}

static int lua_CloseTrade(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->cancelTrade();
    return 0;
}

static int lua_BeginTrade(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->acceptTradeRequest();
    return 0;
}

// --- Keybinding API ---
//
// Which commands exist, and in what order, comes from bindings.xml: the emitter
// turns it into __WoweeBindings (the list the UI walks, header rows included)
// and __WoweeBindingScripts (what each one runs). What each is *bound to* lives
// here instead, because it outlives any one Lua state and has to reach a file.

namespace {

/// command → its two keys, empty meaning not bound. Two because the UI offers a
/// primary and a secondary and will write either.
std::map<std::string, std::array<std::string, 2>>& bindingKeys() {
    static std::map<std::string, std::array<std::string, 2>> keys;
    return keys;
}

std::string bindingsFilePath() {
    return core::getConfigRoot() + "/bindings.cfg";
}

/// ImGui spells keys as "C" and "Space"; the interface expects "C" and "SPACE",
/// and compares them as strings everywhere.
std::string wowKeyFromImGui(ImGuiKey key) {
    // An action with nothing bound to it answers "None", which uppercased is a
    // perfectly ordinary-looking key name and would show as one.
    if (key == ImGuiKey_None) return "";
    const char* name = ImGui::GetKeyName(key);
    if (!name || !*name) return "";
    std::string out(name);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

/// The commands the client has a real action behind. Rebinding one of these has
/// to reach the manager, or the list would show the new key while the client
/// went on answering to the old one.
struct LiveBinding {
    const char* command;
    wowee::ui::KeybindingManager::Action action;
};
const LiveBinding kLiveBindings[] = {
    {"TOGGLECHARACTER0",  wowee::ui::KeybindingManager::Action::TOGGLE_CHARACTER_SCREEN},
    {"TOGGLEBACKPACK",    wowee::ui::KeybindingManager::Action::TOGGLE_BAGS},
    {"TOGGLESPELLBOOK",   wowee::ui::KeybindingManager::Action::TOGGLE_SPELLBOOK},
    {"TOGGLETALENTS",     wowee::ui::KeybindingManager::Action::TOGGLE_TALENTS},
    {"TOGGLEQUESTLOG",    wowee::ui::KeybindingManager::Action::TOGGLE_QUESTS},
    {"TOGGLEWORLDMAP",    wowee::ui::KeybindingManager::Action::TOGGLE_WORLD_MAP},
    {"TOGGLEMINIMAP",     wowee::ui::KeybindingManager::Action::TOGGLE_MINIMAP},
    {"TOGGLEACHIEVEMENT", wowee::ui::KeybindingManager::Action::TOGGLE_ACHIEVEMENTS},
    {"TOGGLEGUILDTAB",    wowee::ui::KeybindingManager::Action::TOGGLE_GUILD_ROSTER},
};

/// The named key whose name matches, or none. ImGui offers no reverse lookup,
/// and the set is small enough that walking it costs nothing next to the file
/// write that follows a rebind.
ImGuiKey imGuiKeyFromWow(const std::string& name) {
    if (name.empty()) return ImGuiKey_None;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        if (wowKeyFromImGui(static_cast<ImGuiKey>(k)) == name) {
            return static_cast<ImGuiKey>(k);
        }
    }
    return ImGuiKey_None;
}

/// Tells the client what a command is bound to now, for the commands it acts
/// on. Silent for the rest, which are listed and saved but not yet answered.
void pushBindingToClient(const std::string& command, const std::string& key) {
    for (const auto& live : kLiveBindings) {
        if (command != live.command) continue;
        const ImGuiKey imKey = imGuiKeyFromWow(key);
        wowee::ui::KeybindingManager::getInstance().setKeyForAction(live.action, imKey);
        return;
    }
}

/// The key the client is listening for, when this is a command it acts on.
///
/// Read at the moment it is asked rather than from the copy below, because the
/// client's own settings panel can rebind these too — and then the copy is a
/// second answer to a question the manager already owns, one keystroke out of
/// date and shown on every action button.
std::optional<std::string> liveKeyFor(const std::string& command) {
    for (const auto& live : kLiveBindings) {
        if (command != live.command) continue;
        const std::string key =
            wowKeyFromImGui(wowee::ui::KeybindingManager::getInstance()
                                .getKeyForAction(live.action));
        if (key.empty()) return std::nullopt;
        return key;
    }
    return std::nullopt;
}

/// The keys the client is actually listening for, asked of the manager that
/// listens rather than restated here — a second copy would be wrong the moment
/// either side moved. Commands the client has no action for keep their retail
/// default, so the list reads correctly even where nothing acts on it yet.
void seedBindingDefaults() {
    auto& keys = bindingKeys();
    if (!keys.empty()) return;

    static const struct { const char* command; const char* key; } kDefaults[] = {
        {"MOVEFORWARD", "W"},   {"MOVEBACKWARD", "S"},
        {"TURNLEFT", "A"},      {"TURNRIGHT", "D"},
        {"STRAFELEFT", "Q"},    {"STRAFERIGHT", "E"},
        {"JUMP", "SPACE"},      {"TOGGLEAUTORUN", "NUMLOCK"},
        {"TOGGLEGAMEMENU", "ESCAPE"},
        {"SCREENSHOT", "PRINTSCREEN"},
        {"ACTIONBUTTON1", "1"}, {"ACTIONBUTTON2", "2"},
        {"ACTIONBUTTON3", "3"}, {"ACTIONBUTTON4", "4"},
        {"ACTIONBUTTON5", "5"}, {"ACTIONBUTTON6", "6"},
        {"ACTIONBUTTON7", "7"}, {"ACTIONBUTTON8", "8"},
        {"ACTIONBUTTON9", "9"}, {"ACTIONBUTTON10", "0"},
        {"ACTIONBUTTON11", "-"},{"ACTIONBUTTON12", "="},
    };
    for (const auto& d : kDefaults) keys[d.command] = {d.key, ""};

    // Where a command corresponds to something the client really does, the key
    // shown is the one it really answers to.
    auto& manager = wowee::ui::KeybindingManager::getInstance();
    for (const auto& live : kLiveBindings) {
        const std::string key = wowKeyFromImGui(manager.getKeyForAction(live.action));
        if (!key.empty()) keys[live.command] = {key, ""};
    }
}

/// The command list the emitter built, or zero if bindings.xml never loaded.
int bindingCount(lua_State* L) {
    lua_getglobal(L, "__WoweeBindings");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    const int n = static_cast<int>(lua_objlen(L, -1));
    lua_pop(L, 1);
    return n;
}

/// The command at a one-based position, empty when out of range.
std::string bindingAt(lua_State* L, int index) {
    lua_getglobal(L, "__WoweeBindings");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return ""; }
    lua_rawgeti(L, -1, index);
    const char* s = lua_tostring(L, -1);
    std::string out = s ? s : "";
    lua_pop(L, 2);
    return out;
}

}  // namespace

// GetBindingKey(command) → key1, key2 (or nil)
static int lua_GetBindingKey(lua_State* L) {
    seedBindingDefaults();
    const std::string command = luaL_checkstring(L, 1);
    if (auto live = liveKeyFor(command)) {
        lua_pushstring(L, live->c_str());
        lua_pushnil(L);
        return 2;
    }
    auto it = bindingKeys().find(command);
    if (it == bindingKeys().end()) { lua_pushnil(L); lua_pushnil(L); return 2; }
    for (const std::string& key : it->second) {
        if (key.empty()) lua_pushnil(L); else lua_pushstring(L, key.c_str());
    }
    return 2;
}

// GetBindingAction(key) → command (or nil)
static int lua_GetBindingAction(lua_State* L) {
    seedBindingDefaults();
    const std::string key = luaL_checkstring(L, 1);
    for (const auto& [command, keys] : bindingKeys()) {
        if (keys[0] == key || keys[1] == key) {
            lua_pushstring(L, command.c_str());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// GetNumBindings() → how many rows the list has, headers included
static int lua_GetNumBindings(lua_State* L) {
    lua_pushinteger(L, bindingCount(L));
    return 1;
}

// GetBinding(index) → command, key1, key2. A header row is a command like
// "HEADER_MOVEMENT" with no keys, which is how the UI tells the two apart.
static int lua_GetBinding(lua_State* L) {
    seedBindingDefaults();
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    const std::string command = bindingAt(L, index);
    if (command.empty()) { lua_pushnil(L); return 1; }
    lua_pushstring(L, command.c_str());
    if (auto live = liveKeyFor(command)) {
        lua_pushstring(L, live->c_str());
        lua_pushnil(L);
        return 3;
    }
    auto it = bindingKeys().find(command);
    if (it == bindingKeys().end()) { lua_pushnil(L); lua_pushnil(L); return 3; }
    for (const std::string& key : it->second) {
        if (key.empty()) lua_pushnil(L); else lua_pushstring(L, key.c_str());
    }
    return 3;
}

// SetBinding(key, command) → true. A nil command clears whatever holds the key,
// which is how the UI unbinds.
static int lua_SetBinding(lua_State* L) {
    seedBindingDefaults();
    const std::string key = luaL_checkstring(L, 1);
    const char* command = lua_isnoneornil(L, 2) ? nullptr : luaL_checkstring(L, 2);

    // A key belongs to one command at a time, so it leaves the one that had it
    // before it joins another — otherwise both claim it and which one answers
    // depends on map order.
    for (auto& [existing, keys] : bindingKeys()) {
        for (std::string& slot : keys) {
            if (slot != key) continue;
            slot.clear();
            // The command that just lost the key has to be told, not only the
            // one that gained it, or the client answers to both. What it
            // answers to now is whichever slot still holds something, which is
            // not always the first.
            pushBindingToClient(existing, keys[0].empty() ? keys[1] : keys[0]);
        }
    }
    if (command) {
        auto& keys = bindingKeys()[command];
        if (keys[0].empty()) keys[0] = key; else keys[1] = key;
        pushBindingToClient(command, keys[0]);
    }
    // Six frames wait on this, the action buttons among them: it is what
    // redraws the little key printed in the corner. Without it a rebind takes
    // effect while every button goes on showing the old key.
    if (auto* gh = getGameHandler(L)) gh->fireAddonEvent("UPDATE_BINDINGS", {});
    lua_pushboolean(L, 1);
    return 1;
}

// SaveBindings(which) → writes what is bound where, for the next run
static int lua_SaveBindings(lua_State* L) {
    (void)L;
    const std::string path = bindingsFilePath();
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        LOG_WARNING("Could not write the key bindings to ", path);
        return 0;
    }
    for (const auto& [command, keys] : bindingKeys()) {
        // Both slots on one line, the second empty when there is no second key,
        // so a command that lost one is recorded as having lost it.
        out << command << "=" << keys[0] << "," << keys[1] << "\n";
    }
    return 0;
}

// LoadBindings(which) → what a previous run saved, over the defaults
static int lua_LoadBindings(lua_State* L) {
    (void)L;
    seedBindingDefaults();
    std::ifstream in(bindingsFilePath());
    if (!in) return 0;   // Nothing saved yet is not a failure.
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string command = line.substr(0, eq);
        const std::string rest = line.substr(eq + 1);
        const size_t comma = rest.find(',');
        bindingKeys()[command] = {
            rest.substr(0, comma),
            comma == std::string::npos ? "" : rest.substr(comma + 1)
        };
        // What was saved is what the client should answer to, not what it
        // started with — otherwise a rebind survives in the list and nowhere
        // else, and only until the next save overwrites it.
        pushBindingToClient(command, bindingKeys()[command][0]);
    }
    if (auto* gh = getGameHandler(L)) gh->fireAddonEvent("UPDATE_BINDINGS", {});
    return 0;
}

// RunBinding(command, keystate) → runs what bindings.xml says the command does
static int lua_RunBinding(lua_State* L) {
    const std::string command = luaL_checkstring(L, 1);
    const char* keystate = lua_isnoneornil(L, 2) ? "down" : luaL_checkstring(L, 2);

    lua_getglobal(L, "__WoweeBindingScripts");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    lua_getfield(L, -1, command.c_str());
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return 0; }
    lua_pushstring(L, keystate);
    if (lua_pcall(L, 1, 0, 0) != 0) {
        LOG_WARNING("Binding ", command, " failed: ",
                    lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 0;
}
static int lua_SetOverrideBindingClick(lua_State* L) { (void)L; return 0; }
static int lua_ClearOverrideBindings(lua_State* L) { (void)L; return 0; }

// Frame methods: SetPoint, SetSize, SetWidth, SetHeight, GetWidth, GetHeight, GetCenter, SetAlpha, GetAlpha

void registerActionLuaAPI(lua_State* L) {
    static const struct { const char* name; lua_CFunction func; } api[] = {
                {"HasAction",           lua_HasAction},
                // A macro's name, which only a macro has; ActionButton_Update
                // shows it under the icon and expects nothing for a spell.
                {"GetActionText",       [](lua_State* L) -> int {
                    lua_pushnil(L);
                    return 1;
                }},
                // Whether the action has a range to be in or out of. Nothing
                // here tracks that yet, and false is what "no range check"
                // looks like — the button simply never dims for distance.
                {"ActionHasRange",      [](lua_State* L) -> int {
                    lua_pushboolean(L, 0);
                    return 1;
                }},
                {"GetActionTexture",    lua_GetActionTexture},
                {"IsCurrentAction",     lua_IsCurrentAction},
                {"IsUsableAction",      lua_IsUsableAction},
                {"IsActionInRange",     lua_IsActionInRange},
                {"GetActionInfo",       lua_GetActionInfo},
                {"GetActionCount",      lua_GetActionCount},
                {"GetActionCooldown",   lua_GetActionCooldown},
                {"UseAction",           lua_UseAction},
                {"PickupAction",        lua_PickupAction},
                {"PlaceAction",         lua_PlaceAction},
                {"PickupSpell",         lua_PickupSpell},
                {"PickupSpellBookItem", lua_PickupSpellBookItem},
                {"PickupContainerItem", lua_PickupContainerItem},
                {"PickupInventoryItem", lua_PickupInventoryItem},
                {"ClearCursor",         lua_ClearCursor},
                {"GetCursorInfo",       lua_GetCursorInfo},
                {"CursorHasItem",       lua_CursorHasItem},
                {"CursorHasSpell",      lua_CursorHasSpell},
                {"DeleteCursorItem",    lua_DeleteCursorItem},
                {"AutoEquipCursorItem", lua_AutoEquipCursorItem},
                {"IsModifiedClick",     lua_IsModifiedClick},
                {"GetModifiedClick",    lua_GetModifiedClick},
                {"SetModifiedClick",    lua_SetModifiedClick},
                // Macros. The client has stored their text all along and
                // persisted it per character; only the interface's way in was
                // missing, so GetNumMacros answered zero and the panel opened
                // empty over a full set.
                //
                // WoW splits the ids: 1-36 are account-wide, 37 and up belong
                // to the character. That split is what GetNumMacros reports and
                // what CreateMacro's perCharacter flag chooses between.
                {"GetNumMacros",        [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            int global = 0, perChar = 0;
            if (gh) {
                for (uint32_t id : gh->getMacroIds()) {
                    if (id <= 36) ++global; else ++perChar;
                }
            }
            lua_pushnumber(L, global);
            lua_pushnumber(L, perChar);
            return 2;
        }},
                // GetMacroInfo(id) → name, icon, body
                {"GetMacroInfo",        [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh || id == 0 || gh->getMacroText(id).empty()) {
                return luaReturnNil(L);
            }
            std::string name = gh->getMacroName(id);
            if (name.empty()) name = "Macro";
            std::string icon = gh->getMacroIcon(id);
            if (icon.empty()) icon = "Interface\\Icons\\INV_Misc_QuestionMark";
            lua_pushstring(L, name.c_str());
            lua_pushstring(L, icon.c_str());
            lua_pushstring(L, gh->getMacroText(id).c_str());
            return 3;
        }},
                // CreateMacro(name, icon, body, perCharacter) → id
                {"CreateMacro",         [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return luaReturnNil(L);
            const char* name = luaL_optstring(L, 1, "Macro");
            const char* icon = luaL_optstring(L, 2, "");
            const char* body = luaL_optstring(L, 3, "");
            const bool perChar = lua_toboolean(L, 4) != 0;
            // The first free id in the half that was asked for. WoW allows 18
            // of each; refusing past that is what stops a full list growing
            // ids the interface will not show.
            const uint32_t first = perChar ? 37u : 1u;
            const uint32_t last  = perChar ? 54u : 36u;
            uint32_t id = 0;
            for (uint32_t candidate = first; candidate <= last; ++candidate) {
                if (gh->getMacroText(candidate).empty()) { id = candidate; break; }
            }
            if (id == 0) return luaReturnNil(L);
            gh->setMacroText(id, body);
            gh->setMacroMeta(id, name, icon);
            lua_pushnumber(L, id);
            return 1;
        }},
                // EditMacro(id, name, icon, body) → id
                {"EditMacro",           [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh || id == 0) return luaReturnNil(L);
            const char* name = luaL_optstring(L, 2, nullptr);
            const char* icon = luaL_optstring(L, 3, nullptr);
            const char* body = luaL_optstring(L, 4, nullptr);
            // Each part is optional and nil means "leave it": EditMacro is
            // called with only a name when a macro is renamed, and writing the
            // other two as empty would erase the macro being renamed.
            if (body) gh->setMacroText(id, body);
            gh->setMacroMeta(id, name ? name : gh->getMacroName(id),
                                 icon ? icon : gh->getMacroIcon(id));
            lua_pushnumber(L, id);
            return 1;
        }},
                {"DeleteMacro",         [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (gh && id != 0) {
                gh->setMacroText(id, "");     // empty text removes it
                gh->setMacroMeta(id, "", "");
            }
            return 0;
        }},
                // Saved as each change is made, so this has nothing to do.
                {"SaveMacros",          [](lua_State* L) -> int { (void)L; return 0; }},
                // PickupMacro(id) — onto the cursor, so it can be dropped on
                // an action slot. The slot type already existed and the packet
                // that sets it already went out for spells and items; a macro
                // was simply never something the cursor could hold.
                {"PickupMacro",         [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (!gh || id == 0 || gh->getMacroText(id).empty()) return 0;
            s_cursorType = CursorType::MACRO;
            s_cursorId = id;
            std::string icon = gh->getMacroIcon(id);
            if (icon.empty()) icon = "Interface\\Icons\\INV_Misc_QuestionMark";
            wowee::ui::frameXmlSetCursorItem(icon);
            return 0;
        }},
                {"GetTradePlayerItemInfo", lua_GetTradePlayerItemInfo},
                {"GetTradeTargetItemInfo", lua_GetTradeTargetItemInfo},
                {"GetTradePlayerItemLink", lua_GetTradePlayerItemLink},
                {"GetTradeTargetItemLink", lua_GetTradeTargetItemLink},
                {"ClickTradeButton",    lua_ClickTradeButton},
                {"ClickTargetTradeButton", lua_ClickTargetTradeButton},
                {"SetTradeMoney",       lua_SetTradeMoney},
                {"CloseTrade",          lua_CloseTrade},
                {"BeginTrade",          lua_BeginTrade},
                {"GetBindingKey",       lua_GetBindingKey},
                {"GetBindingAction",    lua_GetBindingAction},
                {"GetNumBindings",      lua_GetNumBindings},
                {"GetBinding",          lua_GetBinding},
                {"SetBinding",          lua_SetBinding},
                {"SaveBindings",        lua_SaveBindings},
                {"LoadBindings",        lua_LoadBindings},
                {"RunBinding",          lua_RunBinding},
                {"SetOverrideBindingClick", lua_SetOverrideBindingClick},
                {"ClearOverrideBindings", lua_ClearOverrideBindings},
                // Paging lives here, and the getter with it. Both were
                // defined twice — this pair against __WoweeActionBarPage and a
                // bootstrap pair against a local — so whichever won,
                // ChangeActionBarPage could move one number while
                // GetActionBarPage read the other. Removing only the getter
                // made that worse rather than better, which is what happened
                // an hour ago.
                {"GetActionBarPage", [](lua_State* L) -> int {
            lua_getglobal(L, "__WoweeActionBarPage");
            if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 1); }
            return 1;
        }},
                {"ChangeActionBarPage", [](lua_State* L) -> int {
            int page = static_cast<int>(luaL_checknumber(L, 1));
            if (page < 1) page = 1;
            if (page > 6) page = 6;
            lua_pushnumber(L, page);
            lua_setglobal(L, "__WoweeActionBarPage");
            // Fire ACTIONBAR_PAGE_CHANGED via the frame event system
            lua_getglobal(L, "__WoweeEvents");
            if (!lua_isnil(L, -1)) {
                lua_getfield(L, -1, "ACTIONBAR_PAGE_CHANGED");
                if (!lua_isnil(L, -1)) {
                    int n = static_cast<int>(lua_objlen(L, -1));
                    for (int i = 1; i <= n; i++) {
                        lua_rawgeti(L, -1, i);
                        if (lua_isfunction(L, -1)) {
                            lua_pushstring(L, "ACTIONBAR_PAGE_CHANGED");
                            if (lua_pcall(L, 1, 0, 0) != 0) {
                                LOG_ERROR("LuaEngine: ACTIONBAR_PAGE_CHANGED handler error: ",
                                          lua_tostring(L, -1) ? lua_tostring(L, -1) : "(unknown)");
                                lua_pop(L, 1);
                            }
                        } else lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            return 0;
        }},
                // Two returns: whether there is a pet interface at all, and
                // whether it is a hunter's. PetFrame_SetHappiness reads the
                // second to decide whether to draw the happiness icon, and one
                // return left it nil.
                {"HasPetUI", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const bool has = gh && gh->hasPet();
            lua_pushboolean(L, has ? 1 : 0);
            lua_pushboolean(L, 0);   // hunter pet: not distinguished yet
            return 2;
        }},
                // Happiness, for a hunter's pet only. Nil is the honest answer
                // for everyone else and the one PetFrame_SetHappiness guards
                // for — the fallback answering with an object instead made
                // that guard pass and the branch index nothing.
                // GetMirrorTimerInfo(index) → timer, value, maxvalue, scale,
                // paused, label.
                //
                // "UNKNOWN" for a timer that is not running, which is exactly
                // what mirrortimer.lua tests for before using the rest. Absent,
                // the fallback answered with an object, that test passed, and
                // the next line divided a nil by a thousand.
                {"GetMirrorTimerInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_optnumber(L, 1, 1)) - 1;
            static const char* kNames[3]  = {"FATIGUE", "BREATH", "FEIGNDEATH"};
            static const char* kLabels[3] = {"Fatigue", "Breath", "Feign Death"};
            if (!gh || index < 0 || index > 2 || !gh->getMirrorTimer(index).active) {
                lua_pushstring(L, "UNKNOWN");
                lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0);
                lua_pushboolean(L, 0);
                lua_pushstring(L, "");
                return 6;
            }
            const auto& t = gh->getMirrorTimer(index);
            lua_pushstring(L, kNames[index]);
            lua_pushnumber(L, t.value);
            lua_pushnumber(L, t.maxValue);
            lua_pushnumber(L, t.scale);
            lua_pushboolean(L, t.paused ? 1 : 0);
            lua_pushstring(L, kLabels[index]);
            return 6;
        }},
                // GetPetHappiness() → happiness 1..3, and what it does to damage
                //
                // Both values, and that is not tidiness. The pet frame does
                // format(PET_DAMAGE_PERCENTAGE, damagePercentage) — "Causes %d%%
                // of normal damage" — the moment happiness is anything at all,
                // and %d against nil raises. Answering nil, as this did before,
                // was safe only because the line above it returns early on a nil
                // happiness; filling in the first value alone would have turned
                // a blank indicator into a dead pet frame for every hunter.
                //
                // Happiness is a power like mana, at index 4, running from
                // nothing to its own maximum. The three faces are the thirds of
                // that range, and the damage each is worth — three quarters,
                // normal, a quarter more — is the game's, not a guess.
                {"GetPetHappiness", [](lua_State* L) -> int {
            auto* pet = resolveUnit(L, "pet");
            if (!pet) { lua_pushnil(L); return 1; }
            const uint32_t maxHappiness = pet->getMaxPowerByType(4);
            if (maxHappiness == 0) { lua_pushnil(L); return 1; }

            const uint32_t happiness = pet->getPowerByType(4);
            const int level = (happiness * 3 >= maxHappiness * 2) ? 3
                            : (happiness * 3 >= maxHappiness)     ? 2
                                                                  : 1;
            static const int kDamagePercent[4] = {0, 75, 100, 125};
            lua_pushnumber(L, level);
            lua_pushnumber(L, kDamagePercent[level]);
            return 2;
        }},
                // Only referenced from commented-out code in 3.3.5, but a
                // temporary pet's timer is nil when there is no timer.
                {"GetPetTimeRemaining", [](lua_State* L) -> int {
            lua_pushnil(L);
            return 1;
        }},
                // GetPetActionSlotUsable(index) → whether the pet can use it
                //
                // Read as "if usable then draw it normally, else grey it out",
                // so an absent answer greyed out every ability the pet has. A
                // slot holding a spell is usable; an empty one has nothing to
                // grey.
                // CancelItemTempEnchantment(hand) — drop a weapon imbue
                //
                // The interface counts the hands from one and the request from
                // zero, which is the only thing between them.
                {"CancelItemTempEnchantment", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int hand = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (gh && (hand == 1 || hand == 2)) {
                gh->cancelTempEnchantment(static_cast<uint8_t>(hand - 1));
            }
            return 0;
        }},
                {"GetPetActionSlotUsable", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh || index < 1 || index > game::GameHandler::PET_ACTION_BAR_SLOTS) {
                lua_pushboolean(L, 0);
                return 1;
            }
            const uint32_t spellId = gh->getPetActionSlot(index - 1) & 0x00FFFFFF;
            lua_pushboolean(L, spellId != 0 ? 1 : 0);
            return 1;
        }},
                {"GetPetActionInfo", [](lua_State* L) -> int {
            // GetPetActionInfo(index) → name, subtext, texture, isToken, isActive, autoCastAllowed, autoCastEnabled
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || index < 1 || index > game::GameHandler::PET_ACTION_BAR_SLOTS) {
                return luaReturnNil(L);
            }
            uint32_t packed = gh->getPetActionSlot(index - 1);
            uint32_t spellId = packed & 0x00FFFFFF;
            uint8_t actionType = static_cast<uint8_t>((packed >> 24) & 0xFF);
            if (spellId == 0) { return luaReturnNil(L); }
            const std::string& name = gh->getSpellName(spellId);
            std::string iconPath = gh->getSpellIconPath(spellId);
            lua_pushstring(L, name.empty() ? "Unknown" : name.c_str()); // name
            lua_pushstring(L, "");                                       // subtext
            lua_pushstring(L, iconPath.empty() ? "Interface\\Icons\\INV_Misc_QuestionMark" : iconPath.c_str()); // texture
            lua_pushboolean(L, 0);                                       // isToken
            lua_pushboolean(L, (actionType & 0xC0) != 0 ? 1 : 0);      // isActive
            lua_pushboolean(L, 1);                                       // autoCastAllowed
            lua_pushboolean(L, gh->isPetSpellAutocast(spellId) ? 1 : 0); // autoCastEnabled
            return 7;
        }},
                {"GetPetActionCooldown", [](lua_State* L) -> int {
            lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 1);
            return 3;
        }},
                {"PetAttack", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh && gh->hasPet() && gh->hasTarget())
                gh->sendPetAction(
                    game::pet::packPetAction(game::pet::ActionType::Command, game::pet::kAttack),
                    gh->getTargetGuid());
            return 0;
        }},
                {"PetFollow", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh && gh->hasPet())
                gh->sendPetAction(
                    game::pet::packPetAction(game::pet::ActionType::Command, game::pet::kFollow), 0);
            return 0;
        }},
                {"PetWait", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh && gh->hasPet())
                gh->sendPetAction(
                    game::pet::packPetAction(game::pet::ActionType::Command, game::pet::kStay), 0);
            return 0;
        }},
                {"PetPassiveMode", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh && gh->hasPet())
                gh->sendPetAction(
                    game::pet::packPetAction(game::pet::ActionType::Reaction, game::pet::kPassive), 0);
            return 0;
        }},
                {"CastPetAction", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || !gh->hasPet() || index < 1 || index > game::GameHandler::PET_ACTION_BAR_SLOTS) return 0;
            uint32_t packed = gh->getPetActionSlot(index - 1);
            uint32_t spellId = packed & 0x00FFFFFF;
            if (spellId != 0) {
                uint64_t target = gh->hasTarget() ? gh->getTargetGuid() : gh->getPetGuid();
                gh->sendPetAction(packed, target);
            }
            return 0;
        }},
                {"TogglePetAutocast", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || !gh->hasPet() || index < 1 || index > game::GameHandler::PET_ACTION_BAR_SLOTS) return 0;
            uint32_t packed = gh->getPetActionSlot(index - 1);
            uint32_t spellId = packed & 0x00FFFFFF;
            if (spellId != 0) gh->togglePetSpellAutocast(spellId);
            return 0;
        }},
                {"PetDismiss", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh && gh->hasPet())
                gh->sendPetAction(
                    game::pet::packPetAction(game::pet::ActionType::Command, game::pet::kAbandon), 0);
            return 0;
        }},
                {"IsPetAttackActive", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushboolean(L, gh && gh->getPetCommand() == 2 ? 1 : 0); // 2=attack
            return 1;
        }},
                {"PetDefensiveMode", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh && gh->hasPet())
                gh->sendPetAction(
                    game::pet::packPetAction(game::pet::ActionType::Reaction, game::pet::kDefensive), 0);
            return 0;
        }},
    };
    for (const auto& [name, func] : api) {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
    }
}

} // namespace wowee::addons
