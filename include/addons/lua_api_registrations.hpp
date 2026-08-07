// lua_api_registrations.hpp — Forward declarations for per-domain Lua API
// registration functions.  Called from LuaEngine::registerCoreAPI().
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#pragma once

struct lua_State;

namespace wowee::addons {

void registerUnitLuaAPI(lua_State* L);
void registerSpellLuaAPI(lua_State* L);
void registerInventoryLuaAPI(lua_State* L);
void registerQuestLuaAPI(lua_State* L);
void registerLfgLuaAPI(lua_State* L);
void registerSocketLuaAPI(lua_State* L);
void registerSocialLuaAPI(lua_State* L);
void registerSystemLuaAPI(lua_State* L);
void registerActionLuaAPI(lua_State* L);

/// Whether this client performs a binding command itself, without the
/// interface.
///
/// A key answered by both is answered twice, and most bindings toggle
/// something — so twice means the panel opens and shuts again on one press and
/// the key reads as dead. Consulted before running a binding.
bool clientActsOnBinding(const std::string& command);

} // namespace wowee::addons
