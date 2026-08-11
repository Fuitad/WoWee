// ============================================================
// SettingsPanel — extracted from GameScreen
// Owns all settings UI rendering, settings state, and
// graphics preset logic.
// ============================================================
#include "ui/settings_panel.hpp"
#include "ui/settings_schema.hpp"
#include "ui/display_modes.hpp"
#include "ui/inventory_screen.hpp"
#include "ui/chat_panel.hpp"
#include "ui/chat/chat_settings.hpp"
#include "ui/keybinding_manager.hpp"
#include "core/application.hpp"
#include "core/config_paths.hpp"
#include "core/logger.hpp"
#include "core/version.hpp"
#include "rendering/renderer.hpp"
#include "rendering/post_process_pipeline.hpp"
#include "rendering/camera.hpp"
#include "rendering/camera_controller.hpp"
#include "rendering/minimap.hpp"
#include "rendering/terrain_manager.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/character_renderer.hpp"
#include "game/zone_manager.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/audio_engine.hpp"
#include "audio/music_manager.hpp"
#include "audio/ambient_sound_manager.hpp"
#include "audio/ui_sound_manager.hpp"
#include "audio/combat_sound_manager.hpp"
#include "audio/spell_sound_manager.hpp"
#include "audio/movement_sound_manager.hpp"
#include "audio/footstep_manager.hpp"
#include "audio/npc_voice_manager.hpp"
#include "audio/player_voice_manager.hpp"
#include "audio/mount_sound_manager.hpp"
#include "audio/activity_sound_manager.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace wowee { namespace ui {

// The interface tab: the client's own windows, its bars, and what it draws
// over the world.
//
// Three schema categories drawn in order, and one button. Every control here
// used to be written out — a slider, an apply, a saveCallback and a greyed
// note beside it, sixty lines of them — with the note saying something the
// options panel on the other side of the bridge said differently or not at
// all. Both windows read the same rows now.
void SettingsPanel::renderSettingsInterfaceTab(std::function<void()> saveCallback) {
    ImGui::Spacing();
    ImGui::BeginChild("InterfaceSettings", ImVec2(0, -1), true);

    ImGui::SeparatorText("Interface");
    drawSchemaCategory("Interface", saveCallback);

    ImGui::Spacing();
    ImGui::SeparatorText("Action Bars");
    drawSchemaCategory("Action Bars", saveCallback);
    // Not a setting: the two offsets are, and this is the way back to where
    // they started without dragging both sliders to zero by eye.
    if (ImGui::Button("Reset Bottom Left Position")) {
        pendingActionBar2OffsetX = 0.0f;
        pendingActionBar2OffsetY = 0.0f;
        saveCallback();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Combat & HUD");
    drawSchemaCategory("Combat & HUD", saveCallback);

    ImGui::EndChild();
}

// The gameplay tab: the camera, the minimap, and what the client does for you
// at a corpse or a vendor.
//
// The mouse-look speed is drawn from the schema like the rest even though the
// game's own Interface panel drives it too — it is a control this window has
// always had, and both write the same value through the same setter, so they
// cannot disagree.
void SettingsPanel::renderSettingsGameplayTab(std::function<void()> saveCallback) {
    auto* renderer = services_.renderer;
    ImGui::Spacing();
    ImGui::BeginChild("GameplaySettings", ImVec2(0, -1), true);

    ImGui::SeparatorText("Camera");
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("Mouse Sensitivity", &pendingMouseSensitivity, 0.05f, 1.0f, "%.2f")) {
        applySettingSideEffects("mousespeed");
        saveCallback();
    }
    drawSchemaCategory("Camera", saveCallback);

    ImGui::Spacing();
    ImGui::SeparatorText("Minimap");
    drawSchemaCategory("Minimap", saveCallback);
    // Not settings: the zoom is the minimap's own state, stepped rather than
    // chosen, and there is no value to store for it.
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    if (ImGui::Button("  -  ")) {
        if (renderer) {
            if (auto* minimap = renderer->getMinimap()) { minimap->zoomOut(); saveCallback(); }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("  +  ")) {
        if (renderer) {
            if (auto* minimap = renderer->getMinimap()) { minimap->zoomIn(); saveCallback(); }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Gameplay");
    drawSchemaCategory("Gameplay", saveCallback);

    ImGui::Spacing();
    ImGui::SeparatorText("Chat");
    drawSchemaCategory("Chat", saveCallback);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Restore Gameplay Defaults", ImVec2(-1, 0))) {
        pendingMouseSensitivity = 0.2f;
        pendingInvertMouse = false;
        pendingExtendedZoom = false;
        pendingSmoothCameraFollow = false;
        pendingUiOpacity = 65;
        pendingMinimapSquare = false;
        pendingMinimapNpcDots = false;
        pendingShowMinimapClock = false;
        pendingShowMinimapCoordinates = false;
        pendingSeparateBags = true;
        pendingShowKeyring = true;
        pendingBagScale = InventoryScreen::recommendedBagScale(ImGui::GetIO().DisplaySize.y);
        pendingShowMicroMenu = false;
        // The applied copies of these — uiOpacity_, minimapSquare_ and the
        // three beside it — were assigned here as well as beside their
        // sliders, which is the same fact in two places and was already one
        // short: the micro menu was reset and nothing was told.
        for (const char* key : {"mousespeed", "invertmouse", "extendedzoom",
                                "smoothfollow", "uiopacity", "minimapsquare",
                                "minimapnpcdots", "minimapclock", "minimapcoords",
                                "separatebags", "showkeyring", "bagscale"}) {
            applySettingSideEffects(key);
        }
        saveCallback();
    }

    ImGui::EndChild();
}

void SettingsPanel::renderSettingsControlsTab(std::function<void()> saveCallback) {
ImGui::Spacing();

ImGui::Text("Keybindings");
ImGui::Separator();

auto& km = ui::KeybindingManager::getInstance();
int numActions = km.getActionCount();

for (int i = 0; i < numActions; ++i) {
    auto action = static_cast<ui::KeybindingManager::Action>(i);
    const char* actionName = km.getActionName(action);
    ImGuiKey currentKey = km.getKeyForAction(action);

    // Display current binding
    ImGui::Text("%s:", actionName);
    ImGui::SameLine(200);

    // Get human-readable key name (basic implementation)
    const char* keyName = "Unknown";
    if (currentKey >= ImGuiKey_A && currentKey <= ImGuiKey_Z) {
        static char keyBuf[16];
        snprintf(keyBuf, sizeof(keyBuf), "%c", 'A' + (currentKey - ImGuiKey_A));
        keyName = keyBuf;
    } else if (currentKey >= ImGuiKey_0 && currentKey <= ImGuiKey_9) {
        static char keyBuf[16];
        snprintf(keyBuf, sizeof(keyBuf), "%c", '0' + (currentKey - ImGuiKey_0));
        keyName = keyBuf;
    } else if (currentKey == ImGuiKey_Escape) {
        keyName = "Escape";
    } else if (currentKey == ImGuiKey_Enter) {
        keyName = "Enter";
    } else if (currentKey == ImGuiKey_Tab) {
        keyName = "Tab";
    } else if (currentKey == ImGuiKey_Space) {
        keyName = "Space";
    } else if (currentKey >= ImGuiKey_F1 && currentKey <= ImGuiKey_F12) {
        static char keyBuf[16];
        snprintf(keyBuf, sizeof(keyBuf), "F%d", 1 + (currentKey - ImGuiKey_F1));
        keyName = keyBuf;
    }

    ImGui::Text("[%s]", keyName);

    // Rebind button
    ImGui::SameLine(350);
    if (ImGui::Button(awaitingKeyPress_ && pendingRebindAction_ == i ? "Waiting..." : "Rebind", ImVec2(100, 0))) {
        pendingRebindAction_ = i;
        awaitingKeyPress_ = true;
    }
}

// Handle key press during rebinding
if (awaitingKeyPress_ && pendingRebindAction_ >= 0) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Press any key to bind to this action (Esc to cancel)...");

    // Check for any key press
    bool foundKey = false;
    ImGuiKey newKey = ImGuiKey_None;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) {
            if (k == ImGuiKey_Escape) {
                // Cancel rebinding
                awaitingKeyPress_ = false;
                pendingRebindAction_ = -1;
                foundKey = true;
                break;
            }
            newKey = static_cast<ImGuiKey>(k);
            foundKey = true;
            break;
        }
    }

    if (foundKey && newKey != ImGuiKey_None) {
        auto action = static_cast<ui::KeybindingManager::Action>(pendingRebindAction_);
        km.setKeyForAction(action, newKey);
        awaitingKeyPress_ = false;
        pendingRebindAction_ = -1;
        saveCallback();
    }
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
    km.resetToDefaults();
    awaitingKeyPress_ = false;
    pendingRebindAction_ = -1;
    saveCallback();
}

}

void SettingsPanel::renderSettingsAudioTab(std::function<void()> saveCallback) {
ImGui::Spacing();
ImGui::BeginChild("AudioSettings", ImVec2(0, -1), true);

// Helper lambda to apply audio settings
auto applyAudioSettings = [&]() {
    applyAudioVolumes(services_.audioCoordinator);
    saveCallback();
};

// Mute is a saved setting that forces the master volume to zero, and until now
// the only control for it was a 20x20 invisible button at the corner of the
// minimap. A client that starts silent because of a flag set by a stray click
// gives no way to find out why from the place a player looks — here.
if (ImGui::Checkbox("Mute All Sound", &soundMuted_)) {
    if (soundMuted_) {
        preMuteVolume_ = audio::AudioEngine::instance().getMasterVolume();
    }
    applyAudioSettings();
}
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Silences everything. The speaker button by the minimap does the same.");

ImGui::Text("Master Volume");
if (ImGui::SliderInt("##MasterVolume", &pendingMasterVolume, 0, 100, "%d%%")) {
    // Raising the volume means the player wants to hear something, so it clears
    // the mute rather than being silently ignored. Dragging this while muted
    // used to do nothing at all, with nothing on screen saying why.
    if (pendingMasterVolume > 0) soundMuted_ = false;
    applyAudioSettings();
}
ImGui::Text("Sound Effects");
if (ImGui::SliderInt("##EffectsVolume", &pendingEffectsVolume, 0, 100, "%d%%")) {
    applyAudioSettings();
}
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("One scale over every sound below. WoW's Sound Effects slider is this one.");

// The rest of the sound settings, from the schema — the same rows the
// interface's Sound panel is built from.
//
// These were thirteen blocks here of label, slider, apply, hint, each naming
// its own field and each free to describe a setting differently from the panel
// on the other side of the bridge. Master and Sound Effects stay written out
// because they are not in that list: the game's own Sound panel drives them,
// and a schema row would draw a second control for each.
drawSchemaCategory("Sound", saveCallback);

ImGui::EndChild();

if (ImGui::Button("Restore Audio Defaults", ImVec2(-1, 0))) {
    pendingMasterVolume = 100;
    pendingMusicVolume = 30; // default music volume
    pendingAmbientVolume = 100;
    pendingBellVolume = 50;
    pendingUiVolume = 100;
    pendingCombatVolume = 100;
    pendingSpellVolume = 100;
    pendingMovementVolume = 100;
    pendingFootstepVolume = 100;
    pendingNpcVoiceVolume = 100;
    pendingMountVolume = 70;
    pendingActivityVolume = 100;
    pendingCharacterSpeech = true;
    applyAudioSettings();
}

}

void SettingsPanel::renderSettingsAboutTab() {
ImGui::Spacing();
ImGui::Spacing();

ImGui::TextWrapped("WoWee - World of Warcraft Client Emulator");
ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

ImGui::Text("Developer");
ImGui::Indent();
ImGui::Text("Kelsi Davis");
ImGui::Unindent();
ImGui::Spacing();

ImGui::Text("GitHub");
ImGui::Indent();
ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "https://github.com/Kelsidavis/WoWee");
if (ImGui::IsItemHovered()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("Click to copy");
}
if (ImGui::IsItemClicked()) {
    ImGui::SetClipboardText("https://github.com/Kelsidavis/WoWee");
}
ImGui::Unindent();
ImGui::Spacing();

ImGui::Text("Contact");
ImGui::Indent();
ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "github.com/Kelsidavis");
if (ImGui::IsItemHovered()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("Click to copy");
}
if (ImGui::IsItemClicked()) {
    ImGui::SetClipboardText("https://github.com/Kelsidavis");
}
ImGui::Unindent();

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

ImGui::TextWrapped("A multi-expansion WoW client supporting Classic, TBC, and WotLK (3.3.5a).");
ImGui::Spacing();
ImGui::TextDisabled("Built with Vulkan, SDL2, and ImGui");

}

void SettingsPanel::renderSettingsWindow(ChatPanel& chatPanel,
                                             std::function<void()> saveCallback) {
    if (!showSettingsWindow) return;

    auto* window = services_.window;
    auto* renderer = services_.renderer;
    if (!window) return;

    // Shared with the interface's own video panel, whose dropdown carries a
    // position in this list rather than a size — see ui/display_modes.hpp.
    const auto& kResolutions = kDisplayResolutions;
    constexpr int kResCount = kNumDisplayResolutions;
    constexpr int kDefaultResW = 1920;
    constexpr int kDefaultResH = 1080;
    constexpr bool kDefaultFullscreen = false;
    constexpr bool kDefaultVsync = true;
    constexpr bool kDefaultShadows = true;
    constexpr int kDefaultGroundClutterDensity = 100;

    int defaultResIndex = 0;
    for (int i = 0; i < kResCount; i++) {
        if (kResolutions[i][0] == kDefaultResW && kResolutions[i][1] == kDefaultResH) {
            defaultResIndex = i;
            break;
        }
    }

    if (!settingsInit) {
        pendingFullscreen = window->isFullscreen();
        pendingVsync = window->isVsyncEnabled();
        if (renderer) {
            renderer->setShadowsEnabled(pendingShadows);
            renderer->setShadowDistance(pendingShadowDistance);
            // Read non-volume settings from actual state (volumes come from saved settings)
            if (auto* cameraController = renderer->getCameraController()) {
                cameraController->setMouseSensitivity(pendingMouseSensitivity);
                cameraController->setInvertMouse(pendingInvertMouse);
                cameraController->setExtendedZoom(pendingExtendedZoom);
                cameraController->setCameraSmoothSpeed(pendingCameraStiffness);
                cameraController->setPivotHeight(pendingPivotHeight);
                cameraController->setIdleOrbitEnabled(pendingIdleCameraOrbit);
                cameraController->setSmoothCameraFollow(pendingSmoothCameraFollow);
            }
        }
        pendingResIndex = 0;
        int curW = window->getWidth();
        int curH = window->getHeight();
        if (!displaySettingsLoaded_) {
            pendingResolutionWidth = curW;
            pendingResolutionHeight = curH;
        }
        long long bestDistance = std::numeric_limits<long long>::max();
        for (int i = 0; i < kResCount; i++) {
            const long long dx = static_cast<long long>(kResolutions[i][0]) - pendingResolutionWidth;
            const long long dy = static_cast<long long>(kResolutions[i][1]) - pendingResolutionHeight;
            const long long distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                pendingResIndex = i;
            }
        }
        pendingUiOpacity = static_cast<int>(std::lround(uiOpacity_ * 100.0f));
        pendingMinimapRotate = minimapRotate_;
        pendingMinimapSquare = minimapSquare_;
        pendingMinimapNpcDots = minimapNpcDots_;
        pendingShowMinimapClock = showMinimapClock_;
        pendingShowMinimapCoordinates = showMinimapCoordinates_;
        pendingShowLatencyMeter = showLatencyMeter_;
        if (renderer) {
            if (auto* minimap = renderer->getMinimap()) {
                minimap->setRotateWithCamera(minimapRotate_);
                minimap->setSquareShape(minimapSquare_);
            }
            if (auto* zm = renderer->getZoneManager()) {
                pendingUseOriginalSoundtrack = zm->getUseOriginalSoundtrack();
            }
        }
        settingsInit = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;
    // Give the settings surface enough room on high-resolution displays while
    // retaining a sensible minimum for 1080p and laptop screens.
    ImVec2 size(std::clamp(650.0f * appliedWindowUiScale_, 520.0f, screenW * 0.90f),
                std::clamp(std::min(screenH * 0.90f, 900.0f * appliedWindowUiScale_), 560.0f, screenH * 0.90f));
    ImVec2 pos((screenW - size.x) * 0.5f, (screenH - size.y) * 0.5f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin("##SettingsWindow", nullptr, flags)) {
        ImGui::Text("Settings");
        ImGui::SameLine();
        {
            // Right-align the build version against the window's content edge.
            const char* version = core::kVersionString;
            float versionWidth = ImGui::CalcTextSize(version).x;
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - versionWidth);
            ImGui::TextDisabled("%s", version);
        }
        ImGui::Separator();

        // Keep the action row outside the scrolling tab region so it remains
        // visible regardless of which tab or section is active.
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 18.0f;
        ImGui::BeginChild("SettingsTabRegion", ImVec2(0, -footerHeight), false);
        // A tab named by whoever opened the window wins for exactly one frame.
        // FrameXML's game menu asks for Video, Audio or Interface depending on
        // which of its three buttons was pressed.
        auto tabFlagFor = [this](const char* name) -> ImGuiTabItemFlags {
            if (requestedTab_.empty() || requestedTab_ != name) return ImGuiTabItemFlags_None;
            requestedTab_.clear();
            return ImGuiTabItemFlags_SetSelected;
        };
        if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_None)) {
            // ============================================================
            // VIDEO TAB
            // ============================================================
            if (ImGui::BeginTabItem("Video", nullptr, tabFlagFor("Video"))) {
                ImGui::Spacing();

                // Graphics Quality Presets
                {
                    const char* presetLabels[] = { "Custom", "Low", "Medium", "High", "Ultra" };
                    int presetIdx = static_cast<int>(pendingGraphicsPreset);
                    if (ImGui::Combo("Quality Preset", &presetIdx, presetLabels, 5)) {
                        pendingGraphicsPreset = static_cast<GraphicsPreset>(presetIdx);
                        if (pendingGraphicsPreset != GraphicsPreset::CUSTOM) {
                            applyGraphicsPreset(pendingGraphicsPreset);
                            saveCallback();
                        }
                    }
                    ImGui::TextDisabled("Adjust these for custom settings");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Checkbox("Fullscreen", &pendingFullscreen)) {
                    applySettingSideEffects("fullscreen");
                    updateGraphicsPresetFromCurrentSettings();
                    saveCallback();
                }
                if (ImGui::Checkbox("VSync", &pendingVsync)) {
                    applySettingSideEffects("vsync");
                    updateGraphicsPresetFromCurrentSettings();
                    saveCallback();
                }
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::SliderFloat("View Distance", &pendingViewDistance,
                                       400.0f, 2400.0f, "%.0f")) {
                    applySettingSideEffects("viewdistance");
                    updateGraphicsPresetFromCurrentSettings();
                    saveCallback();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Controls terrain, world-object, and doodad draw distance.");
                }
                if (ImGui::Checkbox("Shadows", &pendingShadows)) {
                    applySettingSideEffects("shadows");
                    updateGraphicsPresetFromCurrentSettings();
                    saveCallback();
                }
                if (pendingShadows) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::SliderFloat("Distance##shadow", &pendingShadowDistance, 40.0f, 500.0f, "%.0f")) {
                        applySettingSideEffects("shadowdistance");
                        updateGraphicsPresetFromCurrentSettings();
                        saveCallback();
                    }
                }
                {
                    if (ImGui::Checkbox("Water Refraction", &pendingWaterRefraction)) {
                        applySettingSideEffects("waterrefraction");
                        updateGraphicsPresetFromCurrentSettings();
                        saveCallback();
                    }
                }
                {
                    const char* aaLabels[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
                    bool fsr2Active = renderer && renderer->getPostProcessPipeline()->isFSR2Enabled();
                    if (fsr2Active) {
                        ImGui::BeginDisabled();
                        int disabled = 0;
                        ImGui::Combo("Anti-Aliasing (FSR3)", &disabled, "Off (FSR3 active)\0", 1);
                        ImGui::EndDisabled();
                    } else if (ImGui::Combo("Anti-Aliasing", &pendingAntiAliasing, aaLabels, 4)) {
                        applySettingSideEffects("antialiasing");
                        updateGraphicsPresetFromCurrentSettings();
                        saveCallback();
                    }
                    // FXAA — post-process, combinable with MSAA or FSR3
                    {
                        if (ImGui::Checkbox("FXAA (post-process)", &pendingFXAA)) {
                            applySettingSideEffects("fxaa");
                            updateGraphicsPresetFromCurrentSettings();
                            saveCallback();
                        }
                        if (ImGui::IsItemHovered()) {
                            if (fsr2Active)
                                ImGui::SetTooltip("FXAA applies spatial anti-aliasing after FSR3 upscaling.\nFSR3 + FXAA is the recommended ultra-quality combination.");
                            else
                                ImGui::SetTooltip("FXAA smooths jagged edges as a post-process pass.\nCan be combined with MSAA for extra quality.");
                        }
                    }
                }
                // AMD FidelityFX is not exposed on macOS/MoltenVK. Keep the
                // controls and experimental frame-generation path off that
                // platform rather than advertising unsupported settings.
#ifndef __APPLE__
                // FSR Upscaling
                {
                    // FSR mode selection: Off, FSR 1.0 (Spatial), FSR 3.x (Temporal)
                    const char* fsrModeLabels[] = { "Off", "FSR 1.0 (Spatial)", "FSR 3.x (Temporal)" };
                    int fsrMode = pendingUpscalingMode;
                    if (ImGui::Combo("Upscaling", &fsrMode, fsrModeLabels, 3)) {
                        pendingUpscalingMode = fsrMode;
                        applySettingSideEffects("upscaling");
                        saveCallback();
                    }
                    if (fsrMode > 0) {
                        if (fsrMode == 2 && renderer) {
                            ImGui::TextDisabled("FSR3 backend: %s",
                                renderer->getPostProcessPipeline()->isAmdFsr2SdkAvailable() ? "AMD FidelityFX SDK" : "Internal fallback");
                            if (renderer->getPostProcessPipeline()->isAmdFsr3FramegenSdkAvailable()) {
                                if (ImGui::Checkbox("AMD FSR3 Frame Generation (Experimental)", &pendingAMDFramegen)) {
                                    applySettingSideEffects("framegen");
                                    saveCallback();
                                }
                                const char* runtimeStatus = "Unavailable";
                                if (renderer->getPostProcessPipeline()->isAmdFsr3FramegenRuntimeActive()) {
                                    runtimeStatus = "Active";
                                } else if (renderer->getPostProcessPipeline()->isAmdFsr3FramegenRuntimeReady()) {
                                    runtimeStatus = "Ready";
                                } else {
                                    runtimeStatus = "Unavailable";
                                }
                                ImGui::TextDisabled("Runtime: %s (%s)",
                                    runtimeStatus, renderer->getPostProcessPipeline()->getAmdFsr3FramegenRuntimePath());
                                if (!renderer->getPostProcessPipeline()->isAmdFsr3FramegenRuntimeReady()) {
                                    const std::string& runtimeErr = renderer->getPostProcessPipeline()->getAmdFsr3FramegenRuntimeError();
                                    if (!runtimeErr.empty()) {
                                        ImGui::TextDisabled("Reason: %s", runtimeErr.c_str());
                                    }
                                }
                            } else {
                                ImGui::BeginDisabled();
                                bool disabledFg = false;
                                ImGui::Checkbox("AMD FSR3 Frame Generation (Experimental)", &disabledFg);
                                ImGui::EndDisabled();
                                ImGui::TextDisabled("Requires FidelityFX-SDK framegen headers.");
                            }
                        }
                        const char* fsrQualityLabels[] = { "Native (100%)", "Ultra Quality (77%)", "Quality (67%)", "Balanced (59%)" };
                        // The scale factor each of these means is applySettingSideEffects'
                        // business now; this only has to turn the row the
                        // dropdown shows into the value the setting holds.
                        static constexpr int displayToInternal[] = { 3, 0, 1, 2 };
                        pendingFSRQuality = std::clamp(pendingFSRQuality, 0, 3);
                        int fsrQualityDisplay = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (displayToInternal[i] == pendingFSRQuality) {
                                fsrQualityDisplay = i;
                                break;
                            }
                        }
                        if (ImGui::Combo("FSR Quality", &fsrQualityDisplay, fsrQualityLabels, 4)) {
                            pendingFSRQuality = displayToInternal[fsrQualityDisplay];
                            applySettingSideEffects("fsrquality");
                            saveCallback();
                        }
                        if (ImGui::SliderFloat("FSR Sharpness", &pendingFSRSharpness, 0.0f, 2.0f, "%.1f")) {
                            applySettingSideEffects("fsrsharpness");
                            saveCallback();
                        }
                        if (fsrMode == 2) {
                            ImGui::SeparatorText("FSR3 Tuning");
                            if (ImGui::SliderFloat("Jitter Sign", &pendingFSR2JitterSign, -2.0f, 2.0f, "%.2f")) {
                                applySettingSideEffects("fsrjittersign");
                                saveCallback();
                            }
                            ImGui::TextDisabled("Tip: 0.38 is the current recommended default.");
                        }
                    }
                }
#endif
                if (ImGui::SliderInt("Ground Clutter Density", &pendingGroundClutterDensity, 0, 150, "%d%%")) {
                    applySettingSideEffects("groundclutter");
                    saveCallback();
                }
                if (ImGui::Checkbox("Normal Mapping", &pendingNormalMapping)) {
                    applySettingSideEffects("normalmapping");
                    saveCallback();
                }
                if (pendingNormalMapping) {
                    if (ImGui::SliderFloat("Normal Map Strength", &pendingNormalMapStrength, 0.0f, 2.0f, "%.1f")) {
                        applySettingSideEffects("normalmapstrength");
                        saveCallback();
                    }
                }
                if (ImGui::Checkbox("Parallax Mapping", &pendingPOM)) {
                    applySettingSideEffects("parallax");
                    saveCallback();
                }
                if (pendingPOM) {
                    const char* pomLabels[] = { "Low", "Medium", "High" };
                    if (ImGui::Combo("Parallax Quality", &pendingPOMQuality, pomLabels, 3)) {
                        applySettingSideEffects("parallaxquality");
                        saveCallback();
                    }
                }

                const char* resLabel = "Resolution";
                const char* resItems[kResCount];
                char resBuf[kResCount][16];
                for (int i = 0; i < kResCount; i++) {
                    snprintf(resBuf[i], sizeof(resBuf[i]), "%dx%d", kResolutions[i][0], kResolutions[i][1]);
                    resItems[i] = resBuf[i];
                }
                if (ImGui::Combo(resLabel, &pendingResIndex, resItems, kResCount)) {
                    pendingResolutionWidth = kResolutions[pendingResIndex][0];
                    pendingResolutionHeight = kResolutions[pendingResIndex][1];
                    window->applyResolution(pendingResolutionWidth, pendingResolutionHeight);
                    saveCallback();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderInt("Brightness", &pendingBrightness, 0, 100, "%d%%")) {
                    applySettingSideEffects("brightness");
                    saveCallback();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Restore Video Defaults", ImVec2(-1, 0))) {
                    // The values, then one pass to push them. This used to
                    // assign fourteen fields and then repeat, in a different
                    // order, every renderer call that the sliders above already
                    // make — and it did not repeat all of them: the resolution
                    // was applied and the anti-aliasing was hardcoded back to
                    // one sample rather than read from the field it had just
                    // set, which is the same value only for as long as the
                    // default stays zero.
                    pendingFullscreen = kDefaultFullscreen;
                    pendingVsync = kDefaultVsync;
                    pendingShadows = kDefaultShadows;
                    pendingShadowDistance = 300.0f;
                    pendingGroundClutterDensity = kDefaultGroundClutterDensity;
                    pendingAntiAliasing = 0;
                    pendingNormalMapping = true;
                    pendingNormalMapStrength = 0.8f;
                    pendingPOM = true;
                    pendingPOMQuality = 1;
                    pendingWaterRefraction = false;
                    pendingBrightness = 50;
                    pendingResIndex = defaultResIndex;
                    pendingResolutionWidth = kDefaultResW;
                    pendingResolutionHeight = kDefaultResH;
                    for (const char* key : {"fullscreen", "vsync", "shadows",
                                            "shadowdistance", "groundclutter",
                                            "antialiasing", "normalmapping",
                                            "normalmapstrength", "parallax",
                                            "parallaxquality", "waterrefraction",
                                            "brightness"}) {
                        applySettingSideEffects(key);
                    }
                    // Not a setting with a key of its own: the window is told
                    // directly, as it is by the resolution dropdown.
                    window->applyResolution(pendingResolutionWidth, pendingResolutionHeight);
                    updateGraphicsPresetFromCurrentSettings();
                    saveCallback();
                }

                ImGui::EndTabItem();
            }

            // ============================================================
            // INTERFACE TAB
            // ============================================================
            if (ImGui::BeginTabItem("Interface", nullptr, tabFlagFor("Interface"))) {
                renderSettingsInterfaceTab(saveCallback);
                ImGui::EndTabItem();
            }

            // ============================================================
            // AUDIO TAB
            // ============================================================
            if (ImGui::BeginTabItem("Audio", nullptr, tabFlagFor("Audio"))) {
                renderSettingsAudioTab(saveCallback);
                ImGui::EndTabItem();
            }

            // ============================================================
            // GAMEPLAY TAB
            // ============================================================
            if (ImGui::BeginTabItem("Gameplay", nullptr, tabFlagFor("Gameplay"))) {
                renderSettingsGameplayTab(saveCallback);
                ImGui::EndTabItem();
            }

            // ============================================================
            // CONTROLS TAB
            // ============================================================
            if (ImGui::BeginTabItem("Controls", nullptr, tabFlagFor("Controls"))) {
                renderSettingsControlsTab(saveCallback);
                ImGui::EndTabItem();
            }

            // ============================================================
            // CHAT TAB
            // ============================================================
            if (ImGui::BeginTabItem("Chat", nullptr, tabFlagFor("Chat"))) {
                chatPanel.renderSettingsTab(saveCallback);
                ImGui::EndTabItem();
            }

            // ============================================================
            // ABOUT TAB
            // ============================================================
            if (ImGui::BeginTabItem("About", nullptr, tabFlagFor("About"))) {
                renderSettingsAboutTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 10.0f));
        float saveBtnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Save Settings", ImVec2(saveBtnW, 0))) {
            saveCallback();
        }
        ImGui::SameLine();
        if (ImGui::Button("Back to Game", ImVec2(-1, 0))) {
            showSettingsWindow = false;
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

void SettingsPanel::drawSchemaCategory(const char* category,
                                       const std::function<void()>& saveCallback) {
    std::size_t count = 0;
    const auto* schema = clientSettingsSchema(count);
    std::string heading;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& d = schema[i];
        if (std::string(d.category) != category) continue;
        if (d.section[0] != '\0' && d.section != heading) {
            heading = d.section;
            ImGui::SeparatorText(d.section);
        }

        // Read, draw, and write back only if it moved. The value lives in a
        // field somewhere, but which field is the binding table's business —
        // this side only ever sees the key.
        const std::string current = settingValue(d.key);
        bool changed = false;
        switch (d.kind) {
            case SettingKind::Bool: {
                bool v = settingIsOn(current);
                if (ImGui::Checkbox(d.label, &v)) {
                    changed = setSettingValue(d.key, v ? "1" : "0");
                }
                break;
            }
            case SettingKind::Int: {
                int v = std::atoi(current.c_str());
                if (ImGui::SliderInt(d.label, &v, static_cast<int>(d.minValue),
                                     static_cast<int>(d.maxValue))) {
                    changed = setSettingValue(d.key, std::to_string(v));
                }
                break;
            }
            case SettingKind::Float: {
                float v = static_cast<float>(std::atof(current.c_str()));
                if (ImGui::SliderFloat(d.label, &v, d.minValue, d.maxValue, "%.2f")) {
                    changed = setSettingValue(d.key, settingNumberText(v));
                }
                break;
            }
            case SettingKind::Enum: {
                // The choices are one string separated by bars, because that is
                // what crosses to Lua; ImGui wants them as an array.
                std::vector<std::string> labels;
                std::string choices = d.choices;
                for (std::size_t at = 0; at != std::string::npos;) {
                    const std::size_t bar = choices.find('|', at);
                    labels.push_back(choices.substr(
                        at, bar == std::string::npos ? bar : bar - at));
                    at = (bar == std::string::npos) ? bar : bar + 1;
                }
                std::vector<const char*> items;
                for (const auto& label : labels) items.push_back(label.c_str());
                int v = std::atoi(current.c_str());
                if (ImGui::Combo(d.label, &v, items.data(),
                                 static_cast<int>(items.size()))) {
                    changed = setSettingValue(d.key, std::to_string(v));
                }
                break;
            }
        }
        // The one setting whose control cannot simply apply as it moves: the
        // window scale resizes the window the slider is in, so applying it
        // per frame walks the slider out from under the pointer. The flag is
        // what applyWindowUiScale waits on, and it is read every frame from
        // GameScreen rather than called from here.
        if (std::string(d.key) == "windowuiscale") {
            if (ImGui::IsItemActive()) windowUiScaleEditing_ = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) windowUiScaleEditing_ = false;
        }
        if (d.tooltip[0] != '\0' && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", d.tooltip);
        }
        if (changed && saveCallback) saveCallback();
    }
}

void SettingsPanel::applyWindowUiScale() {
    if (!ImGui::GetCurrentContext()) return;

    pendingWindowUiScale = std::clamp(pendingWindowUiScale, 0.75f, 1.5f);
    if (windowUiScaleEditing_) return;
    if (std::abs(appliedWindowUiScale_ - pendingWindowUiScale) < 0.0001f) {
        ImGui::GetIO().FontGlobalScale = pendingWindowUiScale;
        return;
    }

    // Scale from the currently applied value, not from the already-scaled
    // style, so dragging the slider back and forth never compounds rounding.
    const float ratio = pendingWindowUiScale / appliedWindowUiScale_;
    ImGui::GetStyle().ScaleAllSizes(ratio);
    ImGui::GetIO().FontGlobalScale = pendingWindowUiScale;
    appliedWindowUiScale_ = pendingWindowUiScale;
}

namespace {

/// What each quality preset means, in the order Low, Medium, High, Ultra.
///
/// One row per preset, where there used to be a block per preset — the same
/// ten settings assigned and then pushed at the renderer four times over. The
/// blocks had drifted apart, as four copies of one fact do:
///
///   * Low set a shadow distance of 100 and never told the renderer, so the
///     field said 100 and the shadows stayed at whatever the last preset left.
///   * Only Ultra had an opinion about FXAA. Going from Ultra to Low turned
///     everything else down and left FXAA running.
///   * Low left the normal map strength and parallax quality at whatever they
///     were, which does not matter while both are off and does the moment one
///     is switched back on by hand.
///
/// Reading them as a table is also what lets a preset be recognised again
/// afterwards without writing the numbers out a second time.
struct GraphicsPresetValues {
    float viewDistance;
    bool  shadows;
    float shadowDistance;
    int   antiAliasing;      ///< index into the four the panel offers
    bool  fxaa;
    bool  normalMapping;
    float normalMapStrength;
    bool  parallax;
    int   parallaxQuality;
    int   groundClutter;     ///< percent
};

constexpr GraphicsPresetValues kGraphicsPresets[] = {
    /* Low    */ { 600.0f, false, 100.0f, 0, false, false, 0.6f, false, 0,  25},
    /* Medium */ {1000.0f, true,  200.0f, 1, false, true,  0.6f, true,  0,  60},
    /* High   */ {1600.0f, true,  350.0f, 2, false, true,  0.8f, true,  1, 100},
    /* Ultra  */ {2400.0f, true,  500.0f, 3, true,  true,  1.2f, true,  2, 150},
};

/// The settings a preset has an opinion about, in the order it sets them.
constexpr const char* kGraphicsPresetKeys[] = {
    "viewdistance", "shadows", "shadowdistance", "antialiasing", "fxaa",
    "normalmapping", "normalmapstrength", "parallax", "parallaxquality",
    "groundclutter",
};

}  // namespace

void SettingsPanel::applyGraphicsPreset(GraphicsPreset preset) {
    // Custom is not a set of values — it is the name for "these are whatever
    // you made them", so it changes nothing but the marker.
    const int index = static_cast<int>(preset) - 1;
    if (index >= 0 && index < static_cast<int>(std::size(kGraphicsPresets))) {
        const auto& p = kGraphicsPresets[index];
        pendingViewDistance      = p.viewDistance;
        pendingShadows           = p.shadows;
        pendingShadowDistance    = p.shadowDistance;
        pendingAntiAliasing      = p.antiAliasing;
        pendingFXAA              = p.fxaa;
        pendingNormalMapping     = p.normalMapping;
        pendingNormalMapStrength = p.normalMapStrength;
        pendingPOM               = p.parallax;
        pendingPOMQuality        = p.parallaxQuality;
        pendingGroundClutterDensity = p.groundClutter;
        // Each one goes to the thing it affects through the one function that
        // knows where that is, rather than through a second copy of the same
        // renderer calls written out here.
        for (const char* key : kGraphicsPresetKeys) applySettingSideEffects(key);
    }

    currentGraphicsPreset = preset;
    pendingGraphicsPreset = preset;
}

void SettingsPanel::updateGraphicsPresetFromCurrentSettings() {
    // A preset is the current one when the settings are what it sets. The
    // floats are compared with a little room because they arrive off sliders.
    //
    // This was a second copy of the table above, written as a range per field
    // per preset — the same numbers again, plus or minus twenty. A preset whose
    // values were changed in one place and not the other would have stopped
    // recognising itself and read as Custom for good.
    for (int i = 0; i < static_cast<int>(std::size(kGraphicsPresets)); ++i) {
        const auto& p = kGraphicsPresets[i];
        const bool matches =
            std::abs(pendingViewDistance - p.viewDistance) <= 20.0f &&
            pendingShadows == p.shadows &&
            // A preset with shadows off says nothing about how far they reach.
            (!p.shadows || std::abs(pendingShadowDistance - p.shadowDistance) <= 20.0f) &&
            pendingAntiAliasing == p.antiAliasing &&
            pendingFXAA == p.fxaa &&
            pendingNormalMapping == p.normalMapping &&
            pendingPOM == p.parallax &&
            std::abs(pendingGroundClutterDensity - p.groundClutter) <= 10;
        if (matches) {
            pendingGraphicsPreset = static_cast<GraphicsPreset>(i + 1);
            return;
        }
    }
    pendingGraphicsPreset = GraphicsPreset::CUSTOM;
}

std::string SettingsPanel::getSettingsPath() {
    return core::getConfigRoot() + "/settings.cfg";
}


namespace {

/// Which field a setting key names.
///
/// settingValue and setSettingValue used to spell this out separately — one
/// chain of branches reading the fields, another writing them, and nothing at
/// all to say when the two stopped agreeing about which key meant which field.
/// Adding a setting meant remembering both. This is the fact once; both
/// directions read it.
///
/// Exactly one of the three pointers is set. `fraction` marks the values that
/// travel as a fraction of what the field holds, which is how a CVar carries a
/// percentage.
struct FieldBinding {
    const char* key;
    bool  SettingsPanel::* asBool  = nullptr;
    int   SettingsPanel::* asInt   = nullptr;
    float SettingsPanel::* asFloat = nullptr;
    bool  fraction = false;
};

constexpr FieldBinding kFieldBindings[] = {
    // Bound to a Blizzard control as well, through kClientCVars. These are the
    // six the game's own Video, Sound and Interface panels drive, so they are
    // not in the schema — but they still have to be readable and writable,
    // because that is how those panels reach them.
    {.key = "viewdistance",   .asFloat = &SettingsPanel::pendingViewDistance},
    {.key = "mousespeed",     .asFloat = &SettingsPanel::pendingMouseSensitivity},
    {.key = "minimapclock",   .asBool  = &SettingsPanel::pendingShowMinimapClock},
    {.key = "friendlyplates", .asBool  = &SettingsPanel::showFriendlyNameplates_},
    {.key = "groundclutter",  .asInt   = &SettingsPanel::pendingGroundClutterDensity,
     .fraction = true},
    {.key = "effectsvolume",  .asInt   = &SettingsPanel::pendingEffectsVolume,
     .fraction = true},

    // --- Graphics ---
    {.key = "shadows",           .asBool  = &SettingsPanel::pendingShadows},
    {.key = "shadowdistance",    .asFloat = &SettingsPanel::pendingShadowDistance},
    {.key = "waterrefraction",   .asBool  = &SettingsPanel::pendingWaterRefraction},
    {.key = "antialiasing",      .asInt   = &SettingsPanel::pendingAntiAliasing},
    {.key = "fxaa",              .asBool  = &SettingsPanel::pendingFXAA},
    {.key = "normalmapping",     .asBool  = &SettingsPanel::pendingNormalMapping},
    {.key = "normalmapstrength", .asFloat = &SettingsPanel::pendingNormalMapStrength},
    {.key = "parallax",          .asBool  = &SettingsPanel::pendingPOM},
    {.key = "parallaxquality",   .asInt   = &SettingsPanel::pendingPOMQuality},

    // --- Upscaling ---
    {.key = "upscaling",     .asInt   = &SettingsPanel::pendingUpscalingMode},
    {.key = "fsrquality",    .asInt   = &SettingsPanel::pendingFSRQuality},
    {.key = "fsrsharpness",  .asFloat = &SettingsPanel::pendingFSRSharpness},
    {.key = "framegen",      .asBool  = &SettingsPanel::pendingAMDFramegen},
    {.key = "fsrjittersign", .asFloat = &SettingsPanel::pendingFSR2JitterSign},

    // --- Display ---
    {.key = "fullscreen", .asBool = &SettingsPanel::pendingFullscreen},
    {.key = "vsync",      .asBool = &SettingsPanel::pendingVsync},
    {.key = "brightness", .asInt  = &SettingsPanel::pendingBrightness},

    // --- Camera ---
    {.key = "fov",             .asFloat = &SettingsPanel::pendingFov},
    {.key = "extendedzoom",    .asBool  = &SettingsPanel::pendingExtendedZoom},
    {.key = "camerastiffness", .asFloat = &SettingsPanel::pendingCameraStiffness},
    {.key = "pivotheight",     .asFloat = &SettingsPanel::pendingPivotHeight},
    {.key = "smoothfollow",    .asBool  = &SettingsPanel::pendingSmoothCameraFollow},
    {.key = "idleorbit",       .asBool  = &SettingsPanel::pendingIdleCameraOrbit},
    {.key = "invertmouse",     .asBool  = &SettingsPanel::pendingInvertMouse},

    // --- Interface ---
    {.key = "uiopacity",     .asInt   = &SettingsPanel::pendingUiOpacity},
    {.key = "windowuiscale", .asFloat = &SettingsPanel::pendingWindowUiScale},
    {.key = "latencymeter",  .asBool  = &SettingsPanel::pendingShowLatencyMeter},
    {.key = "micromenu",     .asBool  = &SettingsPanel::pendingShowMicroMenu},
    {.key = "bagscale",      .asFloat = &SettingsPanel::pendingBagScale},
    {.key = "separatebags",  .asBool  = &SettingsPanel::pendingSeparateBags},
    {.key = "showkeyring",   .asBool  = &SettingsPanel::pendingShowKeyring},

    // --- Minimap ---
    {.key = "minimapsquare",  .asBool = &SettingsPanel::pendingMinimapSquare},
    {.key = "minimapnpcdots", .asBool = &SettingsPanel::pendingMinimapNpcDots},
    {.key = "minimapcoords",  .asBool = &SettingsPanel::pendingShowMinimapCoordinates},

    // --- Action bars ---
    {.key = "actionbarscale",  .asFloat = &SettingsPanel::pendingActionBarScale},
    {.key = "buffbarscale",    .asFloat = &SettingsPanel::pendingBuffBarScale},
    {.key = "showbar2",        .asBool  = &SettingsPanel::pendingShowActionBar2},
    {.key = "bar2offsetx",     .asFloat = &SettingsPanel::pendingActionBar2OffsetX},
    {.key = "bar2offsety",     .asFloat = &SettingsPanel::pendingActionBar2OffsetY},
    {.key = "showrightbar",    .asBool  = &SettingsPanel::pendingShowRightBar},
    {.key = "rightbaroffsety", .asFloat = &SettingsPanel::pendingRightBarOffsetY},
    {.key = "showleftbar",     .asBool  = &SettingsPanel::pendingShowLeftBar},
    {.key = "leftbaroffsety",  .asFloat = &SettingsPanel::pendingLeftBarOffsetY},

    // --- Combat and HUD ---
    {.key = "nameplatescale",     .asFloat = &SettingsPanel::nameplateScale_},
    {.key = "dpsmeter",           .asBool  = &SettingsPanel::showDPSMeter_},
    {.key = "cooldowntracker",    .asBool  = &SettingsPanel::showCooldownTracker_},
    {.key = "raretracker",        .asBool  = &SettingsPanel::showRareTracker_},
    {.key = "chesttracker",       .asBool  = &SettingsPanel::showChestTracker_},
    {.key = "damageflash",        .asBool  = &SettingsPanel::damageFlashEnabled_},
    {.key = "lowhealthvignette",  .asBool  = &SettingsPanel::lowHealthVignetteEnabled_},

    // --- Sound ---
    {.key = "musicvolume",     .asInt  = &SettingsPanel::pendingMusicVolume},
    {.key = "ambientvolume",   .asInt  = &SettingsPanel::pendingAmbientVolume},
    {.key = "bellvolume",      .asInt  = &SettingsPanel::pendingBellVolume},
    {.key = "uivolume",        .asInt  = &SettingsPanel::pendingUiVolume},
    {.key = "combatvolume",    .asInt  = &SettingsPanel::pendingCombatVolume},
    {.key = "spellvolume",     .asInt  = &SettingsPanel::pendingSpellVolume},
    {.key = "movementvolume",  .asInt  = &SettingsPanel::pendingMovementVolume},
    {.key = "footstepvolume",  .asInt  = &SettingsPanel::pendingFootstepVolume},
    {.key = "mountvolume",     .asInt  = &SettingsPanel::pendingMountVolume},
    {.key = "activityvolume",  .asInt  = &SettingsPanel::pendingActivityVolume},
    {.key = "npcvoicevolume",  .asInt  = &SettingsPanel::pendingNpcVoiceVolume},
    {.key = "characterspeech", .asBool = &SettingsPanel::pendingCharacterSpeech},
    {.key = "woweemusic",      .asBool = &SettingsPanel::pendingUseOriginalSoundtrack},

    // --- Gameplay ---
    {.key = "autoloot",     .asBool = &SettingsPanel::pendingAutoLoot},
    {.key = "autosellgrey", .asBool = &SettingsPanel::pendingAutoSellGrey},
    {.key = "autorepair",   .asBool = &SettingsPanel::pendingAutoRepair},
};

/// The same, for the settings that belong to the chat panel rather than to
/// this one.
///
/// A separate table because they are fields of a different struct, not because
/// they are a different kind of setting: one lookup tries both, and a caller
/// asking for a setting by name never learns which side answered.
struct ChatFieldBinding {
    const char* key;
    bool ChatSettings::* asBool;
};

constexpr ChatFieldBinding kChatFieldBindings[] = {
    {"joingeneral",      &ChatSettings::autoJoinGeneral},
    {"jointrade",        &ChatSettings::autoJoinTrade},
    {"joinlocaldefense", &ChatSettings::autoJoinLocalDefense},
    {"joinlfg",          &ChatSettings::autoJoinLFG},
    {"joinlocal",        &ChatSettings::autoJoinLocal},
    // Chat's appearance is deliberately absent. Timestamps, the font size, the
    // background and the fade are all fields of this struct too, and all four
    // drive the chat panel this client draws — which is not drawn at all while
    // FrameXML owns chat. The interface has its own controls for each of them,
    // and the timestamp one already reaches the value the chat frame reads.
};

const ChatFieldBinding* findChatFieldBinding(const std::string& key) {
    for (const auto& b : kChatFieldBindings) {
        if (key == b.key) return &b;
    }
    return nullptr;
}

const FieldBinding* findFieldBinding(const std::string& key) {
    for (const auto& b : kFieldBindings) {
        if (key == b.key) return &b;
    }
    return nullptr;
}

/// Whether changing this setting means the audio coordinator has to work the
/// volumes out again.
///
/// Every one of them does, including the two that are not volumes: character
/// speech switches the player voice manager on and off inside the same call,
/// and the effects slider scales seven of the others.
bool isVolumeKey(const std::string& key) {
    return key == "effectsvolume" || key == "musicvolume" || key == "ambientvolume" ||
           key == "bellvolume" || key == "uivolume" || key == "combatvolume" ||
           key == "spellvolume" || key == "movementvolume" || key == "footstepvolume" ||
           key == "mountvolume" || key == "activityvolume" || key == "npcvoicevolume" ||
           key == "characterspeech";
}

}  // namespace

void SettingsPanel::applySettingSideEffects(const std::string& key) {
    // The settings window applies each value where its slider is, so a change
    // made through FrameXML or the Wowee options panel used to update the number
    // and save it and nothing else — the option looked dead until the client was
    // restarted or the same slider was touched in the other window.
    //
    // These are the same calls the sliders make, and nothing more: a setting
    // whose only effect is to be read later, like auto-repair, has no line here
    // and needs none.
    auto* renderer = services_.renderer;
    auto* camera = renderer ? renderer->getCamera() : nullptr;
    auto* cameraController = renderer ? renderer->getCameraController() : nullptr;
    auto* post = renderer ? renderer->getPostProcessPipeline() : nullptr;
    auto* wmo = renderer ? renderer->getWMORenderer() : nullptr;
    auto* chars = renderer ? renderer->getCharacterRenderer() : nullptr;

    if (key == "viewdistance") {
        if (renderer) renderer->setViewDistance(pendingViewDistance);
    } else if (key == "shadows") {
        if (renderer) renderer->setShadowsEnabled(pendingShadows);
    } else if (key == "shadowdistance") {
        if (renderer) renderer->setShadowDistance(pendingShadowDistance);
    } else if (key == "waterrefraction") {
        if (renderer) renderer->setWaterRefractionEnabled(pendingWaterRefraction);
    } else if (key == "groundclutter") {
        if (renderer) {
            if (auto* tm = renderer->getTerrainManager()) {
                tm->setGroundClutterDensityScale(
                    static_cast<float>(pendingGroundClutterDensity) / 100.0f);
            }
        }
    } else if (key == "fov") {
        if (camera) camera->setFov(pendingFov);
    } else if (key == "mousespeed") {
        if (cameraController) cameraController->setMouseSensitivity(pendingMouseSensitivity);
    } else if (key == "extendedzoom") {
        if (cameraController) cameraController->setExtendedZoom(pendingExtendedZoom);
    } else if (key == "camerastiffness") {
        if (cameraController) cameraController->setCameraSmoothSpeed(pendingCameraStiffness);
    } else if (key == "smoothfollow") {
        if (cameraController) cameraController->setSmoothCameraFollow(pendingSmoothCameraFollow);
    } else if (key == "pivotheight") {
        if (cameraController) cameraController->setPivotHeight(pendingPivotHeight);
    } else if (key == "idleorbit") {
        if (cameraController) cameraController->setIdleOrbitEnabled(pendingIdleCameraOrbit);
    } else if (key == "uiopacity") {
        uiOpacity_ = static_cast<float>(pendingUiOpacity) / 100.0f;
    } else if (key == "minimapsquare") {
        minimapSquare_ = pendingMinimapSquare;
        if (renderer) {
            if (auto* mm = renderer->getMinimap()) mm->setSquareShape(pendingMinimapSquare);
        }
    } else if (key == "invertmouse") {
        if (cameraController) cameraController->setInvertMouse(pendingInvertMouse);
    } else if (key == "graphicspreset") {
        applyGraphicsPreset(pendingGraphicsPreset);
    } else if (key == "antialiasing") {
        // The four the panel offers, in the order it offers them.
        static const VkSampleCountFlagBits kSamples[] = {
            VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT};
        if (renderer) {
            renderer->setMsaaSamples(kSamples[std::clamp(pendingAntiAliasing, 0, 3)]);
        }
    } else if (key == "fxaa") {
        if (post) post->setFXAAEnabled(pendingFXAA);
    } else if (key == "normalmapping") {
        if (wmo) wmo->setNormalMappingEnabled(pendingNormalMapping);
        if (chars) chars->setNormalMappingEnabled(pendingNormalMapping);
    } else if (key == "normalmapstrength") {
        if (wmo) wmo->setNormalMapStrength(pendingNormalMapStrength);
        if (chars) chars->setNormalMapStrength(pendingNormalMapStrength);
    } else if (key == "parallax") {
        if (wmo) wmo->setPOMEnabled(pendingPOM);
        if (chars) chars->setPOMEnabled(pendingPOM);
    } else if (key == "parallaxquality") {
        if (wmo) wmo->setPOMQuality(pendingPOMQuality);
        if (chars) chars->setPOMQuality(pendingPOMQuality);
    } else if (key == "upscaling") {
        // pendingFSR is the older flag for "FSR 1 is on" and is what the saved
        // settings still carry, so the mode and the flag are set together
        // rather than left to disagree.
        pendingFSR = (pendingUpscalingMode == 1);
        if (renderer) {
            renderer->setFSREnabled(pendingUpscalingMode == 1);
            renderer->setFSR2Enabled(pendingUpscalingMode == 2);
        }
    } else if (key == "fsrquality") {
        // How far below the display resolution the world is drawn, in the same
        // order the schema lists the choices.
        static constexpr float kScaleFactors[] = {0.77f, 0.67f, 0.59f, 1.00f};
        if (post) post->setFSRQuality(kScaleFactors[std::clamp(pendingFSRQuality, 0, 3)]);
    } else if (key == "fsrsharpness") {
        if (post) post->setFSRSharpness(pendingFSRSharpness);
    } else if (key == "framegen") {
        if (post) post->setAmdFsr3FramegenEnabled(pendingAMDFramegen);
    } else if (key == "fsrjittersign") {
        if (post) {
            post->setFSR2DebugTuning(pendingFSR2JitterSign, pendingFSR2MotionVecScaleX,
                                     pendingFSR2MotionVecScaleY);
        }
    } else if (key == "brightness") {
        // 50 is neutral, so the field is twice the multiplier the pipeline wants.
        if (post) post->setBrightness(static_cast<float>(pendingBrightness) / 50.0f);
    } else if (key == "fullscreen") {
        if (services_.window) {
            services_.window->setFullscreen(pendingFullscreen);
            if (pendingFullscreen) {
                services_.window->applyResolution(pendingResolutionWidth,
                                                  pendingResolutionHeight);
            }
        }
    } else if (key == "vsync") {
        if (services_.window) services_.window->setVsync(pendingVsync);
    } else if (key == "windowuiscale") {
        applyWindowUiScale();
    } else if (key == "minimapnpcdots") {
        minimapNpcDots_ = pendingMinimapNpcDots;
    } else if (key == "minimapclock") {
        showMinimapClock_ = pendingShowMinimapClock;
    } else if (key == "minimapcoords") {
        showMinimapCoordinates_ = pendingShowMinimapCoordinates;
    } else if (key == "latencymeter") {
        showLatencyMeter_ = pendingShowLatencyMeter;
    } else if (key == "separatebags") {
        if (inventoryScreen_) inventoryScreen_->setSeparateBags(pendingSeparateBags);
    } else if (key == "showkeyring") {
        if (inventoryScreen_) inventoryScreen_->setShowKeyring(pendingShowKeyring);
    } else if (key == "bagscale") {
        // The screen clamps, so the field is read back from it rather than left
        // saying something the bags are not doing.
        if (inventoryScreen_) {
            inventoryScreen_->setBagScale(pendingBagScale);
            pendingBagScale = inventoryScreen_->getBagScale();
        }
    } else if (key == "woweemusic") {
        // Not a volume: it changes which tracks the zone rotation can pick, and
        // switching it off has to stop whichever of ours is playing now — the
        // rotation would otherwise honour it only at the next zone change.
        //
        // The interface's options panel has offered this since the schema grew
        // and it did nothing but store the answer, because the only copy of
        // this lived beside the checkbox in the settings window.
        if (renderer) {
            if (auto* zm = renderer->getZoneManager()) {
                zm->setUseOriginalSoundtrack(pendingUseOriginalSoundtrack);
                if (!pendingUseOriginalSoundtrack) {
                    if (auto* ac = renderer->getAudioCoordinator()) {
                        ac->onOriginalSoundtrackDisabled(zm);
                    }
                }
            }
        }
    } else if (isVolumeKey(key)) {
        // Every volume goes through one call, because each of them is a balance
        // against the others and the coordinator works them all out together.
        applyAudioVolumes(services_.audioCoordinator);
    }
}


std::string SettingsPanel::settingValue(const std::string& key) const {
    // The graphics preset is an enum class rather than one of the three field
    // types, and it is the only setting whose value is derived: touching any of
    // the settings it covers moves it to Custom, so what it reads is whatever
    // the others currently amount to.
    if (key == "graphicspreset") {
        return settingNumberText(static_cast<int>(pendingGraphicsPreset));
    }
    if (const ChatFieldBinding* c = findChatFieldBinding(key)) {
        if (!chatSettings_) return {};
        return chatSettings_->*(c->asBool) ? "1" : "0";
    }
    const FieldBinding* b = findFieldBinding(key);
    if (!b) return {};
    if (b->asBool)  return this->*(b->asBool) ? "1" : "0";
    if (b->asInt) {
        const int v = this->*(b->asInt);
        return settingNumberText(b->fraction ? v / 100.0 : static_cast<double>(v));
    }
    const float v = this->*(b->asFloat);
    return settingNumberText(b->fraction ? v / 100.0f : v);
}

bool SettingsPanel::setSettingValue(const std::string& key, const std::string& value) {
    const double v = std::atof(value.c_str());
    const bool on = settingIsOn(value);

    if (key == "graphicspreset") {
        const int idx = std::clamp(static_cast<int>(v + 0.5), 0, 4);
        pendingGraphicsPreset = static_cast<GraphicsPreset>(idx);
        applySettingSideEffects(key);
        return true;
    }
    if (const ChatFieldBinding* c = findChatFieldBinding(key)) {
        if (!chatSettings_) return false;
        chatSettings_->*(c->asBool) = on;
        return true;
    }
    const FieldBinding* b = findFieldBinding(key);
    if (!b) return false;
    if (b->asBool) {
        this->*(b->asBool) = on;
    } else if (b->asInt) {
        this->*(b->asInt) = static_cast<int>((b->fraction ? v * 100.0 : v) + 0.5);
    } else {
        this->*(b->asFloat) = static_cast<float>(b->fraction ? v * 100.0 : v);
    }
    applySettingSideEffects(key);
    return true;
}

void SettingsPanel::applyAudioVolumes(audio::AudioCoordinator* ac) {
    if (!ac) return;
    // Every effect volume is its own balance; this is the one slider over them,
    // which is what Blizzard's Sound Effects control drives.
    const float fx = static_cast<float>(pendingEffectsVolume) / 100.0f;
    float masterScale = soundMuted_ ? 0.0f : static_cast<float>(pendingMasterVolume) / 100.0f;
    audio::AudioEngine::instance().setMasterVolume(masterScale);
    if (auto* music = ac->getMusicManager())
        music->setVolume(pendingMusicVolume);
    if (auto* ambient = ac->getAmbientSoundManager())
    {
        ambient->setVolumeScale(pendingAmbientVolume / 100.0f);
        ambient->setBellVolumeScale(pendingBellVolume / 100.0f);
    }
    if (auto* ui = ac->getUiSoundManager())
        ui->setVolumeScale(fx * pendingUiVolume / 100.0f);
    if (auto* combat = ac->getCombatSoundManager())
        combat->setVolumeScale(fx * pendingCombatVolume / 100.0f);
    if (auto* spell = ac->getSpellSoundManager())
        spell->setVolumeScale(fx * pendingSpellVolume / 100.0f);
    if (auto* movement = ac->getMovementSoundManager())
        movement->setVolumeScale(fx * pendingMovementVolume / 100.0f);
    if (auto* footstep = ac->getFootstepManager())
        footstep->setVolumeScale(fx * pendingFootstepVolume / 100.0f);
    if (auto* npcVoice = ac->getNpcVoiceManager())
        npcVoice->setVolumeScale(fx * pendingNpcVoiceVolume / 100.0f);
    if (auto* playerVoice = ac->getPlayerVoiceManager())
        playerVoice->setEnabled(pendingCharacterSpeech);
    if (auto* mount = ac->getMountSoundManager())
        mount->setVolumeScale(fx * pendingMountVolume / 100.0f);
    if (auto* activity = ac->getActivitySoundManager())
        activity->setVolumeScale(fx * pendingActivityVolume / 100.0f);
}


} // namespace ui
} // namespace wowee
