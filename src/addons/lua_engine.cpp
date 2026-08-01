#include "addons/lua_engine.hpp"
#include "ui/widget_tree.hpp"
#include "addons/lua_api_helpers.hpp"
#include "addons/lua_api_registrations.hpp"
#include "addons/toc_parser.hpp"
#include "core/window.hpp"
#include <imgui.h>
#include <fstream>
#include <filesystem>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace wowee::addons {

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
    // Append frame reference
    int len = static_cast<int>(lua_objlen(L, -1));
    lua_pushvalue(L, 1);  // push frame
    lua_rawseti(L, -2, len + 1);
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
    if (strcmp(scriptType, "OnUpdate") == 0) {
        lua_getglobal(L, "__WoweeOnUpdateFrames");
        if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
        if (lua_isfunction(L, 3)) {
            // Add frame to the list
            int len = static_cast<int>(lua_objlen(L, -1));
            lua_pushvalue(L, 1);
            lua_rawseti(L, -2, len + 1);
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
static int lua_Frame_IsShown(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__visible");
    lua_pushboolean(L, lua_toboolean(L, -1));
    return 1;
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
    if (target == 0) {
        const auto* w = tree->get(id);
        target = w ? w->parent : 0;
    }
    tree->setAllPoints(id, target);
    return 0;
}

int lua_Region_SetSize(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) {
        w->width  = static_cast<float>(luaL_optnumber(L, 2, 0));
        w->height = static_cast<float>(luaL_optnumber(L, 3, 0));
    }
    return 0;
}
int lua_Region_SetWidth(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->width = static_cast<float>(luaL_optnumber(L, 2, 0));
    return 0;
}
int lua_Region_SetHeight(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->height = static_cast<float>(luaL_optnumber(L, 2, 0));
    return 0;
}
int lua_Region_GetWidth(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? (w->rectW > 0.0f ? w->rectW : w->width) : 0.0);
    return 1;
}
int lua_Region_GetHeight(lua_State* L) {
    const auto* w = widgetOf(L, 1);
    lua_pushnumber(L, w ? (w->rectH > 0.0f ? w->rectH : w->height) : 0.0);
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
        w->texCoord[0] = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        w->texCoord[1] = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->texCoord[2] = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        w->texCoord[3] = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    }
    return 0;
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
    if (auto* w = widgetOf(L, 1)) {
        w->level = static_cast<int>(luaL_optnumber(L, 2, 0));
        w->levelExplicit = true;
    }
    return 0;
}
int lua_FontString_SetText(lua_State* L) {
    if (auto* w = widgetOf(L, 1)) w->text = luaL_optstring(L, 2, "");
    lua_pushstring(L, luaL_optstring(L, 2, ""));
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

/// Attach the shared region methods to the table on top of the stack.
void installRegionMethods(lua_State* L, bool isTexture, bool isFontString) {
    auto set = [&](const char* name, lua_CFunction fn) {
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, name);
    };
    set("SetPoint", lua_Region_SetPoint);
    set("ClearAllPoints", lua_Region_ClearAllPoints);
    set("SetAllPoints", lua_Region_SetAllPoints);
    set("SetSize", lua_Region_SetSize);
    set("SetWidth", lua_Region_SetWidth);
    set("SetHeight", lua_Region_SetHeight);
    set("GetWidth", lua_Region_GetWidth);
    set("GetHeight", lua_Region_GetHeight);
    set("Show", lua_Region_Show);
    set("Hide", lua_Region_Hide);
    set("IsShown", lua_Region_IsShown);
    set("IsVisible", lua_Region_IsShown);
    set("SetAlpha", lua_Region_SetAlpha);
    set("GetAlpha", lua_Region_GetAlpha);
    set("SetVertexColor", lua_Region_SetVertexColor);
    set("SetDrawLayer", lua_Region_SetDrawLayer);
    if (isTexture) {
        set("SetTexture", lua_Texture_SetTexture);
        set("GetTexture", lua_Texture_GetTexture);
        set("SetTexCoord", lua_Texture_SetTexCoord);
    }
    if (isFontString) {
        set("SetText", lua_FontString_SetText);
        set("GetText", lua_FontString_GetText);
        set("SetJustifyH", lua_FontString_SetJustifyH);
    }
    // Anything still unimplemented stays a no-op rather than an error, which is
    // what keeps a large addon running while the surface is filled in.
    luaL_dostring(L,
        "return function(t) setmetatable(t, { __index = function(_, k) "
        "  if type(k)=='string' and string.find(k,'^%u') then return function() end end "
        "end }) end");
    lua_pushvalue(L, -2);
    lua_call(L, 1, 0);
}

} // namespace

// Frame method: frame:CreateTexture(name, layer) → a real region
static int lua_Frame_CreateTexture(lua_State* L) {
    auto* tree = wowee::addons::getWidgetTree(L);
    const uint32_t parent = widgetIdOf(L, 1);
    const char* name = luaL_optstring(L, 2, "");
    const char* layer = luaL_optstring(L, 3, "ARTWORK");

    lua_newtable(L);
    if (tree) {
        const uint32_t id = tree->create(wowee::ui::WidgetKind::Texture, parent, name ? name : "");
        if (auto* w = tree->get(id)) w->layer = wowee::ui::parseDrawLayer(layer);
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
        if (auto* w = tree->get(id)) w->layer = wowee::ui::parseDrawLayer(layer);
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

// GetCursorPosition() → x, y (screen coordinates, origin top-left)
static int lua_GetCursorPosition(lua_State* L) {
    const auto& io = ImGui::GetIO();
    lua_pushnumber(L, io.MousePos.x);
    lua_pushnumber(L, io.MousePos.y);
    return 2;
}

// GetScreenWidth() → width
static int lua_GetScreenWidth(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* window = svc ? svc->window : nullptr;
    lua_pushnumber(L, window ? window->getWidth() : 1920);
    return 1;
}

// GetScreenHeight() → height
static int lua_GetScreenHeight(lua_State* L) {
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

// CreateFrame(frameType, name, parent, template)
static int lua_CreateFrame(lua_State* L) {
    const char* frameType = luaL_optstring(L, 1, "Frame");
    const char* name = luaL_optstring(L, 2, nullptr);

    // Create the frame table
    lua_newtable(L);

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
            w->mouseEnabled = (ft == "Button" || ft == "CheckButton");
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

    // Remove unsafe globals from base library
    const char* unsafeGlobals[] = {
        "dofile", "loadfile", "load", "collectgarbage", "newproxy", nullptr
    };
    for (const char** g = unsafeGlobals; *g; ++g) {
        lua_pushnil(L_);
        lua_setglobal(L_, *g);
    }

    // Publish the widget tree before any API is registered, so a script that
    // runs during registration still finds it.
    lua_pushlightuserdata(L_, &widgets_);
    lua_setfield(L_, LUA_REGISTRYINDEX, "wowee_widget_tree");

    registerCoreAPI();
    registerEventAPI();

    LOG_INFO("LuaEngine: initialized (Lua 5.1)");
    return true;
}

void LuaEngine::shutdown() {
    if (L_) {
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

    // SlashCmdList table — addons register slash commands here
    lua_newtable(L_);
    lua_setglobal(L_, "SlashCmdList");

    // Frame metatable with methods
    lua_newtable(L_);  // metatable
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "__index"); // metatable.__index = metatable

    static const struct luaL_Reg frameMethods[] = {
        {"RegisterEvent",   lua_Frame_RegisterEvent},
        {"UnregisterEvent", lua_Frame_UnregisterEvent},
        {"SetScript",       lua_Frame_SetScript},
        {"GetScript",       lua_Frame_GetScript},
        {"GetName",         lua_Frame_GetName},
        {"Show",            lua_Region_Show},
        {"Hide",            lua_Region_Hide},
        {"IsShown",         lua_Region_IsShown},
        {"IsVisible",       lua_Region_IsShown}, // alias
        // Geometry goes through the widget tree. The older table-field
        // versions kept the numbers where only Lua could see them, which is
        // why a frame could be sized and positioned and still never appear.
        {"SetPoint",        lua_Region_SetPoint},
        {"ClearAllPoints",  lua_Region_ClearAllPoints},
        {"SetAllPoints",    lua_Region_SetAllPoints},
        {"SetSize",         lua_Region_SetSize},
        {"SetWidth",        lua_Region_SetWidth},
        {"SetHeight",       lua_Region_SetHeight},
        {"GetWidth",        lua_Region_GetWidth},
        {"GetHeight",       lua_Region_GetHeight},
        {"GetCenter",       lua_Frame_GetCenter},
        {"SetAlpha",        lua_Region_SetAlpha},
        {"GetAlpha",        lua_Region_GetAlpha},
        {"EnableMouse",     lua_Frame_EnableMouse},
        {"IsMouseEnabled",  lua_Frame_IsMouseEnabled},
        {"SetFrameStrata",  lua_Frame_SetFrameStrata},
        {"SetFrameLevel",   lua_Frame_SetFrameLevel},
        {"SetParent",       lua_Frame_SetParent},
        {"GetParent",       lua_Frame_GetParent},
        {"CreateTexture",   lua_Frame_CreateTexture},
        {"CreateFontString", lua_Frame_CreateFontString},
        {nullptr, nullptr}
    };
    for (const luaL_Reg* r = frameMethods; r->name; r++) {
        lua_pushcfunction(L_, r->func);
        lua_setfield(L_, -2, r->name);
    }
    lua_setglobal(L_, "__WoweeFrameMT");

    // Add commonly called no-op frame methods to prevent addon errors
    luaL_dostring(L_,
        "local mt = __WoweeFrameMT\n"

        "function mt:GetFrameLevel() return self.__frameLevel or 1 end\n"
        "function mt:GetFrameStrata() return self.__strata or 'MEDIUM' end\n"
        "function mt:EnableMouse(enable) end\n"
        "function mt:EnableMouseWheel(enable) end\n"
        "function mt:SetMovable(movable) end\n"
        "function mt:SetResizable(resizable) end\n"
        "function mt:RegisterForDrag(...) end\n"
        "function mt:SetClampedToScreen(clamped) end\n"
        "function mt:SetBackdrop(backdrop) end\n"
        "function mt:SetBackdropColor(...) end\n"
        "function mt:SetBackdropBorderColor(...) end\n"
        "function mt:ClearAllPoints() end\n"
        "function mt:SetID(id) self.__id = id end\n"
        "function mt:GetID() return self.__id or 0 end\n"
        "function mt:SetScale(scale) self.__scale = scale end\n"
        "function mt:GetScale() return self.__scale or 1.0 end\n"
        "function mt:GetEffectiveScale() return self.__scale or 1.0 end\n"
        "function mt:SetToplevel(top) end\n"
        "function mt:Raise() end\n"
        "function mt:Lower() end\n"
        "function mt:GetLeft() return 0 end\n"
        "function mt:GetRight() return 0 end\n"
        "function mt:GetTop() return 0 end\n"
        "function mt:GetBottom() return 0 end\n"
        "function mt:GetNumPoints() return 0 end\n"
        "function mt:GetPoint(n) return 'CENTER', nil, 'CENTER', 0, 0 end\n"
        "function mt:SetHitRectInsets(...) end\n"
        "function mt:RegisterForClicks(...) end\n"
        "function mt:SetAttribute(name, value) self['attr_'..name] = value end\n"
        "function mt:GetAttribute(name) return self['attr_'..name] end\n"
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
        "function mt:StartMoving() end\n"
        "function mt:StopMovingOrSizing() end\n"
        "function mt:IsMouseOver() return false end\n"
        "function mt:GetObjectType() return 'Frame' end\n"
    );

    // Catch-all for unimplemented widget methods. Frames are logic-only stubs (not
    // natively rendered), so UI-heavy addons call many widget methods we don't model
    // (sliders: SetMinMaxValues/SetValue; check buttons: SetChecked; buttons:
    // SetNormalTexture; etc.). Without this, the first such call raises "attempt to
    // call a nil value" and aborts the addon before it can register its slash commands.
    // WoW widget methods are PascalCase, so an unknown key starting with an uppercase
    // letter is treated as an unimplemented method (harmless no-op); anything else
    // falls through to nil so ordinary addon fields keep their normal (falsy) meaning.
    luaL_dostring(L_,
        "local mt = __WoweeFrameMT\n"
        "local methods = mt\n"
        "local noop = function() end\n"
        "mt.__index = function(tbl, key)\n"
        "    local v = rawget(methods, key)\n"
        "    if v ~= nil then return v end\n"
        "    if type(key) == 'string' and string.find(key, '^%u') then return noop end\n"
        "    return nil\n"
        "end\n"
    );

    // CreateFrame function
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

    // C_Timer implementation via Lua (uses OnUpdate internally)
    luaL_dostring(L_,
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
    luaL_dostring(L_,
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
    luaL_dostring(L_,
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
    luaL_dostring(L_,
        "local LibStub = LibStub or {}\n"
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
    luaL_dostring(L_,
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
    luaL_dostring(L_,
        "function SetDesaturation() end\n"
        "function SetPortraitTexture() end\n"
        "function StopSound() end\n"
        "function UIParent_OnEvent() end\n"
        "UIParent = CreateFrame('Frame', 'UIParent')\n"
        "UIPanelWindows = {}\n"
        "WorldFrame = CreateFrame('Frame', 'WorldFrame')\n"
        // GameTooltip: global tooltip frame used by virtually all addons
        "GameTooltip = CreateFrame('Frame', 'GameTooltip')\n"
        "GameTooltip.__lines = {}\n"
        "function GameTooltip:SetOwner(owner, anchor) self.__owner = owner; self.__anchor = anchor end\n"
        "function GameTooltip:ClearLines() self.__lines = {} end\n"
        "function GameTooltip:AddLine(text, r, g, b, wrap) table.insert(self.__lines, {text=text or '',r=r,g=g,b=b}) end\n"
        "function GameTooltip:AddDoubleLine(l, r, lr, lg, lb, rr, rg, rb) table.insert(self.__lines, {text=(l or '')..'  '..(r or '')}) end\n"
        "function GameTooltip:SetText(text, r, g, b) self.__lines = {{text=text or '',r=r,g=g,b=b}} end\n"
        "function GameTooltip:GetItem()\n"
        "    if self.__itemId and self.__itemId > 0 then\n"
        "        local name = GetItemInfo(self.__itemId)\n"
        "        local _, itemLink = GetItemInfo(self.__itemId)\n"
        "        return name, itemLink or ('|cffffffff|Hitem:'..self.__itemId..':0|h['..tostring(name)..']|h|r')\n"
        "    end\n"
        "    return nil\n"
        "end\n"
        "function GameTooltip:GetSpell()\n"
        "    if self.__spellId and self.__spellId > 0 then\n"
        "        local name = GetSpellInfo(self.__spellId)\n"
        "        return name, nil, self.__spellId\n"
        "    end\n"
        "    return nil\n"
        "end\n"
        "function GameTooltip:GetUnit() return nil end\n"
        "function GameTooltip:NumLines() return #self.__lines end\n"
        "function GameTooltip:GetText() return self.__lines[1] and self.__lines[1].text or '' end\n"
        "function GameTooltip:SetUnitBuff(unit, index, filter)\n"
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
        "function GameTooltip:SetUnitDebuff(unit, index, filter)\n"
        "    self:ClearLines()\n"
        "    local name, rank, icon, count, debuffType, duration, expTime, caster, steal, consolidate, spellId = UnitDebuff(unit, index, filter)\n"
        "    if name then\n"
        "        self:SetText(name, 1, 0, 0)\n"
        "        if debuffType then self:AddLine(debuffType, 0.5, 0.5, 0.5) end\n"
        "        self.__spellId = spellId\n"
        "    end\n"
        "end\n"
        "function GameTooltip:SetHyperlink(link)\n"
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
        "function GameTooltip:SetInventoryItem(unit, slot)\n"
        "    self:ClearLines()\n"
        "    if unit ~= 'player' then return false, false, 0 end\n"
        "    local link = GetInventoryItemLink(unit, slot)\n"
        "    if not link then return false, false, 0 end\n"
        "    local id = link:match('item:(%d+)')\n"
        "    if not id then return false, false, 0 end\n"
        "    local ok = _WoweePopulateItemTooltip(self, tonumber(id))\n"
        "    return ok or false, false, 0\n"
        "end\n"
        "function GameTooltip:SetBagItem(bag, slot)\n"
        "    self:ClearLines()\n"
        "    local tex, count, locked, quality, readable, lootable, link = GetContainerItemInfo(bag, slot)\n"
        "    if not link then return end\n"
        "    local id = link:match('item:(%d+)')\n"
        "    if not id then return end\n"
        "    _WoweePopulateItemTooltip(self, tonumber(id))\n"
        "    if count and count > 1 then self:AddLine('Count: '..count, 0.5, 0.5, 0.5) end\n"
        "end\n"
        "function GameTooltip:SetSpellByID(spellId)\n"
        "    self:ClearLines()\n"
        "    if not spellId or spellId == 0 then return end\n"
        "    local name, rank, icon, castTime, minRange, maxRange = GetSpellInfo(spellId)\n"
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
        "function GameTooltip:SetAction(slot)\n"
        "    self:ClearLines()\n"
        "    if not slot then return end\n"
        "    local actionType, id = GetActionInfo(slot)\n"
        "    if actionType == 'spell' and id and id > 0 then\n"
        "        self:SetSpellByID(id)\n"
        "    elseif actionType == 'item' and id and id > 0 then\n"
        "        _WoweePopulateItemTooltip(self, id)\n"
        "    end\n"
        "end\n"
        "function GameTooltip:FadeOut() end\n"
        "function GameTooltip:SetFrameStrata(...) end\n"
        "function GameTooltip:SetClampedToScreen(...) end\n"
        "function GameTooltip:IsOwned(f) return self.__owner == f end\n"
        // ShoppingTooltip: used by comparison tooltips
        "ShoppingTooltip1 = CreateFrame('Frame', 'ShoppingTooltip1')\n"
        "ShoppingTooltip2 = CreateFrame('Frame', 'ShoppingTooltip2')\n"
        // Error handling stubs (used by many addons)
        "local _errorHandler = function(err) return err end\n"
        "function geterrorhandler() return _errorHandler end\n"
        "function seterrorhandler(fn) if type(fn)=='function' then _errorHandler=fn end end\n"
        "function debugstack(start, count1, count2) return '' end\n"
        "function securecall(fn, ...) if type(fn)=='function' then return fn(...) end end\n"
        "function issecurevariable(...) return false end\n"
        "function issecure() return false end\n"
        // GetCVarBool wraps C-side GetCVar (registered in table) for boolean queries
        "function GetCVarBool(name) return GetCVar(name) == '1' end\n"
        // Misc compatibility stubs
        // GetScreenWidth, GetScreenHeight, GetNumLootItems are now C functions
        // GetFramerate is now a C function
        "function GetNetStats() return 0, 0, 0, 0 end\n"
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
    luaL_dostring(L_,
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
    luaL_dostring(L_,
        "UISpecialFrames = {}\n"
        // Font object stubs — addons reference these for CreateFontString templates
        "GameFontNormal = {}\n"
        "GameFontNormalSmall = {}\n"
        "GameFontNormalLarge = {}\n"
        "GameFontHighlight = {}\n"
        "GameFontHighlightSmall = {}\n"
        "GameFontHighlightLarge = {}\n"
        "GameFontDisable = {}\n"
        "GameFontDisableSmall = {}\n"
        "GameFontWhite = {}\n"
        "GameFontRed = {}\n"
        "GameFontGreen = {}\n"
        "NumberFontNormal = {}\n"
        "ChatFontNormal = {}\n"
        "SystemFont = {}\n"
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
    luaL_dostring(L_,
        "NUM_ACTIONBAR_BUTTONS = 12\n"
        "NUM_ACTIONBAR_PAGES = 6\n"
        "ACTION_BUTTON_SHOW_GRID_REASON_CVAR = 1\n"
        "ACTION_BUTTON_SHOW_GRID_REASON_EVENT = 2\n"
        // Action bar page tracking
        "local _actionBarPage = 1\n"
        "function GetActionBarPage() return _actionBarPage end\n"
        "function ChangeActionBarPage(page) _actionBarPage = page end\n"
        "function GetBonusBarOffset() return 0 end\n"
        // Action type query
        "function GetActionText(slot) return nil end\n"
        "function GetActionCount(slot) return 0 end\n"
        // Binding functions
        "function GetBindingKey(action) return nil end\n"
        "function GetBindingAction(key) return nil end\n"
        "function SetBinding(key, action) end\n"
        "function SaveBindings(which) end\n"
        "function GetCurrentBindingSet() return 1 end\n"
        // Macro functions
        "function GetNumMacros() return 0, 0 end\n"
        "function GetMacroInfo(id) return nil end\n"
        "function GetMacroBody(id) return nil end\n"
        "function GetMacroIndexByName(name) return 0 end\n"
        // Stance bar
        "function GetNumShapeshiftForms() return 0 end\n"
        "function GetShapeshiftFormInfo(index) return nil, nil, nil, nil end\n"
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
        "function GetPetActionInfo(slot) return nil end\n"
        "function GetPetActionsUsable() return false end\n"
    );

    // WoW table/string utility functions used by many addons
    luaL_dostring(L_,
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

void LuaEngine::fireEvent(const std::string& eventName,
                           const std::vector<std::string>& args) {
    if (!L_) return;

    lua_getglobal(L_, "__WoweeEvents");
    if (lua_isnil(L_, -1)) { lua_pop(L_, 1); return; }

    lua_getfield(L_, -1, eventName.c_str());
    if (lua_isnil(L_, -1)) { lua_pop(L_, 2); return; }

    int handlerCount = static_cast<int>(lua_objlen(L_, -1));
    for (int i = 1; i <= handlerCount; i++) {
        lua_rawgeti(L_, -1, i);
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); continue; }

        // Push arguments: event name first, then extra args
        lua_pushstring(L_, eventName.c_str());
        for (const auto& arg : args) {
            lua_pushstring(L_, arg.c_str());
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
    lua_pop(L_, 2);  // pop handler list + WoweeEvents

    // Also dispatch to frames that registered for this event via frame:RegisterEvent()
    lua_getglobal(L_, "__WoweeFrameEvents");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, eventName.c_str());
        if (lua_istable(L_, -1)) {
            int frameCount = static_cast<int>(lua_objlen(L_, -1));
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
                        for (const auto& arg : args) lua_pushstring(L_, arg.c_str());
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
        }
        lua_pop(L_, 1); // pop event frame list
    }
    lua_pop(L_, 1); // pop __WoweeFrameEvents
}

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
    lua_getfield(L_, -1, script);
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 4); return; }

    lua_pushvalue(L_, -3);              // self
    int nargs = 1;
    if (arg) { lua_pushstring(L_, arg); ++nargs; }
    if (lua_pcall(L_, nargs, 0, 0) != 0) {
        const char* err = lua_tostring(L_, -1);
        LOG_ERROR("LuaEngine: ", script, " error: ", err ? err : "?");
        if (luaErrorCallback_) luaErrorCallback_(err ? err : "script error");
        lua_pop(L_, 1);
    }
    lua_pop(L_, 3);
}

void LuaEngine::dispatchMouse(float x, float y, bool leftDown) {
    if (!L_) return;
    const uint32_t hit = widgets_.hitTest(x, y);

    // Hover first, so a frame that appears under a stationary cursor still gets
    // its OnEnter rather than waiting for the mouse to move.
    if (hit != hoverWid_) {
        if (hoverWid_ != 0) callFrameScript(hoverWid_, "OnLeave");
        hoverWid_ = hit;
        if (hoverWid_ != 0) callFrameScript(hoverWid_, "OnEnter");
    }

    if (leftDown && !leftDown_) {
        leftDown_ = true;
        pressedWid_ = hit;
        if (pressedWid_ != 0) callFrameScript(pressedWid_, "OnMouseDown", "LeftButton");
    } else if (!leftDown && leftDown_) {
        leftDown_ = false;
        if (pressedWid_ != 0) {
            callFrameScript(pressedWid_, "OnMouseUp", "LeftButton");
            // A click is press and release on the same frame, which is what lets
            // a player slide off a button to change their mind.
            if (pressedWid_ == hit) callFrameScript(pressedWid_, "OnClick", "LeftButton");
        }
        pressedWid_ = 0;
    }
}

void LuaEngine::dispatchOnUpdate(float elapsed) {
    if (!L_) return;

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
            lua_getfield(L_, -1, "OnUpdate");
            if (lua_isfunction(L_, -1)) {
                lua_pushvalue(L_, -3);  // self (frame)
                lua_pushnumber(L_, static_cast<double>(elapsed));
                if (lua_pcall(L_, 2, 0, 0) != 0) {
                    const char* uerr = lua_tostring(L_, -1);
                    std::string uerrStr = uerr ? uerr : "(unknown)";
                    LOG_ERROR("LuaEngine: OnUpdate error: ", uerrStr);
                    if (luaErrorCallback_) luaErrorCallback_(uerrStr);
                    lua_pop(L_, 1);
                }
            } else {
                lua_pop(L_, 1);
            }
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

bool LuaEngine::executeFile(const std::string& path) {
    if (!L_) return false;

    int err = luaL_dofile(L_, path.c_str());
    if (err != 0) {
        const char* errMsg = lua_tostring(L_, -1);
        std::string msg = errMsg ? errMsg : "(unknown error)";
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

    int err = luaL_dostring(L_, code.c_str());
    if (err != 0) {
        const char* errMsg = lua_tostring(L_, -1);
        std::string msg = errMsg ? errMsg : "(unknown error)";
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
