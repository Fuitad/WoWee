#include "ui/settings_schema.hpp"

namespace wowee {
namespace ui {

namespace {

// The settings FrameXML has no control of its own for.
//
// Deliberately not everything the settings window shows. View distance, mouse
// speed, the minimap clock, friendly nameplates, fullscreen, ground clutter and
// the sound volumes are all bound to the CVar that Blizzard's own panel already
// drives — putting them here too would draw a second control for the same value
// and let the two disagree.
//
// Ranges match the settings window's own sliders, because they are the ranges
// the client clamps to; a control that offers more than the client accepts is a
// control that appears to do nothing at the ends.
constexpr SettingDesc kSchema[] = {
    // --- Graphics the Blizzard panel has no equivalent for ---
    {"waterrefraction", "Water refraction",        SettingKind::Bool,  0, 0, 0,      "Graphics"},
    {"shadows",         "Shadows",                 SettingKind::Bool,  0, 0, 0,      "Graphics"},
    {"shadowdistance",  "Shadow distance",         SettingKind::Float, 50, 800, 10,  "Graphics"},
    {"fov",             "Field of view",           SettingKind::Float, 50, 110, 1,   "Graphics"},

    // --- Camera ---
    {"extendedzoom",    "Extended zoom out",       SettingKind::Bool,  0, 0, 0,      "Camera"},
    {"camerastiffness", "Camera stiffness",        SettingKind::Float, 5, 60, 1,     "Camera"},
    {"pivotheight",     "Camera pivot height",     SettingKind::Float, 0.5f, 2.5f, 0.1f, "Camera"},
    {"smoothfollow",    "Smooth camera follow",    SettingKind::Bool,  0, 0, 0,      "Camera"},
    {"idleorbit",       "Idle camera orbit",       SettingKind::Bool,  0, 0, 0,      "Camera"},

    // --- Interface ---
    {"uiopacity",       "Window opacity",          SettingKind::Int,   20, 100, 5,   "Interface"},
    {"minimapsquare",   "Square minimap",          SettingKind::Bool,  0, 0, 0,      "Interface"},
    {"minimapnpcdots",  "Minimap NPC dots",        SettingKind::Bool,  0, 0, 0,      "Interface"},
    {"minimapcoords",   "Minimap coordinates",     SettingKind::Bool,  0, 0, 0,      "Interface"},
    {"latencymeter",    "Latency meter",           SettingKind::Bool,  0, 0, 0,      "Interface"},
    {"separatebags",    "Separate bag windows",    SettingKind::Bool,  0, 0, 0,      "Interface"},
    {"showkeyring",     "Show keyring",            SettingKind::Bool,  0, 0, 0,      "Interface"},
    {"bagscale",        "Bag scale",               SettingKind::Float, 0.5f, 1.5f, 0.05f, "Interface"},
    {"buffbarscale",    "Buff bar scale",          SettingKind::Float, 0.5f, 1.5f, 0.05f, "Interface"},
    {"actionbarscale",  "Action bar scale",        SettingKind::Float, 0.5f, 1.5f, 0.05f, "Interface"},

    // --- Gameplay ---
    {"autosellgrey",    "Auto-sell grey items",    SettingKind::Bool,  0, 0, 0,      "Gameplay"},
    {"autorepair",      "Auto-repair at vendors",  SettingKind::Bool,  0, 0, 0,      "Gameplay"},
    {"woweemusic",      "Include WoWee music",     SettingKind::Bool,  0, 0, 0,      "Gameplay"},
    {"characterspeech", "Character speech",        SettingKind::Bool,  0, 0, 0,      "Gameplay"},
};

}  // namespace

const SettingDesc* clientSettingsSchema(std::size_t& count) {
    count = sizeof(kSchema) / sizeof(kSchema[0]);
    return kSchema;
}

}  // namespace ui
}  // namespace wowee
