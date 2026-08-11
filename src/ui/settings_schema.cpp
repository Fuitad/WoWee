#include "ui/settings_schema.hpp"

namespace wowee {
namespace ui {

namespace {

// Every setting this client has, except the six bound to a Blizzard control.
//
// Those six — view distance, mouse speed, the minimap clock, friendly
// nameplates, ground clutter and the sound effects volume — are driven from
// FrameXML's own Video, Sound and Interface panels through kClientCVars, and
// listing them here as well would draw a second control for the same value.
// The root panel names them and says where they are.
//
// The order is the order they are read in: a category is a panel, a section is
// a heading on it, and a setting whose section is "" continues the one above.
// Ranges match the settings window's own sliders, because they are the ranges
// the client clamps to; a control that offers more than the client accepts is a
// control that appears to do nothing at the ends.
constexpr SettingDesc kSchema[] = {
    // ---------------------------------------------------------------- Graphics
    {"graphicspreset", "Quality preset", SettingKind::Enum, 0, 4, 1, "Graphics", "Quality",
     "Sets every graphics option at once. Changing any of them afterwards\n"
     "moves this to Custom.",
     "Custom|Low|Medium|High|Ultra"},
    {"shadows", "Shadows", SettingKind::Bool, 0, 0, 0, "Graphics", "",
     "Cast shadows from the sun and from lights.", ""},
    {"shadowdistance", "Shadow distance", SettingKind::Float, 40, 500, 10, "Graphics", "",
     "How far from you shadows are still drawn.", ""},
    {"waterrefraction", "Water refraction", SettingKind::Bool, 0, 0, 0, "Graphics", "",
     "Bend what is seen through water, rather than drawing it flat.", ""},

    {"antialiasing", "Anti-aliasing", SettingKind::Enum, 0, 3, 1, "Graphics", "Anti-aliasing",
     "Multisampling. Costs memory as well as time, and has no effect while\n"
     "FSR 3 is upscaling — FSR does its own.",
     "Off|2x MSAA|4x MSAA|8x MSAA"},
    {"fxaa", "FXAA", SettingKind::Bool, 0, 0, 0, "Graphics", "",
     "Smooths edges after everything else is drawn. Cheap, slightly soft,\n"
     "and can be used together with MSAA or FSR.", ""},

    {"normalmapping", "Normal mapping", SettingKind::Bool, 0, 0, 0, "Graphics", "Surfaces",
     "Light stone and cloth by their surface detail rather than flat.", ""},
    {"normalmapstrength", "Normal map strength", SettingKind::Float, 0, 2, 0.1f, "Graphics", "",
     "How pronounced that surface detail is. 1 is as authored.", ""},
    {"parallax", "Parallax mapping", SettingKind::Bool, 0, 0, 0, "Graphics", "",
     "Give bricks and cobbles real depth when seen at an angle.", ""},
    {"parallaxquality", "Parallax quality", SettingKind::Enum, 0, 2, 1, "Graphics", "",
     "How many steps each surface is traced with: 16, 32 or 64.",
     "Low|Medium|High"},

    // --------------------------------------------------------------- Upscaling
    {"upscaling", "Upscaling", SettingKind::Enum, 0, 2, 1, "Upscaling", "",
     "Render below your resolution and scale up. FSR 1 is spatial and cheap;\n"
     "FSR 3 is temporal and sharper, and does its own anti-aliasing.",
     "Off|FSR 1 (spatial)|FSR 3 (temporal)"},
    {"fsrquality", "FSR quality", SettingKind::Enum, 0, 3, 1, "Upscaling", "",
     "How far below your resolution the world is drawn.",
     "Ultra Quality (77%)|Quality (67%)|Balanced (59%)|Native (100%)"},
    {"fsrsharpness", "FSR sharpness", SettingKind::Float, 0, 2, 0.1f, "Upscaling", "",
     "Sharpening applied after upscaling.", ""},
    {"framegen", "Frame generation", SettingKind::Bool, 0, 0, 0, "Upscaling", "",
     "Experimental. FSR 3 only, and only where AMD's runtime is present —\n"
     "it is known broken on RADV/Mesa.", ""},
    {"fsrjittersign", "Jitter sign", SettingKind::Float, -2, 2, 0.02f, "Upscaling", "FSR 3 tuning",
     "Which way FSR 3's sub-pixel jitter is applied. 0.38 is the value that\n"
     "currently looks right; the rest of the range is for finding out why.", ""},

    // ----------------------------------------------------------------- Display
    {"fullscreen", "Fullscreen", SettingKind::Bool, 0, 0, 0, "Display", "",
     "Takes effect the next time the window is rebuilt.", ""},
    {"vsync", "Vertical sync", SettingKind::Bool, 0, 0, 0, "Display", "",
     "Wait for the display before showing a frame. Removes tearing, and\n"
     "caps the frame rate at your refresh rate.", ""},
    {"brightness", "Brightness", SettingKind::Int, 0, 100, 1, "Display", "",
     "50 is neutral.", ""},

    // ------------------------------------------------------------------ Camera
    {"fov", "Field of view", SettingKind::Float, 45, 110, 1, "Camera", "",
     "How wide a view the camera takes. 70 is what the original client shows.", ""},
    {"extendedzoom", "Extended zoom out", SettingKind::Bool, 0, 0, 0, "Camera", "",
     "Allow the camera further back than the original client permits.", ""},
    {"camerastiffness", "Camera stiffness", SettingKind::Float, 5, 100, 1, "Camera", "",
     "How closely the camera keeps up with you. Higher is tighter and less\n"
     "floaty.", ""},
    {"pivotheight", "Pivot height", SettingKind::Float, 0, 3, 0.1f, "Camera", "",
     "How far above your feet the camera turns around. Lower feels more\n"
     "attached to the character.", ""},
    {"smoothfollow", "Smooth follow", SettingKind::Bool, 0, 0, 0, "Camera", "",
     "Keep easing the camera while you turn, rather than snapping behind you.", ""},
    {"idleorbit", "Idle orbit", SettingKind::Bool, 0, 0, 0, "Camera", "",
     "Drift the camera slowly around you while you stand still.", ""},
    {"invertmouse", "Invert mouse look", SettingKind::Bool, 0, 0, 0, "Camera", "Mouse",
     "Push the mouse forward to look up.", ""},

    // --------------------------------------------------------------- Interface
    {"uiopacity", "Window opacity", SettingKind::Int, 20, 100, 5, "Interface", "",
     "How solid this client's own windows are drawn.", ""},
    {"windowuiscale", "Window scale", SettingKind::Float, 0.75f, 1.5f, 0.05f, "Interface", "",
     "Size of this client's own windows.", ""},
    {"latencymeter", "Latency meter", SettingKind::Bool, 0, 0, 0, "Interface", "",
     "Show the round trip to the server.", ""},
    {"micromenu", "Micro menu buttons", SettingKind::Bool, 0, 0, 0, "Interface", "",
     "The row of small buttons for the character sheet, spellbook and the rest.", ""},

    {"bagscale", "Bag scale", SettingKind::Float, 0.75f, 1.5f, 0.05f, "Interface", "Bags",
     "Size of the bag windows.", ""},
    {"separatebags", "Separate bag windows", SettingKind::Bool, 0, 0, 0, "Interface", "",
     "One window per bag, rather than everything in one.", ""},
    {"showkeyring", "Show keyring", SettingKind::Bool, 0, 0, 0, "Interface", "",
     "The key ring beside the bags.", ""},

    // ----------------------------------------------------------------- Minimap
    // Rotate-with-camera is deliberately absent. The settings window still
    // draws that checkbox and its handler pins it back off — the minimap is
    // north-up in this client and the control has not worked for as long as it
    // has existed. A tickbox that unticks itself is worse here than no tickbox
    // at all, so this list does not offer one.
    {"minimapsquare", "Square minimap", SettingKind::Bool, 0, 0, 0, "Minimap", "",
     "Draw the map as a square rather than a circle.", ""},
    {"minimapnpcdots", "Nearby NPC dots", SettingKind::Bool, 0, 0, 0, "Minimap", "",
     "Mark creatures near you on the map.", ""},
    {"minimapcoords", "Coordinates", SettingKind::Bool, 0, 0, 0, "Minimap", "",
     "Show your position below the map.", ""},

    // ------------------------------------------------------------- Action Bars
    {"actionbarscale", "Action bar scale", SettingKind::Float, 0.5f, 1.5f, 0.05f,
     "Action Bars", "", "Size of every action bar slot.", ""},
    {"buffbarscale", "Buff bar scale", SettingKind::Float, 0.75f, 1.5f, 0.05f,
     "Action Bars", "", "Size of the buff and debuff icons.", ""},

    {"showbar2", "Bottom left bar", SettingKind::Bool, 0, 0, 0, "Action Bars", "Extra bars",
     "The second bar, above the main one.", ""},
    {"bar2offsetx", "Bottom left — across", SettingKind::Float, -600, 600, 10,
     "Action Bars", "", "Move that bar sideways from its default place.", ""},
    {"bar2offsety", "Bottom left — up", SettingKind::Float, -400, 400, 10,
     "Action Bars", "", "Move that bar up or down from its default place.", ""},
    {"showrightbar", "Right side bar", SettingKind::Bool, 0, 0, 0, "Action Bars", "",
     "The upright bar at the right edge.", ""},
    {"rightbaroffsety", "Right side — up", SettingKind::Float, -400, 400, 10,
     "Action Bars", "", "Move it up or down from the middle of the screen.", ""},
    {"showleftbar", "Left side bar", SettingKind::Bool, 0, 0, 0, "Action Bars", "",
     "The upright bar at the left edge.", ""},
    {"leftbaroffsety", "Left side — up", SettingKind::Float, -400, 400, 10,
     "Action Bars", "", "Move it up or down from the middle of the screen.", ""},

    // ------------------------------------------------------------ Combat & HUD
    {"nameplatescale", "Nameplate scale", SettingKind::Float, 0.5f, 2.0f, 0.05f,
     "Combat & HUD", "Nameplates", "Size of the bars over creatures' heads.", ""},

    {"dpsmeter", "Damage meter", SettingKind::Bool, 0, 0, 0, "Combat & HUD", "Trackers",
     "Your damage and healing per second, while you are in combat.", ""},
    {"cooldowntracker", "Cooldown tracker", SettingKind::Bool, 0, 0, 0, "Combat & HUD", "",
     "Your longer cooldowns, as they run.", ""},
    {"raretracker", "Rare tracker", SettingKind::Bool, 0, 0, 0, "Combat & HUD", "",
     "Mark rare creatures near you on both maps.", ""},
    {"chesttracker", "Chest tracker", SettingKind::Bool, 0, 0, 0, "Combat & HUD", "",
     "Mark chests near you on both maps.", ""},

    {"damageflash", "Damage flash", SettingKind::Bool, 0, 0, 0, "Combat & HUD", "Screen effects",
     "Flash the edges of the screen when you are hit.", ""},
    {"lowhealthvignette", "Low health vignette", SettingKind::Bool, 0, 0, 0,
     "Combat & HUD", "", "A red edge that pulses while you are below a fifth of\n"
     "your health.", ""},

    // ------------------------------------------------------------------- Sound
    {"musicvolume", "Music", SettingKind::Int, 0, 100, 5, "Sound", "",
     "", ""},
    {"ambientvolume", "Ambience", SettingKind::Int, 0, 100, 5, "Sound", "",
     "Wind, water, birds and the rest of the world's own noise.", ""},
    {"bellvolume", "City bells", SettingKind::Int, 0, 100, 5, "Sound", "",
     "The hour struck in the capital cities.", ""},

    {"uivolume", "Interface", SettingKind::Int, 0, 100, 5, "Sound", "Effects",
     "Clicks, bag sounds and window noises. Each of these is a balance\n"
     "against the others; the Sound Effects slider in the game's own Sound\n"
     "panel scales all of them together.", ""},
    {"combatvolume", "Combat", SettingKind::Int, 0, 100, 5, "Sound", "", "", ""},
    {"spellvolume", "Spells", SettingKind::Int, 0, 100, 5, "Sound", "", "", ""},
    {"movementvolume", "Movement", SettingKind::Int, 0, 100, 5, "Sound", "", "", ""},
    {"footstepvolume", "Footsteps", SettingKind::Int, 0, 100, 5, "Sound", "", "", ""},
    {"mountvolume", "Mounts", SettingKind::Int, 0, 100, 5, "Sound", "", "", ""},
    {"activityvolume", "Activity", SettingKind::Int, 0, 100, 5, "Sound", "",
     "Fishing, mining, forges and the rest.", ""},

    {"npcvoicevolume", "NPC voices", SettingKind::Int, 0, 100, 5, "Sound", "Voices", "", ""},
    {"characterspeech", "Character speech", SettingKind::Bool, 0, 0, 0, "Sound", "",
     "Your own character's grunts and greetings.", ""},
    {"woweemusic", "WoWee soundtrack", SettingKind::Bool, 0, 0, 0, "Sound", "",
     "Include this client's own music alongside the game's.", ""},

    // -------------------------------------------------------------------- Chat
    //
    // Which channels to join on entering the world. These are the client's own
    // doing rather than the interface's — it sends the join for each one — so
    // they belong here whichever chat window is on screen.
    //
    // Chat's appearance is deliberately not here. Timestamps, the font size,
    // the background and the fade belong to the chat frame the interface draws,
    // and it has its own controls for them; the copies in this client's own
    // settings window drive a chat panel that is not shown at all while
    // FrameXML owns chat, which is every run by default.
    {"joingeneral", "General", SettingKind::Bool, 0, 0, 0, "Chat", "Channels to join",
     "The zone-wide channel.", ""},
    {"jointrade", "Trade", SettingKind::Bool, 0, 0, 0, "Chat", "",
     "City-wide, and only in a city.", ""},
    {"joinlocaldefense", "LocalDefense", SettingKind::Bool, 0, 0, 0, "Chat", "",
     "Attacks on your zone.", ""},
    {"joinlfg", "LookingForGroup", SettingKind::Bool, 0, 0, 0, "Chat", "", "", ""},
    {"joinlocal", "Local", SettingKind::Bool, 0, 0, 0, "Chat", "", "", ""},

    // ---------------------------------------------------------------- Gameplay
    {"autoloot", "Auto loot", SettingKind::Bool, 0, 0, 0, "Gameplay", "Looting",
     "Take everything from a corpse without opening the window.", ""},
    {"autosellgrey", "Sell grey items", SettingKind::Bool, 0, 0, 0, "Gameplay", "",
     "Sell your grey items whenever you open a merchant.", ""},
    {"autorepair", "Repair at vendors", SettingKind::Bool, 0, 0, 0, "Gameplay", "",
     "Repair whenever you open a merchant who can.", ""},
};

}  // namespace

const SettingDesc* clientSettingsSchema(std::size_t& count) {
    count = sizeof(kSchema) / sizeof(kSchema[0]);
    return kSchema;
}

}  // namespace ui
}  // namespace wowee
