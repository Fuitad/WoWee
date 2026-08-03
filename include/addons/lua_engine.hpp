#pragma once

#include "addons/lua_services.hpp"
#include "ui/widget_tree.hpp"
#include <functional>
#include <unordered_map>
#include <string>
#include <utility>
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
    /// Feeds the widget tree the cursor and which buttons are held.
    ///
    /// Right-click is not a nicety here: it is how WoW opens nearly every
    /// context menu, so a tree that only sees the left button can be looked at
    /// but not used.
    struct MouseButtons {
        bool left = false;
        bool right = false;
        bool middle = false;
    };
    void dispatchMouse(float x, float y, MouseButtons buttons);

    /// The wheel, to the frame under the cursor that asked for it.
    ///
    /// Returns true when a frame took it, so the caller knows not to also zoom
    /// the camera — scrolling a quest log and pulling the camera in at the same
    /// time is what happens otherwise. delta is WoW's: positive is up.
    bool dispatchMouseWheel(float x, float y, float delta);

    /// Tells a scroll frame when the room its child has to move changed.
    ///
    /// A scroll bar sizes and enables itself from OnScrollRangeChanged, so a
    /// range that becomes true without being announced leaves the bar disabled
    /// beside a frame that can scroll perfectly well. The change is noticed
    /// here, once a frame, because it follows from layout rather than from
    /// anything the interface called.
    void updateScrollRanges();

    /// Fires OnShow and OnHide for anything that appeared or went away.
    ///
    /// FrameXML declares 295 OnShow handlers and 189 OnHide, and they are
    /// where a panel fills itself in: QuestLog_OnShow is what puts quests in
    /// the quest log. None of them had ever run, so every panel opened with
    /// whatever it was built with and nothing since.
    ///
    /// Noticed after layout rather than fired from Show, because hiding a
    /// frame hides everything under it and none of those were told.
    void updateVisibility();

    /// Fires OnSizeChanged for anything whose rect changed. Declared by
    /// FrameXML and used heavily by addons, and never fired until now.
    void updateSizeChanges();

    /// Says once, a little after loading, how many frames are listening for
    /// the events a unit frame lives on.
    ///
    /// A bar that never moves has two possible causes and they need opposite
    /// fixes: the event is not arriving, or nothing registered for it. This
    /// separates them without anyone having to ask for a trace first, because
    /// the question comes up every time and the answer is one number.
    void reportEventListenersOnce();

    /// Log what the interface itself can see, for the takeover check.
    void runInterfaceProbe();

private:
    bool eventListenersReported_ = false;
    int  eventReportFrames_ = 0;
public:

    /// Typed text, one UTF-8 chunk as the platform reports it.
    void dispatchText(const char* utf8);
    /// A key that is not text: backspace, the arrows, enter, escape.
    void dispatchKey(int sdlKeycode, bool ctrlHeld);
    /// Whether an edit box currently has focus, so the client knows not to
    /// treat the same keystrokes as movement.
    bool editBoxHasFocus() const { return focusedWid_ != 0; }
    void setEditFocus(uint32_t wid);

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

    /// Runs a bootstrap Lua chunk and says so when it fails.
    ///
    /// Seventeen of these ran with their result thrown away, so a syntax error
    /// in any one silently removed every method that chunk defined — and the
    /// only symptom was a method quietly answering as though unimplemented.
    void bootstrap(const char* code);

    void callFrameScript(uint32_t wid, const char* script, const char* arg = nullptr);
    /// The same, with a number. A handler that compares its argument against
    /// zero cannot be handed a numeric string.
    void callFrameScriptNumber(uint32_t wid, const char* script, double arg);
    bool frameAcceptsClick(uint32_t wid, const char* button);
    /// The nearest frame at or above `wid` registered for this button, or `wid`
    /// itself when none is. A click lands on the topmost frame taking the
    /// mouse, which is not always the one meant to answer it.
    uint32_t clickOwnerOf(uint32_t wid, const char* button);

    uint32_t hoverWid_ = 0;
    /// The last frame clicked and when, so a second click on it can be told
    /// from the first. WoW's threshold, near enough.
    uint32_t lastClickWid_ = 0;
    double   lastClickTime_ = 0.0;
    static constexpr double kDoubleClickSeconds = 0.4;
    /// The rect each frame had last time round, for OnSizeChanged.
    std::unordered_map<uint32_t, std::pair<float, float>> lastSize_;
    /// The edit box taking keystrokes, or zero. One at a time, which is
    /// what focus means.
    uint32_t focusedWid_ = 0;
    /// Per button, because a press and its release belong together: sliding off
    /// a button between them is how a player changes their mind, and holding
    /// one button while clicking another must not confuse the first.
    static constexpr int kMouseButtons = 3;
    uint32_t pressedWid_[kMouseButtons] = {0, 0, 0};
    bool buttonDown_[kMouseButtons] = {false, false, false};
    /// Where each button went down, so a drag can be told from a click by how
    /// far the cursor has travelled since. Without a threshold every click is a
    /// one-pixel drag and nothing would ever reach OnClick.
    float pressX_[kMouseButtons] = {0.0f, 0.0f, 0.0f};
    float pressY_[kMouseButtons] = {0.0f, 0.0f, 0.0f};
    /// The frame whose OnDragStart has run and not yet been stopped, and the
    /// button that started it. The frame is not always the one the press landed
    /// on — a drag belongs to the nearest ancestor registered for it — so the
    /// release is matched by button rather than by frame.
    uint32_t draggingWid_ = 0;
    int draggingButton_ = -1;
    /// Last cursor position, which is what a moving frame is moved by.
    float cursorX_ = 0.0f, cursorY_ = 0.0f;
    bool haveCursor_ = false;

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
