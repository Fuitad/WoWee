#pragma once

#include "addons/lua_services.hpp"
#include "ui/widget_tree.hpp"
#include <functional>
#include <string>
#include <vector>

struct lua_State;

namespace wowee::game { class GameHandler; }

namespace wowee::addons {

struct TocFile;  // forward declaration

class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    bool initialize();
    void shutdown();

    bool executeFile(const std::string& path);
    bool executeString(const std::string& code);

    /// Error from the last executeFile/executeString that returned false.
    /// Lets a caller loading many files report them together rather than
    /// leaving the reasons scattered through the log.
    const std::string& lastError() const { return lastError_; }

    void setGameHandler(game::GameHandler* handler);
    void setLuaServices(const LuaServices& services);

    // Fire a WoW event to all registered Lua handlers.
    void fireEvent(const std::string& eventName,
                   const std::vector<std::string>& args = {});

    // Try to dispatch a slash command via SlashCmdList. Returns true if handled.
    bool dispatchSlashCommand(const std::string& command, const std::string& args);

    // Call OnUpdate scripts on all frames that have one.
    void dispatchOnUpdate(float elapsed);

    /// Feed the mouse to the widget tree: hover changes fire OnEnter/OnLeave,
    /// and a press and release on the same frame is a click. Coordinates are
    /// WoW's, origin bottom-left.
    void dispatchMouse(float x, float y, bool leftDown);

    // SavedVariables: load globals from file, save globals to file
    bool loadSavedVariables(const std::string& path);
    bool saveSavedVariables(const std::string& path, const std::vector<std::string>& varNames);

    // Store addon info in registry for GetAddOnInfo/GetNumAddOns
    void setAddonList(const std::vector<TocFile>& addons);

    /// The widget tree CreateFrame and CreateTexture build into. Owned here so
    /// its lifetime matches the Lua state that holds handles into it.
    ui::WidgetTree& widgets() { return widgets_; }

    lua_State* getState() { return L_; }
    bool isInitialized() const { return L_ != nullptr; }

    /// Abort a chunk that runs longer than this many milliseconds, naming the
    /// Lua source and line it was on. Zero disables it.
    ///
    /// A runaway script otherwise freezes the client outright — the load runs
    /// on the main thread, so the window stops responding and the server drops
    /// the connection for want of a heartbeat. A C++ backtrace only says which
    /// binding it was inside; this says which line of Lua kept calling it.
    void setChunkTimeoutMs(unsigned long long ms) { chunkTimeoutMs_ = ms; }

    // Optional callback for Lua errors (displayed as UI errors to the player)
    using LuaErrorCallback = std::function<void(const std::string&)>;
    void setLuaErrorCallback(LuaErrorCallback cb) { luaErrorCallback_ = std::move(cb); }

private:
    lua_State* L_ = nullptr;
    ui::WidgetTree widgets_;
    game::GameHandler* gameHandler_ = nullptr;
    LuaServices luaServices_;
    LuaErrorCallback luaErrorCallback_;
    std::string lastError_;
    unsigned long long chunkTimeoutMs_ = 0;

    void callFrameScript(uint32_t wid, const char* script, const char* arg = nullptr);

    uint32_t hoverWid_ = 0;
    uint32_t pressedWid_ = 0;
    bool leftDown_ = false;

    /// Make unknown globals answer with a no-op instead of erroring, so a large
    /// body of Lua can be brought up and the names it actually needs collected
    /// from a run. Opt-in through WOWEE_LUA_API_FALLBACK.
    void installMissingApiFallback();
    /// Log the names collected, once, at shutdown.
    void reportMissingApi() const;

    void registerCoreAPI();
    void registerEventAPI();
};

} // namespace wowee::addons
