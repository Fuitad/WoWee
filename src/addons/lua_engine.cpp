#include "addons/lua_engine.hpp"
#include "ui/widget_tree.hpp"
#include "ui/ui_colors.hpp"
#include "ui/framexml_takeover.hpp"
#include <chrono>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <climits>
#include <set>
#include <cstdlib>
#include "addons/lua_api_helpers.hpp"
#include "addons/lua_api_registrations.hpp"
#include "addons/toc_parser.hpp"
#include "core/window.hpp"
#include <imgui.h>
#include <fstream>
#include "core/app_clock.hpp"
#include "core/config_paths.hpp"
#include <filesystem>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace wowee::addons {

namespace {
/// Names asked for and not found, while the fallback is on. File-scope because
/// the recorder is a Lua callback and the report runs at shutdown.
std::set<std::string>& missingApiNames() {
    static std::set<std::string> names;
    return names;
}
}


/// Log at warning from Lua.
///
/// print() goes to chat and to the log at info, and the log carries nothing
/// below warning — so anything printed for diagnosis is invisible in the one
/// place it would be read. This is for the interface probe, which had been
/// producing no output at all for that reason.
static int lua_wowee_warn(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    LOG_WARNING(msg ? msg : "(nil)");
    return 0;
}

static int lua_wow_print(lua_State* L) {
    int nargs = lua_gettop(L);
    std::string result;
    for (int i = 1; i <= nargs; i++) {
        if (i > 1) result += '\t';
        // Lua 5.1: use lua_tostring (luaL_tolstring is 5.3+)
        if (lua_isstring(L, i) || lua_isnumber(L, i)) {
            const char* s = lua_tostring(L, i);
            if (s) result += s;
        } else if (lua_isboolean(L, i)) {
            result += lua_toboolean(L, i) ? "true" : "false";
        } else if (lua_isnil(L, i)) {
            result += "nil";
        } else {
            result += lua_typename(L, lua_type(L, i));
        }
    }

    auto* gh = getGameHandler(L);
    if (gh) {
        game::MessageChatData msg;
        msg.type = game::ChatType::SYSTEM;
        msg.language = game::ChatLanguage::UNIVERSAL;
        msg.message = result;
        gh->addLocalChatMessage(msg);
    }
    LOG_INFO("[Lua] ", result);
    return 0;
}

// WoW-compatible message() — same as print for now
static int lua_wow_message(lua_State* L) {
    return lua_wow_print(L);
}

// Helper: resolve WoW unit IDs to GUID
// Read UNIT_FIELD_TARGET_LO/HI from an entity's update fields to get what it's targeting

// --- Frame system functions ---

static int lua_Frame_RegisterEvent(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);  // self
    const char* eventName = luaL_checkstring(L, 2);

    // Get frame's registered events table (create if needed)
    lua_getfield(L, 1, "__events");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, 1, "__events");
    }
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, eventName);
    lua_pop(L, 1);

    // Also register in global __WoweeFrameEvents for dispatch
    lua_getglobal(L, "__WoweeFrameEvents");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "__WoweeFrameEvents");
    }
    lua_getfield(L, -1, eventName);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, eventName);
    }
    // Append the frame, but only if it is not already listening.
    //
    // The frame's own __events table is keyed by name and so is already
    // idempotent; this list is not. Registering the same event twice put the
    // frame in it twice and its OnEvent then ran twice for every one of those
    // events — and UnregisterEvent removes the first match and stops, so one
    // unregister could not undo a double register. Nothing about the pair is
    // symmetric unless the insert refuses duplicates.
    const int len = static_cast<int>(lua_objlen(L, -1));
    bool already = false;
    for (int i = 1; i <= len && !already; ++i) {
        lua_rawgeti(L, -1, i);
        already = lua_rawequal(L, -1, 1) != 0;
        lua_pop(L, 1);
    }
    if (!already) {
        lua_pushvalue(L, 1);  // push frame
        lua_rawseti(L, -2, len + 1);
    }
    lua_pop(L, 2);  // pop list + __WoweeFrameEvents
    return 0;
}

// Frame method: frame:UnregisterEvent("EVENT")
static int lua_Frame_UnregisterEvent(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* eventName = luaL_checkstring(L, 2);

    // Remove from frame's own events
    lua_getfield(L, 1, "__events");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, eventName);
    }
    lua_pop(L, 1);

    // And from the global list dispatch actually reads. Clearing only the
    // frame's own table left the registration in __WoweeFrameEvents, so a
    // frame went on being handed events it had asked to stop receiving —
    // which is not a missed refresh but an error, because the handler runs in
    // a state its own OnHide has already torn down. The paperdoll's equipment
    // flyout unregisters and nils self.button together, then indexed that nil
    // on the next inventory change.
    lua_getglobal(L, "__WoweeFrameEvents");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, eventName);
        if (lua_istable(L, -1)) {
            const int listeners = lua_objlen(L, -1);
            // Walk backwards so a removal cannot skip the next entry.
            for (int i = listeners; i >= 1; --i) {
                lua_rawgeti(L, -1, i);
                const bool isSelf = lua_rawequal(L, -1, 1);
                lua_pop(L, 1);
                if (!isSelf) continue;
                // table.remove semantics: shift the tail down one.
                for (int j = i; j < listeners; ++j) {
                    lua_rawgeti(L, -1, j + 1);
                    lua_rawseti(L, -2, j);
                }
                lua_pushnil(L);
                lua_rawseti(L, -2, listeners);
                break;
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 0;
}

// Frame method: frame:SetScript("handler", func)
static int lua_Frame_SetScript(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* scriptType = luaL_checkstring(L, 2);
    // arg 3 can be function or nil
    lua_getfield(L, 1, "__scripts");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, 1, "__scripts");
    }
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, scriptType);
    lua_pop(L, 1);

    // Track frames with OnUpdate in __WoweeOnUpdateFrames
    //
    // Once each. Clearing an OnUpdate leaves the frame on this list — the
    // dispatcher skips it because the script is gone — so setting one again
    // appended a second entry, and the frame was then ticked twice a frame
    // with the same elapsed. Start-and-stop is the ordinary shape for this
    // handler (UIFrameFade installs one for the fade and clears it at the
    // end), so a frame that faded five times ran its next OnUpdate five times
    // over and every timer driven by elapsed ran that many times too fast.
    if (strcmp(scriptType, "OnUpdate") == 0) {
        lua_getglobal(L, "__WoweeOnUpdateFrames");
        if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
        if (lua_isfunction(L, 3)) {
            const int len = static_cast<int>(lua_objlen(L, -1));
            bool already = false;
            for (int i = 1; i <= len && !already; ++i) {
                lua_rawgeti(L, -1, i);
                already = lua_rawequal(L, -1, 1) != 0;
                lua_pop(L, 1);
            }
            if (!already) {
                lua_pushvalue(L, 1);
                lua_rawseti(L, -2, len + 1);
            }
        }
        lua_pop(L, 1);
    }
    return 0;
}

// Frame method: frame:GetScript("handler")
static int lua_Frame_GetScript(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* scriptType = luaL_checkstring(L, 2);
    lua_getfield(L, 1, "__scripts");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, scriptType);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// Frame method: frame:GetName()
static int lua_Frame_GetName(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__name");
    return 1;
}

// Frame method: frame:Show() / frame:Hide() / frame:IsShown() / frame:IsVisible()
static int lua_Frame_Show(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, 1);
    lua_setfield(L, 1, "__visible");
    return 0;
}
static int lua_Frame_Hide(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, 0);
    lua_setfield(L, 1, "__visible");
    return 0;
}


// ── Widget-backed regions ───────────────────────────────────────────────────
//
// Frames and regions are Lua tables, as they were, but each now carries a
// __wid handle into the C++ widget tree that holds its geometry and its art.
// Without that the methods below were a table of no-ops: an addon could call
// SetTexture all day and nothing existed to draw.

namespace {

uint32_t widgetIdOf(lua_State* L, int index) {
    if (!lua_istable(L, index)) return 0;
    lua_getfield(L, index, "__wid");
    const uint32_t id = static_cast<uint32_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return id;
}

wowee::ui::Widget* widgetOf(lua_State* L, int index) {
    auto* tree = wowee::addons::getWidgetTree(L);
    if (!tree) return nullptr;
    return tree->get(widgetIdOf(L, index));
}

/// Whether the frame is shown, from the widget rather than a field beside it.
///
/// Show and Hide write both, but they are not the only way a frame's
/// visibility changes, so the two drift. The bag buttons ask this to decide
/// whether to draw themselves pressed — which is why one stayed lit over a bag
/// that had been closed. Defined here rather than with the other frame methods
/// because it needs widgetOf, which is declared just above.

// SetPoint(point [, relativeTo] [, relativePoint] [, x, y]) — every argument
// after the first is optional and the shapes overlap, so decide by type rather
// than by count.
int lua_Region_SetPoint(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;

    wowee::ui::Anchor a;
    a.point = luaL_optstring(L, 2, "CENTER");
    int argi = 3;
    if (lua_istable(L, argi)) {
        a.relativeTo = widgetIdOf(L, argi);
        ++argi;
    } else if (lua_isstring(L, argi) && !lua_isnumber(L, argi)) {
        // A name rather than the frame itself; resolve through the global it
        // was published under, which is how FrameXML refers to most things.
        lua_getglobal(L, lua_tostring(L, argi));
        if (lua_istable(L, -1)) a.relativeTo = widgetIdOf(L, lua_gettop(L));
        lua_pop(L, 1);
        ++argi;
    } else if (lua_isnil(L, argi)) {
        // Explicitly nil, which means the parent — and which still occupies its
        // place in the argument list. Skipping over it read the relative point
        // as the relative frame and everything after it moved up one, so
        // SetPoint(point, nil, "BOTTOM") silently became point-to-point on the
        // parent. FrameXML passes nil here constantly, and a name that failed
        // to resolve arrives the same way.
        ++argi;
    }
    // Anchoring to itself is not a position, and a name can resolve to the
    // frame that was just published under it.
    if (a.relativeTo == id) {
        const auto* self = tree->get(id);
        a.relativeTo = self ? self->parent : 0;
    }
    if (lua_isstring(L, argi) && !lua_isnumber(L, argi)) {
        a.relativePoint = lua_tostring(L, argi);
        ++argi;
    } else {
        a.relativePoint = a.point;   // Blizzard's default is the same point
    }
    if (lua_isnumber(L, argi))     a.x = static_cast<float>(lua_tonumber(L, argi));
    if (lua_isnumber(L, argi + 1)) a.y = static_cast<float>(lua_tonumber(L, argi + 1));

    tree->addPoint(id, a);
    return 0;
}

int lua_Region_ClearAllPoints(lua_State* L) {
    if (auto* tree = wowee::addons::getWidgetTree(L)) tree->clearPoints(widgetIdOf(L, 1));
    return 0;
}

int lua_Region_SetAllPoints(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;
    uint32_t target = 0;
    if (lua_istable(L, 2)) target = widgetIdOf(L, 2);
    else if (lua_isstring(L, 2)) {
        lua_getglobal(L, lua_tostring(L, 2));
        if (lua_istable(L, -1)) target = widgetIdOf(L, lua_gettop(L));
        lua_pop(L, 1);
    }
    // A frame cannot fill itself. FrameXML's own UIParent is declared
    // setAllPoints and its parent is named UIParent — but CreateFrame publishes
    // the new frame under that name first, so by the time this runs the name
    // means the frame itself. Two identical constraints collapse to no size at
    // the origin, and everything anchored to it lands there too.
    const auto* w = tree->get(id);
    if (target == 0 || target == id) target = w ? w->parent : 0;
    tree->setAllPoints(id, target);
    return 0;
}

int lua_Region_SetSize(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (tree && id != 0) {
        tree->setWidth(id, static_cast<float>(luaL_optnumber(L, 2, 0)));
        tree->setHeight(id, static_cast<float>(luaL_optnumber(L, 3, 0)));
    }
    return 0;
}
// Moving a frame, and picking things up out of one.
//
// All five of these were no-ops, which is why a bag window could not be
// dragged anywhere and an item could not be dragged out of it: the interface
// asked to be moved and nothing was listening.
/// SetRotation(radians) / SetFacing(radians) on a model frame.
///
/// The paperdoll's rotate buttons keep a running total and call this with it,
/// so it is an absolute facing. Unimplemented, the buttons ran their handler
/// and the figure never moved.
int lua_Model_SetFacing(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->modelFacing = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    }
    return 0;
}
int lua_Model_GetFacing(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->modelFacing : 0.0);
    return 1;
}

int lua_Frame_SetMovable(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->movable = lua_toboolean(L, 2) != 0;
    return 0;
}
int lua_Frame_IsMovable(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->movable);
    return 1;
}
/// RegisterForDrag("LeftButton", ...) — naming none of them turns dragging off,
/// which is how a frame stops being draggable again.
int lua_Frame_RegisterForDrag(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    w->dragLeft = false;
    w->dragRight = false;
    const int n = lua_gettop(L);
    for (int i = 2; i <= n; ++i) {
        const char* b = lua_tostring(L, i);
        if (!b) continue;
        const std::string name(b);
        if (name == "LeftButton")       w->dragLeft = true;
        else if (name == "RightButton") w->dragRight = true;
    }
    return 0;
}
int lua_Frame_StartMoving(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;
    const auto* w = tree->get(id);
    // WoW raises on a frame that is not movable. Ignoring it loses nothing and
    // keeps a mistake in one frame from taking down the file that asked.
    if (!w || !w->movable) return 0;
    tree->pinToCurrentPosition(id);
    tree->setMovingWidget(id);
    return 0;
}
int lua_Frame_StopMovingOrSizing(lua_State* L) {
    if (auto* tree = wowee::addons::getWidgetTree(L)) tree->setMovingWidget(0);
    return 0;
}

/// A region's name, taken from the tree rather than a Lua field so it is the
/// same name everything else knows it by.
///
/// Regions never had this: GetName fell to the no-op fallback and answered nil,
/// and FrameXML passes the answer straight into SetPoint —
/// bgTextureBottom:SetPoint("TOP", bgTextureMiddle:GetName(), "BOTTOM") anchored
/// a bag's bottom edge to nothing, which put it across the top of the bag.
int lua_FontString_SetText(lua_State* L);

/// SetFormattedText(fmt, ...) on a region.
///
/// It was defined on the frame metatable only, and a label is not a frame — so
/// every FontString in FrameXML still answered with the no-op and kept whatever
/// placeholder its XML carried. The character sheet went on reading "Level level
/// race class" for exactly that reason.
int lua_FontString_SetFormattedText(lua_State* L) {
    const int n = lua_gettop(L);
    if (n < 2 || !lua_isstring(L, 2)) return 0;

    // Through Lua's own string.format, so the format specifiers behave the way
    // the interface expects them to.
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    lua_remove(L, -2);
    for (int i = 2; i <= n; ++i) lua_pushvalue(L, i);
    if (lua_pcall(L, n - 1, 1, 0) != 0) {
        // A format string and its arguments disagreeing is an error in Lua, and
        // losing the file that asked for a label is worse than an unformatted
        // one.
        lua_pop(L, 1);
        lua_pushvalue(L, 2);
    }
    lua_replace(L, 2);
    lua_settop(L, 2);
    return lua_FontString_SetText(L);
}

int lua_Region_GetName(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    if (w && !w->name.empty()) lua_pushstring(L, w->name.c_str());
    else lua_pushnil(L);
    return 1;
}

int lua_Region_SetWidth(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (tree && id != 0) tree->setWidth(id, static_cast<float>(luaL_optnumber(L, 2, 0)));
    return 0;
}
int lua_Region_SetHeight(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (tree && id != 0) tree->setHeight(id, static_cast<float>(luaL_optnumber(L, 2, 0)));
    return 0;
}
/// Width of a string as it would be drawn.
///
/// Through the real font where there is one, so a button sized to its label
/// gets the size the label actually takes. During the FrameXML load there may
/// not be a frame in flight to ask, and an estimate is far better than nothing:
/// the alternative was answering nil, and MoneyFrame does
/// SetWidth(GetTextWidth() + iconWidth) — arithmetic that loses the file.
float measureTextWidth(const std::string& text, float fontHeight) {
    if (text.empty()) return 0.0f;
    const float size = fontHeight > 0.0f ? fontHeight : 12.0f;
    if (ImGui::GetCurrentContext() != nullptr) {
        if (ImFont* font = ImGui::GetFont()) {
            return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
        }
    }
    // Roughly half the height per character, which is about right for the
    // proportional faces the interface uses.
    return static_cast<float>(text.size()) * size * 0.5f;
}

/// The font string a widget measures: itself if it is one, and otherwise the
/// one a button was given, which is where its text actually lives.
const wowee::ui::Widget* textWidgetOf(lua_State* L, int index) {
    const wowee::ui::Widget* w = widgetOf(L, index);
    if (!w || w->kind == wowee::ui::WidgetKind::FontString) return w;
    auto* tree = wowee::addons::getWidgetTree(L);
    if (!tree) return w;
    lua_getfield(L, index, "__fontString");
    const wowee::ui::Widget* fs =
        lua_istable(L, -1) ? tree->get(widgetIdOf(L, lua_gettop(L))) : nullptr;
    lua_pop(L, 1);
    return fs ? fs : w;
}

int lua_Region_GetTextWidth(lua_State* L) {
    const auto* w = textWidgetOf(L, 1);
    lua_pushnumber(L, w ? measureTextWidth(w->text, w->fontHeight) : 0.0);
    return 1;
}

int lua_Region_GetTextHeight(lua_State* L) {
    const auto* w = textWidgetOf(L, 1);
    lua_pushnumber(L, w ? (w->fontHeight > 0.0f ? w->fontHeight : 12.0f) : 0.0);
    return 1;
}

int lua_Region_GetWidth(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    // In the frame's own units, which is what WoW reports: the laid-out rect
    // has the effective scale in it and handing that back would have a script
    // that reads a width and sets it again shrink the frame every time.
    const float es = (w && w->effScale > 0.0f) ? w->effScale : 1.0f;
    if (w && w->rectW <= 0.0f && w->width <= 0.0f &&
        w->kind == wowee::ui::WidgetKind::FontString && !w->text.empty()) {
        // A font string that was never given a width is as wide as its text.
        // That is what WoW answers, and the interface sizes things from it:
        // PanelTemplates_TabResize builds a tab's width out of
        // _G[name.."Text"]:GetWidth(), so answering zero made every tab on the
        // character sheet collapse to the width of its two end textures, with
        // the label clipped out of sight inside it.
        lua_pushnumber(L, measureTextWidth(w->text, w->fontHeight));
        return 1;
    }
    lua_pushnumber(L, w ? (w->rectW > 0.0f ? w->rectW / es : w->width) : 0.0);
    return 1;
}
int lua_Region_GetHeight(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    const float es = (w && w->effScale > 0.0f) ? w->effScale : 1.0f;
    lua_pushnumber(L, w ? (w->rectH > 0.0f ? w->rectH / es : w->height) : 0.0);
    return 1;
}
// The four edges, in the same coordinates the tree lays out in: origin at the
// bottom-left, y growing upward, interface units rather than pixels.
//
// These are read constantly and almost always into arithmetic — the chat frame
// works out where its dock sits, the container frames decide which side to
// open a tooltip on. A no-op behind them is not a getter that answers badly,
// it is nil in a subtraction, which takes the whole file down.
int lua_Region_GetLeft(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->left : 0.0);
    return 1;
}
int lua_Region_GetRight(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? (w->left + w->rectW) : 0.0);
    return 1;
}
int lua_Region_GetBottom(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->bottom : 0.0);
    return 1;
}
int lua_Region_GetTop(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? (w->bottom + w->rectH) : 0.0);
    return 1;
}
/// GetRect() → left, bottom, width, height. All four at once, which is what
/// the newer code in FrameXML reaches for.
/// IsMouseOver() — whether the cursor is inside this frame's rect.
///
/// Answered from the rect rather than from hover, because hover names the
/// mouse-enabled frame under the cursor and the callers ask about frames that
/// are not: mainmenubarmicrobuttons.xml asks it of a micro button to decide
/// whether to put its tooltip back, and floatingchatframe.lua asks it of the
/// dock. It was in the no-op allowlist, so every one of those was false.
int lua_Region_IsMouseOver(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    if (!w || !w->visible) { lua_pushboolean(L, 0); return 1; }
    const float mx = wowee::addons::LuaEngine::lastMouseX();
    const float my = wowee::addons::LuaEngine::lastMouseY();
    const bool inside = mx >= w->left && mx <= w->left + w->rectW &&
                        my >= w->bottom && my <= w->bottom + w->rectH;
    lua_pushboolean(L, inside ? 1 : 0);
    return 1;
}

int lua_Region_GetRect(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    if (!w) return 0;
    lua_pushnumber(L, w->left);
    lua_pushnumber(L, w->bottom);
    lua_pushnumber(L, w->rectW);
    lua_pushnumber(L, w->rectH);
    return 4;
}
/// Per-frame scale is not modelled — the tree scales the whole interface at
/// once, which is what UIParent's scale means and where the number FrameXML
/// wants comes from. One is therefore the true answer for every frame, and it
/// is a number, which is the part that matters where it is divided by.
int lua_Region_GetScale(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->scale : 1.0);
    return 1;
}
int lua_Region_SetScale(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        const float v = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        // A scale of zero collapses the frame and everything under it to
        // nothing, with no way back from Lua; WoW rejects it too.
        if (v > 0.0f) w->scale = v;
    }
    return 0;
}
int lua_Region_GetEffectiveScale(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->effScale : 1.0);
    return 1;
}
int lua_Frame_GetFrameLevel(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->effLevel : 0);
    return 1;
}
/// GetPoint(n) → point, relativeTo, relativePoint, x, y.
///
/// The stub this replaces answered "CENTER", nil, "CENTER", 0, 0 for every
/// frame, which is not a getter answering roughly — FrameXML reads a point and
/// puts it back to move something (a dragged chat window, a frame the panel
/// manager shifts aside), and a constant means every one of those teleports to
/// the middle of its parent.
///
/// relativeTo comes back as the frame itself, not its name, because that is
/// what SetPoint is handed straight back in.
int lua_Region_GetPoint(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    if (!w || w->anchors.empty()) return 0;
    // One-based, and no argument means the first — which is what FrameXML
    // relies on where a frame has only one.
    size_t index = static_cast<size_t>(luaL_optnumber(L, 2, 1));
    if (index < 1) index = 1;
    if (index > w->anchors.size()) return 0;
    const wowee::ui::Anchor& a = w->anchors[index - 1];

    lua_pushstring(L, a.point.c_str());
    // Zero means "my parent", which SetPoint also treats as the default, so it
    // comes back as nil rather than as a frame that was never named.
    if (a.relativeTo == 0) {
        lua_pushnil(L);
    } else {
        lua_getglobal(L, "__WoweeFramesByWid");
        if (lua_istable(L, -1)) {
            lua_pushinteger(L, static_cast<lua_Integer>(a.relativeTo));
            lua_rawget(L, -2);
            lua_remove(L, -2);          // drop the registry, keep the frame
            if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); }
        } else {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
    }
    lua_pushstring(L, a.relativePoint.c_str());
    lua_pushnumber(L, a.x);
    lua_pushnumber(L, a.y);
    return 5;
}

/// The types a widget answers to, most specific first.
///
/// WoW's IsObjectType is true for the type itself and everything it derives
/// from — a Button is a Frame is a Region — and FrameXML relies on that: it
/// asks whether something is a Region to decide it can be positioned at all.
static bool objectTypeMatches(const std::string& actual, const std::string& asked) {
    if (actual == asked) return true;
    const bool isRegion = (actual == "Texture" || actual == "FontString");
    if (isRegion) {
        return asked == "LayeredRegion" || asked == "Region";
    }
    // Everything else this creates is a Frame or derives from one.
    return asked == "Frame" || asked == "Region";
}

int lua_Region_GetObjectType(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushstring(L, w ? w->objectType.c_str() : "Frame");
    return 1;
}
int lua_Region_IsObjectType(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    const char* asked = luaL_optstring(L, 2, "");
    lua_pushboolean(L, w && objectTypeMatches(w->objectType, asked));
    return 1;
}

/// Runs a frame's own handler, given its table already on the stack.
///
/// Separate from callFrameScript, which starts from a widget id and looks the
/// table up: inside a binding the table is the first argument already, and
/// going back through the registry to find what is in hand is both slower and
/// wrong for a frame that was never registered.
static void callScriptOnTable(lua_State* L, int tableIdx, const char* script,
                              double arg) {
    // Positive indices only, which is all any caller here has: lua_absindex
    // is 5.2 and this is 5.1.
    const int abs = tableIdx;
    lua_getfield(L, abs, "__scripts");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, script);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
    lua_pushvalue(L, abs);
    lua_pushnumber(L, arg);
    if (lua_pcall(L, 2, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_ERROR("LuaEngine: ", script, " error: ", err ? err : "?");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

// Scroll frames. A window onto a taller child: the child is laid out at its
// full size and moved by the offset, and what falls outside the frame is
// clipped. Until now SetVerticalScroll was a no-op and every getter answered
// zero, so a scroll bar had nothing to report and nothing to move.
int lua_ScrollFrame_SetScrollChild(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;
    tree->markScrollFrame(id);
    if (auto* w = tree->get(id)) {
        w->scrollChild = lua_istable(L, 2) ? widgetIdOf(L, 2) : 0;
    }
    // Kept on the table too, because GetScrollChild hands the frame itself
    // back and that is what the caller passed in.
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, "__scrollChild");
    return 0;
}

/// How far the child can move before its far edge reaches the frame's. Zero
/// when the child fits, which is how a scroll bar knows to disable itself.
static float scrollRange(const wowee::ui::WidgetTree& tree, uint32_t id, bool vertical) {
    const auto* w = tree.get(id);
    if (!w || w->scrollChild == 0) return 0.0f;
    const auto* child = tree.get(w->scrollChild);
    if (!child) return 0.0f;
    const float over = vertical ? (child->rectH - w->rectH) : (child->rectW - w->rectW);
    return over > 0.0f ? over : 0.0f;
}

int lua_ScrollFrame_SetVerticalScroll(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;
    if (auto* w = tree->get(id)) {
        const float v = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        const float max = scrollRange(*tree, id, true);
        const float clamped = (v < 0.0f) ? 0.0f : (v > max ? max : v);
        const bool moved = (clamped != w->scrollY);
        w->scrollY = clamped;
        // OnVerticalScroll is how the scroll bar beside the frame learns where
        // the frame now is; UIPanelScrollFrameTemplate's body sets the bar's
        // value from it. Announced only on a change, because the interface
        // sets the scroll it already has on every update.
        if (moved) callScriptOnTable(L, 1, "OnVerticalScroll", clamped);
    }
    return 0;
}
int lua_ScrollFrame_SetHorizontalScroll(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;
    if (auto* w = tree->get(id)) {
        const float v = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        const float max = scrollRange(*tree, id, false);
        const float clamped = (v < 0.0f) ? 0.0f : (v > max ? max : v);
        const bool moved = (clamped != w->scrollX);
        w->scrollX = clamped;
        if (moved) callScriptOnTable(L, 1, "OnHorizontalScroll", clamped);
    }
    return 0;
}
int lua_ScrollFrame_GetVerticalScroll(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->scrollY : 0.0f);
    return 1;
}
int lua_ScrollFrame_GetHorizontalScroll(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->scrollX : 0.0f);
    return 1;
}
int lua_ScrollFrame_GetVerticalScrollRange(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    lua_pushnumber(L, (tree && id) ? scrollRange(*tree, id, true) : 0.0f);
    return 1;
}
int lua_ScrollFrame_GetHorizontalScrollRange(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    lua_pushnumber(L, (tree && id) ? scrollRange(*tree, id, false) : 0.0f);
    return 1;
}

/// SetChecked / GetChecked. A check button shows its checked art or none, and
/// the interface both sets this and reads it back to decide what a click meant.
int lua_CheckButton_SetChecked(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        // No argument means checked, as in WoW, where SetChecked() with
        // nothing is how a box is ticked.
        //
        // A number is read as a number, because WoW's widget API takes 0 and 1
        // here and seventy-seven places in this FrameXML write SetChecked(0).
        // lua_toboolean answers true for 0 — only nil and false are false in
        // Lua — so every one of those was setting the box rather than
        // clearing it, and nothing that reported its state by unchecking ever
        // turned off. BagSlotButton_UpdateChecked is one: it counts the open
        // container frames and passes 0 or 1 straight in, so every bag button
        // stayed lit whether or not its bag was open.
        w->checked = lua_isnone(L, 2)    ? true
                   : lua_isnumber(L, 2)  ? (lua_tonumber(L, 2) != 0)
                                         : (lua_toboolean(L, 2) != 0);
    }
    return 0;
}
int lua_CheckButton_GetChecked(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->checked);
    return 1;
}

/// SetButtonState(state, lock) / GetButtonState. The interface holding a
/// button down itself: ActionButton_UpdateState pushes the button for an
/// ability that is toggled on, and no amount of moving the cursor should let
/// it back up.
int lua_Button_SetButtonState(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    const std::string state = luaL_optstring(L, 2, "NORMAL");
    using F = wowee::ui::Widget::Forced;
    if      (state == "PUSHED")   w->forcedState = F::Pushed;
    else if (state == "DISABLED") w->forcedState = F::Disabled;
    else                          w->forcedState = F::Normal;
    return 0;
}
int lua_Button_GetButtonState(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    using F = wowee::ui::Widget::Forced;
    const F f = w ? w->forcedState : F::None;
    lua_pushstring(L, f == F::Pushed ? "PUSHED"
                    : f == F::Disabled ? "DISABLED" : "NORMAL");
    return 1;
}
int lua_Button_LockHighlight(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->highlightLocked = true;
    return 0;
}
int lua_Button_UnlockHighlight(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->highlightLocked = false;
    return 0;
}

int lua_Texture_SetButtonArt(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    const std::string slot = luaL_optstring(L, 2, "");
    using wowee::ui::ButtonArt;
    if      (slot == "NormalTexture")          w->buttonArt = ButtonArt::Normal;
    else if (slot == "PushedTexture")          w->buttonArt = ButtonArt::Pushed;
    else if (slot == "HighlightTexture")       w->buttonArt = ButtonArt::Highlight;
    else if (slot == "DisabledTexture")        w->buttonArt = ButtonArt::Disabled;
    else if (slot == "CheckedTexture")         w->buttonArt = ButtonArt::Checked;
    else if (slot == "DisabledCheckedTexture") w->buttonArt = ButtonArt::DisabledChecked;
    return 0;
}

int lua_Frame_SetWheelEnabled(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;
    if (auto* w = tree->get(id)) w->wheelEnabled = lua_toboolean(L, 2) != 0;
    return 0;
}

// ── Scrolling message frames ───────────────────────────────────────────────
//
// Chat. AddMessage was a name in the method list and nothing else, so every
// line the interface was handed went nowhere: the frame received the events,
// formatted the text, and dropped it.
int lua_MessageFrame_AddMessage(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    wowee::ui::Widget::Message m;
    m.text = luaL_optstring(L, 2, "");
    // WoW's colours are optional and default to the frame's own.
    m.color[0] = static_cast<float>(luaL_optnumber(L, 3, w->color[0]));
    m.color[1] = static_cast<float>(luaL_optnumber(L, 4, w->color[1]));
    m.color[2] = static_cast<float>(luaL_optnumber(L, 5, w->color[2]));
    m.color[3] = 1.0f;
    w->isMessageFrame = true;
    w->messages.push_back(std::move(m));
    while (w->messages.size() > w->maxMessages) w->messages.pop_front();
    // A new line at the bottom means the view follows it, which is what a
    // chat frame does unless someone has scrolled up.
    if (w->messageScroll > 0) ++w->messageScroll;
    return 0;
}
// ── Tooltips ───────────────────────────────────────────────────────────────
//
// AddLine was a name in the method list and nothing else, so every tooltip in
// the interface was empty: the frame was shown, positioned and sized, and had
// nothing in it.
int lua_Tooltip_AddLine(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    wowee::ui::Widget::TooltipLine line;
    line.left = luaL_optstring(L, 2, "");
    line.lc[0] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
    line.lc[1] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    line.lc[2] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    line.lc[3] = 1.0f;
    line.rc[0] = line.rc[1] = line.rc[2] = line.rc[3] = 1.0f;
    w->isTooltip = true;
    w->tooltipLines.push_back(std::move(line));
    return 0;
}
/// GameTooltip:SetFrameStack(showHidden) — what is under the cursor.
///
/// This is /framestack, and it is the answer to every "what is drawing that?"
/// that otherwise costs a screenshot, a guess, and a round trip. It was the
/// last thing Blizzard_DebugTools needed that this client did not answer, and
/// a missing method there is a hard error rather than an empty tooltip.
///
/// Two deliberate differences from the real client, both because this is a
/// diagnostic and being useful beats being faithful:
///
///   * textures and font strings are listed too, not only frames. A stray
///     *region* is exactly the kind of thing worth identifying, and the real
///     framestack cannot name one.
///   * each line carries the rect, so a widget in the wrong place or at the
///     wrong size says so without anything else being opened.
///
/// Ordered outermost first, so the last line is what the cursor is really on.
int lua_Tooltip_SetFrameStack(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* tree = wowee::addons::getWidgetTree(L);
    if (!w || !tree) return 0;
    const bool showHidden = lua_toboolean(L, 2) != 0;

    // The same conversion the input path makes: pixels to interface units,
    // and y flipped, because the widget tree grows upward and ImGui does not.
    const auto& io = ImGui::GetIO();
    const float s = tree->uiScale();
    const float px = io.MousePos.x;
    const float py = io.DisplaySize.y - io.MousePos.y;
    const float x = (s > 0.0f) ? px / s : px;
    const float y = (s > 0.0f) ? py / s : py;

    struct Entry { const wowee::ui::Widget* w; };
    std::vector<Entry> under;
    for (size_t id = 1; id < tree->size(); ++id) {
        const auto* c = tree->get(static_cast<uint32_t>(id));
        if (!c || c->id == 0) continue;
        if (!showHidden && !c->visible) continue;
        if (c->rectW <= 0.0f || c->rectH <= 0.0f) continue;
        if (x < c->left || x > c->left + c->rectW) continue;
        if (y < c->bottom || y > c->bottom + c->rectH) continue;
        under.push_back({c});
    }
    std::sort(under.begin(), under.end(), [](const Entry& a, const Entry& b) {
        if (a.w->effStrata != b.w->effStrata)
            return static_cast<int>(a.w->effStrata) < static_cast<int>(b.w->effStrata);
        return a.w->effLevel < b.w->effLevel;
    });

    w->isTooltip = true;
    w->tooltipLines.clear();
    auto line = [&](std::string text, float r, float g, float b) {
        wowee::ui::Widget::TooltipLine tl;
        tl.left = std::move(text);
        tl.lc[0] = r; tl.lc[1] = g; tl.lc[2] = b; tl.lc[3] = 1.0f;
        tl.rc[0] = tl.rc[1] = tl.rc[2] = tl.rc[3] = 1.0f;
        w->tooltipLines.push_back(std::move(tl));
    };
    auto num = [](float v) {
        std::string s = std::to_string(static_cast<int>(v + (v < 0 ? -0.5f : 0.5f)));
        return s;
    };

    line("Frame Stack  (" + num(x) + ", " + num(y) + ")", 1.0f, 0.82f, 0.0f);
    if (under.empty()) {
        line("nothing under the cursor", 0.6f, 0.6f, 0.6f);
        return 0;
    }
    for (const Entry& e : under) {
        const char* kind = e.w->kind == wowee::ui::WidgetKind::Texture    ? "texture"
                         : e.w->kind == wowee::ui::WidgetKind::FontString ? "label"
                                                                         : "frame";
        std::string text = (e.w->name.empty() ? std::string("(unnamed)") : e.w->name);
        text += "  " + std::string(kind);
        text += "  " + num(e.w->left) + "," + num(e.w->bottom);
        text += " " + num(e.w->rectW) + "x" + num(e.w->rectH);
        if (!e.w->visible) text += "  HIDDEN";
        // A region is dimmer than a frame, so the frames read as the structure
        // and the art hanging off them as detail.
        if (e.w->kind == wowee::ui::WidgetKind::Frame) line(text, 1.0f, 1.0f, 1.0f);
        else                                           line(text, 0.6f, 0.8f, 1.0f);
        if (e.w->kind == wowee::ui::WidgetKind::Texture && !e.w->texturePath.empty()) {
            line("    " + e.w->texturePath, 0.5f, 0.5f, 0.5f);
        }
    }
    return 0;
}

int lua_Tooltip_AddDoubleLine(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    wowee::ui::Widget::TooltipLine line;
    line.left  = luaL_optstring(L, 2, "");
    line.right = luaL_optstring(L, 3, "");
    line.lc[0] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    line.lc[1] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    line.lc[2] = static_cast<float>(luaL_optnumber(L, 6, 1.0));
    line.lc[3] = 1.0f;
    line.rc[0] = static_cast<float>(luaL_optnumber(L, 7, 1.0));
    line.rc[1] = static_cast<float>(luaL_optnumber(L, 8, 1.0));
    line.rc[2] = static_cast<float>(luaL_optnumber(L, 9, 1.0));
    line.rc[3] = 1.0f;
    w->isTooltip = true;
    w->tooltipLines.push_back(std::move(line));
    return 0;
}
/// SetOwner(frame, anchor) — where the tooltip goes, relative to what it is
/// describing. Every tooltip in the interface calls this before filling
/// itself, and it did nothing, so a tooltip with lines in it would still have
/// appeared wherever its XML left it rather than beside the button.
///
/// WoW's anchor names say which side of the owner the tooltip sits on; the
/// pair of points that produces is the whole of the mapping.
int lua_Tooltip_SetOwner(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (!tree || id == 0) return 0;
    auto* w = tree->get(id);
    if (!w) return 0;
    w->isTooltip = true;
    // Cleared here, because SetOwner is what precedes a fresh set of lines.
    w->tooltipLines.clear();

    // Remembered, because IsOwned reads it and nothing wrote it — so every
    // check answered false and a tooltip outlived the frame it belonged to.
    // FrameXML hides a tooltip on OnLeave only when it owns it, which is what
    // stops one panel's tooltip being cleared by another's cursor.
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, "__owner");

    const uint32_t owner = lua_istable(L, 2) ? widgetIdOf(L, 2) : 0;
    const std::string anchor = luaL_optstring(L, 3, "ANCHOR_RIGHT");
    // ANCHOR_PRESERVE is the one that means what it says: keep the anchors.
    if (anchor == "ANCHOR_PRESERVE") return 0;
    if (owner == 0 || anchor == "ANCHOR_NONE") {
        // ANCHOR_NONE means the caller places the tooltip itself, and the call
        // that follows is a SetPoint — which is how GameTooltip_SetDefaultAnchor
        // pins an action button's tooltip to the bottom-right of the screen.
        // Returning without clearing left the anchor from the previous owner in
        // place, and SetPoint only replaces an anchor on the same point: a LEFT
        // anchor to the last button and a BOTTOMRIGHT anchor to UIParent both
        // applied, pulling the tooltip down onto the action bar it came from.
        tree->clearPoints(id);
        return 0;
    }

    struct Pair { const char* name; const char* point; const char* rel; };
    static const Pair kAnchors[] = {
        {"ANCHOR_TOPLEFT",     "BOTTOMLEFT",  "TOPLEFT"},
        {"ANCHOR_TOPRIGHT",    "BOTTOMRIGHT", "TOPRIGHT"},
        {"ANCHOR_BOTTOMLEFT",  "TOPLEFT",     "BOTTOMLEFT"},
        {"ANCHOR_BOTTOMRIGHT", "TOPRIGHT",    "BOTTOMRIGHT"},
        {"ANCHOR_LEFT",        "RIGHT",       "LEFT"},
        {"ANCHOR_RIGHT",       "LEFT",        "RIGHT"},
        {"ANCHOR_TOP",         "BOTTOM",      "TOP"},
        {"ANCHOR_BOTTOM",      "TOP",         "BOTTOM"},
        // The cursor is not a frame, so this lands beside the owner instead —
        // which is where the cursor is, near enough, and better than nowhere.
        {"ANCHOR_CURSOR",      "TOPLEFT",     "BOTTOMRIGHT"},
    };
    const char* point = "LEFT";
    const char* rel = "RIGHT";
    for (const Pair& p : kAnchors) {
        if (anchor == p.name) { point = p.point; rel = p.rel; break; }
    }

    wowee::ui::Anchor a;
    a.point = point;
    a.relativeTo = owner;
    a.relativePoint = rel;
    tree->clearPoints(id);
    tree->addPoint(id, a);
    return 0;
}

/// The one-line form. GameTooltip:SetText replaces what the tooltip says
/// rather than setting a font string, and returns whether it did — so the
/// shared SetText can hand off and stop.
int lua_Tooltip_SetText(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w || !w->isTooltip) { lua_pushboolean(L, 0); return 1; }
    w->tooltipLines.clear();
    wowee::ui::Widget::TooltipLine line;
    line.left = luaL_optstring(L, 2, "");
    line.lc[0] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
    line.lc[1] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    line.lc[2] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    line.lc[3] = 1.0f;
    line.rc[0] = line.rc[1] = line.rc[2] = line.rc[3] = 1.0f;
    w->tooltipLines.push_back(std::move(line));
    w->shown = true;
    lua_pushboolean(L, 1);
    return 1;
}

/// Fills a tooltip from an action bar slot: the spell's or item's name and
/// what it does. ActionButton_SetTooltip asks for this and checks the answer —
/// `if (GameTooltip:SetAction(self.action))` — so returning nothing meant
/// every action button fell to its "no tooltip" branch.
static bool fillItemTooltipById(wowee::ui::Widget* w, game::GameHandler* gh,
                                uint32_t itemId);

/// One spell tooltip, for every path that shows one.
///
/// The action bar and the spellbook each built their own and both stopped at
/// the name and the description — so a spell never said what it costs, how far
/// it reaches or how long it takes to cast, all of which the client already
/// resolves for its own use. Two copies also meant either could be improved
/// alone and quietly drift from the other, which is exactly what happened to
/// the item tooltips.
///
/// Laid out as WoW lays it out: cost on the left of its line with the range on
/// the right, then the cast time, then the description.
static bool fillSpellTooltip(wowee::ui::Widget* w, game::GameHandler* gh,
                             uint32_t spellId) {
    if (!w || !gh || spellId == 0) return false;
    const std::string& name = gh->getSpellName(spellId);
    if (name.empty()) return false;

    auto line = [&w](std::string l, std::string r, float lr, float lg, float lb) {
        wowee::ui::Widget::TooltipLine t;
        t.left = std::move(l);
        t.right = std::move(r);
        t.lc[0] = lr; t.lc[1] = lg; t.lc[2] = lb; t.lc[3] = 1.0f;
        t.rc[0] = t.rc[1] = t.rc[2] = 1.0f; t.rc[3] = 1.0f;
        w->tooltipLines.push_back(std::move(t));
    };

    w->isTooltip = true;
    w->tooltipLines.clear();
    line(name, "", 1.0f, 0.82f, 0.0f);   // gold, as WoW titles a tooltip

    const auto info = gh->getSpellData(spellId);
    std::string cost, range;
    if (info.manaCost > 0) {
        static const char* kPower[] = {"Mana", "Rage", "Focus", "Energy",
                                       "Happiness", "", "Runic Power"};
        const char* unit = info.powerType < 7 ? kPower[info.powerType] : "Mana";
        cost = std::to_string(info.manaCost) + (*unit ? std::string(" ") + unit : "");
    }
    if (info.maxRange > 0.0f) {
        range = std::to_string(static_cast<int>(info.maxRange)) + " yd range";
    }
    if (!cost.empty() || !range.empty()) line(cost, range, 1.0f, 1.0f, 1.0f);

    // Instant is a word rather than a zero, which is what WoW prints.
    const std::string cast = info.castTimeMs > 0
        ? std::to_string(info.castTimeMs / 1000.0f).substr(0, 4) + " sec cast"
        : std::string("Instant");
    line(cast, "", 1.0f, 1.0f, 1.0f);

    const std::string body =
        gh->formatSpellDescription(spellId, gh->getSpellDescription(spellId));
    if (!body.empty()) line(body, "", 1.0f, 1.0f, 1.0f);

    w->shown = true;
    return true;
}

int lua_Tooltip_SetAction(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const int slot = static_cast<int>(luaL_optnumber(L, 2, 0)) - 1;
    if (!w || !gh || slot < 0) { lua_pushboolean(L, 0); return 1; }
    const auto& bar = gh->getActionBar();
    if (slot >= static_cast<int>(bar.size()) || bar[slot].isEmpty()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const auto& action = bar[slot];
    // An item on the bar gets the tooltip the bags give it, stats and all.
    // Naming it and stopping meant the same potion described itself two
    // different ways depending on where it was hovered.
    if (action.type == game::ActionBarSlot::ITEM) {
        lua_pushboolean(L, fillItemTooltipById(w, gh, action.id) ? 1 : 0);
        return 1;
    }
    if (action.type == game::ActionBarSlot::SPELL) {
        lua_pushboolean(L, fillSpellTooltip(w, gh, action.id) ? 1 : 0);
        return 1;
    }
    // A macro is the third kind a slot can hold and it has no tooltip of its
    // own — WoW shows the macro's name from the button rather than from here.
    lua_pushboolean(L, 0);
    return 1;
}

/// The same for a spell asked for by id, which is how the spellbook and the
/// stance bar fill theirs.
int lua_Tooltip_SetSpellByID(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const uint32_t id = static_cast<uint32_t>(luaL_optnumber(L, 2, 0));
    lua_pushboolean(L, fillSpellTooltip(w, gh, id) ? 1 : 0);
    return 1;
}

/// A talent's name, the rank the player has in it, and what it does.
///
/// Hovering a talent used to raise: the metatable had no SetTalent, so the call
/// landed on nil and took the handler with it. Listing it as a no-op stopped
/// that and left the tooltip empty; this fills it, which is the whole reason
/// anyone hovers a talent.
///
/// The description shown is the rank the player actually has. An unlearned
/// talent describes its first rank, which is what taking a point in it would
/// buy — the useful thing to read when deciding.
int lua_Tooltip_SetTalent(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const int tabIndex    = static_cast<int>(luaL_optnumber(L, 2, 0));
    const int talentIndex = static_cast<int>(luaL_optnumber(L, 3, 0));
    if (!w || !gh) { lua_pushboolean(L, 0); return 1; }

    const auto* talent = wowee::addons::talentAt(gh, tabIndex, talentIndex);
    if (!talent) { lua_pushboolean(L, 0); return 1; }

    const uint8_t rank = gh->getTalentRank(talent->talentId);
    // Rank 1's spell names the talent whatever the player has in it, and is the
    // only entry guaranteed to be filled.
    const uint32_t titleSpell = talent->rankSpells[0];
    // The rank being described: what the player has, or the first if none.
    const uint32_t bodySpell = talent->rankSpells[rank > 0 ? rank - 1 : 0];
    if (titleSpell == 0) { lua_pushboolean(L, 0); return 1; }

    const std::string& name = gh->getSpellName(titleSpell);
    if (name.empty()) { lua_pushboolean(L, 0); return 1; }

    w->isTooltip = true;
    w->tooltipLines.clear();

    wowee::ui::Widget::TooltipLine title;
    title.left = name;
    title.lc[0] = 1.0f; title.lc[1] = 0.82f; title.lc[2] = 0.0f; title.lc[3] = 1.0f;
    title.rc[0] = title.rc[1] = title.rc[2] = title.rc[3] = 1.0f;
    w->tooltipLines.push_back(std::move(title));

    if (talent->maxRank > 0) {
        wowee::ui::Widget::TooltipLine rankLine;
        rankLine.left = "Rank " + std::to_string(rank) + "/" +
                        std::to_string(static_cast<int>(talent->maxRank));
        rankLine.lc[0] = rankLine.lc[1] = rankLine.lc[2] = 1.0f; rankLine.lc[3] = 1.0f;
        rankLine.rc[0] = rankLine.rc[1] = rankLine.rc[2] = rankLine.rc[3] = 1.0f;
        w->tooltipLines.push_back(std::move(rankLine));
    }

    // Through the formatter for the same reason SetSpellByID is: a description
    // arrives as a template of $-tokens and reads as one if handed over raw.
    const std::string body =
        gh->formatSpellDescription(bodySpell, gh->getSpellDescription(bodySpell));
    if (!body.empty()) {
        wowee::ui::Widget::TooltipLine desc;
        desc.left = body;
        desc.lc[0] = desc.lc[1] = desc.lc[2] = 1.0f; desc.lc[3] = 1.0f;
        desc.rc[0] = desc.rc[1] = desc.rc[2] = desc.rc[3] = 1.0f;
        w->tooltipLines.push_back(std::move(desc));
    }

    w->shown = true;
    lua_pushboolean(L, 1);
    return 1;
}

/// SetTradeSkillItem(index) — what a recipe in the open profession makes.
///
/// This client knows a recipe by its crafting spell rather than by the item it
/// produces, so what is shown is that spell: its name and its description,
/// which is the line that says what gets made. Not the crafted item's own
/// tooltip, which is what the real client shows — but the useful half of it,
/// and it is what this client actually knows.
int lua_Tooltip_SetTradeSkillItem(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
    if (!w || !gh || index < 1) { lua_pushboolean(L, 0); return 1; }

    const auto recipes = gh->getCraftingRecipes();
    if (index > static_cast<int>(recipes.size())) { lua_pushboolean(L, 0); return 1; }
    const uint32_t spellId = recipes[index - 1].spellId;
    if (spellId == 0) { lua_pushboolean(L, 0); return 1; }

    w->isTooltip = true;
    w->tooltipLines.clear();
    wowee::ui::Widget::TooltipLine title;
    title.left = recipes[index - 1].name;
    title.lc[0] = 1.0f; title.lc[1] = 0.82f; title.lc[2] = 0.0f; title.lc[3] = 1.0f;
    title.rc[0] = title.rc[1] = title.rc[2] = title.rc[3] = 1.0f;
    w->tooltipLines.push_back(std::move(title));

    const std::string body =
        gh->formatSpellDescription(spellId, gh->getSpellDescription(spellId));
    if (!body.empty()) {
        wowee::ui::Widget::TooltipLine desc;
        desc.left = body;
        desc.lc[0] = desc.lc[1] = desc.lc[2] = 1.0f; desc.lc[3] = 1.0f;
        desc.rc[0] = desc.rc[1] = desc.rc[2] = desc.rc[3] = 1.0f;
        w->tooltipLines.push_back(std::move(desc));
    }
    w->shown = true;
    lua_pushboolean(L, 1);
    return 1;
}

/// A unit's name and level, which is what hovering a unit frame shows.
int lua_Tooltip_SetUnit(lua_State* L) {
    auto* w = widgetOf(L, 1);
    // A model frame has a SetUnit of its own — it is how the paperdoll loads
    // the player — and that name collides with the tooltip's. Answering as the
    // tooltip made CharacterModelFrame a tooltip carrying the player's name,
    // which then drew across the rotate arrows and sized the frame to fit one
    // line of text instead of the figure.
    if (w && w->objectType != "GameTooltip") {
        lua_pushboolean(L, 0);
        return 1;
    }
    auto* gh = wowee::addons::getGameHandler(L);
    const char* uid = luaL_optstring(L, 2, "player");
    if (!w || !gh) { lua_pushboolean(L, 0); return 1; }
    std::string uidStr(uid);
    for (char& c : uidStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const uint64_t guid = wowee::addons::resolveUnitGuid(gh, uidStr);
    if (guid == 0) { lua_pushboolean(L, 0); return 1; }
    const std::string name = gh->lookupName(guid);
    if (name.empty()) { lua_pushboolean(L, 0); return 1; }

    w->isTooltip = true;
    w->tooltipLines.clear();
    wowee::ui::Widget::TooltipLine title;
    title.left = name;
    title.lc[0] = title.lc[1] = title.lc[2] = title.lc[3] = 1.0f;
    title.rc[0] = title.rc[1] = title.rc[2] = title.rc[3] = 1.0f;
    w->tooltipLines.push_back(std::move(title));
    w->shown = true;
    lua_pushboolean(L, 1);
    return 1;
}

/// Shared by every setter that ends up naming an item: title in the quality
/// colour, then whatever else is worth saying. One place, because the four
/// item setters differ only in how they find the item.
static void appendItemStats(wowee::ui::Widget* w, const game::ItemQueryResponseData& info);

/// gh is what turns the name into a tooltip: the ItemDef in a bag slot carries
/// the name and the quality, and everything a player actually reads a tooltip
/// for lives in the item cache behind it.
static void fillItemTooltip(wowee::ui::Widget* w, const game::ItemDef& item,
                            game::GameHandler* gh) {
    w->isTooltip = true;
    // A setter that finds something to say also shows the tooltip. That is
    // WoW's behaviour and FrameXML leans on it: ContainerFrameItemButton_OnEnter
    // sets an owner, calls SetBagItem and stops — there is no Show anywhere in
    // it, so a tooltip that only filled itself in stayed hidden and hovering a
    // bag said nothing.
    w->shown = true;
    w->tooltipLines.clear();
    wowee::ui::Widget::TooltipLine title;
    title.left = item.name;
    // WoW's quality colours, which are most of what an item tooltip says at a
    // glance — an epic reads as purple before anyone reads the words.
    // The client's own table rather than a fourth copy. This used to carry its
    // own, and the copies had already drifted: it painted an heirloom cyan
    // where ui_colors paints it the same gold as an artifact. Cyan is a later
    // expansion's token colour; a 3.3.5 heirloom is e6cc80.
    const ImVec4 qc = wowee::ui::getQualityColor(item.quality);
    title.lc[0] = qc.x; title.lc[1] = qc.y; title.lc[2] = qc.z; title.lc[3] = 1.0f;
    title.rc[0] = title.rc[1] = title.rc[2] = title.rc[3] = 1.0f;
    w->tooltipLines.push_back(std::move(title));

    if (!item.subclassName.empty()) {
        wowee::ui::Widget::TooltipLine sub;
        sub.left = item.subclassName;
        sub.lc[0] = sub.lc[1] = sub.lc[2] = 1.0f; sub.lc[3] = 1.0f;
        sub.rc[0] = sub.rc[1] = sub.rc[2] = sub.rc[3] = 1.0f;
        w->tooltipLines.push_back(std::move(sub));
    }

    if (gh && item.itemId != 0) {
        if (const auto* info = gh->getItemInfo(item.itemId); info && info->valid) {
            appendItemStats(w, *info);
        }
    }
}

/// What the item actually does, appended to the two lines above.
///
/// The name and its quality colour were the whole of the tooltip, which reads
/// as a tooltip that works right up until someone wants to know whether the
/// sword is better than the one they are holding. This client's own bag window
/// has always shown the rest; the bags are handed over, so this is the only
/// tooltip left and it has to say the same things.
///
/// Ordered as WoW orders it: binding, then what it is and where it goes, then
/// the numbers, then the requirements, then the flavour text last.
static void appendItemStats(wowee::ui::Widget* w, const game::ItemQueryResponseData& info) {
    auto line = [&w](std::string text, float r, float g, float b) {
        wowee::ui::Widget::TooltipLine l;
        l.left = std::move(text);
        l.lc[0] = r; l.lc[1] = g; l.lc[2] = b; l.lc[3] = 1.0f;
        l.rc[0] = l.rc[1] = l.rc[2] = l.rc[3] = 1.0f;
        w->tooltipLines.push_back(std::move(l));
    };
    constexpr float kW = 1.0f, kGrey = 0.62f, kGold = 1.0f;
    auto white = [&](std::string s) { line(std::move(s), kW, kW, kW); };
    auto grey  = [&](std::string s) { line(std::move(s), kGrey, kGrey, kGrey); };
    auto gold  = [&](std::string s) { line(std::move(s), kGold, 0.82f, 0.0f); };
    auto green = [&](std::string s) { line(std::move(s), 0.0f, 1.0f, 0.0f); };

    switch (info.bindType) {
        case 1: white("Binds when picked up"); break;
        case 2: white("Binds when equipped"); break;
        case 3: white("Binds when used"); break;
        default: break;
    }
    if (info.maxCount == 1)                     gold("Unique");
    else if (info.itemFlags & 0x1000000u)       gold("Unique-Equipped");

    if (info.containerSlots > 0) {
        white(std::to_string(info.containerSlots) + " Slot Container");
    }

    // A weapon says its damage, its speed and the two multiplied out, because
    // damage alone compares two weapons wrongly whenever their speeds differ.
    if (info.damageMax > 0.0f) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.0f - %.0f Damage",
                      static_cast<double>(info.damageMin), static_cast<double>(info.damageMax));
        white(buf);
        if (info.delayMs > 0) {
            const double speed = info.delayMs / 1000.0;
            std::snprintf(buf, sizeof(buf), "Speed %.2f", speed);
            white(buf);
            const double dps = (info.damageMin + info.damageMax) / 2.0 / speed;
            std::snprintf(buf, sizeof(buf), "(%.1f damage per second)", dps);
            grey(buf);
        }
    }
    if (info.armor > 0) white(std::to_string(info.armor) + " Armor");

    const std::pair<int32_t, const char*> kBase[] = {
        {info.strength,  "Strength"},  {info.agility, "Agility"},
        {info.stamina,   "Stamina"},   {info.intellect, "Intellect"},
        {info.spirit,    "Spirit"},
    };
    for (const auto& [v, name] : kBase) {
        if (v != 0) white((v > 0 ? "+" : "") + std::to_string(v) + " " + name);
    }
    const std::pair<int32_t, const char*> kRes[] = {
        {info.holyRes, "Holy"},   {info.fireRes,   "Fire"},  {info.natureRes, "Nature"},
        {info.frostRes, "Frost"}, {info.shadowRes, "Shadow"}, {info.arcaneRes, "Arcane"},
    };
    for (const auto& [v, name] : kRes) {
        if (v != 0) white("+" + std::to_string(v) + " " + name + " Resistance");
    }
    for (const auto& es : info.extraStats) {
        if (es.statValue == 0) continue;
        if (const char* n = game::itemStatName(es.statType)) {
            green(std::string(es.statValue > 0 ? "+" : "") + std::to_string(es.statValue) +
                  " " + n);
        }
    }

    if (info.requiredLevel > 0) white("Requires Level " + std::to_string(info.requiredLevel));
    if (!info.description.empty()) gold("\"" + info.description + "\"");
}

/// The same tooltip for an item known only by its id.
///
/// Bags and the paperdoll hold a whole ItemDef; an auction row holds an entry
/// number and nothing else. Rather than a second tooltip builder for that case,
/// this fills in what the item cache knows and hands it to the one above — so
/// there is one quality colour table and one idea of what an item tooltip looks
/// like. False when the cache has not heard of the item yet, which is the same
/// answer an empty bag slot gives.
static bool fillItemTooltipById(wowee::ui::Widget* w, game::GameHandler* gh,
                                uint32_t itemId) {
    if (!w || !gh || itemId == 0) return false;
    const auto* info = gh->getItemInfo(itemId);
    if (!info || info->name.empty()) return false;
    game::ItemDef def;
    def.itemId  = itemId;
    def.name    = info->name;
    def.quality = static_cast<game::ItemQuality>(info->quality);
    def.subclassName = info->subclassName;
    fillItemTooltip(w, def, gh);
    return true;
}

/// SetAuctionItem(list, index) — the item on an auction row.
///
/// The three lists are the browse results, the player's own auctions and the
/// ones they have bid on, named as GetAuctionItemInfo names them.
int lua_Tooltip_SetAuctionItem(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const char* list = luaL_optstring(L, 2, "list");
    const int index = static_cast<int>(luaL_optnumber(L, 3, 0));
    if (!w || !gh || index < 1) { lua_pushboolean(L, 0); return 1; }

    const std::string which(list ? list : "list");
    const auto& results = (which == "owner")  ? gh->getAuctionOwnerResults()
                        : (which == "bidder") ? gh->getAuctionBidderResults()
                                              : gh->getAuctionBrowseResults();
    if (index > static_cast<int>(results.auctions.size())) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool filled =
        fillItemTooltipById(w, gh, results.auctions[index - 1].itemEntry);
    lua_pushboolean(L, filled ? 1 : 0);
    return 1;
}

/// SetInventoryItem(unit, slot) — the gear on the paperdoll.
int lua_Tooltip_SetInventoryItem(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const int slot = static_cast<int>(luaL_optnumber(L, 3, 0));
    if (!w || !gh || slot < 1 || slot > 19) { lua_pushboolean(L, 0); return 1; }
    const auto& s = gh->getInventory().getEquipSlot(static_cast<game::EquipSlot>(slot - 1));
    if (s.empty()) { lua_pushboolean(L, 0); return 1; }
    fillItemTooltip(w, s.item, gh);
    lua_pushboolean(L, 1);
    return 1;
}

/// SetBagItem(bag, slot) — the same for something in the bags.
int lua_Tooltip_SetBagItem(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const int bag = static_cast<int>(luaL_optnumber(L, 2, 0));
    const int slot = static_cast<int>(luaL_optnumber(L, 3, 0));
    if (!w || !gh || slot < 1) { lua_pushboolean(L, 0); return 1; }
    const auto& inv = gh->getInventory();
    const auto& s = (bag == 0) ? inv.getBackpackSlot(slot - 1)
                               : inv.getBagSlot(bag - 1, slot - 1);
    if (s.empty()) { lua_pushboolean(L, 0); return 1; }
    fillItemTooltip(w, s.item, gh);
    lua_pushboolean(L, 1);
    return 1;
}

/// SetGuildBankItem(tab, slot) — hovering a slot in the guild bank.
///
/// It was in the no-op allowlist, so the call succeeded and wrote nothing: a
/// guild bank where no item has a tooltip, which reads as the tooltip system
/// being broken rather than as one method missing. The data was already there
/// — GetGuildBankItemInfo answers from the same slots.
int lua_Tooltip_SetGuildBankItem(lua_State* L) {
    auto* w = widgetOf(L, 1);
    auto* gh = wowee::addons::getGameHandler(L);
    const int tab  = static_cast<int>(luaL_optnumber(L, 2, 0));
    const int slot = static_cast<int>(luaL_optnumber(L, 3, 0));
    if (!w || !gh || slot < 1) { lua_pushboolean(L, 0); return 1; }

    const auto& data = gh->getGuildBankData();
    // The open tab is the one kept current; any other answers from whatever
    // the last full update left behind, which is what the panel draws too.
    const std::vector<game::GuildBankItemSlot>* items = nullptr;
    if (tab - 1 == data.tabId) {
        items = &data.tabItems;
    } else if (tab >= 1 && tab <= static_cast<int>(data.tabs.size())) {
        items = &data.tabs[tab - 1].items;
    }
    if (!items) { lua_pushboolean(L, 0); return 1; }

    for (const auto& it : *items) {
        if (it.slotId + 1 != slot) continue;
        lua_pushboolean(L, fillItemTooltipById(w, gh, it.itemEntry) ? 1 : 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int lua_Tooltip_ClearLines(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->tooltipLines.clear();
    return 0;
}
int lua_Tooltip_NumLines(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? static_cast<lua_Number>(w->tooltipLines.size()) : 0.0);
    return 1;
}

int lua_MessageFrame_Clear(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) { w->messages.clear(); w->messageScroll = 0; }
    return 0;
}
int lua_MessageFrame_GetNumMessages(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? static_cast<lua_Number>(w->messages.size()) : 0.0);
    return 1;
}
int lua_MessageFrame_SetMaxLines(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        const int n = static_cast<int>(luaL_optnumber(L, 2, 128));
        w->maxMessages = (n > 0) ? static_cast<size_t>(n) : 1;
    }
    return 0;
}
int lua_MessageFrame_ScrollUp(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        if (static_cast<size_t>(w->messageScroll) + 1 < w->messages.size())
            ++w->messageScroll;
    }
    return 0;
}
int lua_MessageFrame_ScrollDown(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        if (w->messageScroll > 0) --w->messageScroll;
    }
    return 0;
}
int lua_MessageFrame_ScrollToBottom(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->messageScroll = 0;
    return 0;
}

int lua_Region_GetNumPoints(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? static_cast<lua_Number>(w->anchors.size()) : 0.0);
    return 1;
}

// Minimap zoom. Five levels, as in WoW, and the level is kept on the widget so
// the buttons that step it can read back what they set — Minimap_Update
// compares GetZoom() against GetZoomLevels() - 1 to decide whether to grey the
// zoom-in button out, and nil there is arithmetic on nothing.
int lua_Minimap_SetZoom(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        int z = static_cast<int>(luaL_optnumber(L, 2, 0));
        w->zoomLevel = (z < 0) ? 0 : (z > 4 ? 4 : z);
    }
    return 0;
}
int lua_Minimap_GetZoom(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->zoomLevel : 0);
    return 1;
}
int lua_Minimap_GetZoomLevels(lua_State* L) {
    (void)L;
    lua_pushnumber(L, 5);
    return 1;
}

/// Enable and Disable, with the handlers that go with them.
///
/// A disabled button is greyed and takes no clicks, and FrameXML both sets
/// this and listens for it — a scroll bar's arrows disable themselves at the
/// end of their range. Fired only on a real change, since the interface
/// disables what is already disabled on every update.
int lua_Button_Enable(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w || w->enabled) return 0;
    w->enabled = true;
    lua_getfield(L, 1, "__scripts");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "OnEnable");
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, 1);
            if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return 0;
}
int lua_Button_Disable(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w || !w->enabled) return 0;
    w->enabled = false;
    lua_getfield(L, 1, "__scripts");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "OnDisable");
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, 1);
            if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return 0;
}
int lua_Button_IsEnabled(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    // A number, not a boolean, because that is what WoW answers and FrameXML
    // is written against it: twenty-two places test `IsEnabled() ~= 0` and six
    // test it for truth. A boolean fails the twenty-two silently — `false ~= 0`
    // compares two different types and is therefore *true* — so a disabled
    // button read as enabled everywhere it was asked properly. Pressing return
    // in the macro name box confirmed through a greyed-out OK button that way.
    //
    // The six truth tests then see 0 as true, which is a real flaw, but it is
    // retail's flaw: FrameXML was authored against a client that answers a
    // number here, and matching it is what keeps the other twenty-two right.
    lua_pushnumber(L, w && w->enabled ? 1 : 0);
    return 1;
}

int lua_Region_Show(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->shown = true;
    lua_pushboolean(L, 1); lua_setfield(L, 1, "__visible");
    return 0;
}
int lua_Region_Hide(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->shown = false;
    lua_pushboolean(L, 0); lua_setfield(L, 1, "__visible");
    return 0;
}
int lua_Region_IsShown(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w ? (w->shown ? 1 : 0) : 0);
    return 1;
}

/// IsVisible() — shown, and every ancestor shown too.
///
/// It was an alias for IsShown, which answers only this frame's own flag, so a
/// frame inside a closed panel reported itself visible. Fifty-six places in
/// FrameXML ask this rather than IsShown, and they ask it precisely because
/// they mean "on screen": SpellBookFrame_OnEvent rebuilds the page on
/// SPELLS_CHANGED only when visible, and a dozen others skip work the same way.
///
/// Walks the parents rather than reading the tree's own computed flag, which
/// is only right after a layout pass — a frame shown and asked in the same
/// handler would otherwise answer no.
int lua_Region_IsVisible(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    auto* tree = wowee::addons::getWidgetTree(L);
    bool on = w != nullptr && w->shown;
    if (on && tree) {
        for (uint32_t id = w->parent; id != 0;) {
            const auto* p = tree->get(id);
            if (!p) break;
            if (!p->shown) { on = false; break; }
            id = p->parent;
        }
    }
    lua_pushboolean(L, on);
    return 1;
}
int lua_Region_SetAlpha(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->alpha = static_cast<float>(luaL_optnumber(L, 2, 1.0));
    return 0;
}
int lua_Region_GetAlpha(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->alpha : 1.0);
    return 1;
}

// SetTexture takes either a path or a colour, and addons use both freely.
int lua_Texture_SetTexture(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    if (lua_isnumber(L, 2)) {
        w->solidColor = true;
        w->texturePath.clear();
        w->color[0] = static_cast<float>(lua_tonumber(L, 2));
        w->color[1] = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        w->color[2] = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        w->color[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    } else {
        w->solidColor = false;
        w->texturePath = luaL_optstring(L, 2, "");
    }
    return 0;
}
int lua_Texture_GetTexture(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushstring(L, w ? w->texturePath.c_str() : "");
    return 1;
}
int lua_Texture_SetTexCoord(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        // Eight numbers is the rotated form: a UV per corner, in WoW's order
        // of upper-left, lower-left, upper-right, lower-right. Four is the
        // plain left/right/top/bottom crop. Reading the first four of an
        // eight-number call treats two corners as a crop rectangle, which is
        // how the paperdoll's sideways flyout arrow became a pale bar.
        if (lua_gettop(L) >= 9) {
            for (int i = 0; i < 8; ++i) {
                w->texCoordQuad[i] = static_cast<float>(luaL_optnumber(L, 2 + i, 0.0));
            }
            w->texCoordRotated = true;
            return 0;
        }
        w->texCoordRotated = false;
        w->texCoord[0] = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->texCoord[1] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->texCoord[2] = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        w->texCoord[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
}
int lua_Texture_SetBlendMode(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        const char* mode = luaL_optstring(L, 2, "BLEND");
        // "ADD" and "ALPHAKEY" are the two that add rather than cover; the rest
        // — BLEND, MOD, DISABLE — are close enough to ordinary blending that
        // telling them apart would not change a pixel here yet.
        w->blendAdd = (std::strcmp(mode, "ADD") == 0);
    }
    return 0;
}
int lua_Texture_GetBlendMode(lua_State* L) {
    auto* w = widgetOf(L, 1);
    lua_pushstring(L, (w && w->blendAdd) ? "ADD" : "BLEND");
    return 1;
}
int lua_Region_SetVertexColor(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->color[0] = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        w->color[1] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->color[2] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        w->color[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
}
int lua_Region_SetDrawLayer(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->layer = wowee::ui::parseDrawLayer(luaL_optstring(L, 2, "ARTWORK"));
        w->subLevel = static_cast<int>(luaL_optnumber(L, 3, 0));
    }
    return 0;
}
int lua_Frame_EnableMouse(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        // Absent argument means true, which is how addons usually write it.
        w->mouseEnabled = lua_isnone(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    }
    return 0;
}
int lua_Frame_IsMouseEnabled(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->mouseEnabled ? 1 : 0);
    return 1;
}

int lua_Frame_SetFrameStrata(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->strata = wowee::ui::parseStrata(luaL_optstring(L, 2, "MEDIUM"));
        w->strataExplicit = true;
    }
    return 0;
}
int lua_Frame_SetFrameLevel(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    const int wanted = static_cast<int>(luaL_optnumber(L, 2, 0));
    // Anything inside that set its own level moves by the same amount, because
    // a child's level is relative to its parent. Raising a window by hand and
    // raising it by clicking it must leave the same arrangement behind.
    if (tree && id) tree->shiftExplicitLevels(id, wanted - w->effLevel);
    w->level = wanted;
    w->levelExplicit = true;
    return 0;
}
int lua_FontString_SetText(lua_State* L) {
    // Anything but a string is taken as no text rather than as an error.
    //
    // WoW raises here, and so would this — except that the missing-API
    // fallback hands back a callable for a name nothing defines, so a label
    // fed a global that does not exist is given a function where WoW would
    // have given it a string. Raising kills the handler that was mid-update,
    // which costs far more than the empty label does: one such call took out
    // chatconfigframe's whole OnEvent.
    const char* text = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    if (auto* w = widgetOf(L, 1)) w->text = text;
    lua_pushstring(L, text);
    lua_setfield(L, 1, "_text");
    return 0;
}
int lua_FontString_GetText(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushstring(L, w ? w->text.c_str() : "");
    return 1;
}
int lua_FontString_SetJustifyH(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->justifyH = luaL_optstring(L, 2, "CENTER");
    return 0;
}
int lua_FontString_SetJustifyV(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->justifyV = luaL_optstring(L, 2, "MIDDLE");
    return 0;
}
int lua_FontString_GetJustifyV(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushstring(L, w ? w->justifyV.c_str() : "MIDDLE");
    return 1;
    return 0;
}

// ── Fonts ───────────────────────────────────────────────────────────────────

int lua_FontString_SetTextColor(lua_State* L) {
    // On a button this colours the label it holds, which is where a button's
    // text actually lives; on a font string it colours itself. FrameXML calls
    // it both ways at two hundred sites — greying an unavailable option,
    // reddening a cost that cannot be paid — and on the frame metatable it was
    // a no-op, so none of that showed.
    auto* tree = wowee::addons::getWidgetTree(L);
    wowee::ui::Widget* w = widgetOf(L, 1);
    if (w && w->kind != wowee::ui::WidgetKind::FontString && tree) {
        lua_getfield(L, 1, "__fontString");
        if (lua_istable(L, -1)) {
            if (auto* fs = tree->get(widgetIdOf(L, lua_gettop(L)))) w = fs;
        }
        lua_pop(L, 1);
    }
    if (w) {
        w->color[0] = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        w->color[1] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->color[2] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        w->color[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
}

int lua_FontString_SetFont(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        if (lua_isstring(L, 2)) w->fontFace = lua_tostring(L, 2);
        // The flags argument, where "OUTLINE" and "THICKOUTLINE" arrive.
        if (const char* flags = lua_isstring(L, 4) ? lua_tostring(L, 4) : nullptr) {
            const std::string f(flags);
            if (f.find("THICK") != std::string::npos)        w->fontOutline = "THICK";
            else if (f.find("OUTLINE") != std::string::npos) w->fontOutline = "NORMAL";
            else                                             w->fontOutline.clear();
        }
        // (path, height, flags). Only the height is honoured for now; the path
        // needs a font atlas rebuild, which cannot happen mid-frame.
        const double h = luaL_optnumber(L, 3, 0.0);
        if (h > 0.0) w->fontHeight = static_cast<float>(h);
    }
    return 0;
}

/// GetFont() → path, height, flags.
///
/// The height is the part anything does arithmetic on: WatchFrame measures a
/// test line with local _, fontHeight = line.text:GetFont() and divides by it
/// two lines later, so answering nothing loses the file.
int lua_FontString_GetFont(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushstring(L, "Fonts\\FRIZQT__.TTF");
    lua_pushnumber(L, w && w->fontHeight > 0.0f ? w->fontHeight : 12.0);
    lua_pushstring(L, "");
    return 3;
}

/// Extra space between wrapped lines. Zero unless set, and a number either
/// way: WorldMapFrame adds it to a font height on the line after asking.
int lua_FontString_GetSpacing(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->lineSpacing : 0.0);
    return 1;
}

int lua_FontString_SetSpacing(lua_State* L) {
    if (auto* w = widgetOf(L, 1))
        w->lineSpacing = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    return 0;
}

/// SetFontObject(obj) where obj is one of the shared font objects, which carry
/// a height and a colour. FrameXML reaches for these more than three thousand
/// times, so a FontString that ignores them is the wrong size and colour nearly
/// everywhere.
/// Read a font object — a table, or the name of one — onto a widget.
///
/// Shared because a button says the same thing a different way: a font string
/// has SetFontObject, a button has SetNormalFontObject and the font belongs to
/// the font string it holds.
static void applyFontObject(lua_State* L, int fontIndex, wowee::ui::Widget* w) {
    if (!w) return;
    if (lua_isstring(L, fontIndex)) {       // by name
        lua_getglobal(L, lua_tostring(L, fontIndex));
    } else {
        lua_pushvalue(L, fontIndex);
    }
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "height");
        if (lua_isnumber(L, -1)) w->fontHeight = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        // Which typeface, not only how big. FrameXML sets its headings in
        // MORPHEUS and its damage numbers in SKURRI, and a font object is
        // where it says so.
        lua_getfield(L, -1, "font");
        if (lua_isstring(L, -1)) w->fontFace = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "outline");
        if (lua_isstring(L, -1)) w->fontOutline = lua_tostring(L, -1);
        lua_pop(L, 1);
        const char* keys[4] = {"r", "g", "b", "a"};
        for (int i = 0; i < 4; ++i) {
            lua_getfield(L, -1, keys[i]);
            if (lua_isnumber(L, -1)) w->color[i] = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        lua_getfield(L, -1, "shadowX");
        if (lua_isnumber(L, -1)) {
            w->hasShadow = true;
            w->shadowX = static_cast<float>(lua_tonumber(L, -1));
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "shadowY");
        if (lua_isnumber(L, -1)) w->shadowY = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        const char* skeys[4] = {"shadowR", "shadowG", "shadowB", "shadowA"};
        for (int i = 0; i < 4; ++i) {
            lua_getfield(L, -1, skeys[i]);
            if (lua_isnumber(L, -1)) {
                w->shadowColor[i] = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

int lua_FontString_SetShadowOffset(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->hasShadow = true;
        w->shadowX = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        w->shadowY = static_cast<float>(luaL_optnumber(L, 3, -1.0));
    }
    return 0;
}
int lua_FontString_SetShadowColor(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->shadowColor[0] = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->shadowColor[1] = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        w->shadowColor[2] = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        w->shadowColor[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
}

int lua_FontString_SetFontObject(lua_State* L) {
    applyFontObject(L, 2, widgetOf(L, 1));
    return 0;
}

/// Button:SetNormalFontObject(font) — the font its label is drawn in.
///
/// FrameXML declares this as <NormalFont style="GameFontNormal"/> on 71 button
/// templates and never sets the font on the label itself, so without this every
/// button in the interface drew its text at the built-in default rather than at
/// the size and face the template asked for.
int lua_Frame_SetNormalFontObject(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    if (!tree) return 0;
    // The label, not the button: a button has no text of its own.
    lua_getfield(L, 1, "__fontString");
    wowee::ui::Widget* fs =
        lua_istable(L, -1) ? tree->get(widgetIdOf(L, lua_gettop(L))) : nullptr;
    lua_pop(L, 1);
    applyFontObject(L, 2, fs ? fs : widgetOf(L, 1));
    return 0;
}

/// Attach the shared region methods to the table on top of the stack.
void installRegionMethods(lua_State* L, bool isTexture, bool isFontString) {
    auto set = [&](const char* name, lua_CFunction fn) {
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, name);
    };
    set("GetName", lua_Region_GetName);
    set("SetPoint", lua_Region_SetPoint);
    set("ClearAllPoints", lua_Region_ClearAllPoints);
    set("SetAllPoints", lua_Region_SetAllPoints);
    set("SetSize", lua_Region_SetSize);
    set("SetWidth", lua_Region_SetWidth);
    set("SetHeight", lua_Region_SetHeight);
    set("GetWidth", lua_Region_GetWidth);
    set("GetTextWidth", lua_Region_GetTextWidth);
    set("GetStringWidth", lua_Region_GetTextWidth);
    set("GetTextHeight", lua_Region_GetTextHeight);
    set("GetStringHeight", lua_Region_GetTextHeight);
    set("GetHeight", lua_Region_GetHeight);
    set("GetLeft", lua_Region_GetLeft);
    set("GetRight", lua_Region_GetRight);
    set("GetBottom", lua_Region_GetBottom);
    set("GetTop", lua_Region_GetTop);
    set("GetRect", lua_Region_GetRect);
    set("GetPoint", lua_Region_GetPoint);
    set("GetObjectType", lua_Region_GetObjectType);
    set("IsObjectType", lua_Region_IsObjectType);
    set("GetNumPoints", lua_Region_GetNumPoints);
    set("GetScale", lua_Region_GetScale);
    set("SetScale", lua_Region_SetScale);
    set("GetEffectiveScale", lua_Region_GetEffectiveScale);
    set("Show", lua_Region_Show);
    set("Hide", lua_Region_Hide);
    set("IsShown", lua_Region_IsShown);
    set("IsVisible", lua_Region_IsVisible);
    set("SetAlpha", lua_Region_SetAlpha);
    set("GetAlpha", lua_Region_GetAlpha);
    set("SetVertexColor", lua_Region_SetVertexColor);
    set("SetDrawLayer", lua_Region_SetDrawLayer);
    if (isTexture) {
        set("SetTexture", lua_Texture_SetTexture);
        set("GetTexture", lua_Texture_GetTexture);
        set("SetTexCoord", lua_Texture_SetTexCoord);
        set("SetBlendMode", lua_Texture_SetBlendMode);
        set("GetBlendMode", lua_Texture_GetBlendMode);
    }
    if (isFontString) {
        set("SetText", lua_FontString_SetText);
        set("SetFormattedText", lua_FontString_SetFormattedText);
        set("GetText", lua_FontString_GetText);
        set("SetJustifyH", lua_FontString_SetJustifyH);
        set("SetJustifyV", lua_FontString_SetJustifyV);
        set("GetJustifyV", lua_FontString_GetJustifyV);
        set("SetTextColor", lua_FontString_SetTextColor);
        set("SetFont", lua_FontString_SetFont);
        set("GetFont", lua_FontString_GetFont);
        set("GetSpacing", lua_FontString_GetSpacing);
        set("SetSpacing", lua_FontString_SetSpacing);
        set("SetFontObject", lua_FontString_SetFontObject);
        set("SetShadowOffset", lua_FontString_SetShadowOffset);
        set("SetShadowColor", lua_FontString_SetShadowColor);
    }
    // Anything still unimplemented stays a no-op rather than an error, which is
    // what keeps a large addon running while the surface is filled in.
    // The same enumerated set the frame metatable uses, and for the same
    // reason: a region carries data fields beside its methods, and answering
    // both with a no-op makes a field that was never set look present.
    // Built once and shared. This used to compile a fresh chunk for every
    // region created, which over a FrameXML load is thousands of compiles of
    // the same three lines.
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_region_mt");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        luaL_dostring(L,
            "local known = __WoweeWidgetMethods or {} "
            "return { __index = function(_, k) "
            "  if type(k)=='string' and known[k] then return function() end end "
            "end }");
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "wowee_region_mt");
    }
    lua_setmetatable(L, -2);
}

} // namespace



// ── Backdrop and StatusBar ──────────────────────────────────────────────────

/// __WoweeSetAnimOffset(frame, x, y) — where a Translation has moved it to.
///
/// Not a WoW function: it is how the animation system, which is written in Lua,
/// reaches the one thing it cannot do from there. Displacing the anchors would
/// leave the movement behind after the animation stopped.
int lua_wowee_setAnimOffset(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->animOffsetX = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->animOffsetY = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    }
    return 0;
}

/// Read just the colour out of a font object, for the states this renderer
/// draws by colour alone. The face and size come from the normal font: a
/// button whose label changed size on hover would jump about.
static void applyStateColor(lua_State* L, int fontIndex, float out[4], bool& flag) {
    if (lua_isstring(L, fontIndex)) {
        lua_getglobal(L, lua_tostring(L, fontIndex));
    } else {
        lua_pushvalue(L, fontIndex);
    }
    if (lua_istable(L, -1)) {
        const char* keys[4] = {"r", "g", "b", "a"};
        for (int i = 0; i < 4; ++i) {
            lua_getfield(L, -1, keys[i]);
            if (lua_isnumber(L, -1)) {
                out[i] = static_cast<float>(lua_tonumber(L, -1));
                flag = true;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

int lua_Frame_SetHighlightFontObject(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        applyStateColor(L, 2, w->highlightColor, w->hasHighlightColor);
    }
    return 0;
}
int lua_Frame_SetDisabledFontObject(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        applyStateColor(L, 2, w->disabledColor, w->hasDisabledColor);
    }
    return 0;
}

int lua_Frame_SetPushedTextOffset(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->pushedTextOffsetX = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->pushedTextOffsetY = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    }
    return 0;
}
int lua_Frame_GetPushedTextOffset(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->pushedTextOffsetX : 0.0);
    lua_pushnumber(L, w ? w->pushedTextOffsetY : 0.0);
    return 2;
}

int lua_Frame_SetHitRectInsets(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->hitInsetLeft   = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->hitInsetRight  = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        w->hitInsetTop    = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        w->hitInsetBottom = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    }
    return 0;
}
int lua_Frame_GetHitRectInsets(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->hitInsetLeft   : 0.0);
    lua_pushnumber(L, w ? w->hitInsetRight  : 0.0);
    lua_pushnumber(L, w ? w->hitInsetTop    : 0.0);
    lua_pushnumber(L, w ? w->hitInsetBottom : 0.0);
    return 4;
}

/// SetUserPlaced marks a frame as positioned by the player.
///
/// The tree already tracks this — it is what stops the interface's own layout
/// pass moving a window the player has dragged — but the two calls that read
/// and set it answered as no-ops, so a frame restored from saved variables was
/// not treated as placed and could be shifted out from under its own position.
int lua_Frame_SetUserPlaced(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->userMoved = lua_toboolean(L, 2) != 0;
    return 0;
}
int lua_Frame_IsUserPlaced(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->userMoved);
    return 1;
}

/// The value a bar is showing, as distinct from the one it was told to reach.
///
/// WoW animates between them; this draws the value directly, so the two are
/// the same number. Answering honestly matters because the smoothing code
/// compares them and loops while they differ — against a no-op returning
/// nothing, that comparison never settles.
int lua_StatusBar_GetCurrentValue(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->barValue : 0.0);
    return 1;
}
int lua_StatusBar_SetDisplayValue(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->barValue = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    }
    return 0;
}

/// GetMouseFocus() — the frame the cursor is over, or nil.
///
/// The tree already knows: it is what decides which frame receives OnEnter and
/// which takes a click. FrameXML compares against it to decide whether a
/// tooltip belongs to the frame under the pointer, and the vehicle bar asks it
/// directly. It answered nothing at all before.
int lua_GetMouseFocus(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    if (!tree) { lua_pushnil(L); return 1; }
    const uint32_t hovered = tree->hoveredWidget();
    if (hovered == 0) { lua_pushnil(L); return 1; }
    lua_getglobal(L, "__WoweeFramesByWid");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(hovered));
    lua_rawget(L, -2);
    lua_remove(L, -2);          // drop the registry table, keep the frame
    return 1;
}

int lua_Frame_EnableKeyboard(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->keyboardEnabled = lua_toboolean(L, 2) != 0;
    return 0;
}
int lua_Frame_IsKeyboardEnabled(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->keyboardEnabled);
    return 1;
}
int lua_Frame_SetPropagateKeyboardInput(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->propagateKeys = lua_toboolean(L, 2) != 0;
    return 0;
}

int lua_Frame_SetToplevel(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->topLevel = lua_toboolean(L, 2) != 0;
    return 0;
}
int lua_Frame_IsToplevel(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->topLevel);
    return 1;
}
int lua_Frame_Raise(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (tree && id) tree->raise(id);
    return 0;
}
int lua_Frame_Lower(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t id = widgetIdOf(L, 1);
    if (tree && id) tree->lower(id);
    return 0;
}

int lua_Frame_SetClampedToScreen(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->clampedToScreen = lua_toboolean(L, 2) != 0;
    return 0;
}
int lua_Frame_IsClampedToScreen(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->clampedToScreen);
    return 1;
}

/// SetClampRectInsets(left, right, top, bottom) — how far past each screen
/// edge a clamped frame is allowed to sit.
///
/// It answered with a no-op, so every clamped frame was held fully on screen.
/// The world map says what that costs in a comment beside its own call —
/// SetClampRectInsets(0, 0, 0, -60), "don't overlap the xp/rep bars" — and the
/// chat frame, which is clamped and movable and asks to overhang on three
/// sides, could be dragged to places the real client does not allow.
int lua_Frame_SetClampRectInsets(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    w->clampInsetL = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    w->clampInsetR = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    w->clampInsetT = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    w->clampInsetB = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    return 0;
}

int lua_Frame_SetBackdrop(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    if (!lua_istable(L, 2)) {          // SetBackdrop(nil) clears it
        w->hasBackdrop = false;
        return 0;
    }
    w->hasBackdrop = true;
    auto str = [&](const char* key, std::string& out) {
        lua_getfield(L, 2, key);
        if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
        lua_pop(L, 1);
    };
    auto num = [&](const char* key, float& out) {
        lua_getfield(L, 2, key);
        if (lua_isnumber(L, -1)) out = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    };
    str("bgFile", w->bgFile);
    str("edgeFile", w->edgeFile);
    num("edgeSize", w->edgeSize);
    // tileSize describes the background's repeat, and edgeSize the border tile.
    // Where only one is given the other is the sensible stand-in.
    num("tileSize", w->edgeSize);
    num("edgeSize", w->edgeSize);
    lua_getfield(L, 2, "tile");
    w->tileBackground = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "insets");
    if (lua_istable(L, -1)) {
        auto inset = [&](const char* key, float& out) {
            lua_getfield(L, -1, key);
            if (lua_isnumber(L, -1)) out = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        };
        inset("left", w->insetLeft);
        inset("right", w->insetRight);
        inset("top", w->insetTop);
        inset("bottom", w->insetBottom);
    }
    lua_pop(L, 1);
    return 0;
}

int lua_Frame_SetBackdropColor(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->backdropColor[0] = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        w->backdropColor[1] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->backdropColor[2] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        w->backdropColor[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
}

int lua_Frame_SetBackdropBorderColor(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->borderColor[0] = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        w->borderColor[1] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->borderColor[2] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        w->borderColor[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
}

int lua_StatusBar_SetMinMaxValues(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->barMin = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->barMax = static_cast<float>(luaL_optnumber(L, 3, 1.0));
    }
    return 0;
}
int lua_StatusBar_GetMinMaxValues(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->barMin : 0.0);
    lua_pushnumber(L, w ? w->barMax : 1.0);
    return 2;
}
int lua_StatusBar_SetValue(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    const float value = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    const bool changed = (value != w->barValue);
    w->barValue = value;

    // A slider set from code fires OnValueChanged, as in WoW — a status bar
    // does not. It is how a scroll bar moved by the wheel or by a button
    // scrolls the frame beside it rather than only redrawing its own thumb.
    // Called through the table so the handler runs with self, and only on a
    // real change, because several of these set the value they already have on
    // every update.
    if (w->isSlider && changed) {
        lua_getfield(L, 1, "__scripts");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "OnValueChanged");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, 1);
                lua_pushnumber(L, value);
                if (lua_pcall(L, 2, 0, 0) != 0) lua_pop(L, 1);
            } else {
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    return 0;
}
/// SetCooldown(start, duration) — both on GetTime's clock. A zero duration is
/// how FrameXML clears one, and it must read as nothing running rather than as
/// a sweep that never finishes.
/// An edit box keeps its own text, so SetText on one is not the font string's.
/// FrameXML uses the same name for both and the widget decides which it means.
int lua_EditBox_SetText(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    w->editText = luaL_optstring(L, 2, "");
    w->cursorPos = w->editText.size();
    return 0;
}
int lua_EditBox_GetText(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushstring(L, w ? w->editText.c_str() : "");
    return 1;
}
int lua_EditBox_GetNumber(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? std::atof(w->editText.c_str()) : 0.0);
    return 1;
}
int lua_EditBox_Insert(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    const std::string add = luaL_optstring(L, 2, "");
    const size_t at = std::min(w->cursorPos, w->editText.size());
    w->editText.insert(at, add);
    w->cursorPos = at + add.size();
    return 0;
}
int lua_MessageFrame_SetPadding(lua_State* L) {
    // WoW takes a horizontal and a vertical padding; only the vertical one
    // changes anything here, because message lines are drawn from the frame's
    // left edge rather than inset.
    if (auto* w = widgetOf(L, 1)) {
        w->messagePadding = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    }
    return 0;
}
int lua_MessageFrame_GetPadding(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, w ? w->messagePadding : 0.0);
    return 2;
}

int lua_EditBox_SetTextInsets(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->textInsetLeft   = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->textInsetRight  = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        w->textInsetTop    = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        w->textInsetBottom = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    }
    return 0;
}
int lua_EditBox_GetTextInsets(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->textInsetLeft   : 0.0);
    lua_pushnumber(L, w ? w->textInsetRight  : 0.0);
    lua_pushnumber(L, w ? w->textInsetTop    : 0.0);
    lua_pushnumber(L, w ? w->textInsetBottom : 0.0);
    return 4;
}

int lua_EditBox_SetMaxLetters(lua_State* L) {
    if (auto* w = widgetOf(L, 1))
        w->editMaxLetters = static_cast<int>(luaL_optnumber(L, 2, 0));
    return 0;
}
int lua_EditBox_SetNumeric(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->editNumeric = lua_toboolean(L, 2) != 0;
    return 0;
}
int lua_EditBox_SetAutoFocus(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->editAutoFocus = lua_toboolean(L, 2) != 0;
    return 0;
}

int lua_EditBox_SetMultiLine(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->editMultiLine = lua_toboolean(L, 2) != 0;
    return 0;
}
int lua_EditBox_SetCursorPosition(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    const double at = luaL_optnumber(L, 2, 0.0);
    w->cursorPos = static_cast<size_t>(std::clamp(
        at, 0.0, static_cast<double>(w->editText.size())));
    return 0;
}
int lua_EditBox_GetCursorPosition(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? static_cast<double>(w->cursorPos) : 0.0);
    return 1;
}

/// How many *characters* precede the cursor, where GetCursorPosition counts
/// bytes. The two agree for plain ASCII and diverge the moment anything
/// accented is typed, which is why the interface asks for this one by name
/// wherever it is doing arithmetic on a position.
///
/// It was in the no-op method list, so it answered nil — and autocomplete.lua
/// writes `self:GetUTF8CursorPosition() - strlenutf8(command) - 1`, which
/// raises on nil rather than misbehaving. Typing a slash command or a player
/// name took the chat frame's autocomplete down with it.
int lua_EditBox_GetUTF8CursorPosition(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    if (!w) { lua_pushnumber(L, 0); return 1; }
    const size_t upTo = std::min(w->cursorPos, w->editText.size());
    int chars = 0;
    for (size_t i = 0; i < upTo; ++i) {
        // A continuation byte is 10xxxxxx; every other byte opens a character.
        if ((static_cast<unsigned char>(w->editText[i]) & 0xC0) != 0x80) ++chars;
    }
    lua_pushnumber(L, chars);
    return 1;
}

int lua_Cooldown_SetCooldown(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->cooldownStart = luaL_optnumber(L, 2, 0.0);
        w->cooldownDuration = luaL_optnumber(L, 3, 0.0);
    }
    return 0;
}
int lua_Cooldown_GetCooldownTimes(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    // Milliseconds, which is what this one answers in.
    lua_pushnumber(L, w ? w->cooldownStart * 1000.0 : 0.0);
    lua_pushnumber(L, w ? w->cooldownDuration * 1000.0 : 0.0);
    return 2;
}
int lua_Cooldown_Clear(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) { w->cooldownStart = 0.0; w->cooldownDuration = 0.0; }
    return 0;
}

int lua_Slider_SetValueStep(lua_State* L) {
    if (auto* w = widgetOf(L, 1))
        w->sliderStep = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    return 0;
}
int lua_Slider_GetValueStep(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->sliderStep : 0.0);
    return 1;
}
/// The draggable part. Given a path rather than a texture here, the same way
/// the button art setters take one.
int lua_Slider_SetThumbTexture(lua_State* L) {
    auto* w = widgetOf(L, 1);
    if (!w) return 0;
    if (lua_isstring(L, 2)) {
        w->thumbTexture = lua_tostring(L, 2);
    } else if (lua_istable(L, 2)) {
        auto* tree = wowee::addons::getWidgetTree(L);
        const auto* t = tree ? tree->get(widgetIdOf(L, 2)) : nullptr;
        if (t) w->thumbTexture = t->texturePath;
    }
    return 0;
}

int lua_StatusBar_GetValue(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? w->barValue : 0.0);
    return 1;
}
int lua_StatusBar_SetStatusBarTexture(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        // Takes a path or an existing texture object, and addons use both.
        if (lua_isstring(L, 2)) w->barTexture = lua_tostring(L, 2);
        else if (lua_istable(L, 2)) {
            if (auto* tex = widgetOf(L, 2)) w->barTexture = tex->texturePath;
        }
    }
    return 0;
}
int lua_StatusBar_SetStatusBarColor(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->barColor[0] = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        w->barColor[1] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->barColor[2] = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        w->barColor[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
}
int lua_StatusBar_SetOrientation(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        const std::string o = luaL_optstring(L, 2, "HORIZONTAL");
        w->barVertical = (o == "VERTICAL");
    }
    return 0;
}

// Frame method: frame:CreateTexture(name, layer) → a real region
static int lua_Frame_CreateTexture(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t parent = widgetIdOf(L, 1);
    const char* name = luaL_optstring(L, 2, "");
    const char* layer = luaL_optstring(L, 3, "ARTWORK");

    lua_newtable(L);
    if (tree) {
        const uint32_t id = tree->create(wowee::ui::WidgetKind::Texture, parent, name ? name : "");
        if (auto* w = tree->get(id)) {
            w->layer = wowee::ui::parseDrawLayer(layer);
            w->objectType = "Texture";
        }
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_setfield(L, -2, "__wid");
    }
    installRegionMethods(L, /*isTexture=*/true, /*isFontString=*/false);
    if (name && *name) {
        lua_pushvalue(L, -1);
        lua_setglobal(L, name);
    }
    return 1;
}

// Frame method: frame:CreateFontString(name, layer, template) → a real region
static int lua_Frame_CreateFontString(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t parent = widgetIdOf(L, 1);
    const char* name = luaL_optstring(L, 2, "");
    const char* layer = luaL_optstring(L, 3, "ARTWORK");

    lua_newtable(L);
    if (tree) {
        const uint32_t id = tree->create(wowee::ui::WidgetKind::FontString, parent, name ? name : "");
        if (auto* w = tree->get(id)) {
            w->layer = wowee::ui::parseDrawLayer(layer);
            w->objectType = "FontString";
        }
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_setfield(L, -2, "__wid");
    }
    lua_pushstring(L, "");
    lua_setfield(L, -2, "_text");
    installRegionMethods(L, /*isTexture=*/false, /*isFontString=*/true);
    if (name && *name) {
        lua_pushvalue(L, -1);
        lua_setglobal(L, name);
    }
    return 1;
}

static int lua_GetFramerate(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(ImGui::GetIO().Framerate));
    return 1;
}

// GetCursorPosition() → x, y — pixels, measured from the BOTTOM left.
//
// Pixels rather than interface units is right, and deliberate: every caller
// divides by UIParent:GetScale() itself. channelframe.lua does exactly that on
// the line after asking, and then anchors what it dragged to BOTTOMLEFT — which
// is the half that was wrong. ImGui measures the cursor from the top, so y came
// back mirrored and anything positioned from it landed as far from the bottom
// as the cursor was from the top.
static int lua_GetCursorPosition(lua_State* L) {
    const auto& io = ImGui::GetIO();
    lua_pushnumber(L, io.MousePos.x);
    lua_pushnumber(L, io.DisplaySize.y - io.MousePos.y);
    return 2;
}

// GetScreenWidth() → width
/// The screen in interface units, not pixels — which is what FrameXML means
/// by it. On a 1528-tall window GetScreenHeight() is 768, the same as it would
/// be on any other, and a frame sized against it comes out the same size.
static int lua_GetScreenWidth(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const auto* root = tree ? tree->get(tree->rootId()) : nullptr;
    if (root && root->rectW > 0.0f) { lua_pushnumber(L, root->rectW); return 1; }
    auto* svc = getLuaServices(L);
    auto* window = svc ? svc->window : nullptr;
    lua_pushnumber(L, window ? window->getWidth() : 1920);
    return 1;
}

// GetScreenHeight() → height
static int lua_GetScreenHeight(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const auto* root = tree ? tree->get(tree->rootId()) : nullptr;
    if (root && root->rectH > 0.0f) { lua_pushnumber(L, root->rectH); return 1; }
    auto* svc = getLuaServices(L);
    auto* window = svc ? svc->window : nullptr;
    lua_pushnumber(L, window ? window->getHeight() : 1080);
    return 1;
}

// Modifier key state queries using ImGui IO

static int lua_Frame_SetPoint(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* point = luaL_optstring(L, 2, "CENTER");
    // Store point info in frame table
    lua_pushstring(L, point);
    lua_setfield(L, 1, "__point");
    // Optional x/y offsets (args 4,5 if relativeTo is given, or 3,4 if not)
    double xOfs = 0, yOfs = 0;
    if (lua_isnumber(L, 4)) { xOfs = lua_tonumber(L, 4); yOfs = lua_tonumber(L, 5); }
    else if (lua_isnumber(L, 3)) { xOfs = lua_tonumber(L, 3); yOfs = lua_tonumber(L, 4); }
    lua_pushnumber(L, xOfs);
    lua_setfield(L, 1, "__xOfs");
    lua_pushnumber(L, yOfs);
    lua_setfield(L, 1, "__yOfs");
    return 0;
}

static int lua_Frame_SetSize(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    double w = luaL_optnumber(L, 2, 0);
    double h = luaL_optnumber(L, 3, 0);
    lua_pushnumber(L, w);
    lua_setfield(L, 1, "__width");
    lua_pushnumber(L, h);
    lua_setfield(L, 1, "__height");
    return 0;
}

static int lua_Frame_SetWidth(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnumber(L, luaL_checknumber(L, 2));
    lua_setfield(L, 1, "__width");
    return 0;
}

static int lua_Frame_SetHeight(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnumber(L, luaL_checknumber(L, 2));
    lua_setfield(L, 1, "__height");
    return 0;
}

static int lua_Frame_GetWidth(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__width");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 0); }
    return 1;
}

static int lua_Frame_GetHeight(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__height");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 0); }
    return 1;
}

static int lua_Frame_GetCenter(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__xOfs");
    double x = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
    lua_pop(L, 1);
    lua_getfield(L, 1, "__yOfs");
    double y = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
    lua_pop(L, 1);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int lua_Frame_SetAlpha(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnumber(L, luaL_checknumber(L, 2));
    lua_setfield(L, 1, "__alpha");
    return 0;
}

static int lua_Frame_GetAlpha(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__alpha");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 1.0); }
    return 1;
}

static int lua_Frame_SetParent(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_istable(L, 2) || lua_isnil(L, 2)) {
        lua_pushvalue(L, 2);
        lua_setfield(L, 1, "__parent");
    }
    return 0;
}

static int lua_Frame_GetParent(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__parent");
    return 1;
}


/// Records a global FrameXML or an addon asked for and did not find. Logged
/// once per name; the set is reported at shutdown so the gap can be read off a
/// run rather than guessed at.
static int lua_RecordMissingApi(lua_State* L) {
    const char* name = luaL_optstring(L, 1, "");
    if (name && *name) {
        // Once per name, so a warning here is a bounded list rather than
        // a stream, and it is the only trace of a gap as it happens.
        LOG_WARNING("[Lua] missing API called: ", name);
        missingApiNames().insert(name);
    }
    return 0;
}

// CreateFrame(frameType, name, parent, template)
static int lua_CreateFrame(lua_State* L) {
    const char* frameType = luaL_optstring(L, 1, "Frame");
    const char* name = luaL_optstring(L, 2, nullptr);

    // Create the frame table
    lua_newtable(L);

    // Record the parent table, not only the widget id. GetParent() is
    // everywhere in FrameXML — a nested button's OnLoad opens with
    // self:GetParent().toggle = self — and it answered nil for every frame
    // ever created, because only an explicit SetParent recorded one. That
    // failed the template declaring the button, so the button's owner never
    // got its size, and the loop sizing a list by its first button's height
    // divided by zero.
    if (lua_istable(L, 3)) {
        lua_pushvalue(L, 3);
        lua_setfield(L, -2, "__parent");
    } else {
        // A name, or nothing at all, which means UIParent.
        if (lua_isstring(L, 3)) lua_getglobal(L, lua_tostring(L, 3));
        else lua_getglobal(L, "UIParent");
        if (lua_istable(L, -1)) lua_setfield(L, -2, "__parent");
        else lua_pop(L, 1);
    }

    // Back it with a real widget so its geometry is somewhere the renderer can
    // reach. Parent is the third argument when given, and UIParent otherwise,
    // which is what an addon means by leaving it out.
    if (auto* tree = wowee::addons::getWidgetTree(L)) {
        uint32_t parent = 0;
        if (lua_istable(L, 3)) {
            parent = widgetIdOf(L, 3);
        } else if (lua_isstring(L, 3)) {
            lua_getglobal(L, lua_tostring(L, 3));
            if (lua_istable(L, -1)) parent = widgetIdOf(L, lua_gettop(L));
            lua_pop(L, 1);
        }
        const uint32_t id = tree->create(wowee::ui::WidgetKind::Frame, parent,
                                         name ? name : "");
        // A Button takes the mouse without being asked; a plain Frame does not,
        // which is what EnableMouse is for.
        if (auto* w = tree->get(id)) {
            const std::string ft = frameType ? frameType : "Frame";
            // Kept as it was asked for, so GetObjectType and IsObjectType
            // can answer with it rather than with "Frame" for everything.
            w->objectType = ft;
            w->mouseEnabled = (ft == "Button" || ft == "CheckButton");
            w->isStatusBar = (ft == "StatusBar");
            // A slider takes the mouse by nature: it exists to be dragged.
            w->isSlider = (ft == "Slider");
            w->isCooldown = (ft == "Cooldown");
            // Marked at creation rather than only when a child is set, so a
            // scroll frame clips what is under it even while it is empty.
            if (ft == "ScrollFrame") tree->markScrollFrame(id);
            // An edit box is clicked into, so it takes the mouse as well.
            w->isEditBox = (ft == "EditBox");
            if (w->isEditBox) {
                w->mouseEnabled = true;
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "__isEditBox");
            }
            if (w->isSlider) w->mouseEnabled = true;
        }
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_setfield(L, -2, "__wid");

        // Remember the table against its widget id so input dispatch can get
        // back from a hit to the frame whose scripts must run.
        lua_getglobal(L, "__WoweeFramesByWid");
        if (lua_istable(L, -1)) {
            lua_pushinteger(L, static_cast<lua_Integer>(id));
            lua_pushvalue(L, -3);
            lua_rawset(L, -3);
        }
        lua_pop(L, 1);
    }

    // Set frame name
    if (name && *name) {
        lua_pushstring(L, name);
        lua_setfield(L, -2, "__name");
        // Also set as a global so other addons can find it by name
        lua_pushvalue(L, -1);
        lua_setglobal(L, name);
    }

    // Set initial visibility
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "__visible");

    // Apply frame metatable with methods
    lua_getglobal(L, "__WoweeFrameMT");
    lua_setmetatable(L, -2);

    // The fourth argument names a template, which FrameXML uses constantly:
    // CreateFrame("BUTTON", name, self, "OptionsListButtonTemplate"). Ignoring
    // it was not merely a missing feature. OptionsList_OnLoad makes one button,
    // divides the list's height by that button's height to decide how many fit,
    // and loops to that number — so a template that never arrives means no
    // size, a height of zero, a count of (h-8)/0, and Lua divides by zero
    // happily. The loop then creates frames under fresh names until memory runs
    // out, which is exactly what froze the client on VideoOptionsFrame.
    //
    // Applied after the metatable, so the template's body can call methods on
    // what it is given.
    if (const char* templates = lua_isstring(L, 4) ? lua_tostring(L, 4) : nullptr) {
        const std::string list(templates);
        size_t start = 0;
        while (start <= list.size()) {
            const size_t comma = list.find(',', start);
            std::string one = list.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            const size_t b = one.find_first_not_of(" \t");
            const size_t e = one.find_last_not_of(" \t");
            one = (b == std::string::npos) ? std::string() : one.substr(b, e - b + 1);

            if (!one.empty()) {
                lua_getglobal(L, "__WoweeTemplates");
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, one.c_str());
                    if (lua_isfunction(L, -1)) {
                        lua_pushvalue(L, -3);            // the frame
                        if (lua_pcall(L, 1, 0, 0) != 0) {
                            // Once per template. A template that fails fails
                            // for every frame using it, and the loop this very
                            // failure causes then repeats it: one run wrote the
                            // same line 675,000 times, which cost more than the
                            // fault it was reporting.
                            static std::set<std::string> reported;
                            if (reported.insert(one).second) {
                                LOG_WARNING("CreateFrame: template '", one, "' failed: ",
                                            lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
                            }
                            lua_pop(L, 1);               // error
                        }
                    } else {
                        lua_pop(L, 1);                   // not a function
                    }
                }
                lua_pop(L, 1);                           // __WoweeTemplates
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        // Built from a template here, so it is loaded here — which is what
        // CreateFrame does in the real client. The XML path does not pass a
        // template to this function; it applies them separately and fires
        // OnLoad once, after the frame's own body. So this covers exactly the
        // frames Lua builds, and OptionsList_OnLoad builds a list of them:
        // their OnLoad is what gives each button the .text it is asked for
        // moments later.
        lua_getfield(L, -1, "__scripts");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "OnLoad");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -3);            // the frame
                if (lua_pcall(L, 1, 0, 0) != 0) {
                    LOG_WARNING("CreateFrame: OnLoad failed: ",
                                lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    return 1;
}

// --- WoW Utility Functions ---

// strsplit(delimiter, str) — WoW's string split

LuaEngine::LuaEngine() = default;

LuaEngine::~LuaEngine() {
    shutdown();
}

bool LuaEngine::initialize() {
    if (L_) return true;

    L_ = luaL_newstate();
    if (!L_) {
        LOG_ERROR("LuaEngine: failed to create Lua state");
        return false;
    }

    // Open safe standard libraries (no io, os, debug, package)
    luaopen_base(L_);
    luaopen_table(L_);
    luaopen_string(L_);
    luaopen_math(L_);

    // Remove unsafe globals from base library.
    //
    // newproxy is not among them, despite the name. It returns a userdata with
    // a fresh metatable and reaches nothing else; what it buys is __index and
    // __newindex on a value that cannot be tampered with, which is exactly how
    // Blizzard's own RestrictedFrames builds secure frame handles. Removing it
    // cost us SecureHandlerTemplates and everything inheriting from it.
    const char* unsafeGlobals[] = {
        "dofile", "loadfile", "load", "collectgarbage", nullptr
    };
    for (const char** g = unsafeGlobals; *g; ++g) {
        lua_pushnil(L_);
        lua_setglobal(L_, *g);
    }

    // Publish the widget tree before any API is registered, so a script that
    // runs during registration still finds it.
    lua_pushlightuserdata(L_, &widgets_);
    lua_setfield(L_, LUA_REGISTRYINDEX, "wowee_widget_tree");

    // The engine itself, for the few bindings that need to do more than touch a
    // widget — taking focus fires handlers on the frame losing it as well as
    // the one gaining it, and only the engine knows which that was.
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, "wowee_lua_engine");

    registerCoreAPI();
    registerEventAPI();

    // Last, so every bootstrap block above reads a _G that answers honestly.
    installMissingApiFallback();

    LOG_INFO("LuaEngine: initialized (Lua 5.1)");
    return true;
}

void LuaEngine::shutdown() {
    // Inside the guard, not above it. The report asks _G whether each recorded
    // name is still absent, so it needs the state it is asking about. Shutdown
    // runs twice on the way out — AddonManager's destructor calls it, and then
    // destroying the engine member calls it again — and the second pass found
    // a closed state and dereferenced it.
    if (L_) {
        reportMissingApi();
        lua_close(L_);
        L_ = nullptr;
        LOG_INFO("LuaEngine: shut down");
    }
}

void LuaEngine::setGameHandler(game::GameHandler* handler) {
    gameHandler_ = handler;
    if (L_) {
        lua_pushlightuserdata(L_, handler);
        lua_setfield(L_, LUA_REGISTRYINDEX, "wowee_game_handler");
    }
}

void LuaEngine::setLuaServices(const LuaServices& services) {
    luaServices_ = services;
    if (L_) {
        lua_pushlightuserdata(L_, &luaServices_);
        lua_setfield(L_, LUA_REGISTRYINDEX, "wowee_lua_services");
    }
}


void LuaEngine::registerCoreAPI() {
    // Override print() to go to chat
    lua_pushcfunction(L_, lua_wow_print);
    lua_setglobal(L_, "print");

    lua_pushcfunction(L_, lua_wowee_warn);
    lua_setglobal(L_, "__WoweeWarn");

    // The micro menu's game-menu button reaches this client's own settings.
    // GameMenuFrame is suppressed, so ToggleGameMenu had nothing to show and
    // the button did nothing at all; which interface owns that panel is a
    // decision rather than a gap, and this is the decision.
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto* self = static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (self && self->openSettingsCallbackRef()) self->openSettingsCallbackRef()();
        return 0;
    }, 1);
    lua_setglobal(L_, "__WoweeOpenClientSettings");

    lua_pushcfunction(L_, lua_wowee_setAnimOffset);
    lua_setglobal(L_, "__WoweeSetAnimOffset");

    lua_pushcfunction(L_, lua_GetMouseFocus);
    lua_setglobal(L_, "GetMouseFocus");


    lua_pushcfunction(L_, [](lua_State* L) -> int {
        LOG_WARNING("[FrameXML] ", luaL_optstring(L, 1, ""));
        return 0;
    });
    lua_setglobal(L_, "__WoweeLogWarning");

    // WoW API stubs
    lua_pushcfunction(L_, lua_wow_message);
    lua_setglobal(L_, "message");

    // --- Per-domain Lua API registration ---
    registerUnitLuaAPI(L_);
    registerSpellLuaAPI(L_);
    registerInventoryLuaAPI(L_);
    registerQuestLuaAPI(L_);
    registerSocialLuaAPI(L_);
    registerSystemLuaAPI(L_);
    registerActionLuaAPI(L_);
    registerLfgLuaAPI(L_);

    // WoW aliases
    lua_getglobal(L_, "string");
    lua_getfield(L_, -1, "format");
    lua_setglobal(L_, "format");
    lua_pop(L_, 1);  // pop string table

    // tinsert/tremove aliases
    lua_getglobal(L_, "table");
    lua_getfield(L_, -1, "insert");
    lua_setglobal(L_, "tinsert");
    lua_getfield(L_, -1, "remove");
    lua_setglobal(L_, "tremove");
    lua_pop(L_, 1);  // pop table

    // WoW's Lua predates the 5.1 module tables and exposes most of math, string
    // and table as bare globals as well. FrameXML calls min, ceil and PI
    // directly at file scope, and one nil there loses the whole file: mainmenubar
    // and spellbookframe each died on a single arithmetic name.
    //
    // Skipped rather than assumed where the vendored Lua lacks one — getn is
    // compiled out here, and setting a global to nil would be no better than
    // leaving it absent.
    struct Alias { const char* lib; const char* field; const char* global; };
    static constexpr Alias kAliases[] = {
        {"math", "abs", "abs"},        {"math", "ceil", "ceil"},
        {"math", "floor", "floor"},    {"math", "max", "max"},
        {"math", "min", "min"},        {"math", "fmod", "mod"},
        {"math", "sqrt", "sqrt"},      {"math", "random", "random"},
        {"string", "gsub", "gsub"},    {"string", "sub", "strsub"},
        {"string", "len", "strlen"},   {"string", "upper", "strupper"},
        {"string", "lower", "strlower"}, {"string", "find", "strfind"},
        {"string", "rep", "strrep"},   {"string", "byte", "strbyte"},
        {"string", "char", "strchar"}, {"string", "match", "strmatch"},
        {"string", "gmatch", "gmatch"}, {"table", "sort", "sort"},
        {"table", "getn", "getn"},     {"table", "concat", "tconcat"},
    };
    for (const auto& a : kAliases) {
        lua_getglobal(L_, a.lib);
        if (lua_istable(L_, -1)) {
            lua_getfield(L_, -1, a.field);
            if (lua_isnil(L_, -1)) lua_pop(L_, 1);
            else lua_setglobal(L_, a.global);
        }
        lua_pop(L_, 1);
    }
    // A constant, not a function, so the fallback's rule for SCREAMING_SNAKE
    // names leaves it nil and gametime.lua does arithmetic on nothing.
    lua_getglobal(L_, "math");
    lua_getfield(L_, -1, "pi");
    lua_setglobal(L_, "PI");
    lua_pop(L_, 1);

    // WoW-specific and not derivable from a standard library.
    bootstrap(
        "function strtrim(s, chars)\n"
        "  chars = chars or ' \\t\\r\\n'\n"
        "  local p = '[' .. chars:gsub('(%W)', '%%%1') .. ']'\n"
        "  return (s:gsub('^' .. p .. '*', ''):gsub(p .. '*$', ''))\n"
        "end\n");

    // SlashCmdList table — addons register slash commands here
    lua_newtable(L_);
    lua_setglobal(L_, "SlashCmdList");

    // Frame metatable with methods
    lua_newtable(L_);  // metatable
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "__index"); // metatable.__index = metatable

    // Defined with the other edit-box bindings further down; declared here
    // because the table below refers to them first.
    int lua_EditBox_SetFocus(lua_State* L);
    int lua_EditBox_ClearFocus(lua_State* L);
    int lua_EditBox_HasFocus(lua_State* L);

    static const struct luaL_Reg frameMethods[] = {
        {"RegisterEvent",   lua_Frame_RegisterEvent},
        {"UnregisterEvent", lua_Frame_UnregisterEvent},
        {"SetScript",       lua_Frame_SetScript},
        {"GetScript",       lua_Frame_GetScript},
        {"GetName",         lua_Frame_GetName},
        {"Show",            lua_Region_Show},
        {"Hide",            lua_Region_Hide},
        {"IsShown",         lua_Region_IsShown},
        {"IsVisible",       lua_Region_IsVisible},
        // Geometry goes through the widget tree. The older table-field
        // versions kept the numbers where only Lua could see them, which is
        // why a frame could be sized and positioned and still never appear.
        {"SetPoint",        lua_Region_SetPoint},
        {"ClearAllPoints",  lua_Region_ClearAllPoints},
        {"SetAllPoints",    lua_Region_SetAllPoints},
        {"SetSize",         lua_Region_SetSize},
        {"SetWidth",        lua_Region_SetWidth},
        {"SetHeight",       lua_Region_SetHeight},
        // Frames, not regions: only a frame is dragged or moved. These live in
        // this table rather than the shared region one because that one is
        // installed on textures and font strings alone — putting them there
        // gave the methods to everything except the things that use them.
        {"SetMovable",      lua_Frame_SetMovable},
        {"SetRotation",     lua_Model_SetFacing},
        {"SetFacing",       lua_Model_SetFacing},
        {"GetFacing",       lua_Model_GetFacing},
        {"IsMovable",       lua_Frame_IsMovable},
        {"RegisterForDrag", lua_Frame_RegisterForDrag},
        {"StartMoving",     lua_Frame_StartMoving},
        {"StopMovingOrSizing", lua_Frame_StopMovingOrSizing},
        {"GetWidth",        lua_Region_GetWidth},
        {"SetScale",        lua_Region_SetScale},
        {"GetScale",        lua_Region_GetScale},
        {"GetEffectiveScale", lua_Region_GetEffectiveScale},
        {"GetTextWidth",    lua_Region_GetTextWidth},
        {"GetStringWidth",  lua_Region_GetTextWidth},
        {"GetTextHeight",   lua_Region_GetTextHeight},
        {"GetStringHeight", lua_Region_GetTextHeight},
        {"GetHeight",       lua_Region_GetHeight},
        {"GetLeft",         lua_Region_GetLeft},
        {"GetRight",        lua_Region_GetRight},
        {"GetBottom",       lua_Region_GetBottom},
        {"GetTop",          lua_Region_GetTop},
        {"GetRect",         lua_Region_GetRect},
        {"IsMouseOver",     lua_Region_IsMouseOver},
        {"GetFrameLevel",   lua_Frame_GetFrameLevel},
        {"GetNumPoints",    lua_Region_GetNumPoints},
        {"AddMessage",      lua_MessageFrame_AddMessage},
        {"AddLine",         lua_Tooltip_AddLine},
        {"SetOwner",        lua_Tooltip_SetOwner},
        {"SetAction",       lua_Tooltip_SetAction},
        {"SetInventoryItem", lua_Tooltip_SetInventoryItem},
        {"SetBagItem",      lua_Tooltip_SetBagItem},
        {"SetGuildBankItem", lua_Tooltip_SetGuildBankItem},
        {"SetSpellByID",    lua_Tooltip_SetSpellByID},
        {"SetTalent",       lua_Tooltip_SetTalent},
        {"SetAuctionItem",  lua_Tooltip_SetAuctionItem},
        {"SetTradeSkillItem", lua_Tooltip_SetTradeSkillItem},
        {"SetUnit",         lua_Tooltip_SetUnit},
        {"AddDoubleLine",   lua_Tooltip_AddDoubleLine},
        {"ClearLines",      lua_Tooltip_ClearLines},
        {"SetFrameStack",   lua_Tooltip_SetFrameStack},
        {"NumLines",        lua_Tooltip_NumLines},
        {"Clear",           lua_MessageFrame_Clear},
        {"GetNumMessages",  lua_MessageFrame_GetNumMessages},
        {"SetMaxLines",     lua_MessageFrame_SetMaxLines},
        {"ScrollUp",        lua_MessageFrame_ScrollUp},
        {"ScrollDown",      lua_MessageFrame_ScrollDown},
        {"ScrollToBottom",  lua_MessageFrame_ScrollToBottom},
        {"Enable",          lua_Button_Enable},
        {"SetChecked",      lua_CheckButton_SetChecked},
        {"SetButtonState",  lua_Button_SetButtonState},
        {"GetButtonState",  lua_Button_GetButtonState},
        {"LockHighlight",   lua_Button_LockHighlight},
        {"UnlockHighlight", lua_Button_UnlockHighlight},
        {"GetChecked",      lua_CheckButton_GetChecked},
        {"Disable",         lua_Button_Disable},
        {"IsEnabled",       lua_Button_IsEnabled},
        {"SetScrollChild",  lua_ScrollFrame_SetScrollChild},
        {"SetVerticalScroll",   lua_ScrollFrame_SetVerticalScroll},
        {"SetHorizontalScroll", lua_ScrollFrame_SetHorizontalScroll},
        {"GetVerticalScroll",   lua_ScrollFrame_GetVerticalScroll},
        {"GetHorizontalScroll", lua_ScrollFrame_GetHorizontalScroll},
        {"GetVerticalScrollRange",   lua_ScrollFrame_GetVerticalScrollRange},
        {"GetHorizontalScrollRange", lua_ScrollFrame_GetHorizontalScrollRange},
        {"GetObjectType",   lua_Region_GetObjectType},
        {"IsObjectType",    lua_Region_IsObjectType},
        {"GetPoint",        lua_Region_GetPoint},
        {"SetZoom",         lua_Minimap_SetZoom},
        {"GetZoom",         lua_Minimap_GetZoom},
        {"GetZoomLevels",   lua_Minimap_GetZoomLevels},
        {"GetCenter",       lua_Frame_GetCenter},
        {"SetAlpha",        lua_Region_SetAlpha},
        {"GetAlpha",        lua_Region_GetAlpha},
        {"EnableMouse",     lua_Frame_EnableMouse},
        {"IsMouseEnabled",  lua_Frame_IsMouseEnabled},
        {"SetNormalFontObject",   lua_Frame_SetNormalFontObject},
        {"SetTextColor",          lua_FontString_SetTextColor},
        {"SetTextFontObject",     lua_Frame_SetNormalFontObject},
        {"SetHighlightFontObject", lua_Frame_SetHighlightFontObject},
        {"SetDisabledFontObject",  lua_Frame_SetDisabledFontObject},
        {"SetPushedTextOffset",   lua_Frame_SetPushedTextOffset},
        {"GetPushedTextOffset",   lua_Frame_GetPushedTextOffset},
        {"SetHitRectInsets",      lua_Frame_SetHitRectInsets},
        {"GetHitRectInsets",      lua_Frame_GetHitRectInsets},
        {"SetUserPlaced",         lua_Frame_SetUserPlaced},
        {"IsUserPlaced",          lua_Frame_IsUserPlaced},
        {"GetCurrentValue",       lua_StatusBar_GetCurrentValue},
        {"SetDisplayValue",       lua_StatusBar_SetDisplayValue},
        {"EnableKeyboard",        lua_Frame_EnableKeyboard},
        {"IsKeyboardEnabled",     lua_Frame_IsKeyboardEnabled},
        {"SetPropagateKeyboardInput", lua_Frame_SetPropagateKeyboardInput},
        {"SetToplevel",           lua_Frame_SetToplevel},
        {"IsToplevel",            lua_Frame_IsToplevel},
        {"Raise",                 lua_Frame_Raise},
        {"Lower",                 lua_Frame_Lower},
        {"SetClampedToScreen",    lua_Frame_SetClampedToScreen},
        {"SetClampRectInsets",    lua_Frame_SetClampRectInsets},
        {"IsClampedToScreen",     lua_Frame_IsClampedToScreen},
        {"SetBackdrop",           lua_Frame_SetBackdrop},
        {"SetBackdropColor",      lua_Frame_SetBackdropColor},
        {"SetBackdropBorderColor",lua_Frame_SetBackdropBorderColor},
        {"SetMinMaxValues",       lua_StatusBar_SetMinMaxValues},
        {"GetMinMaxValues",       lua_StatusBar_GetMinMaxValues},
        {"SetValue",              lua_StatusBar_SetValue},
        {"GetValue",              lua_StatusBar_GetValue},
        {"SetStatusBarTexture",   lua_StatusBar_SetStatusBarTexture},
        {"SetStatusBarColor",     lua_StatusBar_SetStatusBarColor},
        {"SetOrientation",        lua_StatusBar_SetOrientation},
        {"SetValueStep",          lua_Slider_SetValueStep},
        {"GetValueStep",          lua_Slider_GetValueStep},
        {"SetThumbTexture",       lua_Slider_SetThumbTexture},
        {"SetCooldown",           lua_Cooldown_SetCooldown},
        {"GetNumber",             lua_EditBox_GetNumber},
        {"Insert",                lua_EditBox_Insert},
        {"SetMaxLetters",         lua_EditBox_SetMaxLetters},        // The limit here is applied against the text's size in bytes, which is
        // what SetMaxBytes asks for; SetMaxLetters is the same field because
        // this counts the same way for both. Reporting it back matters more
        // than the distinction: an edit box that answers nothing for its limit
        // is one FrameXML will not stop typing into.
        {"SetMaxBytes",          lua_EditBox_SetMaxLetters},
        {"SetTextInsets",        lua_EditBox_SetTextInsets},
        {"SetPadding",           lua_MessageFrame_SetPadding},
        {"GetPadding",           lua_MessageFrame_GetPadding},
        {"GetTextInsets",        lua_EditBox_GetTextInsets},
        {"SetNumeric",            lua_EditBox_SetNumeric},
        {"SetMultiLine",          lua_EditBox_SetMultiLine},
        {"SetAutoFocus",          lua_EditBox_SetAutoFocus},
        {"SetCursorPosition",     lua_EditBox_SetCursorPosition},
        {"GetCursorPosition",     lua_EditBox_GetCursorPosition},
        {"GetUTF8CursorPosition", lua_EditBox_GetUTF8CursorPosition},
        {"SetFocus",              lua_EditBox_SetFocus},
        {"ClearFocus",            lua_EditBox_ClearFocus},
        {"HasFocus",              lua_EditBox_HasFocus},
        {"GetCooldownTimes",      lua_Cooldown_GetCooldownTimes},
        {"SetFrameStrata",  lua_Frame_SetFrameStrata},
        {"SetFrameLevel",   lua_Frame_SetFrameLevel},
        {"SetParent",       lua_Frame_SetParent},
        {"GetParent",       lua_Frame_GetParent},
        {"CreateTexture",   lua_Frame_CreateTexture},
        {"CreateFontString", lua_Frame_CreateFontString},
        {nullptr, nullptr}
    };
    auto applyFrameMethods = [&]() {
        lua_getglobal(L_, "__WoweeFrameMT");
        for (const luaL_Reg* r = frameMethods; r->name; r++) {
            lua_pushcfunction(L_, r->func);
            lua_setfield(L_, -2, r->name);
        }
        lua_pop(L_, 1);
    };

    for (const luaL_Reg* r = frameMethods; r->name; r++) {
        lua_pushcfunction(L_, r->func);
        lua_setfield(L_, -2, r->name);
    }
    lua_setglobal(L_, "__WoweeFrameMT");

    // Commonly called frame methods that are no-ops for now, so an addon
    // calling one gets silence rather than an error.
    //
    // Anything bound in C above must not appear here. These run afterwards and
    // simply overwrite it, turning a working method into a no-op that still
    // answers — EnableMouse was defined here and so no frame ever took the
    // mouse, however plainly the call read in the addon.
    bootstrap(
        "local mt = __WoweeFrameMT\n"

        "function mt:GetFrameStrata() return self.__strata or 'MEDIUM' end\n"
        "function mt:EnableMouseWheel(enable)\n"
        "    __WoweeSetWheelEnabled(self, enable ~= false)\n"
        "end\n"
        "function mt:SetResizable(resizable) end\n"
        // A scroll frame's range is recomputed after every layout, so asking
        // for it again has nothing to do — but answering rather than falling
        // through to the no-op list keeps it out of a report whose whole
        // purpose is naming things that are genuinely absent.
        "function mt:UpdateScrollChildRect() end\n"
        // Message frames here do not scroll: every line is drawn and the frame
        // is always showing its newest, which is what AtBottom asks. Answering
        // false would have the interface offer a scroll-to-bottom button that
        // does nothing.
        "function mt:AtBottom() return true end\n"
        // Click() runs the frame's own OnClick, with the same PreClick and
        // PostClick around it that a real press produces. FrameXML activates
        // buttons this way — a keybinding that presses an action button, a
        // dropdown that picks its default — and it answered as a no-op, so
        // none of those did anything.
        "function mt:Click(button, down)\n"
        "    local s = rawget(self, '__scripts')\n"
        "    if not s then return end\n"
        "    button = button or 'LeftButton'\n"
        "    if s.PreClick then s.PreClick(self, button, down) end\n"
        "    if s.OnClick then s.OnClick(self, button, down) end\n"
        "    if s.PostClick then s.PostClick(self, button, down) end\n"
        "end\n"
    );

    // Animations. Written in Lua because it is almost entirely bookkeeping —
    // what is playing, how far through, in what order — and the only thing it
    // cannot do from here is move a frame without disturbing its anchors.
    //
    // Nothing existed before this: CreateAnimationGroup was not defined, so a
    // frame that declared one got the missing-API fallback and every call on
    // the group returned nil. FrameXML animates the tutorial pointer and the
    // alert frames this way, and addons use it far more.
    bootstrap(
        "local mt = __WoweeFrameMT\n"
        "__WoweePlayingAnimations = {}\n"
        "local playing = __WoweePlayingAnimations\n"

        "local animMeta = {}\n"
        "animMeta.__index = animMeta\n"
        "function animMeta:SetDuration(d) self.duration = d or 0 end\n"
        "function animMeta:GetDuration() return self.duration or 0 end\n"
        "function animMeta:SetChange(c) self.change = c end\n"
        "function animMeta:GetChange() return self.change end\n"
        "function animMeta:SetFromAlpha(a) self.fromAlpha = a end\n"
        "function animMeta:SetToAlpha(a) self.toAlpha = a end\n"
        "function animMeta:SetOffset(x, y) self.offsetX, self.offsetY = x, y end\n"
        "function animMeta:GetOffset() return self.offsetX or 0, self.offsetY or 0 end\n"
        // An animation carries scripts of its own, and OnFinished on the
        // *animation* is how FrameXML hides a faded-out frame:
        // alertframes.xml puts `self:GetRegionParent():Hide()` there. Only the
        // group's OnFinished was ever called, so an achievement banner faded to
        // nothing and stayed on screen forever.
        "function animMeta:SetScript(k, f) self[k] = f end\n"
        "function animMeta:GetScript(k) return self[k] end\n"
        "function animMeta:GetRegionParent() return self.group and self.group.parent end\n"
        "function animMeta:SetOrder(o) self.order = o or 1 end\n"
        "function animMeta:GetOrder() return self.order or 1 end\n"
        "function animMeta:SetStartDelay(d) self.startDelay = d or 0 end\n"
        "function animMeta:GetStartDelay() return self.startDelay or 0 end\n"
        "function animMeta:SetEndDelay(d) self.endDelay = d or 0 end\n"
        "function animMeta:SetSmoothing(s) self.smoothing = s end\n"
        "function animMeta:SetScale(x, y) self.scaleX, self.scaleY = x, y end\n"
        "function animMeta:SetDegrees(d) self.degrees = d end\n"
        "function animMeta:GetProgress() return self.progress or 0 end\n"
        // The same progress with the animation's own easing applied, which is
        // what anything driving a value off an animation actually wants.
        // The calendar reads it directly —
        // flashTexture:SetAlpha(CalendarViewEventFlashTimer:GetSmoothProgress())
        // on an <Animation smoothing="OUT"> — and a missing *method* is not a
        // nil to be checked but a hard error, so the whole event view went
        // down on the line that makes a highlight pulse.
        "function animMeta:GetSmoothProgress()\n"
        "    local t = self.progress or 0\n"
        "    if t < 0 then t = 0 elseif t > 1 then t = 1 end\n"
        "    local s = self.smoothing\n"
        "    if s == 'IN' then return t * t end\n"
        "    if s == 'OUT' then return t * (2 - t) end\n"
        "    if s == 'IN_OUT' then\n"
        "        if t < 0.5 then return 2 * t * t end\n"
        "        local u = 1 - t\n"
        "        return 1 - 2 * u * u\n"
        "    end\n"
        "    if s == 'OUT_IN' then\n"
        "        if t < 0.5 then local u = t * 2 return u * (2 - u) * 0.5 end\n"
        "        local u = (t - 0.5) * 2\n"
        "        return 0.5 + u * u * 0.5\n"
        "    end\n"
        // No smoothing named, or one this does not model: the linear progress
        // is the honest answer and reads as a steady fade rather than nothing.
        "    return t\n"
        "end\n"
        "function animMeta:GetElapsed() return self.elapsed or 0 end\n"
        "function animMeta:SetParent(p) self.parent = p end\n"
        "function animMeta:GetRegionParent() return self.group and self.group.parent end\n"
        "function animMeta:SetTarget(t) self.target = t end\n"
        "function animMeta:IsDelaying() return (self.elapsed or 0) < (self.startDelay or 0) end\n"
        "function animMeta:IsPlaying() return self.group and self.group:IsPlaying() end\n"
        // An animation answers Play, Pause, Stop and Finish as well as its
        // group does, and acts on the group when it does. Leaving these off
        // was worse than having no animations at all: an undefined
        // TutorialFrameCallOutPulser was a harmless fallback object that
        // swallowed :Stop(), and a real table without the method is a hard
        // error that took the whole file down with it.
        "function animMeta:Play()   if self.group then self.group:Play()   end end\n"
        "function animMeta:Stop()   if self.group then self.group:Stop()   end end\n"
        "function animMeta:Pause()  if self.group then self.group:Pause()  end end\n"
        "function animMeta:Resume() if self.group then self.group:Resume() end end\n"
        "function animMeta:Finish() if self.group then self.group:Finish() end end\n"
        "function animMeta:GetSmoothing() return self.smoothing end\n"
        "function animMeta:GetOrder() return self.order or 1 end\n"
        "function animMeta:SetScript(k, f) self[k] = f end\n"
        "function animMeta:GetScript(k) return self[k] end\n"

        "local groupMeta = {}\n"
        "groupMeta.__index = groupMeta\n"
        "function groupMeta:CreateAnimation(kind, name)\n"
        "    local a = setmetatable({kind = kind or 'Alpha', group = self,\n"
        "                            duration = 0, order = 1, startDelay = 0}, animMeta)\n"
        "    table.insert(self.animations, a)\n"
        "    if name then _G[name] = a end\n"
        "    return a\n"
        "end\n"
        "function groupMeta:GetAnimations() return unpack(self.animations) end\n"
        "function groupMeta:SetLooping(m) self.looping = m end\n"
        "function groupMeta:GetLooping() return self.looping or 'NONE' end\n"
        "function groupMeta:IsPlaying() return self.isPlaying == true end\n"
        "function groupMeta:IsDone() return self.isPlaying ~= true end\n"
        "function groupMeta:SetScript(k, f) self[k] = f end\n"
        "function groupMeta:GetScript(k) return self[k] end\n"
        "function groupMeta:HookScript(k, f)\n"
        "    local prev = self[k]\n"
        "    self[k] = function(...) if prev then prev(...) end f(...) end\n"
        "end\n"
        "function groupMeta:SetParent(p) self.parent = p end\n"
        "function groupMeta:GetParent() return self.parent end\n"
        "function groupMeta:GetDuration()\n"
        "    local total = 0\n"
        "    for _, a in ipairs(self.animations) do\n"
        "        local t = (a.startDelay or 0) + (a.duration or 0)\n"
        "        if t > total then total = t end\n"
        "    end\n"
        "    return total\n"
        "end\n"
        // The frame's alpha at the moment Play is called is what an Alpha
        // animation's change is relative to. Captured here rather than at
        // creation, because a group replayed later starts from wherever the
        // frame is then.
        "function groupMeta:Play()\n"
        "    self.isPlaying = true\n"
        "    self.reversed = false\n"
        "    self.baseAlpha = self.parent and self.parent:GetAlpha() or 1\n"
        "    for _, a in ipairs(self.animations) do a.elapsed = 0 a.progress = 0 a.finished = nil end\n"
        "    playing[self] = true\n"
        "    if self.OnPlay then self:OnPlay() end\n"
        "end\n"
        "function groupMeta:Stop()\n"
        "    self.isPlaying = false\n"
        "    playing[self] = nil\n"
        // Put back what the animations moved, or a stopped group leaves the
        // frame transparent or displaced with nothing to restore it.
        "    if self.parent then\n"
        "        if self.baseAlpha then self.parent:SetAlpha(self.baseAlpha) end\n"
        "        __WoweeSetAnimOffset(self.parent, 0, 0)\n"
        "    end\n"
        "    if self.OnStop then self:OnStop() end\n"
        "end\n"
        "function groupMeta:Finish()\n"
        "    self.isPlaying = false\n"
        "    playing[self] = nil\n"
        "    if self.OnFinished then self:OnFinished() end\n"
        "end\n"
        "function groupMeta:Pause() self.paused = true end\n"
        "function groupMeta:Resume() self.paused = nil end\n"

        "function mt:CreateAnimationGroup(name)\n"
        "    local g = setmetatable({parent = self, animations = {}}, groupMeta)\n"
        "    if name then _G[name] = g end\n"
        "    self.__animGroups = self.__animGroups or {}\n"
        "    table.insert(self.__animGroups, g)\n"
        "    return g\n"
        "end\n"

        // Advanced once a frame from dispatchOnUpdate.
        "function __WoweeTickAnimations(elapsed)\n"
        "    for g in pairs(playing) do\n"
        "        if g.paused then\n"
        "        else\n"
        "            local anyRunning = false\n"
        "            local dx, dy = 0, 0\n"
        "            local alpha = g.baseAlpha or 1\n"
        "            for _, a in ipairs(g.animations) do\n"
        "                a.elapsed = (a.elapsed or 0) + elapsed\n"
        "                local t = a.elapsed - (a.startDelay or 0)\n"
        "                local d = a.duration or 0\n"
        "                if t < 0 then\n"
        "                    anyRunning = true\n"
        "                elseif d <= 0 then\n"
        "                    a.progress = 1\n"
        "                else\n"
        "                    local p = t / d\n"
        "                    local done = false\n"
        "                    if p >= 1 then p = 1 done = true else anyRunning = true end\n"
        "                    if g.reversed then p = 1 - p end\n"
        "                    a.progress = p\n"
        // Once per run, and before the group finishes, because the frame this
        // hides is the one the group is still animating.
        "                    if done and not a.finished then\n"
        "                        a.finished = true\n"
        "                        if a.OnFinished then a:OnFinished() end\n"
        "                    end\n"
        "                    if a.kind == 'Alpha' then\n"
        "                        if a.fromAlpha and a.toAlpha then\n"
        "                            alpha = a.fromAlpha + (a.toAlpha - a.fromAlpha) * p\n"
        "                        elseif a.change then\n"
        "                            alpha = (g.baseAlpha or 1) + a.change * p\n"
        "                        end\n"
        "                    elseif a.kind == 'Translation' then\n"
        "                        dx = dx + (a.offsetX or 0) * p\n"
        "                        dy = dy + (a.offsetY or 0) * p\n"
        "                    elseif a.kind == 'Scale' then\n"
        "                        local sx = a.scaleX\n"
        "                        if sx and g.parent then\n"
        "                            g.parent:SetScale(1 + (sx - 1) * p)\n"
        "                        end\n"
        "                    end\n"
        "                end\n"
        "            end\n"
        "            if g.parent then\n"
        "                if alpha < 0 then alpha = 0 elseif alpha > 1 then alpha = 1 end\n"
        "                g.parent:SetAlpha(alpha)\n"
        "                __WoweeSetAnimOffset(g.parent, dx, dy)\n"
        "            end\n"
        "            if not anyRunning then\n"
        "                local mode = g.looping or 'NONE'\n"
        "                if mode == 'REPEAT' or mode == 'BOUNCE' then\n"
        // BOUNCE plays back the way it came; REPEAT starts over. Either way the
        // clocks reset, or the next round finishes instantly.
        "                    if mode == 'BOUNCE' then g.reversed = not g.reversed end\n"
        "                    for _, a in ipairs(g.animations) do a.elapsed = 0 a.finished = nil end\n"
        "                    if g.OnLoop then g:OnLoop() end\n"
        "                else\n"
        "                    g:Finish()\n"
        "                end\n"
        "            end\n"
        "        end\n"
        "    end\n"
        "end\n"

        // SetID and GetID are defined further down, in the later chunk that
        // binds the same metatable, and that copy wins. The pair here was
        // byte-identical to it — same body, same __id key — so nothing
        // depended on which one ran. Removed so the duplicate-definition
        // check has nothing left to report but real faults.
        // The four edges are real bindings now, applied after this block.
        // Left here they would only be a silent fallback if that order ever
        // changed, and every frame reporting itself at the origin is worse
        // than none of them answering.
        // GetPoint and GetNumPoints are real bindings now, applied after this
        // block. Leaving the flat versions here would only be a silent
        // fallback if that order ever changed, and a constant point is worse
        // than none: it is where every frame goes.
        // Recorded, because a frame only receives the clicks it asks for.
        // FrameXML calls RegisterForClicks("LeftButtonUp", "RightButtonUp") on
        // the frames that want a context menu, and without this every frame
        // would answer a right-click whether it wanted one or not.
        "function mt:RegisterForClicks(...)\n"
        "    local set = {}\n"
        "    for i = 1, select('#', ...) do set[select(i, ...)] = true end\n"
        "    self.__clicks = set\n"
        "end\n"

        // SetAttribute and GetAttribute are defined further down, on the same
        // metatable, in a later chunk that overwrites whatever is here — so an
        // earlier pair is dead the moment it is written. The pair that used to
        // sit here kept its values under a different key and took one argument
        // where the real one takes three, and it is on the path every unit
        // frame's click goes through: SecureButton_GetModifiedAttribute asks
        // GetAttribute(prefix, name, suffix). Had the chunks ever been
        // reordered, clicking a unit frame would have stopped targeting and
        // right-clicking would have stopped opening a menu, with nothing to
        // say why.
        "function mt:HookScript(scriptType, fn)\n"
        "    local orig = self.__scripts and self.__scripts[scriptType]\n"
        "    if orig then\n"
        "        self:SetScript(scriptType, function(...) orig(...); fn(...) end)\n"
        "    else\n"
        "        self:SetScript(scriptType, fn)\n"
        "    end\n"
        "end\n"
        "function mt:SetMinResize(...) end\n"
        "function mt:SetMaxResize(...) end\n"
        "function mt:IsMouseOver() return false end\n"
    );

    // Button art, which XML declares as <NormalTexture>, <HighlightTexture>,
    // <ButtonText> and so on. The catch-all below would answer these with a
    // no-op, which is worse than it sounds: the setter would appear to work and
    // the matching getter would hand back nil, so button:GetNormalTexture()
    // :SetVertexColor(...) — which FrameXML does constantly to grey out an
    // unusable action — fails somewhere far from the cause.
    bootstrap(
        "local mt = __WoweeFrameMT\n"
        // A path is as valid an argument as a texture, and FrameXML uses both:
        // LoadMicroButtonTextures does
        // self:SetDisabledTexture("Interface\\Buttons\\...-Disabled"), then the
        // next line asks for it back and calls SetDesaturated on it. Storing
        // the string verbatim handed a string back, and a string has no widget
        // methods at all. A path makes or updates the slot's own texture.
        "for _, slot in ipairs({'NormalTexture', 'PushedTexture', 'HighlightTexture',\n"
        "                       'DisabledTexture', 'CheckedTexture',\n"
        "                       'DisabledCheckedTexture'}) do\n"
        "    local key = '__' .. slot\n"
        "    local layer = (slot == 'HighlightTexture') and 'HIGHLIGHT' or 'ARTWORK'\n"
        "    mt['Set' .. slot] = function(self, tex)\n"
        "        if type(tex) == 'string' then\n"
        "            local existing = self[key]\n"
        "            if type(existing) == 'table' then existing:SetTexture(tex) return end\n"
        "            local made = self:CreateTexture(nil, layer)\n"
        "            made:SetTexture(tex)\n"
        "            made:SetAllPoints(self)\n"
        "            __WoweeSetButtonArt(made, slot)\n"
        "            self[key] = made\n"
        "            return\n"
        "        end\n"
        "        if type(tex) == 'table' then __WoweeSetButtonArt(tex, slot) end\n"
        "        self[key] = tex\n"
        "    end\n"
        "    mt['Get' .. slot] = function(self) return self[key] end\n"
        "end\n"
        // Attributes, and the OnAttributeChanged they fire.
        //
        // This is how FrameXML passes state to a handler without a global:
        // UIDropDownMenu_Initialize does
        // UIDropDownMenuDelegate:SetAttribute("initmenu", frame), and the
        // delegate's OnAttributeChanged is what actually sets
        // UIDROPDOWNMENU_INIT_MENU. No-opping SetAttribute left that nil, so
        // every menu built afterwards indexed nothing.
        // Formats and sets in one call, which is how FrameXML writes most of
        // its labels — 114 places, the character sheet's "Level 14 Human Mage"
        // among them. Unimplemented, every one of those kept whatever
        // placeholder its XML carried: that line read "Level level race class"
        // because that is literally what paperdollframe.xml says.
        //
        // Guarded, because a format string and its arguments disagreeing is a
        // Lua error, and taking down the file that asked for a label is worse
        // than showing the unformatted string.
        "function mt:SetFormattedText(fmt, ...)\n"
        "    if type(fmt) ~= 'string' then return end\n"
        "    local ok, out = pcall(string.format, fmt, ...)\n"
        "    self:SetText(ok and out or fmt)\n"
        "end\n"
        "function mt:SetAttribute(name, value)\n"
        "    self.__attributes = self.__attributes or {}\n"
        "    self.__attributes[name] = value\n"
        "    local handler = self.__scripts and self.__scripts.OnAttributeChanged\n"
        "    if handler then handler(self, name, value) end\n"
        "end\n"
        // The three-argument form names one attribute in pieces, and falls
        // back to the bare name when no piece-specific value was set — which
        // is how every action button works. ActionButton_OnLoad sets "type",
        // and the secure code asks for it as prefix "", name "type", suffix
        // "1", because the suffix for LeftButton is "1". Without the fallback
        // the lookup was for "type1", found nothing, and the click ran no
        // handler at all: the button was hit, and no spell was cast.
        // A '*' stands in for either piece, and both have to be tried.
        //
        // SecureUnitButton_OnLoad sets "*type1" and "*type2" — the asterisk
        // meaning "whatever modifier is held". Asking for prefix "", name
        // "type", suffix "1" looked for "type1", then for "type", and found
        // neither, so clicking a unit frame ran no handler at all: the player
        // frame did not target and right-clicking it opened no menu. Both
        // symptoms, one missing lookup.
        //
        // The order is the client's: most specific first, the bare name last.
        "function mt:GetAttribute(a, b, c)\n"
        "    if not self.__attributes then return nil end\n"
        "    if b == nil then return self.__attributes[a] end\n"
        "    local at = self.__attributes\n"
        "    local p, s = a or '', c or ''\n"
        "    local v = at[p .. b .. s]\n"
        "    if v == nil then v = at['*' .. b .. s] end\n"
        "    if v == nil then v = at[p .. b .. '*'] end\n"
        "    if v == nil then v = at['*' .. b .. '*'] end\n"
        "    if v == nil then v = at[b] end\n"
        "    return v\n"
        "end\n"
        // A scroll frame's content frame.
        // Nothing to scroll until the tree has been laid out, and zero is the
        // honest answer then. ScrollFrame_OnScrollRangeChanged compares the
        // bar value against this the moment a scroll frame is built.
        // The scroll getters are real bindings now, applied after this block.
        // Zero when unset, which is what the real client answers and what
        // FrameXML concatenates into a name without checking.
        "function mt:SetID(id) self.__id = id end\n"
        "function mt:GetID() return self.__id or 0 end\n"
        "function mt:GetScrollChild() return self.__scrollChild end\n"
        "function mt:SetFontString(fs) self.__fontString = fs end\n"
        // Made on demand when a button is asked for one it has not been
        // given. Every button has a font string in the real client, and
        // FrameXML assumes it: FCF_SetTabColor does
        // minFrame:GetFontString():SetTextColor(...) without checking.
        "function mt:GetFontString()\n"
        "    if not self.__fontString then\n"
        "        self.__fontString = self:CreateFontString(nil, 'OVERLAY')\n"
        "    end\n"
        "    return self.__fontString\n"
        "end\n"
        // A button's text is its font string's text; keeping them apart means
        // SetText on the button quietly does nothing, which is how a bar full
        // of blank buttons happens.
        // An edit box keeps its own text; a button shows its font string's.
        // FrameXML calls SetText on both and the widget decides which it means.
        "function mt:SetText(text, r, g, b)\n"
        // OnTextSet belongs to the box, not to the typing: it fires when the
        // text is *set* rather than entered, which is how the chat box learns
        // that something put a channel prefix in it. Declared on the chat edit
        // box and dispatched by nothing until now.
        "    if self.__isEditBox then\n"
        "        __WoweeEditSetText(self, text)\n"
        "        local h = self.__scripts and self.__scripts.OnTextSet\n"
        "        if h then h(self) end\n"
        "        return\n"
        "    end\n"
        // A tooltip's SetText is its first line, not a font string's text.
        // It answers whether it took the call, so this can stop there.
        "    if __WoweeTooltipSetText(self, text, r, g, b) then return end\n"
        "    self.__text = text\n"
        "    if self.__fontString then self.__fontString:SetText(text) end\n"
        "end\n"
        "function mt:GetText()\n"
        "    if self.__isEditBox then return __WoweeEditGetText(self) end\n"
        "    if self.__fontString then return self.__fontString:GetText() end\n"
        "    return self.__text\n"
        "end\n"
    );

    // Catch-all for unimplemented widget methods. Frames are logic-only stubs (not
    // natively rendered), so UI-heavy addons call many widget methods we don't model
    // (sliders: SetMinMaxValues/SetValue; check buttons: SetChecked; buttons:
    // SetNormalTexture; etc.). Without this, the first such call raises "attempt to
    // call a nil value" and aborts the addon before it can register its slash commands.
    // WoW widget methods are PascalCase, so an unknown key starting with an uppercase
    // letter is treated as an unimplemented method (harmless no-op); anything else
    // falls through to nil so ordinary addon fields keep their normal (falsy) meaning.
    // The widget methods this stands in for, named rather than guessed at.
    //
    // Answering every PascalCase key with a no-op was wrong for data. A field
    // is PascalCase as readily as a method — textStatusBar.TextString is the
    // one that surfaced it — and a function is truthy, so FrameXML's own
    // "if (x.Field) then use it" ran the branch against something that was
    // never there. Methods and data cannot be told apart by shape: of the 307
    // method names FrameXML calls, eighteen read as nouns (AppendText,
    // NumLines, PageUp, AtBottom), and of the PascalCase fields it assigns,
    // several are method names held in a table.
    //
    // So the set is enumerated: every method FrameXML calls on a widget, plus
    // the standard widget API for addons. A name in it answers with a no-op;
    // anything else is data and answers nil, which is what it would be.
    bootstrap(
        "__WoweeWidgetMethods = {\n"
        "AddDoubleLine=1,AddHistoryLine=1,AddLine=1,AddMessage=1,AddTexture=1,\n"
        "AddToAutoHide=1,AllowAttributeChanges=1,Animate=1,AppendText=1,\n"
        "CallMethod=1,CanSaveTabardNow=1,ChildUpdate=1,Clear=1,ClearAllPoints=1,\n"
        "ClearBinding=1,ClearBindings=1,ClearFocus=1,ClearHistory=1,ClearLines=1,\n"
        "ClearModel=1,CreateFontString=1,CreatePlayerArrowFrame=1,\n"
        "CreateTexture=1,CreateTitleRegion=1,CycleVariation=1,Disable=1,DrawQuestBlob=1,\n"
        "Dress=1,Enable=1,EnableKeyboard=1,EnableMouse=1,EnableMouseWheel=1,\n"
        "EnableSubtitles=1,FadeOut=1,Free=1,GetAlpha=1,GetAnchorType=1,GetAttribute=1,\n"
        "GetBackdrop=1,GetBottom=1,GetButtonState=1,GetCenter=1,GetChecked=1,\n"
        "GetCheckedTexture=1,GetChildList=1,GetChildren=1,GetColorRGB=1,\n"
        "GetCursorPosition=1,GetDisabledCheckedTexture=1,\n"
        "GetDisabledTexture=1,GetDrawLayer=1,GetEffectiveAttribute=1,\n"
        "GetEffectiveScale=1,GetFieldSize=1,GetFileHeight=1,GetFileWidth=1,GetFont=1,\n"
        "GetFontObject=1,GetFontString=1,GetFrame=1,GetFrameLevel=1,GetFrameRef=1,\n"
        "GetFrameStrata=1,GetHeight=1,GetHighlightTexture=1,GetHorizontalScroll=1,\n"
        "GetHorizontalScrollRange=1,GetID=1,GetInputLanguage=1,GetInventorySlot=1,\n"
        "GetItem=1,GetLeft=1,GetLowerEmblemTexture=1,GetMessageInfo=1,GetMinimumWidth=1,\n"
        "GetMinMaxValues=1,GetMousePosition=1,GetName=1,GetNormalTexture=1,GetNumber=1,\n"
        "GetNumChildren=1,GetNumMessages=1,GetNumPoints=1,GetNumTooltips=1,\n"
        "GetObjectType=1,GetParent=1,GetPoint=1,GetPushedTexture=1,GetRect=1,\n"
        "GetRegionParent=1,GetRegions=1,GetRight=1,GetScale=1,GetScript=1,\n"
        "GetScrollChild=1,GetSize=1,GetSpacing=1,GetStatusBarTexture=1,\n"
        "GetStringHeight=1,GetStringWidth=1,GetTexCoord=1,GetText=1,GetTextColor=1,\n"
        "GetTextHeight=1,GetTexture=1,GetTextWidth=1,GetTooltipIndex=1,GetTop=1,\n"
        // GetUTF8CursorPosition is a real binding now, applied after this set.
        // A real method wins the lookup either way, but a name left here is a
        // claim that nothing implements it, and autocomplete's arithmetic is
        // the reason it could not stay a no-op.
        "GetUIPanel=1,GetUpperEmblemTexture=1,GetValue=1,\n"
        "GetVertexColor=1,GetVerticalScroll=1,GetVerticalScrollRange=1,GetWidth=1,\n"
        "GetZoom=1,GetZoomLevels=1,HasFocus=1,HasScript=1,Hide=1,HideUIPanel=1,\n"
        "HighlightText=1,HookScript=1,IgnoreDepth=1,InitializeTabardColors=1,Insert=1,\n"
        "IsEnabled=1,IsEquippedItem=1,IsEventRegistered=1,IsMouseEnabled=1,\n"
        "IsObjectType=1,IsProtected=1,IsShown=1,IsUnderMouse=1,\n"
        "IsUnit=1,IsVisible=1,LockHighlight=1,Lower=1,MoveUIPanel=1,\n"
        "New=1,NumLines=1,OnFinished=1,OnUpdate=1,PageDown=1,PageUp=1,PingLocation=1,\n"
        "Play=1,Raise=1,RefreshUnit=1,RefreshValue=1,RegisterAutoHide=1,RegisterEvent=1,\n"
        "RegisterForClicks=1,RegisterForDrag=1,ReleaseFrame=1,\n"
        "RemoveMessagesByAccessID=1,ReplaceIconTexture=1,Reset=1,Reuse=1,Run=1,\n"
        "RunAttribute=1,RunFor=1,Save=1,ScrollDown=1,ScrollToBottom=1,ScrollUp=1,\n"
        "SelectWindow=1,SetAction=1,SetAllPoints=1,SetAlpha=1,SetAlphaGradient=1,\n"
        "SetAnchorType=1,SetAttribute=1,SetBackdrop=1,\n"
        "SetBackdropBorderColor=1,SetBackdropColor=1,SetBagItem=1,SetBinding=1,\n"
        "SetBindingClick=1,SetBindingItem=1,SetBindingMacro=1,SetBindingSpell=1,\n"
        "SetBlendMode=1,SetBorderAlpha=1,SetBorderScalar=1,SetBorderTexture=1,\n"
        "SetButtonState=1,SetCamera=1,SetChecked=1,SetCheckedTexture=1,\n"
        "SetClampedToScreen=1,SetColorRGB=1,SetCooldown=1,\n"
        "SetCreature=1,SetCursorPosition=1,SetDesaturated=1,SetDisabledCheckedTexture=1,\n"
        "SetDisabledFontObject=1,SetDisabledTexture=1,SetDrawLayer=1,\n"
        "SetEquipmentSet=1,SetFacing=1,SetFillAlpha=1,SetFillTexture=1,SetFocus=1,\n"
        "SetFont=1,SetFontObject=1,SetFontString=1,SetFormattedText=1,SetFrameLevel=1,\n"
        "SetFrameRate=1,SetFrameStrata=1,SetHeight=1,SetHighlightFontObject=1,\n"
        "SetHighlightTexture=1,SetHitRectInsets=1,SetHorizontalScroll=1,SetHyperlink=1,\n"
        "SetHyperlinkCompareItem=1,SetHyperlinksEnabled=1,SetID=1,\n"
        "SetInventoryItem=1,SetJustifyH=1,SetJustifyV=1,SetLFGCompletionReward=1,\n"
        "SetLFGDungeonReward=1,SetLight=1,SetMaxBytes=1,\n"
        "SetMaxLetters=1,SetMaxResize=1,SetMerchantCostItem=1,\n"
        "SetMinimumWidth=1,SetMinMaxValues=1,SetMinResize=1,SetModel=1,SetModelScale=1,\n"
        "SetMovable=1,SetMultiLine=1,SetNormalFontObject=1,SetNormalTexture=1,\n"
        "SetNumber=1,SetNumeric=1,SetOwner=1,SetParent=1,SetPetAction=1,\n"
        "SetPlayerTextureHeight=1,SetPlayerTextureWidth=1,SetPoint=1,SetPosition=1,\n"
        "SetPossession=1,SetPropagateKeyboardInput=1,SetPushedTexture=1,SetQuestItem=1,\n"
        "SetQuestLogRewardSpell=1,SetQuestLogSpecialItem=1,\n"
        "SetResizable=1,SetRotation=1,SetScale=1,SetScript=1,\n"
        "SetScrollChild=1,SetSelection=1,SetSequence=1,\n"
        "SetSequenceTime=1,SetShadowOffset=1,SetShapeshift=1,SetShown=1,SetSize=1,\n"
        "SetSpacing=1,SetSpell=1,SetSpellByID=1,SetStartDelay=1,SetStatusBarColor=1,\n"
        // Tooltip setters for things this client cannot describe yet. They
        // belong here rather than nowhere: a name the metatable does not answer
        // comes back nil, and GameTooltip:SetTalent(...) on nil is "attempt to
        // call method", which takes down the handler that was only trying to
        // show a tooltip. Hovering a talent did that, and the talent, auction
        // and trade skill panels all load. A no-op leaves the tooltip empty and
        // is recorded, so it stays visible as a gap instead of a crash.
        // SetTalent has left this list because it is implemented now. Leaving
        // it would not have broken anything — the lookup rawgets the real
        // method table first and only falls through to here — but this set says
        // "cannot describe it yet", and a name in it that works reads as a gap
        // that is not there, in the one place someone would check.
        "SetGlyph=1,SetSocketGem=1,SetSocketedItem=1,SetExistingSocketGem=1,\n"
        "SetScrollOffset=1,RegisterAllEvents=1,\n"
        "SetStatusBarTexture=1,SetTexCoord=1,SetText=1,SetTextHeight=1,\n"
        "SetTexture=1,SetToplevel=1,SetTracking=1,\n"
        "SetTradeTargetItem=1,SetUIPanel=1,SetUnit=1,\n"
        "SetUnitBuff=1,SetUnitDebuff=1,SetValue=1,SetValueStep=1,\n"
        "SetVertexColor=1,SetVerticalScroll=1,SetWidth=1,SetZoom=1,Show=1,ShowUIPanel=1,\n"
        "ShowUIPanelFailed=1,StartMovie=1,StartMoving=1,StartSizing=1,Stop=1,\n"
        "StopMovie=1,StopMovingOrSizing=1,ToggleInputLanguage=1,TryOn=1,\n"
        "UIParentManageFramePositions=1,UnlockHighlight=1,UnregisterAllEvents=1,\n"
        "UnregisterAutoHide=1,UnregisterEvent=1,UpdateColorByID=1,\n"
        "UpdateMouseOverTooltip=1,UpdateTooltip=1,\n"
        "UpdateUIPanelPositions=1,\n"
        "}\n"
    );
    bootstrap(
        "local mt = __WoweeFrameMT\n"
        "local methods = mt\n"
        "local known = __WoweeWidgetMethods\n"
        "local noop = function() end\n"
        "local seen = {}\n"
        // __index is the method table itself, with the fallback moved to a
        // metatable on that table.
        //
        // FrameXML reaches through it to call the original of an overridden
        // method — BlizzardOptionsPanel_Slider_Enable is
        // getmetatable(slider).__index.Enable(slider) — which needs a table
        // there. A function answered every lookup correctly and broke every one
        // of those.
        "setmetatable(mt, { __index = function(tbl, key)\n"
        "    local v = rawget(methods, key)\n"
        "    if v ~= nil then return v end\n"
        "    if type(key) ~= 'string' then return nil end\n"
        // A name in the set answers with a no-op — and is recorded, because a
        // method that quietly does nothing is indistinguishable from one that
        // works. SetFormattedText sat in this set unimplemented while 114
        // labels across FrameXML kept their XML placeholder text, and nothing
        // anywhere said so.
        "    if known[key] then\n"
        "        if not seen[key] then\n"
        "            seen[key] = true\n"
        "            if __WoweeRecordMissingApi then __WoweeRecordMissingApi('noop:' .. key) end\n"
        "        end\n"
        "        return noop\n"
        "    end\n"
        // Recorded once so a method missing from the set is visible rather
        // than silently answering nil, which is the failure this trades for.
        // Not On*: those are script handler names, and reading one as a field
        // is how FrameXML asks whether a handler is set. Nil is the right
        // answer there, so recording it would be reporting correct behaviour
        // as a gap.
        "    if string.find(key, '^%u') and not string.find(key, '^On%u')\n"
        "       and not seen[key] then\n"
        "        seen[key] = true\n"
        "        if __WoweeRecordMissingApi then __WoweeRecordMissingApi('widget:' .. key) end\n"
        "    end\n"
        "    return nil\n"
        "end })\n"
        // The lookup itself goes through the method table, so a frame finds its
        // methods by rawget and anything unknown falls through to the function
        // above.
        "mt.__index = mt\n"
    );

    // The fallback is installed at the very end of initialize(), not here.
    // Everything below is still bootstrap Lua, and much of it opens with the
    // "LibStub = LibStub or {}" idiom — which reads nil only while _G answers
    // honestly. With the fallback already in place those never see nil, and
    // hang their tables off the fallback object instead of a fresh one.

    // Put the C bindings back over anything the Lua above defined with the same
    // name. That block exists to give unimplemented methods a harmless no-op,
    // and it runs later, so any name it shares with a real binding silently
    // replaces it — a method that answers and does nothing, which is far harder
    // to spot than one that errors. EnableMouse was lost this way and no frame
    // took the mouse at all; SetBackdrop and its two colour setters were about
    // to go the same way. Ordering the two makes the class of mistake
    // impossible rather than something to keep noticing.
    applyFrameMethods();

    // CreateFrame function
    lua_pushcfunction(L_, lua_EditBox_SetText);
    lua_setglobal(L_, "__WoweeEditSetText");
    lua_pushcfunction(L_, lua_Tooltip_SetText);
    lua_setglobal(L_, "__WoweeTooltipSetText");
    lua_pushcfunction(L_, lua_EditBox_GetText);
    lua_setglobal(L_, "__WoweeEditGetText");

    lua_pushcfunction(L_, lua_CreateFrame);
    lua_setglobal(L_, "CreateFrame");

    // Cursor/screen/FPS functions
    lua_pushcfunction(L_, lua_GetCursorPosition);
    lua_setglobal(L_, "GetCursorPosition");
    lua_pushcfunction(L_, lua_GetScreenWidth);
    lua_setglobal(L_, "GetScreenWidth");
    lua_pushcfunction(L_, lua_GetScreenHeight);
    lua_setglobal(L_, "GetScreenHeight");
    lua_pushcfunction(L_, lua_GetFramerate);
    lua_setglobal(L_, "GetFramerate");

    // Frame event dispatch table
    lua_newtable(L_);
    lua_setglobal(L_, "__WoweeFrameEvents");

    // OnUpdate frame tracking table
    lua_newtable(L_);
    lua_setglobal(L_, "__WoweeOnUpdateFrames");

    // widget id -> frame table, so a hit test can find the scripts to run.
    lua_newtable(L_);
    lua_setglobal(L_, "__WoweeFramesByWid");

    // Reached from EnableMouseWheel, which is written in Lua on the metatable
    // so it applies to every frame without another entry in the method list.
    lua_pushcfunction(L_, lua_Frame_SetWheelEnabled);
    lua_setglobal(L_, "__WoweeSetWheelEnabled");

    // Reached from the button-art setters, which are written in Lua so they
    // cover every slot from one loop.
    lua_pushcfunction(L_, lua_Texture_SetButtonArt);
    lua_setglobal(L_, "__WoweeSetButtonArt");

    // Where XML templates land. A virtual frame compiles to a function that
    // replays itself onto a real frame, and inherits= calls it; both halves are
    // emitted by the FrameXML loader and meet here.
    bootstrap(
        "__WoweeTemplates = {}\n"
        "local reported = {}\n"
        "function __WoweeMissingTemplate(name)\n"
        "  if reported[name] then return end\n"
        "  reported[name] = true\n"
        "  -- Said once per template. A frame inheriting one that never loaded\n"
        "  -- still gets built, just without whatever the template gave it,\n"
        "  -- which is a much better outcome than refusing the whole file.\n"
        "  __WoweeLogWarning('missing XML template: ' .. tostring(name))\n"
        "end\n");

    // C_Timer implementation via Lua (uses OnUpdate internally)
    bootstrap(
        "C_Timer = {}\n"
        "local timers = {}\n"
        "local timerFrame = CreateFrame('Frame', '__WoweeTimerFrame')\n"
        "timerFrame:SetScript('OnUpdate', function(self, elapsed)\n"
        "    local i = 1\n"
        "    while i <= #timers do\n"
        "        timers[i].remaining = timers[i].remaining - elapsed\n"
        "        if timers[i].remaining <= 0 then\n"
        "            local cb = timers[i].callback\n"
        "            table.remove(timers, i)\n"
        "            cb()\n"
        "        else\n"
        "            i = i + 1\n"
        "        end\n"
        "    end\n"
        "    if #timers == 0 then self:Hide() end\n"
        "end)\n"
        "timerFrame:Hide()\n"
        "function C_Timer.After(seconds, callback)\n"
        "    tinsert(timers, {remaining = seconds, callback = callback})\n"
        "    timerFrame:Show()\n"
        "end\n"
        "function C_Timer.NewTicker(seconds, callback, iterations)\n"
        "    local count = 0\n"
        "    local maxIter = iterations or -1\n"
        "    local ticker = {cancelled = false}\n"
        "    local function tick()\n"
        "        if ticker.cancelled then return end\n"
        "        count = count + 1\n"
        "        callback(ticker)\n"
        "        if maxIter > 0 and count >= maxIter then return end\n"
        "        C_Timer.After(seconds, tick)\n"
        "    end\n"
        "    C_Timer.After(seconds, tick)\n"
        "    function ticker:Cancel() self.cancelled = true end\n"
        "    return ticker\n"
        "end\n"
    );

    // DEFAULT_CHAT_FRAME with AddMessage method (used by many addons)
    bootstrap(
        "DEFAULT_CHAT_FRAME = {}\n"
        "function DEFAULT_CHAT_FRAME:AddMessage(text, r, g, b)\n"
        "    if r and g and b then\n"
        "        local hex = format('|cff%02x%02x%02x', "
        "            math.floor(r*255), math.floor(g*255), math.floor(b*255))\n"
        "        print(hex .. tostring(text) .. '|r')\n"
        "    else\n"
        "        print(tostring(text))\n"
        "    end\n"
        "end\n"
        "ChatFrame1 = DEFAULT_CHAT_FRAME\n"
    );

    // hooksecurefunc — hook a function to run additional code after it
    bootstrap(
        "function hooksecurefunc(tblOrName, nameOrFunc, funcOrNil)\n"
        "    local tbl, name, hook\n"
        "    if type(tblOrName) == 'table' then\n"
        "        tbl, name, hook = tblOrName, nameOrFunc, funcOrNil\n"
        "    else\n"
        "        tbl, name, hook = _G, tblOrName, nameOrFunc\n"
        "    end\n"
        "    local orig = tbl[name]\n"
        "    if type(orig) ~= 'function' then return end\n"
        "    tbl[name] = function(...)\n"
        "        local r = {orig(...)}\n"
        "        hook(...)\n"
        "        return unpack(r)\n"
        "    end\n"
        "end\n"
    );

    // LibStub — universal library version management used by Ace3 and virtually all addon libs.
    // This is the standard WoW LibStub implementation that addons embed/expect globally.
    bootstrap(
        // rawget, so the missing-API fallback cannot answer this. Read through
        // the metatable, "LibStub or {}" is never nil — it is the fallback
        // object — and the shim then hangs its tables off that instead of a
        // fresh one, so every library registering against it dies indexing a
        // field that was never really there.
        "local LibStub = rawget(_G, 'LibStub') or {}\n"
        "LibStub.libs = LibStub.libs or {}\n"
        "LibStub.minors = LibStub.minors or {}\n"
        "function LibStub:NewLibrary(major, minor)\n"
        "    assert(type(major) == 'string', 'LibStub:NewLibrary: bad argument #1 (string expected)')\n"
        "    minor = assert(tonumber(minor or (type(minor) == 'string' and minor:match('(%d+)'))), 'LibStub:NewLibrary: bad argument #2 (number expected)')\n"
        "    local oldMinor = self.minors[major]\n"
        "    if oldMinor and oldMinor >= minor then return nil end\n"
        "    local lib = self.libs[major] or {}\n"
        "    self.libs[major] = lib\n"
        "    self.minors[major] = minor\n"
        "    return lib, oldMinor\n"
        "end\n"
        "function LibStub:GetLibrary(major, silent)\n"
        "    if not self.libs[major] and not silent then\n"
        "        error('Cannot find a library instance of \"' .. tostring(major) .. '\".')\n"
        "    end\n"
        "    return self.libs[major], self.minors[major]\n"
        "end\n"
        "function LibStub:IterateLibraries() return pairs(self.libs) end\n"
        "setmetatable(LibStub, { __call = LibStub.GetLibrary })\n"
        "_G['LibStub'] = LibStub\n"
    );

    // CallbackHandler-1.0 — minimal implementation for Ace3-based addons
    bootstrap(
        "if LibStub then\n"
        "  local CBH = LibStub:NewLibrary('CallbackHandler-1.0', 7)\n"
        "  if CBH then\n"
        "    CBH.mixins = { 'RegisterCallback', 'UnregisterCallback', 'UnregisterAllCallbacks', 'Fire' }\n"
        "    function CBH:New(target, regName, unregName, unregAllName, onUsed)\n"
        "      local registry = setmetatable({}, { __index = CBH })\n"
        "      registry.callbacks = {}\n"
        "      target = target or {}\n"
        "      target[regName or 'RegisterCallback'] = function(self, event, method, ...)\n"
        "        if not registry.callbacks[event] then registry.callbacks[event] = {} end\n"
        "        local handler = type(method) == 'function' and method or self[method]\n"
        "        registry.callbacks[event][self] = handler\n"
        "      end\n"
        "      target[unregName or 'UnregisterCallback'] = function(self, event)\n"
        "        if registry.callbacks[event] then registry.callbacks[event][self] = nil end\n"
        "      end\n"
        "      target[unregAllName or 'UnregisterAllCallbacks'] = function(self)\n"
        "        for event, handlers in pairs(registry.callbacks) do handlers[self] = nil end\n"
        "      end\n"
        "      registry.Fire = function(self, event, ...)\n"
        "        if not self.callbacks[event] then return end\n"
        "        for obj, handler in pairs(self.callbacks[event]) do\n"
        "          handler(obj, event, ...)\n"
        "        end\n"
        "      end\n"
        "      return registry\n"
        "    end\n"
        "  end\n"
        "end\n"
    );

    // Noop stubs for commonly called functions that don't need implementation
    bootstrap(
        // Empty a table in place, keeping the table itself. WoW's, not Lua's —
        // it exists as both a global and table.wipe, and FrameXML calls it 21
        // times. Missing, BuffFrame_Update errored on its first line and no
        // buff button was ever created.
        // The global already exists as a binding; only the table form was
        // missing, and FrameXML calls both.
        // Positional format specifiers, which this Lua does not have.
        //
        // The client's format accepts "%2$s" — argument two, whatever its
        // place in the string — and the interface leans on it hard: 189 uses
        // of %2$s, 184 of %4$s, 154 of %1$s, nearly all of them combat log
        // lines in GlobalStrings. Stock Lua 5.1 answers "invalid option '%$'
        // to 'format'" and raises, so every one of those took down whatever
        // was building the line. The combat log raised on its own first entry.
        //
        // Rewritten rather than reimplemented: the specifiers are stripped to
        // plain ones and the arguments put in the order they asked for, then
        // the real format does the work. A string with no positional specifier
        // takes the original path untouched.
        "do\n"
        "    local rawformat = string.format\n"
        "    local function positional(fmt, ...)\n"
        "        if type(fmt) ~= 'string' or not fmt:find('%%%d+%$') then\n"
        "            return rawformat(fmt, ...)\n"
        "        end\n"
        // A literal %% is not the start of a specifier, so it is put aside
        // before the scan and restored after — otherwise "%%2$s" would be read
        // as an argument reference and eat the escape.
        "        local ESC = '\\1'\n"
        "        local work = fmt:gsub('%%%%', ESC)\n"
        "        local order = {}\n"
        "        work = work:gsub('%%(%d+)%$', function(n)\n"
        "            order[#order + 1] = tonumber(n)\n"
        "            return '%'\n"
        "        end)\n"
        "        work = work:gsub(ESC, '%%%%')\n"
        "        local args = {...}\n"
        "        local picked = {}\n"
        "        for i = 1, #order do picked[i] = args[order[i]] end\n"
        // Guarded like the rest of the formatting here: a format string and
        // its arguments disagreeing is an error, and losing the line is
        // better than losing the frame that was writing it.
        "        local ok, out = pcall(rawformat, work, unpack(picked, 1, #order))\n"
        "        return ok and out or fmt\n"
        "    end\n"
        "    string.format = positional\n"
        "    format = positional\n"
        "end\n"
        "table.wipe = wipe\n"
        "function SetDesaturation() end\n"
        // A class circle rather than the 3D portrait the real client renders,
        // which needs a model rendered to a texture. The coordinates come from
        // FrameXML's own CLASS_ICON_TCOORDS, read when called so it does not
        // matter that this is defined before that table exists.
        "function SetPortraitTexture(texture, unit)\n"
        "    if type(texture) ~= 'table' then return end\n"
        "    local _, class = UnitClass(unit or 'player')\n"
        "    local coords = class and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[class]\n"
        "    if coords then\n"
        "        texture:SetTexture('Interface\\\\TargetingFrame\\\\UI-Classes-Circles')\n"
        "        texture:SetTexCoord(coords[1], coords[2], coords[3], coords[4])\n"
        // Nothing at all when the class is not known yet, rather than the
        // placeholder. This runs at world entry now that events reach frames,
        // which is before the player's entity resolves — so it answered
        // "Unknown", stamped the placeholder shield over the portrait, and
        // never ran again to correct it.
        "    end\n"
        "end\n"
        "function StopSound() end\n"
        "function UIParent_OnEvent() end\n"
        // Filling the screen, not sitting at a point on it. The widget tree's
        // root is already the screen, and a frame created with no anchors falls
        // to the centre-on-parent default with no size — so every frame
        // FrameXML hangs off UIParent inherited a zero-size box in the middle,
        // including its own UIParent, which fills this one. That is why the
        // player frame's name was drawn in the centre of the world.
        //
        // SetAllPoints with no argument fills the parent, which for these is
        // the root.
        "UIParent = CreateFrame('Frame', 'UIParent')\n"
        "UIParent:SetAllPoints()\n"
        "UIPanelWindows = {}\n"
        "WorldFrame = CreateFrame('Frame', 'WorldFrame')\n"
        "WorldFrame:SetAllPoints()\n"
        // GameTooltip: global tooltip frame used by virtually all addons
        // Created as a GameTooltip, not a Frame, so it is one as far as the
        // widget tree is concerned and its lines are drawn.
        //
        // The lines used to be kept in a table here, with SetOwner, AddLine,
        // AddDoubleLine, SetText and ClearLines defined directly on this
        // table — and a field on the table beats the metatable, so those five
        // shadowed the real implementations for the one tooltip that matters
        // most. They stored text nothing draws.
        "GameTooltip = CreateFrame('GameTooltip', 'GameTooltip')\n"
        "GameTooltip.__lines = {}\n"
        // SetHyperlinkCompareItem(link, index, shift, anchor) — the tooltip
        // that appears beside an item when Shift is held.
        //
        // GameTooltip_ShowCompareItem calls this on each of the three shopping
        // tooltips in turn and shows the ones that answer true. It was never
        // implemented, so every one answered nil and nothing was ever shown:
        // shift-hovering an item did nothing at all, silently.
        //
        // The index picks which of the equipped counterparts to show, which
        // matters only for the slots there are two of. A ring is compared
        // against both rings, a trinket against both trinkets, a one-hander
        // against main and off hand; everything else has one slot and answers
        // false for index 2.
        "__WoweeCompareSlots = {\n"
        "    INVTYPE_FINGER = {11, 12},\n"
        "    INVTYPE_TRINKET = {13, 14},\n"
        "    INVTYPE_WEAPON = {16, 17},\n"
        "    INVTYPE_HEAD = {1}, INVTYPE_NECK = {2}, INVTYPE_SHOULDER = {3},\n"
        "    INVTYPE_BODY = {4}, INVTYPE_CHEST = {5}, INVTYPE_ROBE = {5},\n"
        "    INVTYPE_WAIST = {6}, INVTYPE_LEGS = {7}, INVTYPE_FEET = {8},\n"
        "    INVTYPE_WRIST = {9}, INVTYPE_HAND = {10}, INVTYPE_CLOAK = {15},\n"
        "    INVTYPE_2HWEAPON = {16}, INVTYPE_WEAPONMAINHAND = {16},\n"
        "    INVTYPE_WEAPONOFFHAND = {17}, INVTYPE_HOLDABLE = {17},\n"
        "    INVTYPE_SHIELD = {17}, INVTYPE_RANGED = {18},\n"
        "    INVTYPE_RANGEDRIGHT = {18}, INVTYPE_THROWN = {18},\n"
        "    INVTYPE_RELIC = {18}, INVTYPE_TABARD = {19},\n"
        "}\n"
        "function __WoweeFrameMT:SetHyperlinkCompareItem(link, index, shift, anchor)\n"
        "    self:ClearLines()\n"
        "    if not link then return false end\n"
        "    local id = tonumber(link:match('item:(%d+)'))\n"
        "    if not id then return false end\n"
        "    local _, _, _, _, _, _, _, _, equipSlot = GetItemInfo(id)\n"
        "    if not equipSlot or equipSlot == '' then return false end\n"
        "    local slots = __WoweeCompareSlots[equipSlot]\n"
        "    if not slots then return false end\n"
        "    local slot = slots[index or 1]\n"
        "    if not slot then return false end\n"
        // Nothing worn there is not a comparison, it is an empty tooltip —
        // and answering true for one would show a blank box beside the item.
        "    local wornLink = GetInventoryItemLink('player', slot)\n"
        "    if not wornLink then return false end\n"
        "    local wornId = tonumber(wornLink:match('item:(%d+)'))\n"
        "    if not wornId then return false end\n"
        // Comparing something against itself says nothing. WoW leaves the
        // second tooltip off when the item is already the one worn.
        "    if wornId == id then return false end\n"
        "    if not _WoweePopulateItemTooltip(self, wornId) then return false end\n"
        "    self:Show()\n"
        "    return true\n"
        "end\n"
        "function __WoweeFrameMT:GetItem()\n"
        "    if self.__itemId and self.__itemId > 0 then\n"
        "        local name = GetItemInfo(self.__itemId)\n"
        "        local _, itemLink = GetItemInfo(self.__itemId)\n"
        "        return name, itemLink or ('|cffffffff|Hitem:'..self.__itemId..':0|h['..tostring(name)..']|h|r')\n"
        "    end\n"
        "    return nil\n"
        "end\n"
        "function __WoweeFrameMT:GetSpell()\n"
        "    if self.__spellId and self.__spellId > 0 then\n"
        "        local name = GetSpellInfo(self.__spellId)\n"
        "        return name, nil, self.__spellId\n"
        "    end\n"
        "    return nil\n"
        "end\n"
        "function __WoweeFrameMT:GetUnit() return nil end\n"
        // NumLines and GetText come from the widget itself, which is where
        // the lines now live.
        "function __WoweeFrameMT:SetUnitBuff(unit, index, filter)\n"
        "    self:ClearLines()\n"
        "    local name, rank, icon, count, debuffType, duration, expTime, caster, steal, consolidate, spellId = UnitBuff(unit, index, filter)\n"
        "    if name then\n"
        "        self:SetText(name, 1, 1, 1)\n"
        "        if duration and duration > 0 then\n"
        "            self:AddLine(string.format('%.0f sec remaining', expTime - GetTime()), 1, 1, 1)\n"
        "        end\n"
        "        self.__spellId = spellId\n"
        "    end\n"
        "end\n"
        // The one the buff frame actually calls. SetUnitBuff and
        // SetUnitDebuff were written and this was left in the no-op
        // allowlist, so every buff and debuff on the default buff frame
        // hovered to an empty tooltip — buffframe.lua reaches for SetUnitAura
        // in both its handlers and neither of the two that exist.
        // Three more that were left in the no-op allowlist while everything
        // they need was bound. Each is a hover that wrote nothing: a quest
        // reward in the questgiver window, its spell reward, and an item on
        // either side of a trade.
        //
        // They route through the item link rather than rebuilding a tooltip,
        // because SetHyperlink already knows how to render one and the links
        // are what the getters hand back.
        // Two more tooltip methods, and these RAISE rather than answering a
        // no-op — they are in neither the method table nor the allowlist, so
        // the fallback returns nil and the call takes its handler with it.
        // Hovering a trainer's spell, or the item in the auction sell slot.
        // Hovering an item in loot, at a merchant, or in the mail. All six
        // sat in the no-op allowlist while every getter they need was bound,
        // so the windows worked and nothing in them had a tooltip — the same
        // shape as SetUnitAura, and the reason the allowlist is worth
        // auditing rather than trusting.
        //
        // Each prefers the item link, because SetHyperlink already renders one
        // and a link is what these getters hand back.
        "function __WoweeFrameMT:SetLootItem(slot)\n"
        "    self:ClearLines()\n"
        "    local link = GetLootSlotLink and GetLootSlotLink(slot)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local _, name = GetLootSlotInfo(slot)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetLootRollItem(rollId)\n"
        "    self:ClearLines()\n"
        "    local link = GetLootRollItemLink and GetLootRollItemLink(rollId)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "end\n"
        "function __WoweeFrameMT:SetMerchantItem(index)\n"
        "    self:ClearLines()\n"
        "    local link = GetMerchantItemLink and GetMerchantItemLink(index)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local name = GetMerchantItemInfo(index)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetBuybackItem(index)\n"
        "    self:ClearLines()\n"
        "    local link = GetBuybackItemLink and GetBuybackItemLink(index)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local name = GetBuybackItemInfo(index)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetInboxItem(index, attachIndex)\n"
        "    self:ClearLines()\n"
        "    local link = GetInboxItemLink and GetInboxItemLink(index, attachIndex)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local name = GetInboxItem(index, attachIndex)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetSendMailItem(index)\n"
        "    self:ClearLines()\n"
        "    local name = GetSendMailItem(index)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        // A totem's tooltip is its spell's, and the totem bar is drawn by
        // whichever side owns it.
        "function __WoweeFrameMT:SetTotem(slot)\n"
        "    self:ClearLines()\n"
        "    local _, name, _, duration = GetTotemInfo(slot)\n"
        "    if not name or name == '' then return end\n"
        "    self:SetText(name, 1, 1, 1)\n"
        "    if duration and duration > 0 then\n"
        "        self:AddLine(string.format('%.0f sec remaining', duration), 1, 1, 1)\n"
        "    end\n"
        "end\n"
        "function __WoweeFrameMT:SetTrainerService(index)\n"
        "    self:ClearLines()\n"
        "    if not index then return end\n"
        "    local link = GetTrainerServiceItemLink and GetTrainerServiceItemLink(index)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local name, subText = GetTrainerServiceInfo(index)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "    if subText and subText ~= '' then self:AddLine(subText, 0.5, 0.5, 0.5) end\n"
        "end\n"
        "function __WoweeFrameMT:SetAuctionSellItem()\n"
        "    self:ClearLines()\n"
        "    local name, _, count, quality = GetAuctionSellItemInfo()\n"
        "    if not name then return end\n"
        "    local r, g, b = GetItemQualityColor(quality or 1)\n"
        "    self:SetText(name, r, g, b)\n"
        "    if count and count > 1 then self:AddLine(count .. ' in stack', 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetQuestLogItem(itemType, index)\n"
        "    self:ClearLines()\n"
        "    local link = GetQuestLogItemLink and GetQuestLogItemLink(itemType, index)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local name = GetQuestItemInfo(itemType, index)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetQuestItem(itemType, index)\n"
        "    self:ClearLines()\n"
        "    local link = GetQuestItemLink and GetQuestItemLink(itemType, index)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local name = GetQuestItemInfo(itemType, index)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetQuestRewardSpell()\n"
        "    self:ClearLines()\n"
        "    local _, _, _, name = GetRewardSpell()\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetTradePlayerItem(index)\n"
        "    self:ClearLines()\n"
        "    local link = GetTradePlayerItemLink and GetTradePlayerItemLink(index)\n"
        "    if link then return self:SetHyperlink(link) end\n"
        "    local name = GetTradePlayerItemInfo(index)\n"
        "    if name then self:SetText(name, 1, 1, 1) end\n"
        "end\n"
        "function __WoweeFrameMT:SetUnitAura(unit, index, filter)\n"
        "    self:ClearLines()\n"
        "    local name, rank, icon, count, debuffType, duration, expTime, caster, steal, consolidate, spellId = UnitAura(unit, index, filter)\n"
        "    if not name then return end\n"
        // Harmful auras name their school and are titled in red; a buff is
        // white. filter is what the caller asked for, debuffType is what came
        // back, and either is enough to tell them apart.
        "    local harmful = debuffType ~= nil or (filter and string.find(filter, 'HARMFUL'))\n"
        "    if harmful then self:SetText(name, 1, 0, 0) else self:SetText(name, 1, 1, 1) end\n"
        "    if debuffType then self:AddLine(debuffType, 0.5, 0.5, 0.5) end\n"
        "    if count and count > 1 then self:AddLine(count .. ' stacks', 1, 1, 1) end\n"
        "    if duration and duration > 0 and expTime then\n"
        "        self:AddLine(string.format('%.0f sec remaining', expTime - GetTime()), 1, 1, 1)\n"
        "    end\n"
        "    self.__spellId = spellId\n"
        "end\n"
        "function __WoweeFrameMT:SetUnitDebuff(unit, index, filter)\n"
        "    self:ClearLines()\n"
        "    local name, rank, icon, count, debuffType, duration, expTime, caster, steal, consolidate, spellId = UnitDebuff(unit, index, filter)\n"
        "    if name then\n"
        "        self:SetText(name, 1, 0, 0)\n"
        "        if debuffType then self:AddLine(debuffType, 0.5, 0.5, 0.5) end\n"
        "        self.__spellId = spellId\n"
        "    end\n"
        "end\n"
        "function __WoweeFrameMT:SetHyperlink(link)\n"
        "    self:ClearLines()\n"
        "    if not link then return end\n"
        "    local id = link:match('item:(%d+)')\n"
        "    if id then\n"
        "        _WoweePopulateItemTooltip(self, tonumber(id))\n"
        "        return\n"
        "    end\n"
        "    id = link:match('spell:(%d+)')\n"
        "    if id then\n"
        "        self:SetSpellByID(tonumber(id))\n"
        "        return\n"
        "    end\n"
        "end\n"
        // Shared item tooltip builder using GetItemInfo return values
        "function _WoweePopulateItemTooltip(self, itemId)\n"
        "    local name, itemLink, quality, iLevel, reqLevel, class, subclass, maxStack, equipSlot, texture, sellPrice = GetItemInfo(itemId)\n"
        "    if not name then return false end\n"
        "    local qColors = {[0]={0.62,0.62,0.62},[1]={1,1,1},[2]={0.12,1,0},[3]={0,0.44,0.87},[4]={0.64,0.21,0.93},[5]={1,0.5,0},[6]={0.9,0.8,0.5},[7]={0,0.8,1}}\n"
        "    local c = qColors[quality or 1] or {1,1,1}\n"
        "    self:SetText(name, c[1], c[2], c[3])\n"
        "    -- Item level for equipment\n"
        "    if equipSlot and equipSlot ~= '' and iLevel and iLevel > 0 then\n"
        "        self:AddLine('Item Level '..iLevel, 1, 0.82, 0)\n"
        "    end\n"
        "    -- Equip slot and subclass on same line\n"
        "    if equipSlot and equipSlot ~= '' then\n"
        "        local slotNames = {INVTYPE_HEAD='Head',INVTYPE_NECK='Neck',INVTYPE_SHOULDER='Shoulder',\n"
        "            INVTYPE_CHEST='Chest',INVTYPE_WAIST='Waist',INVTYPE_LEGS='Legs',INVTYPE_FEET='Feet',\n"
        "            INVTYPE_WRIST='Wrist',INVTYPE_HAND='Hands',INVTYPE_FINGER='Finger',\n"
        "            INVTYPE_TRINKET='Trinket',INVTYPE_CLOAK='Back',INVTYPE_WEAPON='One-Hand',\n"
        "            INVTYPE_SHIELD='Off Hand',INVTYPE_2HWEAPON='Two-Hand',INVTYPE_RANGED='Ranged',\n"
        "            INVTYPE_WEAPONMAINHAND='Main Hand',INVTYPE_WEAPONOFFHAND='Off Hand',\n"
        "            INVTYPE_HOLDABLE='Held In Off-Hand',INVTYPE_TABARD='Tabard',INVTYPE_ROBE='Chest'}\n"
        "        local slotText = slotNames[equipSlot] or ''\n"
        "        local subText = (subclass and subclass ~= '') and subclass or ''\n"
        "        if slotText ~= '' or subText ~= '' then\n"
        "            self:AddDoubleLine(slotText, subText, 1,1,1, 1,1,1)\n"
        "        end\n"
        "    elseif class and class ~= '' then\n"
        "        self:AddLine(class, 1, 1, 1)\n"
        "    end\n"
        "    -- Fetch detailed stats from C side\n"
        "    local data = _GetItemTooltipData(itemId)\n"
        "    if data then\n"
        "        -- Bind type\n"
        "        if data.isHeroic then self:AddLine('Heroic', 0, 1, 0) end\n"
        "        if data.isUnique then self:AddLine('Unique', 1, 1, 1)\n"
        "        elseif data.isUniqueEquipped then self:AddLine('Unique-Equipped', 1, 1, 1) end\n"
        "        if data.bindType == 1 then self:AddLine('Binds when picked up', 1, 1, 1)\n"
        "        elseif data.bindType == 2 then self:AddLine('Binds when equipped', 1, 1, 1)\n"
        "        elseif data.bindType == 3 then self:AddLine('Binds when used', 1, 1, 1) end\n"
        "        -- Armor\n"
        "        if data.armor and data.armor > 0 then\n"
        "            self:AddLine(data.armor..' Armor', 1, 1, 1)\n"
        "        end\n"
        "        -- Weapon damage and speed\n"
        "        if data.damageMin and data.damageMax and data.damageMin > 0 then\n"
        "            local speed = (data.speed or 0) / 1000\n"
        "            if speed > 0 then\n"
        "                self:AddDoubleLine(string.format('%.0f - %.0f Damage', data.damageMin, data.damageMax), string.format('Speed %.2f', speed), 1,1,1, 1,1,1)\n"
        "                local dps = (data.damageMin + data.damageMax) / 2 / speed\n"
        "                self:AddLine(string.format('(%.1f damage per second)', dps), 1, 1, 1)\n"
        "            end\n"
        "        end\n"
        "        -- Stats\n"
        "        if data.stamina then self:AddLine('+'..data.stamina..' Stamina', 0, 1, 0) end\n"
        "        if data.strength then self:AddLine('+'..data.strength..' Strength', 0, 1, 0) end\n"
        "        if data.agility then self:AddLine('+'..data.agility..' Agility', 0, 1, 0) end\n"
        "        if data.intellect then self:AddLine('+'..data.intellect..' Intellect', 0, 1, 0) end\n"
        "        if data.spirit then self:AddLine('+'..data.spirit..' Spirit', 0, 1, 0) end\n"
        "        -- Extra stats (hit, crit, haste, AP, SP, etc.)\n"
        "        if data.extraStats then\n"
        "            local statNames = {[3]='Agility',[4]='Strength',[5]='Intellect',[6]='Spirit',[7]='Stamina',\n"
        "                [12]='Defense Rating',[13]='Dodge Rating',[14]='Parry Rating',[15]='Block Rating',\n"
        "                [16]='Melee Hit Rating',[17]='Ranged Hit Rating',[18]='Spell Hit Rating',\n"
        "                [19]='Melee Crit Rating',[20]='Ranged Crit Rating',[21]='Spell Crit Rating',\n"
        "                [28]='Melee Haste Rating',[29]='Ranged Haste Rating',[30]='Spell Haste Rating',\n"
        "                [31]='Hit Rating',[32]='Crit Rating',[36]='Haste Rating',\n"
        "                [33]='Resilience Rating',[34]='Attack Power',[35]='Spell Power',\n"
        "                [37]='Expertise Rating',[38]='Attack Power',[39]='Ranged Attack Power',\n"
        "                [43]='Mana per 5 sec.',[44]='Armor Penetration Rating',\n"
        "                [45]='Spell Power',[46]='Health per 5 sec.',[47]='Spell Penetration'}\n"
        "            for _, stat in ipairs(data.extraStats) do\n"
        "                local name = statNames[stat.type]\n"
        "                if name and stat.value ~= 0 then\n"
        "                    local prefix = stat.value > 0 and '+' or ''\n"
        "                    self:AddLine(prefix..stat.value..' '..name, 0, 1, 0)\n"
        "                end\n"
        "            end\n"
        "        end\n"
        "        -- Resistances\n"
        "        if data.fireRes and data.fireRes ~= 0 then self:AddLine('+'..data.fireRes..' Fire Resistance', 0, 1, 0) end\n"
        "        if data.natureRes and data.natureRes ~= 0 then self:AddLine('+'..data.natureRes..' Nature Resistance', 0, 1, 0) end\n"
        "        if data.frostRes and data.frostRes ~= 0 then self:AddLine('+'..data.frostRes..' Frost Resistance', 0, 1, 0) end\n"
        "        if data.shadowRes and data.shadowRes ~= 0 then self:AddLine('+'..data.shadowRes..' Shadow Resistance', 0, 1, 0) end\n"
        "        if data.arcaneRes and data.arcaneRes ~= 0 then self:AddLine('+'..data.arcaneRes..' Arcane Resistance', 0, 1, 0) end\n"
        "        -- Item spell effects (Use: / Equip: / Chance on Hit:)\n"
        "        if data.itemSpells then\n"
        "            local triggerLabels = {[0]='Use: ',[1]='Equip: ',[2]='Chance on hit: ',[5]=''}\n"
        "            for _, sp in ipairs(data.itemSpells) do\n"
        "                local label = triggerLabels[sp.trigger] or ''\n"
        "                local text = sp.description or sp.name or ''\n"
        "                if text ~= '' then\n"
        "                    self:AddLine(label .. text, 0, 1, 0)\n"
        "                end\n"
        "            end\n"
        "        end\n"
        "        -- Gem sockets\n"
        "        if data.sockets then\n"
        "            local socketNames = {[1]='Meta',[2]='Red',[4]='Yellow',[8]='Blue'}\n"
        "            for _, sock in ipairs(data.sockets) do\n"
        "                local colorName = socketNames[sock.color] or 'Prismatic'\n"
        "                self:AddLine('[' .. colorName .. ' Socket]', 0.5, 0.5, 0.5)\n"
        "            end\n"
        "        end\n"
        "        -- Required level\n"
        "        if data.requiredLevel and data.requiredLevel > 1 then\n"
        "            self:AddLine('Requires Level '..data.requiredLevel, 1, 1, 1)\n"
        "        end\n"
        "        -- Flavor text\n"
        "        if data.description then self:AddLine('\"'..data.description..'\"', 1, 0.82, 0) end\n"
        "        if data.startsQuest then self:AddLine('This Item Begins a Quest', 1, 0.82, 0) end\n"
        "    end\n"
        "    -- Sell price from GetItemInfo\n"
        "    if sellPrice and sellPrice > 0 then\n"
        "        local gold = math.floor(sellPrice / 10000)\n"
        "        local silver = math.floor((sellPrice % 10000) / 100)\n"
        "        local copper = sellPrice % 100\n"
        "        local parts = {}\n"
        "        if gold > 0 then table.insert(parts, gold..'g') end\n"
        "        if silver > 0 then table.insert(parts, silver..'s') end\n"
        "        if copper > 0 then table.insert(parts, copper..'c') end\n"
        "        if #parts > 0 then self:AddLine('Sell Price: '..table.concat(parts, ' '), 1, 1, 1) end\n"
        "    end\n"
        "    self.__itemId = itemId\n"
        "    return true\n"
        "end\n"
        "function __WoweeFrameMT:SetInventoryItem(unit, slot)\n"
        "    self:ClearLines()\n"
        "    if unit ~= 'player' then return false, false, 0 end\n"
        "    local link = GetInventoryItemLink(unit, slot)\n"
        "    if not link then return false, false, 0 end\n"
        "    local id = link:match('item:(%d+)')\n"
        "    if not id then return false, false, 0 end\n"
        "    local ok = _WoweePopulateItemTooltip(self, tonumber(id))\n"
        "    return ok or false, false, 0\n"
        "end\n"
        "function __WoweeFrameMT:SetBagItem(bag, slot)\n"
        "    self:ClearLines()\n"
        "    local tex, count, locked, quality, readable, lootable, link = GetContainerItemInfo(bag, slot)\n"
        "    if not link then return end\n"
        "    local id = link:match('item:(%d+)')\n"
        "    if not id then return end\n"
        "    _WoweePopulateItemTooltip(self, tonumber(id))\n"
        "    if count and count > 1 then self:AddLine('Count: '..count, 0.5, 0.5, 0.5) end\n"
        "end\n"
        "function __WoweeFrameMT:SetSpellByID(spellId)\n"
        "    self:ClearLines()\n"
        "    if not spellId or spellId == 0 then return end\n"
        // Nine values, in the client's order. This used to read the fourth as
        // a cast time, which is where the cost is — so every spell tooltip
        // printed its mana cost as a cast time in seconds.
        "    local name, rank, icon, _cost, _isFunnel, _powerType, castTime, minRange, maxRange = GetSpellInfo(spellId)\n"
        "    if name then\n"
        "        self:SetText(name, 1, 1, 1)\n"
        "        if rank and rank ~= '' then self:AddLine(rank, 0.5, 0.5, 0.5) end\n"
        "        -- Mana cost\n"
        "        local cost, costType = GetSpellPowerCost(spellId)\n"
        "        if cost and cost > 0 then\n"
        "            local powerNames = {[0]='Mana',[1]='Rage',[2]='Focus',[3]='Energy',[6]='Runic Power'}\n"
        "            self:AddLine(cost..' '..(powerNames[costType] or 'Mana'), 1, 1, 1)\n"
        "        end\n"
        "        -- Range\n"
        "        if maxRange and maxRange > 0 then\n"
        "            self:AddDoubleLine(string.format('%.0f yd range', maxRange), '', 1,1,1, 1,1,1)\n"
        "        end\n"
        "        -- Cast time\n"
        "        if castTime and castTime > 0 then\n"
        "            self:AddDoubleLine(string.format('%.1f sec cast', castTime / 1000), '', 1,1,1, 1,1,1)\n"
        "        else\n"
        "            self:AddDoubleLine('Instant', '', 1,1,1, 1,1,1)\n"
        "        end\n"
        "        -- Description\n"
        "        local desc = GetSpellDescription(spellId)\n"
        "        if desc and desc ~= '' then\n"
        "            self:AddLine(desc, 1, 0.82, 0)\n"
        "        end\n"
        "        -- Cooldown\n"
        "        local start, dur = GetSpellCooldown(spellId)\n"
        "        if dur and dur > 0 then\n"
        "            local rem = start + dur - GetTime()\n"
        "            if rem > 0.1 then self:AddLine(string.format('%.0f sec cooldown', rem), 1, 0, 0) end\n"
        "        end\n"
        "        self.__spellId = spellId\n"
        "    end\n"
        "end\n"
        // Answers whether it filled anything, because the caller asks:
        // ActionButton_SetTooltip is `if (GameTooltip:SetAction(self.action))`,
        // and returning nothing sent every action button down its no-tooltip
        // branch however much the tooltip itself could do.
        "function __WoweeFrameMT:SetAction(slot)\n"
        "    self:ClearLines()\n"
        "    if not slot then return false end\n"
        "    local actionType, id = GetActionInfo(slot)\n"
        "    if actionType == 'spell' and id and id > 0 then\n"
        "        self:SetSpellByID(id)\n"
        "        return self:NumLines() > 0\n"
        "    elseif actionType == 'item' and id and id > 0 then\n"
        "        _WoweePopulateItemTooltip(self, id)\n"
        "        return self:NumLines() > 0\n"
        "    end\n"
        "    return false\n"
        "end\n"
        "function __WoweeFrameMT:FadeOut() end\n"
        // SetFrameStrata is a real binding; a no-op here would shadow it and
        // leave the tooltip in whatever stratum it inherited, under the frames
        // it is meant to sit above.
        // Not a no-op here, for the reason given directly above: SetClampedToScreen
        // is a real binding, and the empty one that used to sit on this line
        // shadowed it. Nothing then ever set the flag — so the clamp in
        // layoutWidget, which exists precisely to keep a tooltip anchored near
        // an edge from running off it, could not fire, because its condition
        // was never true. GameTooltipTemplate declares clampedToScreen="true"
        // and the emitter turns that into the very call that was being
        // swallowed.
        // On the frame metatable rather than on GameTooltip, because a tooltip
        // is not always that one — item comparison uses ShoppingTooltip1 and 2
        // — and a copy on the table itself would shadow this for no gain.
        // Named in full rather than through the local `mt`, which belongs to a
        // different bootstrap chunk: referring to it here left the chunk
        // failing to load, so these two never existed — and with them removed
        // from the no-op list at the same time, nothing answered IsOwned at
        // all. FrameXML calls it from CursorOnUpdate, so it raised every frame
        // until the device went down.
        "function __WoweeFrameMT:GetOwner() return rawget(self, '__owner') end\n"
        "function __WoweeFrameMT:IsOwned(f) return rawget(self, '__owner') == f end\n"
        // ShoppingTooltip: used by comparison tooltips
        "ShoppingTooltip1 = CreateFrame('Frame', 'ShoppingTooltip1')\n"
        "ShoppingTooltip2 = CreateFrame('Frame', 'ShoppingTooltip2')\n"
        // Error handling stubs (used by many addons)
        "local _errorHandler = function(err) return err end\n"
        "function geterrorhandler() return _errorHandler end\n"
        "function seterrorhandler(fn) if type(fn)=='function' then _errorHandler=fn end end\n"
        "function debugstack(start, count1, count2) return '' end\n"
        // A name is as valid as a function here, and FrameXML mostly passes a
        // name: UIDropDownMenu_Initialize does
        // securecall("UIDropDownMenu_InitializeHelper", frame), and the helper
        // is what sets UIDROPDOWNMENU_INIT_MENU and zeroes every list's
        // numButtons. Accepting only a function meant that call did nothing at
        // all, silently, and eight files died further on indexing what it
        // should have set.
        //
        // rawget, so a name this client does not have stays nil rather than
        // becoming the missing-API object, which is not callable as a function.
        "function securecall(fn, ...)\n"
        "    if type(fn) == 'string' then fn = rawget(_G, fn) end\n"
        "    if type(fn) == 'function' then return fn(...) end\n"
        "end\n"
        // Iterating a table the secure way, which for our purposes is next.
        "SecureNext = next\n"
        "function issecurevariable(...) return false end\n"
        "function issecure() return false end\n"
        // GetCVarBool wraps C-side GetCVar (registered in table) for boolean queries
        // Misc compatibility stubs
        // GetScreenWidth, GetScreenHeight, GetNumLootItems are now C functions
        // GetFramerate is now a C function
        "function IsLoggedIn() return true end\n"
        "function StaticPopup_Show() end\n"
        "function StaticPopup_Hide() end\n"
        // UI Panel management — Show/Hide standard WoW panels
        "UIPanelWindows = {}\n"
        "function ShowUIPanel(frame, force)\n"
        "    if frame and frame.Show then frame:Show() end\n"
        "end\n"
        "function HideUIPanel(frame)\n"
        "    if frame and frame.Hide then frame:Hide() end\n"
        "end\n"
        "function ToggleFrame(frame)\n"
        "    if frame then\n"
        "        if frame:IsShown() then frame:Hide() else frame:Show() end\n"
        "    end\n"
        "end\n"
        "function GetUIPanel(which) return nil end\n"
        "function CloseWindows(ignoreCenter) return false end\n"
        // TEXT localization stub — returns input string unchanged
        "function TEXT(text) return text end\n"
        // Faux scroll frame helpers (used by many list UIs)
        "function FauxScrollFrame_GetOffset(frame)\n"
        "    return frame and frame.offset or 0\n"
        "end\n"
        "function FauxScrollFrame_Update(frame, numItems, numVisible, valueStep, button, smallWidth, bigWidth, highlightFrame, smallHighlightWidth, bigHighlightWidth)\n"
        "    if not frame then return false end\n"
        "    frame.offset = frame.offset or 0\n"
        "    local showScrollBar = numItems > numVisible\n"
        "    return showScrollBar\n"
        "end\n"
        "function FauxScrollFrame_SetOffset(frame, offset)\n"
        "    if frame then frame.offset = offset or 0 end\n"
        "end\n"
        "function FauxScrollFrame_OnVerticalScroll(frame, value, itemHeight, updateFunction)\n"
        "    if not frame then return end\n"
        "    frame.offset = math.floor(value / (itemHeight or 1) + 0.5)\n"
        "    if updateFunction then updateFunction() end\n"
        "end\n"
        // SecureCmdOptionParse — parses conditional macros like [target=focus]
        "function SecureCmdOptionParse(options)\n"
        "    if not options then return nil end\n"
        "    -- Simple: return the unconditional fallback (text after last semicolon or the whole string)\n"
        "    local result = options:match(';%s*(.-)$') or options:match('^%[.*%]%s*(.-)$') or options\n"
        "    return result\n"
        "end\n"
        // ChatFrame message group stubs
        "function ChatFrame_AddMessageGroup(frame, group) end\n"
        "function ChatFrame_RemoveMessageGroup(frame, group) end\n"
        "function ChatFrame_AddChannel(frame, channel) end\n"
        "function ChatFrame_RemoveChannel(frame, channel) end\n"
        // CreateTexture/CreateFontString are now C frame methods in the metatable
        "do\n"
        "  local function cc(r,g,b)\n"
        "    local t = {r=r, g=g, b=b}\n"
        "    t.colorStr = string.format('%02x%02x%02x', math.floor(r*255), math.floor(g*255), math.floor(b*255))\n"
        "    function t:GenerateHexColor() return '|cff' .. self.colorStr end\n"
        "    function t:GenerateHexColorMarkup() return '|cff' .. self.colorStr end\n"
        "    return t\n"
        "  end\n"
        "  RAID_CLASS_COLORS = {\n"
        "    WARRIOR=cc(0.78,0.61,0.43), PALADIN=cc(0.96,0.55,0.73),\n"
        "    HUNTER=cc(0.67,0.83,0.45), ROGUE=cc(1.0,0.96,0.41),\n"
        "    PRIEST=cc(1.0,1.0,1.0), DEATHKNIGHT=cc(0.77,0.12,0.23),\n"
        "    SHAMAN=cc(0.0,0.44,0.87), MAGE=cc(0.41,0.80,0.94),\n"
        "    WARLOCK=cc(0.58,0.51,0.79), DRUID=cc(1.0,0.49,0.04),\n"
        "  }\n"
        "end\n"
        // GetClassColor(className) — returns r, g, b, colorString
        "function GetClassColor(className)\n"
        "    local c = RAID_CLASS_COLORS[className]\n"
        "    if c then return c.r, c.g, c.b, c.colorStr end\n"
        "    return 1, 1, 1, 'ffffffff'\n"
        "end\n"
        // QuestDifficultyColors table for quest level coloring
        "QuestDifficultyColors = {\n"
        "    impossible = {r=1.0,g=0.1,b=0.1,font='QuestDifficulty_Impossible'},\n"
        "    verydifficult = {r=1.0,g=0.5,b=0.25,font='QuestDifficulty_VeryDifficult'},\n"
        "    difficult = {r=1.0,g=1.0,b=0.0,font='QuestDifficulty_Difficult'},\n"
        "    standard = {r=0.25,g=0.75,b=0.25,font='QuestDifficulty_Standard'},\n"
        "    trivial = {r=0.5,g=0.5,b=0.5,font='QuestDifficulty_Trivial'},\n"
        "    header = {r=1.0,g=0.82,b=0.0,font='QuestDifficulty_Header'},\n"
        "}\n"
        // Money formatting utility
        "function GetCoinTextureString(copper)\n"
        "    if not copper or copper == 0 then return '0c' end\n"
        "    copper = math.floor(copper)\n"
        "    local g = math.floor(copper / 10000)\n"
        "    local s = math.floor(math.fmod(copper, 10000) / 100)\n"
        "    local c = math.fmod(copper, 100)\n"
        "    local r = ''\n"
        "    if g > 0 then r = r .. g .. 'g ' end\n"
        "    if s > 0 then r = r .. s .. 's ' end\n"
        "    if c > 0 or r == '' then r = r .. c .. 'c' end\n"
        "    return r\n"
        "end\n"
        "GetCoinText = GetCoinTextureString\n"
    );

    // UIDropDownMenu framework — minimal compat for addons using dropdown menus
    bootstrap(
        "UIDROPDOWNMENU_MENU_LEVEL = 1\n"
        "UIDROPDOWNMENU_MENU_VALUE = nil\n"
        "UIDROPDOWNMENU_OPEN_MENU = nil\n"
        "local _ddMenuList = {}\n"
        "function UIDropDownMenu_Initialize(frame, initFunc, displayMode, level, menuList)\n"
        "    if frame then frame.__initFunc = initFunc end\n"
        "end\n"
        "function UIDropDownMenu_CreateInfo() return {} end\n"
        "function UIDropDownMenu_AddButton(info, level) table.insert(_ddMenuList, info) end\n"
        "function UIDropDownMenu_SetWidth(frame, width) end\n"
        "function UIDropDownMenu_SetButtonWidth(frame, width) end\n"
        "function UIDropDownMenu_SetText(frame, text)\n"
        "    if frame then frame.__text = text end\n"
        "end\n"
        "function UIDropDownMenu_GetText(frame)\n"
        "    return frame and frame.__text or ''\n"
        "end\n"
        "function UIDropDownMenu_SetSelectedID(frame, id) end\n"
        "function UIDropDownMenu_SetSelectedValue(frame, value) end\n"
        "function UIDropDownMenu_GetSelectedID(frame) return 1 end\n"
        "function UIDropDownMenu_GetSelectedValue(frame) return nil end\n"
        "function UIDropDownMenu_JustifyText(frame, justify) end\n"
        "function UIDropDownMenu_EnableDropDown(frame) end\n"
        "function UIDropDownMenu_DisableDropDown(frame) end\n"
        "function CloseDropDownMenus() end\n"
        "function ToggleDropDownMenu(level, value, frame, anchor, xOfs, yOfs) end\n"
    );

    // UISpecialFrames: frames in this list close on Escape key
    bootstrap(
        "UISpecialFrames = {}\n"
        // Shared font objects, carrying the height and colour a FontString takes
        // from them. They were empty tables, so inheriting one changed nothing
        // and every label came out the same size in the same colour — and
        // FrameXML inherits one more than three thousand times.
        //
        // The colours are Blizzard's: normal is the familiar gold, highlight is
        // white, disabled grey, and the quest fonts near-black on parchment.
        "local function font(h, r, g, b) return { height = h, r = r, g = g, b = b, a = 1 } end\n"
        "GameFontNormal            = font(12, 1.00, 0.82, 0.00)\n"
        "GameFontNormalSmall       = font(10, 1.00, 0.82, 0.00)\n"
        "GameFontNormalLarge       = font(16, 1.00, 0.82, 0.00)\n"
        "GameFontNormalHuge        = font(20, 1.00, 0.82, 0.00)\n"
        "GameFontHighlight         = font(12, 1.00, 1.00, 1.00)\n"
        "GameFontHighlightSmall    = font(10, 1.00, 1.00, 1.00)\n"
        "GameFontHighlightLarge    = font(16, 1.00, 1.00, 1.00)\n"
        "GameFontDisable           = font(12, 0.50, 0.50, 0.50)\n"
        "GameFontDisableSmall      = font(10, 0.50, 0.50, 0.50)\n"
        "GameFontDisableLarge      = font(16, 0.50, 0.50, 0.50)\n"
        "GameFontWhite             = font(12, 1.00, 1.00, 1.00)\n"
        "GameFontRed               = font(12, 1.00, 0.13, 0.13)\n"
        "GameFontGreen             = font(12, 0.13, 1.00, 0.13)\n"
        "NumberFontNormal          = font(12, 1.00, 1.00, 1.00)\n"
        "NumberFontNormalSmall     = font(10, 1.00, 1.00, 1.00)\n"
        "NumberFontNormalLarge     = font(16, 1.00, 1.00, 1.00)\n"
        "ChatFontNormal            = font(12, 1.00, 1.00, 1.00)\n"
        "SystemFont                = font(12, 1.00, 0.82, 0.00)\n"
        "SystemFontSmall           = font(10, 1.00, 0.82, 0.00)\n"
        "QuestFont                 = font(13, 0.18, 0.12, 0.06)\n"
        "QuestFontNormalSmall      = font(11, 0.18, 0.12, 0.06)\n"
        "QuestTitleFont            = font(15, 0.00, 0.00, 0.00)\n"
        "Tooltip_Med               = font(12, 1.00, 1.00, 1.00)\n"
        "Tooltip_Small             = font(10, 1.00, 1.00, 1.00)\n"
        // InterfaceOptionsFrame: addons register settings panels here
        "InterfaceOptionsFrame = CreateFrame('Frame', 'InterfaceOptionsFrame')\n"
        "InterfaceOptionsFramePanelContainer = CreateFrame('Frame', 'InterfaceOptionsFramePanelContainer')\n"
        "function InterfaceOptions_AddCategory(panel) end\n"
        "function InterfaceOptionsFrame_OpenToCategory(panel) end\n"
        // Commonly expected global tables
        "SLASH_RELOAD1 = '/reload'\n"
        "SLASH_RELOADUI1 = '/reloadui'\n"
        "GRAY_FONT_COLOR = {r=0.5,g=0.5,b=0.5}\n"
        "NORMAL_FONT_COLOR = {r=1.0,g=0.82,b=0.0}\n"
        "HIGHLIGHT_FONT_COLOR = {r=1.0,g=1.0,b=1.0}\n"
        "GREEN_FONT_COLOR = {r=0.1,g=1.0,b=0.1}\n"
        "RED_FONT_COLOR = {r=1.0,g=0.1,b=0.1}\n"
        // C_ChatInfo — addon message prefix API used by some addons
        "C_ChatInfo = C_ChatInfo or {}\n"
        "C_ChatInfo.RegisterAddonMessagePrefix = RegisterAddonMessagePrefix\n"
        "C_ChatInfo.IsAddonMessagePrefixRegistered = IsAddonMessagePrefixRegistered\n"
        "C_ChatInfo.SendAddonMessage = SendAddonMessage\n"
    );

    // Action bar constants and functions used by action bar addons
    bootstrap(
        "NUM_ACTIONBAR_BUTTONS = 12\n"
        "NUM_ACTIONBAR_PAGES = 6\n"
        "ACTION_BUTTON_SHOW_GRID_REASON_CVAR = 1\n"
        "ACTION_BUTTON_SHOW_GRID_REASON_EVENT = 2\n"
        // Action bar page tracking
        // GetActionBarPage and ChangeActionBarPage are bindings, and they
        // share their storage there. A second pair here against a local of its
        // own meant the two could disagree about what page the bar was on.
        // These names have real bindings, registered before this runs. A stub
        // here does not sit beside one — it replaces it, because the bootstrap
        // is later. GetPetActionInfo, GetNumShapeshiftForms and the rest had
        // working implementations that never ran once.
        //
        // Binding functions
        "function GetCurrentBindingSet() return 1 end\n"
        // Macro functions
        "function GetMacroBody(id) return nil end\n"
        "function GetMacroIndexByName(name) return 0 end\n"
        // Stance bar
        // Pet action bar
        "NUM_PET_ACTION_SLOTS = 10\n"
        // Common WoW constants used by many addons
        "MAX_TALENT_TABS = 3\n"
        "MAX_NUM_TALENTS = 100\n"
        "BOOKTYPE_SPELL = 0\n"
        "BOOKTYPE_PET = 1\n"
        "MAX_PARTY_MEMBERS = 4\n"
        "MAX_RAID_MEMBERS = 40\n"
        "MAX_ARENA_TEAMS = 3\n"
        "INVSLOT_FIRST_EQUIPPED = 1\n"
        "INVSLOT_LAST_EQUIPPED = 19\n"
        "NUM_BAG_SLOTS = 4\n"
        "NUM_BANKBAGSLOTS = 7\n"
        "CONTAINER_BAG_OFFSET = 0\n"
        "MAX_SKILLLINE_TABS = 8\n"
        "TRADE_ENCHANT_SLOT = 7\n"
        "function GetPetActionsUsable() return false end\n"
    );

    // WoW table/string utility functions used by many addons
    bootstrap(
        // Table utilities
        "function tContains(tbl, item)\n"
        "    for _, v in pairs(tbl) do if v == item then return true end end\n"
        "    return false\n"
        "end\n"
        "function tInvert(tbl)\n"
        "    local inv = {}\n"
        "    for k, v in pairs(tbl) do inv[v] = k end\n"
        "    return inv\n"
        "end\n"
        "function CopyTable(src)\n"
        "    if type(src) ~= 'table' then return src end\n"
        "    local copy = {}\n"
        "    for k, v in pairs(src) do copy[k] = CopyTable(v) end\n"
        "    return setmetatable(copy, getmetatable(src))\n"
        "end\n"
        "function tDeleteItem(tbl, item)\n"
        "    for i = #tbl, 1, -1 do if tbl[i] == item then table.remove(tbl, i) end end\n"
        "end\n"
        // Mixin pattern — used by modern addons for OOP-style object creation
        "function Mixin(obj, ...)\n"
        "    for i = 1, select('#', ...) do\n"
        "        local mixin = select(i, ...)\n"
        "        for k, v in pairs(mixin) do obj[k] = v end\n"
        "    end\n"
        "    return obj\n"
        "end\n"
        "function CreateFromMixins(...)\n"
        "    return Mixin({}, ...)\n"
        "end\n"
        "function CreateAndInitFromMixin(mixin, ...)\n"
        "    local obj = CreateFromMixins(mixin)\n"
        "    if obj.Init then obj:Init(...) end\n"
        "    return obj\n"
        "end\n"
        "function MergeTable(dest, src)\n"
        "    for k, v in pairs(src) do dest[k] = v end\n"
        "    return dest\n"
        "end\n"
        // String utilities (WoW globals that alias Lua string functions)
        "strupper = string.upper\n"
        "strlower = string.lower\n"
        "strfind = string.find\n"
        "strsub = string.sub\n"
        "strlen = string.len\n"
        "strrep = string.rep\n"
        "strbyte = string.byte\n"
        "strchar = string.char\n"
        "strgfind = string.gmatch\n"
        "function tostringall(...)\n"
        "    local n = select('#', ...)\n"
        "    if n == 0 then return end\n"
        "    local r = {}\n"
        "    for i = 1, n do r[i] = tostring(select(i, ...)) end\n"
        "    return unpack(r, 1, n)\n"
        "end\n"
        "strrev = string.reverse\n"
        "gsub = string.gsub\n"
        "gmatch = string.gmatch\n"
        "strjoin = function(delim, ...)\n"
        "    return table.concat({...}, delim)\n"
        "end\n"
        // Math utilities
        "function Clamp(val, lo, hi) return math.min(math.max(val, lo), hi) end\n"
        "function Round(val) return math.floor(val + 0.5) end\n"
        // Bit operations (WoW provides these; Lua 5.1 doesn't have native bit ops)
        "bit = bit or {}\n"
        "bit.band = bit.band or function(a, b) local r,m=0,1 for i=0,31 do if a%2==1 and b%2==1 then r=r+m end a=math.floor(a/2) b=math.floor(b/2) m=m*2 end return r end\n"
        "bit.bor = bit.bor or function(a, b) local r,m=0,1 for i=0,31 do if a%2==1 or b%2==1 then r=r+m end a=math.floor(a/2) b=math.floor(b/2) m=m*2 end return r end\n"
        "bit.bxor = bit.bxor or function(a, b) local r,m=0,1 for i=0,31 do if (a%2==1)~=(b%2==1) then r=r+m end a=math.floor(a/2) b=math.floor(b/2) m=m*2 end return r end\n"
        "bit.bnot = bit.bnot or function(a) return 4294967295 - a end\n"
        "bit.lshift = bit.lshift or function(a, n) return a * (2^n) end\n"
        "bit.rshift = bit.rshift or function(a, n) return math.floor(a / (2^n)) end\n"
    );
}

// ---- Event System ----
// Lua-side: WoweeEvents table holds { ["EVENT_NAME"] = { handler1, handler2, ... } }
// RegisterEvent("EVENT", handler) adds a handler function
// UnregisterEvent("EVENT", handler) removes it


static int lua_RegisterEvent(lua_State* L) {
    const char* eventName = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // Get or create the WoweeEvents table
    lua_getglobal(L, "__WoweeEvents");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "__WoweeEvents");
    }

    // Get or create the handler list for this event
    lua_getfield(L, -1, eventName);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, eventName);
    }

    // Append the handler function to the list
    int len = static_cast<int>(lua_objlen(L, -1));
    lua_pushvalue(L, 2);  // push the handler function
    lua_rawseti(L, -2, len + 1);

    lua_pop(L, 2);  // pop handler list + WoweeEvents
    return 0;
}

static int lua_UnregisterEvent(lua_State* L) {
    const char* eventName = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_getglobal(L, "__WoweeEvents");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return 0; }

    lua_getfield(L, -1, eventName);
    if (lua_isnil(L, -1)) { lua_pop(L, 2); return 0; }

    // Remove matching handler from the list
    int len = static_cast<int>(lua_objlen(L, -1));
    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, -1, i);
        if (lua_rawequal(L, -1, 2)) {
            lua_pop(L, 1);
            // Shift remaining elements down
            for (int j = i; j < len; j++) {
                lua_rawgeti(L, -1, j + 1);
                lua_rawseti(L, -2, j);
            }
            lua_pushnil(L);
            lua_rawseti(L, -2, len);
            break;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
    return 0;
}

void LuaEngine::registerEventAPI() {
    lua_pushcfunction(L_, lua_RegisterEvent);
    lua_setglobal(L_, "RegisterEvent");

    lua_pushcfunction(L_, lua_UnregisterEvent);
    lua_setglobal(L_, "UnregisterEvent");

    // Create the events table
    lua_newtable(L_);
    lua_setglobal(L_, "__WoweeEvents");
}

namespace {
/// Pushes an event argument with the type WoW gives it.
///
/// Every argument crosses this boundary as a string, and some of them are
/// numbers on the other side: ChatFrame_MessageEventHandler does
/// `if (arg8 > 0)`, and comparing a string with a number is not false in Lua,
/// it is an error that takes the handler down — which for chat is every
/// message. Only a plain integer converts, so a name, a unit id and a hex guid
/// all stay strings.
void pushEventArg(lua_State* L, const std::string& arg) {
    if (!arg.empty() && arg.size() < 12) {
        size_t at = (arg[0] == '-') ? 1 : 0;
        if (at < arg.size()) {
            bool digits = true;
            // One decimal point is still a number. UPDATE_TICKET carries ages
            // and wait times measured in days, all of them fractions of one,
            // and the help frame compares them against zero — as a string that
            // is the same error this exists to avoid, not a wrong answer.
            int points = 0;
            for (size_t i = at; i < arg.size(); ++i) {
                if (arg[i] == '.' && points == 0 && i != at && i + 1 < arg.size()) {
                    ++points;
                    continue;
                }
                if (arg[i] < '0' || arg[i] > '9') { digits = false; break; }
            }
            // "007" is not a number anyone meant; a leading zero is a string,
            // except the one in front of a decimal point.
            const bool canonical = digits &&
                (arg.size() - at == 1 || arg[at] != '0' ||
                 (points == 1 && arg[at + 1] == '.'));
            if (canonical) {
                lua_pushnumber(L, std::stod(arg));
                return;
            }
        }
    }
    lua_pushstring(L, arg.c_str());
}
}  // namespace

void LuaEngine::fireEvent(const std::string& eventName,
                           const std::vector<std::string>& args) {
    if (!L_) return;

    // An event handler may cause another event, which is ordinary and has to
    // keep working — but a cycle between two of them recurses through both this
    // stack and Lua's, inside one frame, until the process dies. Reporting a
    // script error used to be such a cycle: the report fired an event, the
    // handler for it errored, and the error was reported the same way.
    //
    // Deep enough that no legitimate chain reaches it, and it says which event
    // it stopped, because the name is the only clue to which cycle it was.
    constexpr int kMaxEventDepth = 8;
    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& v) : d(v) { ++d; }
        ~DepthGuard() { --d; }
    } depthGuard{eventDepth_};
    if (eventDepth_ > kMaxEventDepth) {
        LOG_WARNING("Event '", eventName, "' is ", eventDepth_,
                    " deep and was dropped — handlers are triggering each other");
        return;
    }

    // Addon-side handlers, where there are any.
    //
    // Their absence is not a reason to stop. FrameXML registers through
    // frame:RegisterEvent, which fills __WoweeFrameEvents — a different table
    // entirely — and returning here meant every event no addon happened to
    // want was never delivered to the interface at all. PLAYER_TARGET_CHANGED
    // is one: fifty-four frames were registered for it, the client fired it,
    // and not one of them ever heard it, which is why the target frame stayed
    // hidden with a target that existed and resolved.
    lua_getglobal(L_, "__WoweeEvents");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, eventName.c_str());
        if (lua_istable(L_, -1)) {
            int handlerCount = static_cast<int>(lua_objlen(L_, -1));
            for (int i = 1; i <= handlerCount; i++) {
                lua_rawgeti(L_, -1, i);
                if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); continue; }

                // Push arguments: event name first, then extra args
                lua_pushstring(L_, eventName.c_str());
                for (const auto& arg : args) {
                    pushEventArg(L_, arg);
                }

                int nargs = 1 + static_cast<int>(args.size());
                if (lua_pcall(L_, nargs, 0, 0) != 0) {
                    const char* err = lua_tostring(L_, -1);
                    std::string errStr = err ? err : "(unknown)";
                    LOG_ERROR("LuaEngine: event '", eventName, "' handler error: ", errStr);
                    if (luaErrorCallback_) luaErrorCallback_(errStr);
                    lua_pop(L_, 1);
                }
            }
        }
        lua_pop(L_, 1);   // handler list, or whatever was there instead
    }
    lua_pop(L_, 1);       // __WoweeEvents, or whatever was there instead

    // Also dispatch to frames that registered for this event via frame:RegisterEvent()
    //
    // WOWEE_EVENT_TRACE names events to report, comma separated. An event that
    // does not arrive and an event nobody listens for look identical from
    // outside — the frame simply does not change — and they need opposite
    // fixes, so the count of frames that received it is the thing worth
    // knowing. Reported every time, because these are rare enough to read and
    // the ones worth tracing are the ones that are not arriving.
    static const std::set<std::string> traced = [] {
        std::set<std::string> out;
        const char* raw = std::getenv("WOWEE_EVENT_TRACE");
        if (!raw || !*raw) return out;
        std::string v(raw);
        size_t at = 0;
        while (at <= v.size()) {
            const size_t comma = v.find(',', at);
            std::string one = v.substr(at, comma == std::string::npos
                                               ? std::string::npos : comma - at);
            if (!one.empty()) out.insert(one);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        return out;
    }();

    lua_getglobal(L_, "__WoweeFrameEvents");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, eventName.c_str());
        if (lua_istable(L_, -1)) {
            int frameCount = static_cast<int>(lua_objlen(L_, -1));
            if (traced.count(eventName)) {
                LOG_WARNING("EventTrace: ", eventName, " (",
                            args.empty() ? "" : args[0], ") reached ",
                            frameCount, " frames");
            }
            // Iterate a copy, because a handler is allowed to unregister while
            // it runs and several do — answering an event by deciding you no
            // longer want it is ordinary. UnregisterEvent shifts the tail down,
            // so the frame that moved into the vacated index was stepped over
            // and never heard that event at all. The list is a handful of
            // entries, and this only copies references.
            lua_createtable(L_, frameCount, 0);
            for (int i = 1; i <= frameCount; ++i) {
                lua_rawgeti(L_, -2, i);
                lua_rawseti(L_, -2, i);
            }
            lua_remove(L_, -2);   // drop the live list; the copy stands in
            for (int i = 1; i <= frameCount; i++) {
                lua_rawgeti(L_, -1, i);
                if (!lua_istable(L_, -1)) { lua_pop(L_, 1); continue; }

                // Get the frame's OnEvent script
                lua_getfield(L_, -1, "__scripts");
                if (lua_istable(L_, -1)) {
                    lua_getfield(L_, -1, "OnEvent");
                    if (lua_isfunction(L_, -1)) {
                        lua_pushvalue(L_, -3);  // self (frame)
                        lua_pushstring(L_, eventName.c_str());
                        for (const auto& arg : args) pushEventArg(L_, arg);
                        int nargs = 2 + static_cast<int>(args.size());
                        if (lua_pcall(L_, nargs, 0, 0) != 0) {
                            const char* ferr = lua_tostring(L_, -1);
                            std::string ferrStr = ferr ? ferr : "(unknown)";
                            LOG_ERROR("LuaEngine: frame OnEvent error: ", ferrStr);
                            if (luaErrorCallback_) luaErrorCallback_(ferrStr);
                            lua_pop(L_, 1);
                        }
                    } else {
                        lua_pop(L_, 1); // pop non-function
                    }
                }
                lua_pop(L_, 2); // pop __scripts + frame
            }
        } else if (traced.count(eventName)) {
            LOG_WARNING("EventTrace: ", eventName, " (",
                        args.empty() ? "" : args[0],
                        ") — no frame has registered for it");
        }
        lua_pop(L_, 1); // pop event frame list
    }
    lua_pop(L_, 1); // pop __WoweeFrameEvents
}

namespace {
/// Defined with the other pcall helpers further down; declared here because
/// callFrameScript needs it and comes first.
int luaTracebackHandler(lua_State* L);
}  // namespace

void LuaEngine::callFrameScript(uint32_t wid, const char* script,
                                const char* arg) {
    if (!L_ || wid == 0) return;
    lua_getglobal(L_, "__WoweeFramesByWid");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return; }
    lua_pushinteger(L_, static_cast<lua_Integer>(wid));
    lua_rawget(L_, -2);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 2); return; }

    lua_getfield(L_, -1, "__scripts");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 3); return; }
    // The traceback handler has to sit below the function it is handling for,
    // so it goes on before the script is fetched. A handler that fails now says
    // where it was called from, the same as one that fails during the load.
    lua_pushcfunction(L_, luaTracebackHandler);
    const int handlerIdx = lua_gettop(L_);
    lua_getfield(L_, handlerIdx - 1, script);
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 5); return; }

    lua_pushvalue(L_, handlerIdx - 2);  // self
    int nargs = 1;
    if (arg) { lua_pushstring(L_, arg); ++nargs; }
    if (lua_pcall(L_, nargs, 0, handlerIdx) != 0) {
        const char* err = lua_tostring(L_, -1);
        LOG_ERROR("LuaEngine: ", script, " error: ", err ? err : "?");
        if (luaErrorCallback_) luaErrorCallback_(err ? err : "script error");
        lua_pop(L_, 1);
    }
    // Four, not three: the traceback handler is still below.
    lua_pop(L_, 4);
}


void LuaEngine::callFrameScriptNumber(uint32_t wid, const char* script, double arg) {
    if (!L_ || wid == 0) return;
    lua_getglobal(L_, "__WoweeFramesByWid");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return; }
    lua_pushinteger(L_, static_cast<lua_Integer>(wid));
    lua_rawget(L_, -2);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 2); return; }

    lua_getfield(L_, -1, "__scripts");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 3); return; }
    lua_pushcfunction(L_, luaTracebackHandler);
    const int handlerIdx = lua_gettop(L_);
    lua_getfield(L_, handlerIdx - 1, script);
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 5); return; }

    lua_pushvalue(L_, handlerIdx - 2);  // self
    // A number, not a numeric string: OnMouseWheel bodies compare the delta
    // against zero, and Lua raises on comparing a string with a number even
    // where it would happily add them.
    lua_pushnumber(L_, arg);
    if (lua_pcall(L_, 2, 0, handlerIdx) != 0) {
        const char* err = lua_tostring(L_, -1);
        LOG_ERROR("LuaEngine: ", script, " error: ", err ? err : "?");
        if (luaErrorCallback_) luaErrorCallback_(err ? err : "script error");
        lua_pop(L_, 1);
    }
    lua_pop(L_, 4);
}

void LuaEngine::installMissingApiFallback() {
    // Off unless asked for. With it on, every unknown global answers, so code
    // that checks whether a function exists before using it — which addons do
    // constantly — sees everything as present and takes branches meant for a
    // newer client. That is the right trade for bringing FrameXML up, where the
    // point is to get past a missing name and find out what actually matters,
    // and the wrong one for everyday addon loading.
    auto isSet = [](const char* name) {
        const char* v = std::getenv(name);
        return v && *v && std::string(v) != "0";
    };
    // Loading FrameXML implies it. FrameXML cannot get through its own load
    // without the fallback, so two separate switches where one is useless
    // without the other is only a way to be handed a wall of failures for
    // setting the obvious one.
    //
    // Said explicitly, though, the setting wins either way. The fallback is not
    // free — it makes every feature check read as present — and now that the
    // real gaps are closing it is worth being able to ask what it is still
    // buying, which needs a way to turn it off with FrameXML on.
    const char* explicitSetting = std::getenv("WOWEE_LUA_API_FALLBACK");
    const char* loadSetting = std::getenv("WOWEE_LOAD_FRAMEXML");
    const bool frameXmlOn = loadSetting ? (std::string(loadSetting) != "0") : true;
    const bool enabled = (explicitSetting && *explicitSetting)
                             ? std::string(explicitSetting) != "0"
                             : frameXmlOn;
    (void)isSet;
    if (!enabled) return;

    lua_pushcfunction(L_, lua_RecordMissingApi);
    lua_setglobal(L_, "__WoweeRecordMissingApi");

    // Counting functions answer zero rather than nothing.
    //
    // A missing name is usually survivable — the guard around it fails and the
    // branch behind it does not run. A missing count is not: FrameXML writes
    // `for id = 1, GetNumTrackingTypes() do`, and nil as a loop limit is not a
    // loop that runs no times, it is "'for' limit must be a number" and the
    // whole file is lost. That is exactly what took minimap.xml down the moment
    // the minimap started being built at all.
    //
    // Thirty-seven of these are called across FrameXML with nothing behind
    // them. Zero is the honest answer for a feature this client does not model
    // — no titles, no companions, no arena teams — and where it does model one,
    // a real implementation replaces the stub by simply existing: the loop
    // below skips any name already defined.
    //
    // Recorded under a "count:" prefix so they stay in the missing-API report.
    // Defining them would otherwise hide them from it, which is the one thing
    // that report is for.
    bootstrap(
        "local counting = {\n"
        "  'GetCurrencyListSize','GetFieldSize','GetInventoryItemCount',\n"
        "  'GetLFDLockPlayerCount','GetNumArenaTeamMembers',\n"
        
        "  'GetNumBattlefields','GetNumBuybackItems','GetNumChannelMembers',\n"
        "  'GetNumCompanions',\n"
        "  'GetNumGuildBankTabs','GetNumGuildEvents','GetNumLanguages',\n"
        "  'GetNumMessages','GetNumMutes',\n"
        "  'GetNumPoints','GetNumQuestItemDrops','GetNumQuestItems',\n"
        "  'GetNumQuestLogRewardFactions',\n"
        "  'GetNumRandomDungeons',\n"
        "  'GetNumStationeries',\n"
        "  'GetNumTitles','GetNumTooltips','GetNumTrackingTypes',\n"
        "  'GetNumVoiceSessionMembersBySessionID',\n"
        "  'GetNumDungeonMapLevels','GetNumMapOverlays','GetNumVoiceSessions',\n"
        "  'GuildControlGetNumRanks','BNGetNumConversationMembers','GetKeyRingSize',\n"
        // The ignore list asks for all three of these before it draws a row,
        // and compares each against zero without checking. There is no
        // Battle.net here, so none of them is an unknown quantity being guessed
        // at — nobody can have a Battle.net block or a pending invite, and zero
        // is what is true. Without them, opening the Ignore tab raised.
        "  'BNGetNumBlocked','BNGetNumBlockedToons','BNGetNumFriendInvites',\n"
        // Both are counts of something that cannot exist here: nothing tracks
        // achievements, and no battleground port is ever pending. The
        // achievement panel compares the first against its own limit before
        // adding a tracker, and the battlefield frame the second against zero.
        "  'GetNumTrackedAchievements','GetBattlefieldPortExpiration',\n"
        "}\n"
        "local told = {}\n"
        "for _, name in ipairs(counting) do\n"
        "  if rawget(_G, name) == nil then\n"
        "    _G[name] = function()\n"
        "      if not told[name] then\n"
        "        told[name] = true\n"
        "        __WoweeRecordMissingApi('count:' .. name)\n"
        "      end\n"
        "      return 0\n"
        "    end\n"
        "  end\n"
        "end\n");

    // A name in SCREAMING_SNAKE_CASE is a constant, and handing back a function
    // where a number or a string was wanted turns a missing value into a
    // confusing type error further away. Those stay nil. UpperCamelCase is a
    // function, and gets one that does nothing.
    bootstrap(
        // Callable, and every field of it is a method answering nil.
        //
        // A bare function was not enough. FrameXML looks frames up by name as
        // often as it calls functions — local t = _G[name.."PrefixText"] — and
        // it guards them properly, with if (t) then t:GetText(). A function
        // passes that guard and then dies on the indexing, so the correct check
        // was worse than no check at all: eleven files went down on that one
        // line. Answering nil from every method lets the guarded branch run and
        // come to nothing, which is what a missing frame should look like.
        // Methods answer; data fields do not.
        //
        // Answering everything made feature checks on a missing frame's own
        // state read as present: FCFMin_UpdateColors tests
        // minFrame.selectedColorTable and takes the branch that dereferences
        // it. The same convention the fallback already uses for names —
        // PascalCase is a method, anything else is data — applies inside the
        // object too, so a field is nil and the guard around it works.
        "local missing = setmetatable({}, {\n"
        "  __call = function() end,\n"
        "  __index = function(_, k)\n"
        "    if type(k) == 'string' and string.find(k, '^%u') then\n"
        "      return function() return nil end\n"
        "    end\n"
        "    return nil\n"
        "  end,\n"
        "})\n"
        "__WoweeLoadOnDemandFrames = {\n"
        "  AchievementFrame = true, ArenaEnemyFrames = true,\n"
        "  BattlefieldMinimap = true, BattlefieldMinimapTab = true,\n"
        "  KeyBindingFrame = true, MacroFrame = true, PlayerTalentFrame = true,\n"
        "  StopwatchTicker = true, TimeManagerClockButton = true,\n"
        "  TimeManagerFrame = true,\n"
        "}\n"
        "local seen = {}\n"
        "setmetatable(_G, { __index = function(_, k)\n"
        "  if type(k) ~= 'string' then return nil end\n"
        "  if not string.find(k, '^%u') then return nil end\n"
        "  if string.find(k, '^[A-Z][A-Z0-9_]*$') then return nil end\n"
        // A digit in the name means an instance, not an API function, and an
        // instance that does not exist must read as absent. FrameXML looks
        // frames up by building the name — _G["ChatFrame"..id.."Minimized"] —
        // and then guards the result properly with if (frame). Answering makes
        // that guard pass and the branch behind it runs against nothing.
        //
        // Measured rather than assumed: of the 4,100 distinct names FrameXML
        // calls as functions, four contain a digit, and three of those it
        // defines itself. Being wrong here costs a no-op for one API name,
        // which is where this started.
        "  if string.find(k, '%d') then return nil end\n"
        // An addon's namespace table, which is absent because the addon is not
        // loaded — and FrameXML feature-detects exactly these:
        // `if ( not Blizzard_CombatLog_Filters )`. Answering makes the guard
        // pass and the branch behind it indexes a table that has no fields, so
        // the panel's whole update dies on a nil length.
        "  if string.find(k, '^Blizzard_') then return nil end\n"
        // The panels that load on demand, which FrameXML asks for by name
        // before deciding to load them: `if ( not AchievementFrame ) then
        // AchievementFrame_LoadUI() end`. Answering with the no-op made every
        // one of those guards read as "already loaded", so the panel was never
        // asked for and the branch behind the guard ran against a stand-in —
        // watchframe goes straight on to `AchievementFrame:IsShown()`, which
        // answered a no-op too, so tracking an achievement did nothing at all.
        //
        // A list rather than a rule about the shape of the name, because the
        // shape does not separate them: PlayerArrowEffectFrame and
        // WorldMapBlobFrame are addressed with no guard at all, and answering
        // nil for those would take the world map's OnLoad down. These ten are
        // the ones FrameXML feature-detects, and every use of them sits behind
        // that test.
        "  if __WoweeLoadOnDemandFrames[k] then return nil end\n"
        // Punctuation means this is not an API name at all. A Lua identifier
        // cannot contain a hyphen, so _G["KEY_-"] is a table lookup built by
        // concatenation — GetBindingText does exactly that for the key bound
        // to action button eleven — and the answer is nil, not an object that
        // the caller then tries to concatenate. Two files died on that one.
        "  if string.find(k, '[^%w_]') then return nil end\n"
        "  if not seen[k] then seen[k] = true; __WoweeRecordMissingApi(k) end\n"
        "  return missing\n"
        "end })\n");

    LOG_WARNING("LuaEngine: missing-API fallback is ON — unknown globals answer "
                "with a no-op, so feature detection will read as present");
}

void LuaEngine::reportMissingApi() const {
    // Every name is checked against _G before being reported, so without a
    // state there is nothing to say. Guarded here as well as at the call site
    // because the crash this cost was a null state, not an empty list.
    if (!L_) return;
    const auto& names = missingApiNames();
    if (names.empty()) return;
    // At warning level, because release builds drop INFO and this is the whole
    // point of recording them: the list is the measured gap, once per session,
    // and it was being written where nobody could read it.
    // A name recorded here was missing when it was read, which is not the same
    // as missing. FrameXML reads a global before the file that defines it has
    // loaded all the time — a frame asks for another panel's frame in its
    // OnLoad, a font object is defined as `X = X or {}` — and every one of
    // those was landing in a list whose only value is that everything in it is
    // real. Ask again now, at the end, and keep what is still absent.
    // Widget methods that answered with a no-op. Not globals, so the test
    // below does not apply to them, and worth their own list: each is a call
    // FrameXML makes that does nothing, which is the surface of unimplemented
    // behaviour and reads as working from every other angle.
    std::vector<std::string> noops;
    std::vector<std::string> globals;
    for (const auto& n : names) {
        if (n.rfind("noop:", 0) == 0) noops.push_back(n.substr(5));
        else globals.push_back(n);
    }

    std::vector<std::string> absent;
    absent.reserve(globals.size());
    for (const auto& n : globals) {
        lua_pushstring(L_, n.c_str());
        lua_rawget(L_, LUA_GLOBALSINDEX);
        const bool defined = !lua_isnil(L_, -1);
        lua_pop(L_, 1);
        if (!defined) absent.push_back(n);
    }
    // Only the globals section has nothing to say. The no-op list is a
    // separate question and returning here took it down with it: once the
    // global surface went clean — which is the whole point of the transition —
    // the report stopped being written at all, and every widget method
    // answering with a no-op went quiet with it.
    //
    // That is how five real tooltips hid. SetUnitAura was serving a no-op on
    // every buff hover, recording itself faithfully each time, into a report
    // that was never produced.
    if (absent.empty() && noops.empty()) return;

    // A name built from an existing frame's is a part that frame may or may
    // not have, not an API that is missing.
    //
    // FrameXML asks for these constantly — uipaneltemplates.lua does
    // _G[self:GetName() .. "Top"] and guards the result with if(top and
    // bottom) — because a scroll frame only has border art if its own XML
    // declared it. On one session 125 of 222 names were exactly this, all of
    // them correctly absent, which is a report whose number means the opposite
    // of what it says. They are counted apart rather than dropped: a genuinely
    // missing sub-frame would hide here too, and the count is where it shows.
    std::vector<std::string> partsOfFrames;
    std::vector<std::string> realGaps;
    for (const auto& n : absent) {
        bool isPart = false;
        // The suffixes in play are short — Top, Middle, Bottom, Count, Text —
        // and the frame they hang off is never tiny.
        for (size_t suffix = 1; suffix <= 16 && n.size() > suffix + 5; ++suffix) {
            if (widgets_.findByName(std::string_view(n).substr(0, n.size() - suffix))) {
                isPart = true;
                break;
            }
        }
        (isPart ? partsOfFrames : realGaps).push_back(n);
    }

    if (!noops.empty()) {
        std::sort(noops.begin(), noops.end());
        std::string all;
        for (const auto& n : noops) { all += n; all += ' '; }
        LOG_WARNING("LuaEngine: ", noops.size(), " widget methods answered with a "
                    "no-op: ", all);
    }

    if (!realGaps.empty() || !partsOfFrames.empty()) {
    LOG_WARNING("LuaEngine: ", realGaps.size(), " distinct API names were called "
                "and are still not defined (", globals.size() - absent.size(),
                " more were read before whatever defines them had loaded, and ",
                partsOfFrames.size(), " were optional parts of frames that do "
                "exist)");
    }
    std::string line;
    for (const auto& n : realGaps) {
        line += n;
        line += ' ';
        if (line.size() > 900) { LOG_WARNING("  missing: ", line); line.clear(); }
    }
    if (!line.empty()) LOG_WARNING("  missing: ", line);

    // And to a file of its own, one name per line.
    //
    // This is the most useful measurement a session produces and the log is
    // the worst place to keep it: the next run truncates it, and a list of two
    // hundred names is what gets lost. A file beside the log survives, sorts,
    // and diffs against the last run — which is the question worth asking of
    // it anyway, since what matters is what changed.
    const std::string path = core::getConfigRoot() + "/missing_api.txt";
    if (std::ofstream out(path); out) {
        for (const auto& n : realGaps) out << n << "\n";
        out << "\n-- optional parts of frames that exist, correctly absent --\n";
        for (const auto& n : partsOfFrames) out << n << "\n";
        out << "\n-- widget methods that answered with a no-op --\n";
        for (const auto& n : noops) out << n << "\n";
        LOG_WARNING("LuaEngine: the full list is in ", path);
    }
}

/// Whether a frame asked for this button's clicks.
///
/// WoW gives a button LeftButtonUp and nothing else unless it says otherwise,
/// and FrameXML says otherwise exactly where a context menu is wanted. Without
/// the check every frame would answer a right-click, which is a menu opening
/// under a cursor that never asked for one.
bool LuaEngine::frameAcceptsClick(uint32_t wid, const char* button) {
    lua_getglobal(L_, "__WoweeFramesByWid");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_pushinteger(L_, static_cast<lua_Integer>(wid));
    lua_rawget(L_, -2);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 2); return false; }

    lua_getfield(L_, -1, "__clicks");
    bool accepts;
    if (lua_istable(L_, -1)) {
        // Registered explicitly: either edge counts, since this only models
        // the release. "Any" means any button, which is what every action
        // button in the interface registers — ActionButton_OnLoad calls
        // RegisterForClicks("AnyUp"), and matching only LeftButtonUp meant no
        // action button on the bar ever received a click.
        const std::string names[] = {
            std::string(button) + "Up", std::string(button) + "Down",
            "AnyUp", "AnyDown"
        };
        accepts = false;
        for (const std::string& n : names) {
            lua_getfield(L_, -1, n.c_str());
            accepts = lua_toboolean(L_, -1) != 0;
            lua_pop(L_, 1);
            if (accepts) break;
        }
    } else {
        accepts = (std::strcmp(button, "LeftButton") == 0);
    }
    lua_pop(L_, 3);
    return accepts;
}

namespace {
LuaEngine* engineFrom(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_lua_engine");
    auto* e = static_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return e;
}

}  // namespace

int lua_EditBox_SetFocus(lua_State* L) {
    if (auto* e = engineFrom(L)) e->setEditFocus(widgetIdOf(L, 1));
    return 0;
}
int lua_EditBox_ClearFocus(lua_State* L) {
    if (auto* e = engineFrom(L)) e->setEditFocus(0);
    return 0;
}
int lua_EditBox_HasFocus(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushboolean(L, w && w->editFocused ? 1 : 0);
    return 1;
}

void LuaEngine::setEditFocus(uint32_t wid) {
    if (focusedWid_ == wid) return;
    if (focusedWid_ != 0) {
        if (auto* old = widgets_.get(focusedWid_)) old->editFocused = false;
        callFrameScript(focusedWid_, "OnEditFocusLost");
    }
    focusedWid_ = wid;
    if (focusedWid_ != 0) {
        if (auto* w = widgets_.get(focusedWid_)) w->editFocused = true;
        callFrameScript(focusedWid_, "OnEditFocusGained");
    }
}

void LuaEngine::dispatchText(const char* utf8) {
    if (!L_ || focusedWid_ == 0 || !utf8) return;
    auto* w = widgets_.get(focusedWid_);
    if (!w || !w->isEditBox) return;

    std::string add(utf8);
    if (add.empty()) return;
    // A numeric box takes digits and nothing else, which is what stops a
    // quantity field filling with letters.
    if (w->editNumeric) {
        add.erase(std::remove_if(add.begin(), add.end(),
                                 [](unsigned char c) { return std::isdigit(c) == 0; }),
                  add.end());
        if (add.empty()) return;
    }
    if (w->editMaxLetters > 0 &&
        static_cast<int>(w->editText.size() + add.size()) > w->editMaxLetters) {
        const int room = w->editMaxLetters - static_cast<int>(w->editText.size());
        if (room <= 0) return;
        add.resize(static_cast<size_t>(room));
    }

    const size_t at = std::min(w->cursorPos, w->editText.size());
    w->editText.insert(at, add);
    w->cursorPos = at + add.size();
    // The handler that tells a search field to filter, and a chat box to look
    // for a channel prefix.
    callFrameScript(focusedWid_, "OnTextChanged");
    // A space is its own handler, and the chat box is what wants it: typing
    // "/w Bob " is how a whisper gets its target, and ChatEdit_OnSpacePressed
    // is what reads the name out and turns the box into a whisper with a
    // header. Without it the slash command stayed literal text until it was
    // sent.
    if (add.find(' ') != std::string::npos) {
        callFrameScript(focusedWid_, "OnSpacePressed");
    }
}

/// SDL's keycode as WoW names it, or empty for one WoW has no name for.
///
/// The handlers compare against these by name — CoinPickupFrame checks for
/// "ESCAPE" and the digits — so a wrong spelling is a handler that never
/// matches rather than an error anyone would see.
static std::string wowKeyName(int sym) {
    if (sym >= 'a' && sym <= 'z') return std::string(1, static_cast<char>(sym - 32));
    if (sym >= '0' && sym <= '9') return std::string(1, static_cast<char>(sym));
    switch (sym) {
        case 27:         return "ESCAPE";
        case ' ':        return "SPACE";
        case '\r':       return "ENTER";
        case '\t':       return "TAB";
        case '\b':       return "BACKSPACE";
        case 0x4000004A: return "HOME";
        case 0x4000004D: return "END";
        case 0x4000004B: return "PAGEUP";
        case 0x4000004E: return "PAGEDOWN";
        case 0x4000004C: return "DELETE";
        case 0x40000049: return "INSERT";
        case 0x40000050: return "LEFT";
        case 0x4000004F: return "RIGHT";
        case 0x40000052: return "UP";
        case 0x40000051: return "DOWN";
        default: break;
    }
    // F1..F12 are contiguous in SDL's scancode-derived range.
    if (sym >= 0x4000003A && sym <= 0x40000045) {
        return "F" + std::to_string(sym - 0x4000003A + 1);
    }
    return {};
}

bool LuaEngine::dispatchFrameKey(int sdlKeycode, bool down) {
    if (!L_) return false;
    const std::string key = wowKeyName(sdlKeycode);
    if (key.empty()) return false;

    // The topmost frame that is both visible and listening. Everything that
    // declares a key handler in the interface is a dialog that is hidden until
    // it is wanted, so in ordinary play there is nothing here and the key goes
    // straight through to the game.
    const ui::Widget* best = nullptr;
    for (size_t id = 1; id < widgets_.size(); ++id) {
        const ui::Widget* w = widgets_.get(static_cast<uint32_t>(id));
        if (!w || !w->keyboardEnabled || !w->visible) continue;
        if (!best) { best = w; continue; }
        if (w->effStrata > best->effStrata ||
            (w->effStrata == best->effStrata && w->effLevel >= best->effLevel)) {
            best = w;
        }
    }
    if (!best) return false;

    callFrameScript(best->id, down ? "OnKeyDown" : "OnKeyUp", key.c_str());
    // Consumed unless the frame asked for the key to carry on, which is WoW's
    // default and the reason a dialog stops the character walking.
    return !best->propagateKeys;
}

void LuaEngine::dispatchKey(int sdlKeycode, bool ctrlHeld) {
    if (!L_ || focusedWid_ == 0) return;
    auto* w = widgets_.get(focusedWid_);
    if (!w || !w->isEditBox) return;
    (void)ctrlHeld;

    // Keycodes are SDL's, which is what the window reports; the caller does not
    // translate them so this stays the only place that knows.
    constexpr int kBackspace = '\b';
    constexpr int kReturn    = '\r';
    constexpr int kEscape    = 27;
    constexpr int kDelete    = 0x4000004C;  // SDLK_DELETE
    constexpr int kLeft      = 0x40000050;
    constexpr int kRight     = 0x4000004F;
    constexpr int kHome      = 0x4000004A;
    constexpr int kEnd       = 0x4000004D;
    constexpr int kTab       = '\t';

    const size_t len = w->editText.size();
    switch (sdlKeycode) {
        case kBackspace:
            if (w->cursorPos > 0 && len > 0) {
                w->editText.erase(w->cursorPos - 1, 1);
                --w->cursorPos;
                callFrameScript(focusedWid_, "OnTextChanged");
            }
            break;
        case kDelete:
            if (w->cursorPos < len) {
                w->editText.erase(w->cursorPos, 1);
                callFrameScript(focusedWid_, "OnTextChanged");
            }
            break;
        case kLeft:  if (w->cursorPos > 0) --w->cursorPos; break;
        case kRight: if (w->cursorPos < len) ++w->cursorPos; break;
        case kHome:  w->cursorPos = 0; break;
        case kEnd:   w->cursorPos = len; break;
        case kReturn:
            // A box declared multiLine takes the return as a line break rather
            // than as "done". The mail body says multiLine="true" and letters
            // ="500"; reading the limit and not the flag left a letter that
            // stops at five hundred characters and still cannot hold two
            // paragraphs.
            //
            // Blizzard's own boxes rely on this split: the chat box has no
            // multiLine and submits, the mail body has it and does not, and
            // both are the same OnEnterPressed handler.
            if (w->editMultiLine) {
                // The break counts against the limit like any other character;
                // inserting it directly would let a full box grow by one every
                // time return was pressed.
                if (w->editMaxLetters > 0 &&
                    static_cast<int>(w->editText.size()) >= w->editMaxLetters) {
                    break;
                }
                const size_t at = std::min(w->cursorPos, w->editText.size());
                w->editText.insert(at, 1, '\n');
                w->cursorPos = at + 1;
                callFrameScript(focusedWid_, "OnTextChanged");
                break;
            }
            // The handler decides what to do with it, including whether to let
            // go of focus — a chat box does, a search field does not.
            callFrameScript(focusedWid_, "OnEnterPressed");
            break;
        case kEscape:
            callFrameScript(focusedWid_, "OnEscapePressed");
            setEditFocus(0);
            break;
        case kTab:
            // Focus stays where it is unless the handler moves it. Twelve
            // boxes in the interface declare this, and every one of them
            // reaches for the next field by name — which is the frame's own
            // business, not something to guess at from here.
            callFrameScript(focusedWid_, "OnTabPressed");
            break;
        default: break;
    }
}

void LuaEngine::reportEventListenersOnce() {
    if (!L_ || eventListenersReported_) return;
    // Counted from when there is an interface to count, not from startup:
    // FrameXML loads on entering the world, and a report timed from the
    // client's first frame ran before any of it existed and said zero for
    // everything — including events that demonstrably work.
    if (widgets_.size() < 200) return;
    if (++eventReportFrames_ < 120) return;
    eventListenersReported_ = true;

    lua_getglobal(L_, "__WoweeFrameEvents");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return; }
    static const char* kWatched[] = {
        "UNIT_HEALTH", "UNIT_MAXHEALTH", "UNIT_MANA", "UNIT_MAXMANA",
        "UNIT_RAGE", "UNIT_ENERGY", "UNIT_DISPLAYPOWER", "PLAYER_ENTERING_WORLD",
        // The frames that have gone wrong most recently, so that "nothing is
        // listening" is ruled in or out before anything else is looked at.
        "PLAYER_TARGET_CHANGED", "UNIT_AURA", "PLAYER_XP_UPDATE", "BAG_UPDATE",
    };
    std::string line;
    for (const char* name : kWatched) {
        lua_getfield(L_, -1, name);
        const int n = lua_istable(L_, -1)
            ? static_cast<int>(lua_objlen(L_, -1)) : 0;
        lua_pop(L_, 1);
        line += name;
        line += '=';
        line += std::to_string(n);
        line += ' ';
    }
    lua_pop(L_, 1);
    LOG_WARNING("Event listeners: ", line);
}

/// Fire OnSizeChanged for anything whose rect changed since the last frame.
///
/// Declared by FrameXML and reached for constantly by addons, which resize a
/// panel and expect the pieces inside it to be told. Noticed here rather than
/// fired from SetWidth, because a frame is far more often resized by its
/// anchors than by anyone calling a setter — a scroll child stretched by its
/// parent never goes near SetWidth.
void LuaEngine::updateSizeChanges() {
    if (!L_) return;
    for (size_t id = 1; id < widgets_.size(); ++id) {
        const ui::Widget* wp = widgets_.get(static_cast<uint32_t>(id));
        if (!wp) continue;
        const ui::Widget& w = *wp;
        if (w.id == 0 || w.kind != ui::WidgetKind::Frame) continue;
        const bool known = (w.lastReportedW >= 0.0f);
        if (known && w.lastReportedW == w.rectW && w.lastReportedH == w.rectH) {
            continue;
        }
        // Written through the tree, since the loop reads a const view.
        if (auto* mut = widgets_.get(w.id)) {
            mut->lastReportedW = w.rectW;
            mut->lastReportedH = w.rectH;
        }
        // Nothing is fired the first time a frame is measured: every frame in
        // the interface would report a change on the first layout, which says
        // nothing and runs 3000 handlers to say it.
        if (!known) continue;
        if (w.rectW <= 0.0f || w.rectH <= 0.0f) continue;
        lua_getglobal(L_, "__WoweeFramesByWid");
        if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return; }
        lua_pushinteger(L_, static_cast<lua_Integer>(w.id));
        lua_rawget(L_, -2);
        if (lua_istable(L_, -1)) {
            lua_getfield(L_, -1, "OnSizeChanged");
            if (lua_isfunction(L_, -1)) {
                lua_pushvalue(L_, -2);
                lua_pushnumber(L_, w.rectW);
                lua_pushnumber(L_, w.rectH);
                if (lua_pcall(L_, 3, 0, 0) != 0) {
                    LOG_WARNING("OnSizeChanged error: ",
                                luaL_optstring(L_, -1, "?"));
                    lua_pop(L_, 1);
                }
            } else {
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 2);
    }
}

void LuaEngine::updateVisibility() {
    if (!L_) return;
    // By index and re-fetched each time: a handler is free to create frames,
    // and OnShow very often does.
    for (uint32_t id = 1; id < widgets_.size(); ++id) {
        auto* w = widgets_.get(id);
        if (!w || w->id == 0) continue;
        if (w->visible == w->reportedVisible) continue;
        w->reportedVisible = w->visible;
        callFrameScript(id, w->visible ? "OnShow" : "OnHide");
        // A box that asked for the keyboard takes it as it appears, and gives
        // it up when it goes. Only the two that ask: the rest say autoFocus
        // ="false" precisely so that opening a panel does not swallow the
        // player's next keystroke.
        if (w->isEditBox && w->editAutoFocus) {
            if (w->visible) setEditFocus(id);
            else if (focusedWid_ == id) setEditFocus(0);
        }
    }
}

void LuaEngine::updateScrollRanges() {
    if (!L_) return;
    for (uint32_t id : widgets_.scrollFrames()) {
        auto* w = widgets_.get(id);
        if (!w) continue;
        float rangeX = 0.0f, rangeY = 0.0f;
        if (const auto* child = widgets_.get(w->scrollChild)) {
            rangeX = child->rectW - w->rectW;
            rangeY = child->rectH - w->rectH;
            if (rangeX < 0.0f) rangeX = 0.0f;
            if (rangeY < 0.0f) rangeY = 0.0f;
        }
        if (rangeX == w->reportedRangeX && rangeY == w->reportedRangeY) continue;
        w->reportedRangeX = rangeX;
        w->reportedRangeY = rangeY;

        // Both ranges, in WoW's order. ScrollFrame_OnScrollRangeChanged reads
        // the second and hides the bar when it is zero, which is how a list
        // that fits shows no scroll bar at all.
        lua_getglobal(L_, "__WoweeFramesByWid");
        if (!lua_istable(L_, -1)) { lua_pop(L_, 1); continue; }
        lua_pushinteger(L_, static_cast<lua_Integer>(id));
        lua_rawget(L_, -2);
        if (!lua_istable(L_, -1)) { lua_pop(L_, 2); continue; }
        lua_getfield(L_, -1, "__scripts");
        if (lua_istable(L_, -1)) {
            lua_getfield(L_, -1, "OnScrollRangeChanged");
            if (lua_isfunction(L_, -1)) {
                lua_pushvalue(L_, -3);          // self
                lua_pushnumber(L_, rangeX);
                lua_pushnumber(L_, rangeY);
                if (lua_pcall(L_, 3, 0, 0) != 0) {
                    const char* err = lua_tostring(L_, -1);
                    LOG_ERROR("LuaEngine: OnScrollRangeChanged error: ",
                              err ? err : "?");
                    lua_pop(L_, 1);
                }
            } else {
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 3);
    }
}

bool LuaEngine::dispatchMouseWheel(float x, float y, float delta) {
    if (!L_) return false;
    const float s = widgets_.uiScale();
    if (s > 0.0f) { x /= s; y /= s; }

    // Up from whatever is under the cursor to the first frame that asked for
    // the wheel. WoW works the same way: a scroll frame's child fills it and
    // takes the hit, and the scroll frame above is what handles the wheel.
    uint32_t wid = widgets_.hitTest(x, y);
    while (wid != 0) {
        const auto* w = widgets_.get(wid);
        if (!w) break;
        if (w->wheelEnabled) {
            callFrameScriptNumber(wid, "OnMouseWheel", delta);
            return true;
        }
        wid = w->parent;
    }
    return false;
}

bool LuaEngine::holdsMousePress() const {
    for (int i = 0; i < kMouseButtons; ++i) {
        if (buttonDown_[i]) return true;
    }
    // A drag or a moved frame counts even with nothing held, because that is
    // precisely the state a stranded drag leaves behind and it has to stay
    // reachable long enough for the release to clear it.
    return draggingWid_ != 0 || widgets_.movingWidget() != 0;
}

void LuaEngine::releaseMouseHover() {
    if (!L_) return;
    if (hoverWid_ != 0) {
        callFrameScript(hoverWid_, "OnLeave");
        hoverWid_ = 0;
    }
    widgets_.setInteraction(0, 0);
    // Nothing is under the cursor and nothing is holding it, which is the
    // camera's cue that it may turn again.
    ui::frameXmlNoteMouseOwned(false);
    // The next position the tree hears is a fresh one rather than the far end
    // of however far the cursor travelled while it was not listening.
    haveCursor_ = false;
}

void LuaEngine::dispatchMouse(float x, float y, MouseButtons buttons) {
    if (!L_) return;
    // The cursor arrives in pixels and the tree is in interface units, so this
    // is where the two meet. Hit testing against unconverted pixels would miss
    // every frame by the scale factor.
    const float s = widgets_.uiScale();
    if (s > 0.0f) { x /= s; y /= s; }
    const uint32_t hit = widgets_.hitTest(x, y);
    lastMouseHit_ = hit;
    // Kept so IsMouseOver can answer from a frame's own rect. Hover alone is
    // not enough: it names the mouse-enabled frame that was hit, and a
    // container the cursor is plainly inside is often neither.
    sLastMouseX_ = x;
    sLastMouseY_ = y;

    // Throttled, and only while there is something to hit. Whether the mouse
    // reaches the widget tree at all is otherwise invisible: a frame that never
    // lights up looks the same whether the dispatch is not running, the
    // coordinates are wrong, or the frame is not taking the mouse.
    static double lastReport = 0.0;
    const double now = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
    if (now - lastReport >= 1.0) {
        size_t mouseFrames = 0;
        for (uint32_t id = 1; id < widgets_.size(); ++id) {
            const auto* w = widgets_.get(id);
            if (w && w->mouseEnabled && w->visible) ++mouseFrames;
        }
        // Reported whenever anything is on screen at all, not only when
        // something is mouse-enabled. Gating on that hid the one case that was
        // actually happening: no frame took the mouse, so the count was zero,
        // so nothing was logged, so the silence looked like the dispatch never
        // running. A diagnostic must not go quiet in the state it exists to
        // report.
        size_t visibleFrames = 0;
        for (uint32_t id = 1; id < widgets_.size(); ++id) {
            const auto* w = widgets_.get(id);
            if (w && w->visible) ++visibleFrames;
        }
        if (visibleFrames > 0) {
            lastReport = now;
            LOG_INFO("WidgetInput: mouse=(", x, ",", y, ") hit=", hit,
                     " hover=", hoverWid_, " mouseEnabled=", mouseFrames,
                     " visible=", visibleFrames);
        }
    }

    // What a press landed on, once a second while one is held. The question
    // "did my click reach anything" has no other answer from outside.
    if (buttons.left) {
        static double lastPress = 0.0;
        const double now = core::appTimeSeconds();
        if (now - lastPress > 1.0) {
            lastPress = now;
            const auto* w = widgets_.get(hit);
            LOG_WARNING("WidgetInput: press at (", x, ",", y, ") hit ",
                        hit == 0 ? "nothing"
                                 : (w && !w->name.empty() ? w->name.c_str() : "(unnamed)"));
        }
    }

    // Told before anything else reads it: which of a button's textures to draw
    // depends on both, and the draw order is collected during layout, which has
    // already happened by the time this runs — so this frame's press shows on
    // the next, which at sixty frames a second is not a wait anyone sees.
    widgets_.setInteraction(hit, buttonDown_[0] ? pressedWid_[0] : 0);

    // Tell the rest of the client the interface has the cursor, so the camera
    // does not turn while a bag item is being clicked or dragged. A press keeps
    // it owned even once the cursor has left the frame, because letting go of
    // the interface halfway through a drag would hand the rest of the drag to
    // the camera.
    ui::frameXmlNoteMouseOwned(hit != 0 || pressedWid_[0] != 0 ||
                               pressedWid_[1] != 0 || pressedWid_[2] != 0);

    // Hover first, so a frame that appears under a stationary cursor still gets
    // its OnEnter rather than waiting for the mouse to move.
    if (hit != hoverWid_) {
        if (hoverWid_ != 0) callFrameScript(hoverWid_, "OnLeave");
        hoverWid_ = hit;
        if (hoverWid_ != 0) callFrameScript(hoverWid_, "OnEnter");
    }

    // How far the cursor has travelled since last frame, which is what carries
    // a frame that is being moved and what tells a drag from a click.
    const float dx = haveCursor_ ? (x - cursorX_) : 0.0f;
    const float dy = haveCursor_ ? (y - cursorY_) : 0.0f;
    cursorX_ = x;
    cursorY_ = y;
    haveCursor_ = true;

    // A frame that StartMoving picked up follows the cursor until something
    // puts it down.
    //
    // With nothing held it is put down here, whatever else went wrong. A frame
    // stuck to the cursor follows it forever and there is no way back from it
    // in the running client, so this does not rely on the release path having
    // matched the drag correctly.
    const bool anyHeld = buttonDown_[0] || buttonDown_[1] || buttonDown_[2];
    if (!anyHeld && (widgets_.movingWidget() != 0 || draggingWid_ != 0)) {
        if (draggingWid_ != 0) callFrameScript(draggingWid_, "OnDragStop", "LeftButton");
        widgets_.setMovingWidget(0);
        draggingWid_ = 0;
        draggingButton_ = -1;
    }
    if (const uint32_t moving = widgets_.movingWidget()) {
        if (dx != 0.0f || dy != 0.0f) widgets_.nudge(moving, dx, dy);
    }

    // Begin a drag once the cursor has left the button it went down on. WoW
    // draws the same line: below the threshold it is a click, above it the
    // frame's OnDragStart runs and the click is abandoned.
    if (draggingWid_ == 0) {
        constexpr float kDragThreshold = 4.0f;   // interface units
        for (int i = 0; i < kMouseButtons; ++i) {
            if (!buttonDown_[i] || pressedWid_[i] == 0) continue;
            // Up through the parents until something is registered for this
            // button, because a drag belongs to the nearest frame that asked
            // for one rather than to whatever the press happened to land on.
            // PaperDollFrame covers the whole character sheet and takes the
            // mouse without taking drags, so every press on the sheet stopped
            // there and it could not be moved.
            uint32_t owner = 0;
            for (uint32_t id = pressedWid_[i]; id != 0;) {
                const auto* cand = widgets_.get(id);
                if (!cand) break;
                const bool takes = (i == 0) ? cand->dragLeft
                                 : (i == 1) ? cand->dragRight
                                            : false;
                if (takes) { owner = id; break; }
                id = cand->parent;
            }
            if (owner == 0) continue;
            const float mx = x - pressX_[i], my = y - pressY_[i];
            if (mx * mx + my * my < kDragThreshold * kDragThreshold) continue;
            draggingWid_ = owner;
            draggingButton_ = i;
            callFrameScript(draggingWid_, "OnDragStart",
                            i == 0 ? "LeftButton" : "RightButton");
            const auto* dw = widgets_.get(draggingWid_);
            LOG_WARNING("WidgetInput: drag started on ",
                        dw && !dw->name.empty() ? dw->name.c_str() : "(unnamed)");
            break;
        }
    }

    // A slider follows the cursor for as long as it is held, which is the only
    // widget where what happens between press and release is the point. The
    // frame keeps the grab even when the cursor leaves it, because letting go
    // of a scroll bar by sliding sideways is not what anyone means.
    if (buttonDown_[0] && pressedWid_[0] != 0) {
        if (auto* w = widgets_.get(pressedWid_[0]); w && w->isSlider) {
            const float span = w->barMax - w->barMin;
            if (span > 0.0f) {
                // Vertical sliders run top to bottom, and the tree's y grows
                // upward, so the fraction is measured from the far edge.
                const float extent = w->barVertical ? w->rectH : w->rectW;
                float f = 0.0f;
                if (extent > 0.0f) {
                    f = w->barVertical ? (w->bottom + w->rectH - y) / extent
                                       : (x - w->left) / extent;
                }
                f = std::clamp(f, 0.0f, 1.0f);
                float value = w->barMin + f * span;
                if (w->sliderStep > 0.0f) {
                    value = w->barMin +
                            std::round((value - w->barMin) / w->sliderStep) * w->sliderStep;
                    value = std::clamp(value, w->barMin, w->barMax);
                }
                if (value != w->barValue) {
                    w->barValue = value;
                    // With the value, because that is the argument the handler
                    // names and uses: UIPanelScrollBarTemplate's body is
                    // self:GetParent():SetVerticalScroll(value), and a nil
                    // there scrolls to zero — so dragging a scroll bar snapped
                    // the view back to the top instead of moving it.
                    callFrameScriptNumber(pressedWid_[0], "OnValueChanged", value);
                }
            }
        }
    }

    // The names WoW uses, in the order the state arrays are indexed.
    struct Button { const char* name; bool down; };
    const Button pressed[kMouseButtons] = {
        {"LeftButton",   buttons.left},
        {"RightButton",  buttons.right},
        {"MiddleButton", buttons.middle},
    };

    for (int i = 0; i < kMouseButtons; ++i) {
        const Button& b = pressed[i];
        if (b.down && !buttonDown_[i]) {
            buttonDown_[i] = true;
            pressedWid_[i] = clickOwnerOf(hit, b.name);
            // Bring the window to the front, the way clicking one does in WoW.
            // The nearest frame at or above what was hit that asked to be
            // toplevel — a click lands on a button inside the window, not on
            // the window itself, so raising only what was hit would raise
            // nothing. Done on the press so the frame is already in front
            // while the click is still being held.
            for (uint32_t id = hit; id != 0;) {
                const auto* cand = widgets_.get(id);
                if (!cand) break;
                if (cand->topLevel) { widgets_.raise(id); break; }
                id = cand->parent;
            }
            pressX_[i] = x;
            pressY_[i] = y;
            // Clicking into an edit box takes focus; clicking anywhere else
            // gives it up, which is what makes a chat box stop eating keys.
            if (i == 0) {
                const auto* hw = hit ? widgets_.get(hit) : nullptr;
                setEditFocus(hw && hw->isEditBox ? hit : 0);
            }
            if (pressedWid_[i] != 0)
                callFrameScript(pressedWid_[i], "OnMouseDown", b.name);
        } else if (!b.down && buttonDown_[i]) {
            buttonDown_[i] = false;
            if (pressedWid_[i] != 0) {
                callFrameScript(pressedWid_[i], "OnMouseUp", b.name);
                // A click is press and release on the same frame, which is what
                // lets a player slide off a button to change their mind.
                const auto* pressed = widgets_.get(pressedWid_[i]);
                // A disabled button is greyed and takes no clicks. Setting
                // enabled without honouring it here would be the same shape of
                // half-feature as drawing a scroll frame's clip without
                // clipping its hit test: a scroll arrow at the end of its
                // range would still scroll.
                // A drag that ends is not also a click. The frame that was
                // dragged is told to stop, and whatever the cursor was let go
                // over is offered what is being carried — which is how an item
                // moves from one bag slot to another.
                const bool wasDragged = (draggingWid_ != 0 && draggingButton_ == i);
                if (wasDragged) {
                    callFrameScript(draggingWid_, "OnDragStop", b.name);
                    widgets_.setMovingWidget(0);
                    if (hit != 0 && hit != draggingWid_) {
                        callFrameScript(hit, "OnReceiveDrag", b.name);
                    }
                    const auto* target = hit ? widgets_.get(hit) : nullptr;
                    LOG_WARNING("WidgetInput: drag dropped on ",
                                target && !target->name.empty() ? target->name.c_str()
                                                                : "nothing");
                    draggingWid_ = 0;
                    draggingButton_ = -1;
                }

                const bool takesIt = frameAcceptsClick(pressedWid_[i], b.name);
                // Resolved the same way the press was, or a press on a bar and
                // a release on the same bar would compare an ancestor against a
                // child and never match.
                const uint32_t releasedOn = clickOwnerOf(hit, b.name);
                if (!wasDragged && pressedWid_[i] == releasedOn &&
                    (!pressed || pressed->enabled) && takesIt) {
                    // PreClick and PostClick bracket the click. Secure buttons
                    // use them to set up and tear down around an action, and
                    // an addon that only has PostClick would otherwise never
                    // hear that its button was used.
                    callFrameScript(pressedWid_[i], "PreClick", b.name);
                    callFrameScript(pressedWid_[i], "OnClick", b.name);
                    callFrameScript(pressedWid_[i], "PostClick", b.name);

                    // A second click on the same frame, soon enough after the
                    // first, is also a double click — WoW sends both, and the
                    // frames that care about one usually care about the other.
                    const double now = core::appTimeSeconds();
                    if (pressedWid_[i] == lastClickWid_ &&
                        (now - lastClickTime_) <= kDoubleClickSeconds) {
                        callFrameScript(pressedWid_[i], "OnDoubleClick", b.name);
                        // Cleared, or a third click reads as a second double.
                        lastClickWid_ = 0;
                        lastClickTime_ = 0.0;
                    } else {
                        lastClickWid_ = pressedWid_[i];
                        lastClickTime_ = now;
                    }
                }
                // The other half of the press report: a click that lands and a
                // click that is handled are different things, and the gap
                // between them is where a button that looks right does
                // nothing. Says which of the three conditions refused it.
                if (pressedWid_[i] != 0 && pressed && !pressed->name.empty()) {
                    LOG_WARNING("WidgetInput: release on ", pressed->name,
                                pressedWid_[i] != releasedOn ? " — cursor had moved off it"
                                : !pressed->enabled  ? " — the frame is disabled"
                                : !takesIt           ? " — it did not register for this button"
                                                     : " — OnClick ran");
                }
            }
            pressedWid_[i] = 0;
        }
    }
}

/// The frame a click on `wid` belongs to: the nearest one, itself or above it,
/// that registered for this button.
///
/// A click lands on the topmost frame taking the mouse, which is not always the
/// one meant to answer it. A unit frame's health bar takes the mouse so it can
/// show its numbers on hover, and it sits over the button that does the
/// targeting — so without this, clicking a target frame anywhere but its border
/// did nothing at all. Drags already resolve their owner this way.
///
/// Falls back to the frame that was hit when nothing above it wants the button,
/// so the refusal is still reported against the frame the player actually
/// clicked rather than against UIParent.
uint32_t LuaEngine::clickOwnerOf(uint32_t wid, const char* button) {
    for (uint32_t id = wid; id != 0;) {
        const auto* cand = widgets_.get(id);
        if (!cand) break;
        if (frameAcceptsClick(id, button)) return id;
        id = cand->parent;
    }
    return wid;
}

/// Ask the interface what it can see, in its own words.
///
/// The widget report says whether a frame is shown; this says whether it should
/// be. A hidden target frame is correct with no target and a fault with one, and
/// from the tree alone those are the same line.
void LuaEngine::runInterfaceProbe() {
    if (!L_) return;
    const bool ok = executeString(
        "local function yn(v) return v and 'yes' or 'no' end\n"
        "local auras = 0\n"
        "for i = 1, 40 do if not UnitAura('player', i) then break end auras = i end\n"
        "__WoweeWarn('[fxcheck] target=' .. yn(UnitExists('target')) ..\n"
        "      ' name=' .. tostring(UnitExists('target') and UnitName('target')) ..\n"
        "      ' | TargetFrame shown=' .. yn(TargetFrame and TargetFrame:IsShown()) ..\n"
        "      ' unit=' .. tostring(TargetFrame and TargetFrame.unit) ..\n"
        "      ' | player auras=' .. auras ..\n"
        "      ' | XP=' .. tostring(UnitXP('player')) .. '/' .. tostring(UnitXPMax('player')) ..\n"
        "      ' | bag0 slots=' .. tostring(GetContainerNumSlots(0)))\n"
        // The player frame's top-left icon comes from these three, and which
        // one is answering wrongly is not visible from the widget tree: the
        // texture is set from Lua, so an icon that should not be there looks
        // exactly like one that should.
        "__WoweeWarn('[fxcheck] pvp=' .. yn(UnitIsPVP('player')) ..\n"
        "      ' ffa=' .. yn(UnitIsPVPFreeForAll('player')) ..\n"
        "      ' faction=' .. tostring(UnitFactionGroup('player')) ..\n"
        "      ' | PlayerPVPIcon shown=' ..\n"
        "      yn(PlayerPVPIcon and PlayerPVPIcon:IsShown()))\n"
        // The state icons share one sheet and one corner with the PvP icon, so
        // "a badge at the top left" does not say which of them it is.
        "__WoweeWarn('[fxcheck] resting=' .. yn(IsResting()) ..\n"
        "      ' combat=' .. yn(PlayerFrame.inCombat) ..\n"
        "      ' | rest icon=' .. yn(PlayerRestIcon and PlayerRestIcon:IsShown()) ..\n"
        "      ' attack icon=' .. yn(PlayerAttackIcon and PlayerAttackIcon:IsShown()) ..\n"
        "      ' status=' .. yn(PlayerStatusTexture and PlayerStatusTexture:IsShown()))\n");
    if (!ok) LOG_WARNING("interface probe did not run: ", lastError());
}

void LuaEngine::dispatchOnUpdate(float elapsed) {
    // Asked for by the check, and answered here because only this side can ask
    // the interface anything.
    if (ui::frameXmlTakeProbeRequest()) runInterfaceProbe();

    if (!L_) return;

    // Animations first, so a frame's own OnUpdate sees this frame's values
    // rather than the previous one's.
    lua_getglobal(L_, "__WoweeTickAnimations");
    if (lua_isfunction(L_, -1)) {
        lua_pushnumber(L_, elapsed);
        if (lua_pcall(L_, 1, 0, 0) != 0) {
            LOG_WARNING("animation tick error: ", luaL_optstring(L_, -1, "?"));
            lua_pop(L_, 1);
        }
    } else {
        lua_pop(L_, 1);
    }

    lua_getglobal(L_, "__WoweeOnUpdateFrames");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return; }

    int count = static_cast<int>(lua_objlen(L_, -1));
    for (int i = 1; i <= count; i++) {
        lua_rawgeti(L_, -1, i);
        if (!lua_istable(L_, -1)) { lua_pop(L_, 1); continue; }

        // Check if frame is visible
        lua_getfield(L_, -1, "__visible");
        bool visible = lua_toboolean(L_, -1);
        lua_pop(L_, 1);
        if (!visible) { lua_pop(L_, 1); continue; }

        // Get OnUpdate script
        lua_getfield(L_, -1, "__scripts");
        if (lua_istable(L_, -1)) {
            // Below the function, so a handler that fails every frame says
            // where it was reached from rather than only which line broke.
            lua_pushcfunction(L_, luaTracebackHandler);
            const int hIdx = lua_gettop(L_);
            lua_getfield(L_, hIdx - 1, "OnUpdate");
            if (lua_isfunction(L_, -1)) {
                lua_pushvalue(L_, hIdx - 2);  // self (frame)
                lua_pushnumber(L_, static_cast<double>(elapsed));
                if (lua_pcall(L_, 2, 0, hIdx) != 0) {
                    const char* uerr = lua_tostring(L_, -1);
                    std::string uerrStr = uerr ? uerr : "(unknown)";
                    lua_pop(L_, 1);

                    // A handler that fails once will fail every frame, and this
                    // runs every frame: five broken OnUpdates produced five and
                    // a half thousand identical errors in one session, which
                    // costs time and buries everything else in the log.
                    //
                    // After a few tries the handler is unhooked and said so
                    // once. The frame keeps working — it simply stops being
                    // asked to do the thing it cannot do.
                    // Indexed from the handler rather than the top: hIdx - 1
                    // is __scripts, and the traceback handler now sits above
                    // it, so the old relative offsets pointed at the wrong
                    // table.
                    constexpr int kMaxConsecutiveFailures = 5;
                    const int scriptsIdx = hIdx - 1;
                    lua_getfield(L_, scriptsIdx, "__onUpdateFailures");
                    const int failures = static_cast<int>(lua_tointeger(L_, -1)) + 1;
                    lua_pop(L_, 1);
                    lua_pushinteger(L_, failures);
                    lua_setfield(L_, scriptsIdx, "__onUpdateFailures");

                    if (failures >= kMaxConsecutiveFailures) {
                        lua_pushnil(L_);
                        lua_setfield(L_, scriptsIdx, "OnUpdate");
                        LOG_ERROR("LuaEngine: OnUpdate disabled after ", failures,
                                  " failures: ", uerrStr);
                        if (luaErrorCallback_) luaErrorCallback_(uerrStr);
                    } else if (failures == 1) {
                        LOG_ERROR("LuaEngine: OnUpdate error: ", uerrStr);
                        if (luaErrorCallback_) luaErrorCallback_(uerrStr);
                    }
                } else {
                    // Consecutive, so a handler that recovers is not punished
                    // for an early stumble.
                    lua_pushinteger(L_, 0);
                    lua_setfield(L_, hIdx - 1, "__onUpdateFailures");
                }
            } else {
                lua_pop(L_, 1);   // the OnUpdate field, which was not a function
            }
            lua_pop(L_, 1);       // the traceback handler
        }
        lua_pop(L_, 2); // pop __scripts + frame
    }
    lua_pop(L_, 1); // pop __WoweeOnUpdateFrames
}

bool LuaEngine::dispatchSlashCommand(const std::string& command, const std::string& args) {
    if (!L_) return false;

    // Check each SlashCmdList entry: for key NAME, check SLASH_NAME1, SLASH_NAME2, etc.
    lua_getglobal(L_, "SlashCmdList");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }

    std::string cmdLower = command;
    toLowerInPlace(cmdLower);

    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
        // Stack: SlashCmdList, key, handler
        if (!lua_isfunction(L_, -1) || !lua_isstring(L_, -2)) {
            lua_pop(L_, 1);
            continue;
        }
        const char* name = lua_tostring(L_, -2);

        // Check SLASH_<NAME>1 through SLASH_<NAME>9
        for (int i = 1; i <= 9; i++) {
            std::string globalName = "SLASH_" + std::string(name) + std::to_string(i);
            lua_getglobal(L_, globalName.c_str());
            if (lua_isstring(L_, -1)) {
                std::string slashStr = lua_tostring(L_, -1);
                toLowerInPlace(slashStr);
                if (slashStr == cmdLower) {
                    lua_pop(L_, 1); // pop global
                    // Call the handler with args
                    lua_pushvalue(L_, -1); // copy handler
                    lua_pushstring(L_, args.c_str());
                    if (lua_pcall(L_, 1, 0, 0) != 0) {
                        LOG_ERROR("LuaEngine: SlashCmdList['", name, "'] error: ",
                                  lua_tostring(L_, -1));
                        lua_pop(L_, 1);
                    }
                    lua_pop(L_, 3); // pop handler, key, SlashCmdList
                    return true;
                }
            }
            lua_pop(L_, 1); // pop global
        }
        lua_pop(L_, 1); // pop handler, keep key for next iteration
    }
    lua_pop(L_, 1); // pop SlashCmdList
    return false;
}

// ---- SavedVariables serialization ----

static void serializeLuaValue(lua_State* L, int idx, std::string& out, int indent);

static void serializeLuaTable(lua_State* L, int idx, std::string& out, int indent) {
    out += "{\n";
    std::string pad(indent + 2, ' ');
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        out += pad;
        // Key
        if (lua_type(L, -2) == LUA_TSTRING) {
            const char* k = lua_tostring(L, -2);
            out += "[\"";
            for (const char* p = k; *p; ++p) {
                if (*p == '"' || *p == '\\') out += '\\';
                out += *p;
            }
            out += "\"] = ";
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            out += "[" + std::to_string(static_cast<long long>(lua_tonumber(L, -2))) + "] = ";
        } else {
            lua_pop(L, 1);
            continue;
        }
        // Value
        serializeLuaValue(L, lua_gettop(L), out, indent + 2);
        out += ",\n";
        lua_pop(L, 1);
    }
    out += std::string(indent, ' ') + "}";
}

static void serializeLuaValue(lua_State* L, int idx, std::string& out, int indent) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:     out += "nil"; break;
        case LUA_TBOOLEAN: out += lua_toboolean(L, idx) ? "true" : "false"; break;
        case LUA_TNUMBER: {
            double v = lua_tonumber(L, idx);
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", v);
            out += buf;
            break;
        }
        case LUA_TSTRING: {
            const char* s = lua_tostring(L, idx);
            out += "\"";
            for (const char* p = s; *p; ++p) {
                if (*p == '"' || *p == '\\') out += '\\';
                else if (*p == '\n') { out += "\\n"; continue; }
                else if (*p == '\r') continue;
                out += *p;
            }
            out += "\"";
            break;
        }
        case LUA_TTABLE:
            serializeLuaTable(L, idx, out, indent);
            break;
        default:
            out += "nil"; // Functions, userdata, etc. can't be serialized
            break;
    }
}

void LuaEngine::setAddonList(const std::vector<TocFile>& addons) {
    if (!L_) return;
    lua_pushnumber(L_, static_cast<double>(addons.size()));
    lua_setfield(L_, LUA_REGISTRYINDEX, "wowee_addon_count");

    lua_newtable(L_);
    for (size_t i = 0; i < addons.size(); i++) {
        lua_newtable(L_);
        lua_pushstring(L_, addons[i].addonName.c_str());
        lua_setfield(L_, -2, "name");
        lua_pushstring(L_, addons[i].getTitle().c_str());
        lua_setfield(L_, -2, "title");
        auto notesIt = addons[i].directives.find("Notes");
        lua_pushstring(L_, notesIt != addons[i].directives.end() ? notesIt->second.c_str() : "");
        lua_setfield(L_, -2, "notes");
        // Store all TOC directives for GetAddOnMetadata
        lua_newtable(L_);
        for (const auto& [key, val] : addons[i].directives) {
            lua_pushstring(L_, val.c_str());
            lua_setfield(L_, -2, key.c_str());
        }
        lua_setfield(L_, -2, "metadata");
        // Marked, because this list doubles as the answer to IsAddOnLoaded and
        // a load-on-demand addon is listed long before it is loaded. Without
        // the flag, adding them here would report every one of them as loaded
        // from the moment the client started.
        lua_pushboolean(L_, addons[i].isLoadOnDemand() ? 1 : 0);
        lua_setfield(L_, -2, "loadOnDemand");
        lua_rawseti(L_, -2, static_cast<int>(i + 1));
    }
    lua_setfield(L_, LUA_REGISTRYINDEX, "wowee_addon_info");
}

bool LuaEngine::loadSavedVariables(const std::string& path) {
    if (!L_) return false;
    std::ifstream f(path);
    if (!f.is_open()) return false; // No saved data yet — not an error
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.empty()) return true;
    int err = luaL_dostring(L_, content.c_str());
    if (err != 0) {
        LOG_WARNING("LuaEngine: error loading saved variables from '", path, "': ",
                    lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool LuaEngine::saveSavedVariables(const std::string& path, const std::vector<std::string>& varNames) {
    if (!L_ || varNames.empty()) return false;
    std::string output;
    for (const auto& name : varNames) {
        lua_getglobal(L_, name.c_str());
        if (!lua_isnil(L_, -1)) {
            output += name + " = ";
            serializeLuaValue(L_, lua_gettop(L_), output, 0);
            output += "\n";
        }
        lua_pop(L_, 1);
    }
    if (output.empty()) return true;

    // Ensure directory exists
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        std::error_code ec;
        std::filesystem::create_directories(path.substr(0, lastSlash), ec);
    }

    std::ofstream f(path);
    if (!f.is_open()) {
        LOG_WARNING("LuaEngine: cannot write saved variables to '", path, "'");
        return false;
    }
    f << output;
    LOG_INFO("LuaEngine: saved variables to '", path, "' (", output.size(), " bytes)");
    return true;
}

namespace {

/// Appends the Lua call stack to an error message.
///
/// An error says where it happened; the interesting part is nearly always how
/// it got there. "dropdownMenu is nil at unitpopup.lua:484" cost several rounds
/// of reading to trace back to the OnLoad that started it, and the stack was
/// there the whole time — it just was not being asked for. Installed as the
/// message handler so it runs before the stack unwinds.
///
/// Written by hand rather than through debug.traceback because the debug
/// library is deliberately not opened.
int luaTracebackHandler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    std::string out = msg ? msg : "(error)";
    for (int level = 1; level < 12; ++level) {
        lua_Debug ar;
        if (!lua_getstack(L, level, &ar)) break;
        if (!lua_getinfo(L, "Sln", &ar)) break;
        out += "\n      at ";
        out += (ar.short_src[0] ? ar.short_src : "?");
        out += ":" + std::to_string(ar.currentline);
        if (ar.name) { out += " in "; out += ar.name; }
    }
    lua_pushstring(L, out.c_str());
    return 1;
}

/// Loads and runs a chunk with the traceback handler in place. Returns the
/// same non-zero-on-error convention as luaL_dostring.
int runChunk(lua_State* L, const char* chunk, size_t len, const char* name) {
    const int base = lua_gettop(L);
    lua_pushcfunction(L, luaTracebackHandler);
    if (luaL_loadbuffer(L, chunk, len, name) != 0) {
        // A syntax error has no stack to walk; leave the message where the
        // caller expects it and drop the handler underneath it.
        lua_remove(L, base + 1);
        return 1;
    }
    const int rc = lua_pcall(L, 0, 0, base + 1);
    lua_remove(L, base + 1);
    return rc;
}

/// When the running chunk must give up. Wall clock rather than a count of VM
/// instructions: the runaway this was written for spends nearly all its time
/// inside one C binding — a table rehash that grows with every call — so it
/// executes very few Lua instructions per second and a generous instruction
/// budget never came due while the client sat frozen.
std::chrono::steady_clock::time_point gChunkDeadline{};

/// Reports where the VM actually is — the Lua source and line — which a C++
/// backtrace cannot tell you: that only names the binding being called, not
/// the loop calling it.
void runawayHook(lua_State* L, lua_Debug*) {
    if (std::chrono::steady_clock::now() < gChunkDeadline) return;

    std::string where = "unknown";
    lua_Debug info;
    if (lua_getstack(L, 0, &info) && lua_getinfo(L, "Sl", &info)) {
        where = std::string(info.short_src[0] ? info.short_src : "?") + ":" +
                std::to_string(info.currentline);
    }
    // Several levels of it, because the innermost line is often a helper and
    // the loop that will not end is the caller.
    for (int level = 1; level < 6; ++level) {
        lua_Debug up;
        if (!lua_getstack(L, level, &up) || !lua_getinfo(L, "Sln", &up)) break;
        LOG_ERROR("LuaEngine:   called from ",
                  up.short_src[0] ? up.short_src : "?", ":", up.currentline,
                  up.name ? " in " : "", up.name ? up.name : "");
    }
    // Off before unwinding, or it fires again inside the error path.
    lua_sethook(L, nullptr, 0, 0);
    LOG_ERROR("LuaEngine: runaway script aborted at ", where);
    luaL_error(L, "runaway script aborted at %s", where.c_str());
}

/// Installs the deadline for one chunk and takes it off again however that
/// chunk leaves — including by error, which is the case that matters.
struct BudgetGuard {
    lua_State* L;
    explicit BudgetGuard(lua_State* state, unsigned long long ms) : L(state) {
        if (L && ms > 0) {
            gChunkDeadline = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(ms);
            // Every few hundred instructions. A deadline is only as sharp as
            // how often it is looked at, and a loop whose every iteration sits
            // in a slow C call executes very few per second: at 10,000
            // this overran 5s by 43s and then by 99s before the check came
            // round. The check is a clock read, which costs nothing beside the
            // work it is bounding.
            lua_sethook(L, runawayHook, LUA_MASKCOUNT, 500);
        }
    }
    ~BudgetGuard() { if (L) lua_sethook(L, nullptr, 0, 0); }
};

} // namespace

void LuaEngine::bootstrap(const char* code) {
    if (luaL_dostring(L_, code) == 0) return;
    const char* e = lua_tostring(L_, -1);
    const std::string head(code, std::min<size_t>(70, std::strlen(code)));
    LOG_ERROR("LuaEngine: bootstrap chunk failed: ", e ? e : "?",
              "  [chunk began: ", head, "]");
    lua_pop(L_, 1);
}

bool LuaEngine::executeFile(const std::string& path) {
    if (!L_) return false;

    BudgetGuard guard(L_, chunkTimeoutMs_);
    // Read and run rather than luaL_dofile, so the traceback handler is in
    // place: a file that fails deep inside a handler otherwise reports only
    // the line that broke, never the OnLoad that reached it.
    std::string source;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            lastError_ = "cannot open " + path;
            LOG_ERROR("LuaEngine: cannot open '", path, "'");
            return false;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        source = ss.str();
    }
    const std::string chunkName = "@" + path;
    int err = runChunk(L_, source.c_str(), source.size(), chunkName.c_str());
    if (err != 0) {
        const char* errMsg = lua_tostring(L_, -1);
        std::string msg = errMsg ? errMsg : "(unknown error)";
        lastError_ = msg;
        LOG_ERROR("LuaEngine: error loading '", path, "': ", msg);
        if (luaErrorCallback_) luaErrorCallback_(msg);
        if (gameHandler_) {
            game::MessageChatData errChat;
            errChat.type = game::ChatType::SYSTEM;
            errChat.language = game::ChatLanguage::UNIVERSAL;
            errChat.message = "|cffff4040[Lua Error] " + msg + "|r";
            gameHandler_->addLocalChatMessage(errChat);
        }
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool LuaEngine::executeString(const std::string& code) {
    if (!L_) return false;

    BudgetGuard guard(L_, chunkTimeoutMs_);
    int err = runChunk(L_, code.c_str(), code.size(), code.c_str());
    if (err != 0) {
        const char* errMsg = lua_tostring(L_, -1);
        std::string msg = errMsg ? errMsg : "(unknown error)";
        lastError_ = msg;
        LOG_ERROR("LuaEngine: script error: ", msg);
        if (luaErrorCallback_) luaErrorCallback_(msg);
        if (gameHandler_) {
            game::MessageChatData errChat;
            errChat.type = game::ChatType::SYSTEM;
            errChat.language = game::ChatLanguage::UNIVERSAL;
            errChat.message = "|cffff4040[Lua Error] " + msg + "|r";
            gameHandler_->addLocalChatMessage(errChat);
        }
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

} // namespace wowee::addons
