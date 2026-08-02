// lua_action_api.cpp — Action bar, cursor/pickup, keyboard input, key bindings, and pet actions Lua API bindings.
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#include "addons/lua_api_helpers.hpp"
#include "ui/framexml_takeover.hpp"
#include "game/pet_action.hpp"
#include "imgui.h"

namespace wowee::addons {

enum class CursorType { NONE, SPELL, ITEM, ACTION };
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
        gh->fireAddonEvent("ITEM_LOCK_CHANGED", {"-1", std::to_string(slot)});
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

static int lua_IsShiftKeyDown(lua_State* L) {
    lua_pushboolean(L, ImGui::GetIO().KeyShift ? 1 : 0);
    return 1;
}
static int lua_IsControlKeyDown(lua_State* L) {
    lua_pushboolean(L, ImGui::GetIO().KeyCtrl ? 1 : 0);
    return 1;
}
static int lua_IsAltKeyDown(lua_State* L) {
    lua_pushboolean(L, ImGui::GetIO().KeyAlt ? 1 : 0);
    return 1;
}

// IsModifiedClick(action) → boolean
// Checks if a modifier key combo matches a named click action.
// Common actions: "CHATLINK" (shift-click), "DRESSUP" (ctrl-click),
//                 "SPLITSTACK" (shift-click), "SELFCAST" (alt-click)
static int lua_IsModifiedClick(lua_State* L) {
    const char* action = luaL_optstring(L, 1, "");
    std::string act(action);
    for (char& c : act) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto& io = ImGui::GetIO();
    bool result = false;
    if (act == "CHATLINK" || act == "SPLITSTACK")
        result = io.KeyShift;
    else if (act == "DRESSUP" || act == "COMPAREITEMS")
        result = io.KeyCtrl;
    else if (act == "SELFCAST" || act == "FOCUSCAST")
        result = io.KeyAlt;
    else if (act == "STICKYCAMERA")
        result = io.KeyCtrl;
    else
        result = io.KeyShift; // Default: shift for unknown actions
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

// --- Keybinding API ---
// Maps WoW binding names like "ACTIONBUTTON1" to key display strings like "1"

// GetBindingKey(command) → key1, key2 (or nil)
static int lua_GetBindingKey(lua_State* L) {
    const char* cmd = luaL_checkstring(L, 1);
    std::string command(cmd);
    // Return intuitive default bindings for action buttons
    if (command.find("ACTIONBUTTON") == 0) {
        std::string num = command.substr(12);
        int n = 0;
        try { n = std::stoi(num); } catch(...) {}
        if (n >= 1 && n <= 9) {
            lua_pushstring(L, num.c_str());
            return 1;
        } else if (n == 10) {
            lua_pushstring(L, "0");
            return 1;
        } else if (n == 11) {
            lua_pushstring(L, "-");
            return 1;
        } else if (n == 12) {
            lua_pushstring(L, "=");
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// GetBindingAction(key) → command (or nil)
static int lua_GetBindingAction(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    std::string k(key);
    // Simple reverse mapping for number keys
    if (k.size() == 1 && k[0] >= '1' && k[0] <= '9') {
        lua_pushstring(L, ("ACTIONBUTTON" + k).c_str());
        return 1;
    } else if (k == "0") {
        lua_pushstring(L, "ACTIONBUTTON10");
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int lua_GetNumBindings(lua_State* L) { return luaReturnZero(L); }
static int lua_GetBinding(lua_State* L) { (void)L; lua_pushnil(L); return 1; }
static int lua_SetBinding(lua_State* L) { (void)L; return 0; }
static int lua_SaveBindings(lua_State* L) { (void)L; return 0; }
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
                {"IsShiftKeyDown",      lua_IsShiftKeyDown},
                {"IsControlKeyDown",    lua_IsControlKeyDown},
                {"IsAltKeyDown",        lua_IsAltKeyDown},
                {"IsModifiedClick",     lua_IsModifiedClick},
                {"GetModifiedClick",    lua_GetModifiedClick},
                {"SetModifiedClick",    lua_SetModifiedClick},
                {"GetBindingKey",       lua_GetBindingKey},
                {"GetBindingAction",    lua_GetBindingAction},
                {"GetNumBindings",      lua_GetNumBindings},
                {"GetBinding",          lua_GetBinding},
                {"SetBinding",          lua_SetBinding},
                {"SaveBindings",        lua_SaveBindings},
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
                {"HasPetUI", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushboolean(L, gh && gh->hasPet() ? 1 : 0);
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
