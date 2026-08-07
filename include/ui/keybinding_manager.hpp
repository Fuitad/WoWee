#pragma once

#include <imgui.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

namespace wowee::ui {

/**
 * Manages keybinding configuration for in-game actions.
 * Supports loading/saving from config files and runtime rebinding.
 */
class KeybindingManager {
public:
    enum class Action {
        TOGGLE_CHARACTER_SCREEN,
        TOGGLE_INVENTORY,
        TOGGLE_BAGS,
        TOGGLE_SPELLBOOK,
        TOGGLE_TALENTS,
        TOGGLE_QUESTS,
        TOGGLE_MINIMAP,
        TOGGLE_SETTINGS,
        TOGGLE_CHAT,
        TOGGLE_GUILD_ROSTER,
        TOGGLE_DUNGEON_FINDER,
        TOGGLE_WORLD_MAP,
        TOGGLE_NAMEPLATES,
        TOGGLE_RAID_FRAMES,
        TOGGLE_ACHIEVEMENTS,
        TOGGLE_SKILLS,
        ACTION_COUNT
    };

    static KeybindingManager& getInstance();

    /**
     * Check if an action's keybinding was just pressed.
     * Uses ImGui::IsKeyPressed() internally with the bound key.
     */
    bool isActionPressed(Action action, bool repeat = false);


    /**
     * Get the currently bound key for an action.
     */
    ImGuiKey getKeyForAction(Action action) const;

    /**
     * Rebind an action to a different key.
     */
    void setKeyForAction(Action action, ImGuiKey key);

    /**
     * Reset all keybindings to defaults.
     */
    void resetToDefaults();

    /**
     * Load keybindings from config file.
     */
    void loadFromConfigFile(const std::string& filePath);

    /**
     * Save keybindings to config file.
     */
    void saveToConfigFile(const std::string& filePath) const;

    /**
     * Get human-readable name for an action.
     */
    static const char* getActionName(Action action);

    /**
     * Get all actions for iteration.
     */
    static constexpr int getActionCount() { return static_cast<int>(Action::ACTION_COUNT); }

private:
    KeybindingManager();

    std::unordered_map<int, ImGuiKey> bindings_;  // action -> key

    void initializeDefaults();
};

/**
 * Is someone typing into the interface that ImGui does not draw?
 *
 * There are two interfaces in this client and only one of them is ImGui, so
 * the io.WantTextInput that everything here used to ask is blind to a chat box
 * FrameXML draws. It answers false for the whole time someone is typing into
 * one, and the keys then do double duty: into the box, and into whatever the
 * key otherwise does. Typing "/logout" opened the quest log on the l and the
 * social panel on the o, and walked the character on the o's neighbours.
 *
 * Asked from several places — bindings, the camera, the sheathe key — so the
 * answer lives here once rather than at each of them.
 */
bool interfaceTakingTypedInput();

/// How to answer the question above. Set once, while the interface is built.
void setTypedInputProbe(std::function<bool()> probe);

}  // namespace wowee::ui
