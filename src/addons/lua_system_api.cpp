// lua_system_api.cpp — System, time, sound, locale, map, addons, instances, and utilities Lua API bindings.
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#include <array>
#include <algorithm>
#include <cstring>
#include <set>
#include <vector>
#include "imgui.h"
#include "addons/lua_api_helpers.hpp"
#include "addons/lua_engine.hpp"
#include "game/bg_score_defs.hpp"
#include "audio/activity_sound_manager.hpp"
#include "audio/ambient_sound_manager.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/audio_engine.hpp"
#include "audio/combat_sound_manager.hpp"
#include "audio/footstep_manager.hpp"
#include "audio/mount_sound_manager.hpp"
#include "audio/movement_sound_manager.hpp"
#include "audio/music_manager.hpp"
#include "audio/npc_voice_manager.hpp"
#include "audio/player_voice_manager.hpp"
#include "audio/spell_sound_manager.hpp"
#include "audio/ui_sound_manager.hpp"
#include "core/app_clock.hpp"
#include "core/window.hpp"

#include <SDL2/SDL.h>
#include "game/expansion_profile.hpp"
#include "core/coordinates.hpp"
#include "rendering/world_map/coordinate_projection.hpp"

namespace wowee::addons {

// CombatLog_Object_IsA(unitFlags, mask) — does a combat log unit match a filter.
//
// The flags are four exclusive categories packed together (affiliation,
// reaction, control, unit type) plus a set of non-exclusive special bits, and a
// filter names every value it accepts within a category. COMBATLOG_FILTER_MINE
// is AFFILIATION_MINE + REACTION_FRIENDLY + CONTROL_PLAYER + TYPE_PLAYER +
// TYPE_OBJECT, and a player only ever carries one of those two type bits — so
// the obvious (flags & mask) == mask never matches anything, and the whole
// combat log filters itself empty.
//
// A category the mask says nothing about is not a constraint.
static int lua_CombatLog_Object_IsA(lua_State* L) {
    const auto flags = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
    const auto mask  = static_cast<uint32_t>(luaL_optnumber(L, 2, 0));

    static constexpr uint32_t kCategories[] = {
        0x0000000Fu,  // affiliation: mine / party / raid / outsider
        0x000000F0u,  // reaction: friendly / neutral / hostile
        0x00000300u,  // control: player / npc
        0x0000FC00u,  // type: player / npc / pet / guardian / object
    };
    for (const uint32_t cat : kCategories) {
        const uint32_t wanted = mask & cat;
        if (wanted == 0) continue;            // unconstrained
        if ((flags & wanted) == 0) { lua_pushboolean(L, 0); return 1; }
    }
    // The special bits are non-exclusive, so every one asked for must be present.
    const uint32_t special = mask & 0xFFFF0000u;
    if (special != 0 && (flags & special) != special) { lua_pushboolean(L, 0); return 1; }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_PlaySound(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* ac = svc ? svc->audioCoordinator : nullptr;
    if (!ac) return 0;
    auto* sfx = ac->getUiSoundManager();
    if (!sfx) return 0;

    // Accept numeric sound ID or string name
    std::string sound;
    if (lua_isnumber(L, 1)) {
        uint32_t id = static_cast<uint32_t>(lua_tonumber(L, 1));
        // Map common WoW sound IDs to named sounds
        switch (id) {
            case 856: case 1115: sfx->playButtonClick(); return 0; // igMainMenuOption
            case 840: sfx->playQuestActivate(); return 0;          // igQuestListOpen
            case 841: sfx->playQuestComplete(); return 0;           // igQuestListComplete
            case 862: sfx->playBagOpen(); return 0;                // igBackPackOpen
            case 863: sfx->playBagClose(); return 0;               // igBackPackClose
            case 867: sfx->playError(); return 0;                  // igPlayerInvite
            case 888: sfx->playLevelUp(); return 0;                // LEVELUPSOUND
            default: return 0;
        }
    } else {
        const char* name = luaL_optstring(L, 1, "");
        sound = name;
        for (char& c : sound) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (sound == "IGMAINMENUOPTION" || sound == "IGMAINMENUOPTIONCHECKBOXON")
            sfx->playButtonClick();
        else if (sound == "IGQUESTLISTOPEN") sfx->playQuestActivate();
        else if (sound == "IGQUESTLISTCOMPLETE") sfx->playQuestComplete();
        else if (sound == "IGBACKPACKOPEN") sfx->playBagOpen();
        else if (sound == "IGBACKPACKCLOSE") sfx->playBagClose();
        else if (sound == "LEVELUPSOUND") sfx->playLevelUp();
        else if (sound == "IGPLAYERINVITEACCEPTED") sfx->playButtonClick();
        else if (sound == "TALENTSCREENOPEN") sfx->playCharacterSheetOpen();
        else if (sound == "TALENTSCREENCLOSE") sfx->playCharacterSheetClose();
    }
    return 0;
}

// PlaySoundFile(path) — stub (file-based sounds not loaded from Lua)
static int lua_PlaySoundFile(lua_State* L) { (void)L; return 0; }

static int lua_GetPlayerMapPosition(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) {
        const auto& mi = gh->getMovementInfo();
        lua_pushnumber(L, mi.x);
        lua_pushnumber(L, mi.y);
        return 2;
    }
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
}

// GetPlayerFacing() → radians (0 = north, increasing counter-clockwise)
static int lua_GetPlayerFacing(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) {
        float facing = gh->getMovementInfo().orientation;
        // Normalize to [0, 2π)
        while (facing < 0) facing += 6.2831853f;
        while (facing >= 6.2831853f) facing -= 6.2831853f;
        lua_pushnumber(L, facing);
    } else {
        lua_pushnumber(L, 0);
    }
    return 1;
}

// GetCVar(name) → value string (stub for most, real for a few)
/// CVars the player or the interface has actually set, which win over the
/// defaults below.
///
/// SetCVar was a no-op, so every option the interface changed reverted the
/// instant it was read back: ticking a box in the interface options did
/// nothing, and any code that writes a CVar and then reads it to confirm — of
/// which FrameXML has a fair amount — saw its own write disappear.
static std::unordered_map<std::string, std::string>& cvarStore() {
    static std::unordered_map<std::string, std::string> store;
    return store;
}

/// A sound CVar's value, or the stock client's default for it.
///
/// The defaults matter as much as the store does. Sound_MasterVolumeUp reads
/// the CVar, runs it through tonumber and adds a step — with nothing stored and
/// no default it reads nil, the `if (volume)` guard below it fails, and the
/// volume keys do nothing at all rather than anything visible.
static float soundCVar(const char* key, float fallback) {
    if (auto it = cvarStore().find(key); it != cvarStore().end()) {
        try {
            return std::stof(it->second);
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

/// Push the sound CVars at the audio system, which is what makes them settings
/// rather than a record of what was clicked.
///
/// Every channel is recomputed from the store rather than from the one CVar
/// that changed, so enable and volume compose and the order they arrive in does
/// not matter — Sound_ToggleSound writes EnableSFX and EnableAmbience one after
/// the other, and the interface's options panel writes a volume and an enable
/// together on apply.
///
/// This client splits sound finer than the interface does: it has separate
/// volumes for combat, spells, movement, footsteps and the rest, where FrameXML
/// has one "sound effects". Turning SFX off and on again therefore levels those
/// channels rather than restoring them. That is the retail behaviour — there is
/// no finer control there to restore — and the client's own settings panel
/// re-applies its sliders whenever it is used, so neither owner is stuck with
/// the other's answer.
static void applySoundCVars(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* ac = svc ? svc->audioCoordinator : nullptr;
    if (!ac) return;

    const bool allSound = soundCVar("sound_enableallsound", 1.0f) != 0.0f;
    const float master  = std::clamp(soundCVar("sound_mastervolume", 1.0f), 0.0f, 1.0f);
    audio::AudioEngine::instance().setMasterVolume(allSound ? master : 0.0f);

    const bool  musicOn  = soundCVar("sound_enablemusic", 1.0f) != 0.0f;
    const float musicVol = std::clamp(soundCVar("sound_musicvolume", 1.0f), 0.0f, 1.0f);
    if (auto* music = ac->getMusicManager())
        music->setVolume(static_cast<int>((musicOn ? musicVol : 0.0f) * 100.0f));

    const bool  ambOn  = soundCVar("sound_enableambience", 1.0f) != 0.0f;
    const float ambVol = std::clamp(soundCVar("sound_ambiencevolume", 1.0f), 0.0f, 1.0f);
    if (auto* ambient = ac->getAmbientSoundManager()) {
        ambient->setVolumeScale(ambOn ? ambVol : 0.0f);
        ambient->setBellVolumeScale(ambOn ? ambVol : 0.0f);
    }

    const bool  sfxOn  = soundCVar("sound_enablesfx", 1.0f) != 0.0f;
    const float sfxVol = std::clamp(soundCVar("sound_sfxvolume", 1.0f), 0.0f, 1.0f);
    const float sfx    = sfxOn ? sfxVol : 0.0f;
    if (auto* m = ac->getUiSoundManager())        m->setVolumeScale(sfx);
    if (auto* m = ac->getCombatSoundManager())    m->setVolumeScale(sfx);
    if (auto* m = ac->getSpellSoundManager())     m->setVolumeScale(sfx);
    if (auto* m = ac->getMovementSoundManager())  m->setVolumeScale(sfx);
    if (auto* m = ac->getFootstepManager())       m->setVolumeScale(sfx);
    if (auto* m = ac->getActivitySoundManager())  m->setVolumeScale(sfx);
    if (auto* m = ac->getMountSoundManager())     m->setVolumeScale(sfx);
    if (auto* m = ac->getNpcVoiceManager())       m->setVolumeScale(sfx);
    if (auto* m = ac->getPlayerVoiceManager())    m->setVolumeScale(sfx);
}

static int lua_GetCVar(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    // Folded to lower case, because the client's CVar names are not
    // case-sensitive and the interface does not spell them consistently.
    // uidropdownmenu.lua asks for "uiscale" where everything else says
    // "uiScale"; an exact match answered "0" for it, tonumber("0") is 0, and
    // every dropdown menu in the interface opened at SetScale(0) — laid out,
    // drawn, and invisible.
    std::string n(name);
    toLowerInPlace(n);
    // Asked of the client before the store, for the two settings it also owns.
    // The V key toggles nameplates and the settings panel turns the minimap,
    // neither of which goes through SetCVar; answering from the store would
    // report whatever the interface last wrote, which by then is a guess.
    if (n == "nameplateshowenemies") {
        if (auto* svc = getLuaServices(L); svc && svc->getNameplatesShown) {
            lua_pushstring(L, svc->getNameplatesShown() ? "1" : "0");
            return 1;
        }
    } else if (n == "rotateminimap") {
        if (auto* svc = getLuaServices(L); svc && svc->getMinimapRotate) {
            lua_pushstring(L, svc->getMinimapRotate() ? "1" : "0");
            return 1;
        }
    } else if (n == "autoselfcast") {
        if (auto* gh = getGameHandler(L)) {
            lua_pushstring(L, gh->isAutoSelfCast() ? "1" : "0");
            return 1;
        }
    }
    if (auto it = cvarStore().find(n); it != cvarStore().end()) {
        lua_pushstring(L, it->second.c_str());
        return 1;
    }
    // Return sensible defaults for commonly queried CVars
    // The sound ones read back as on and at full, which is what this client
    // starts as. Volume up/down step from whatever is read here, so answering
    // nothing leaves those keys inert rather than merely at a default.
    if (n.rfind("sound_enable", 0) == 0) lua_pushstring(L, "1");
    else if (n == "sound_mastervolume" || n == "sound_musicvolume" ||
             n == "sound_sfxvolume" || n == "sound_ambiencevolume") {
        lua_pushstring(L, "1");
    }
    else if (n == "uiscale") lua_pushstring(L, "1");
    else if (n == "useuiscale") lua_pushstring(L, "1");
    else if (n == "screenwidth" || n == "gxresolution") {
        auto* svc = getLuaServices(L);
        auto* win = svc ? svc->window : nullptr;
        lua_pushstring(L, std::to_string(win ? win->getWidth() : 1920).c_str());
    } else if (n == "screenheight" || n == "gxfullscreenresolution") {
        auto* svc = getLuaServices(L);
        auto* win = svc ? svc->window : nullptr;
        lua_pushstring(L, std::to_string(win ? win->getHeight() : 1080).c_str());
    } else if (n == "nameplateshowfriends") lua_pushstring(L, "1");
    else if (n == "nameplateshowenemies") lua_pushstring(L, "1");
    else if (n == "sound_enablesfx") lua_pushstring(L, "1");
    else if (n == "sound_enablemusic") lua_pushstring(L, "1");
    else if (n == "chatbubbles") lua_pushstring(L, "1");
    else if (n == "autolootdefault") lua_pushstring(L, "1");
    // On, as it is for a fresh account. The XP bar and the unit frames put
    // their whole tooltip behind this one: GameTooltip_AddNewbieTip is called
    // with noNormalText set, so with tips off it does nothing at all and
    // hovering the experience bar says nothing.
    else if (n == "shownewbietips") lua_pushstring(L, "1");
    // The numbers on a unit frame's bars. A stock 3.3.5 client keeps these off
    // and shows them on mouseover; on this one they are wanted permanently,
    // which is what the Status Text interface option turns on.
    // The unit frames each ask about their own, not about "statusText" — the
    // player frame's bars carry cvar = "playerStatusText". Defaulting only the
    // general one left every bar's numbers hidden, correct text and all.
    else if (n == "statustext" || n == "playerstatustext" ||
             n == "targetstatustext" || n == "petstatustext" ||
             n == "partystatustext") {
        lua_pushstring(L, "1");
    }
    else if (n == "statustextpercentage") lua_pushstring(L, "0");
    // Which stat category each column of the character sheet shows. These are
    // not preferences with a sensible fallback — UpdatePaperdollStats compares
    // the value against five names and fills the column from whichever matches,
    // so an unrecognised one matches nothing and every row is left blank. That
    // is what "0" gave it, and it is why the character sheet showed two empty
    // panels below the model with no error anywhere to say why: the code ran to
    // completion and simply had nothing to write.
    //
    // The two names below are what a fresh 3.3.5 account has.
    else if (n == "playerstatleftdropdown")  lua_pushstring(L, "PLAYERSTAT_BASE_STATS");
    else if (n == "playerstatrightdropdown") lua_pushstring(L, "PLAYERSTAT_MELEE_COMBAT");
    // Whether a conversation opens in its own window or in the chat frame.
    // "0" already behaved as "inline" — the only test is against "popout" —
    // so this changes nothing today. It is written out because the value is a
    // name rather than a number, which is the case where falling through to
    // "0" is luck rather than a default.
    else if (n == "conversationmode") lua_pushstring(L, "inline");
    // Who last spoke to you as a GM, and empty means nobody has.
    //
    // uiparent.lua does `if ( lastTalkedToGM ~= "" )` at login and, when that
    // passes, loads Blizzard_GMChatUI and *shows* it with a "your last session"
    // line. Falling through to "0" made that test pass every time, so the GM
    // chat window opened on every login for a player no GM had ever contacted.
    //
    // The empty string is not a placeholder here — it is the value the client
    // stores until a GM actually writes.
    else if (n == "lasttalkedtogm") lua_pushstring(L, "");
    // On, as a stock client has them. Each of these gates something off
    // entirely when it reads false, so "0" is not a quiet preference — it is
    // the feature missing with no way to ask for it back.
    //
    //   chatMouseScroll  the chat frame only calls EnableMouseWheel(true)
    //                    inside this test, so the wheel did nothing over chat
    //   showKeyring      MainMenuBar_UpdateKeyRing only ever calls
    //                    KeyRingButton:Show() inside it, so the keyring was
    //                    unreachable despite the slots being tracked
    // On, as WotLK has it — and everything behind it is built.
    //
    // This was left off last time for being untested, on the grounds that
    // turning it on changes what clicking a talent does. Checking rather than
    // assuming: all four preview functions are implemented, not stubbed —
    // AddPreviewTalentPoints stages against the real max rank,
    // GetGroupPreviewTalentPointsSpent totals the staging map,
    // LearnPreviewTalents sends one request per rank, and
    // ResetGroupPreviewTalentPoints clears it and fires the event the frame
    // redraws on. GetTalentInfo already answers previewRank and the
    // preview-aware availability flag.
    //
    // So the staging flow was written deliberately and then reached by
    // nothing, because the CVar that gates every one of those eight call
    // sites answered false.
    else if (n == "previewtalents") lua_pushstring(L, "1");
    else if (n == "chatmousescroll") lua_pushstring(L, "1");
    else if (n == "showkeyring")     lua_pushstring(L, "1");
    // Full volume and sound on, which is what a fresh client has. These are
    // read as numbers by the sound options, where zero reads as silence
    // rather than as "unset".
    else if (n == "sound_mastervolume")   lua_pushstring(L, "1");
    else if (n == "sound_enableallsound") lua_pushstring(L, "1");
    // On, as a stock client has it. ActionButton_SetTooltip branches on this:
    // with it off the tooltip is anchored to the right of the button itself, so
    // an action bar tooltip appeared at the bottom of the screen across the
    // icons. On, it goes through GameTooltip_SetDefaultAnchor to the
    // bottom-right corner, clear of the bar, which is where WoW puts it.
    else if (n == "ubertooltips") lua_pushstring(L, "1");
    // The social options panel branches on this and raises on anything it does
    // not recognise, so "0" — what an unknown CVar answers — took its whole
    // update down. "classic" is the stock setting.
    else if (n == "chatstyle") lua_pushstring(L, "classic");
    else lua_pushstring(L, "0");
    return 1;
}

/// GetCVarBool(name) → the setting as a boolean.
///
/// FrameXML branches on this, and one of those branches decides how a unit
/// frame's health bar keeps itself current: predictedHealth sends it down an
/// OnUpdate poll instead of registering UNIT_HEALTH. Unimplemented, the call
/// answered nil through the fallback and took the event branch by luck — which
/// is the branch that works here, but only until the fallback is off, when the
/// same call errors instead.
static int lua_GetCVarBool(lua_State* L) {
    lua_pushvalue(L, 1);
    lua_GetCVar(L);
    const char* v = lua_tostring(L, -1);
    const bool on = v && *v && std::string(v) != "0";
    lua_pop(L, 1);
    lua_pushboolean(L, on);
    return 1;
}

// SetCVar(name, value [, scriptCVar])
static int lua_SetCVar(lua_State* L) {
    const char* name = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
    if (!name) return 0;
    // A number is as valid as a string here and FrameXML passes both.
    std::string value;
    if (lua_isstring(L, 2) || lua_isnumber(L, 2)) {
        value = lua_tostring(L, 2);
    } else if (lua_isboolean(L, 2)) {
        value = lua_toboolean(L, 2) ? "1" : "0";
    }
    // The same folding as the read side, or a value written as "uiScale"
    // would be invisible to a read of "uiscale".
    std::string key(name);
    toLowerInPlace(key);
    cvarStore()[key] = value;
    // A sound CVar is a setting, not a note. Without this the interface's
    // volume keys and its Sound options both wrote to a map nobody read, so
    // turning music off left it playing.
    if (key.rfind("sound_", 0) == 0) applySoundCVars(L);
    // The two other CVars this client can act on. "0" is the only false value
    // a CVar carries — and it arrives as a string, which in Lua would be true.
    else if (key == "nameplateshowenemies") {
        if (auto* svc = getLuaServices(L); svc && svc->setNameplatesShown)
            svc->setNameplatesShown(value != "0");
    } else if (key == "rotateminimap") {
        if (auto* svc = getLuaServices(L); svc && svc->setMinimapRotate)
            svc->setMinimapRotate(value != "0");
    } else if (key == "autoselfcast") {
        if (auto* gh = getGameHandler(L)) gh->setAutoSelfCast(value != "0");
    }
    // Announced, because nine frames listen for it — the options panels redraw
    // themselves from this rather than from the click that caused it.
    // Through the engine in the registry, which is where it puts itself; the
    // event tables are its business rather than this file's.
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_lua_engine");
    auto* engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (engine) engine->fireEvent("CVAR_UPDATE", {name, value});
    return 0;
}


static int lua_GetNumAddOns(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_addon_count");
    return 1;
}

static int lua_GetAddOnInfo(lua_State* L) {
    // Accept index (1-based) or addon name
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_addon_info");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return luaReturnNil(L);
    }

    int idx = 0;
    if (lua_isnumber(L, 1)) {
        idx = static_cast<int>(lua_tonumber(L, 1));
    } else if (lua_isstring(L, 1)) {
        // Search by name
        const char* name = lua_tostring(L, 1);
        int count = static_cast<int>(lua_objlen(L, -1));
        for (int i = 1; i <= count; i++) {
            lua_rawgeti(L, -1, i);
            lua_getfield(L, -1, "name");
            const char* aName = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (aName && strcmp(aName, name) == 0) { idx = i; lua_pop(L, 1); break; }
            lua_pop(L, 1);
        }
    }

    if (idx < 1) { lua_pop(L, 1); lua_pushnil(L); return 1; }

    lua_rawgeti(L, -1, idx);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return 1; }

    lua_getfield(L, -1, "name");
    lua_getfield(L, -2, "title");
    lua_getfield(L, -3, "notes");
    lua_pushboolean(L, 1); // loadable (always true for now)
    lua_pushstring(L, "INSECURE"); // security
    lua_pop(L, 1); // pop addon info entry (keep others)
    // Return: name, title, notes, loadable, reason, security
    return 5;
}

// GetAddOnMetadata(addonNameOrIndex, key) → value
static int lua_GetAddOnMetadata(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_addon_info");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

    int idx = 0;
    if (lua_isnumber(L, 1)) {
        idx = static_cast<int>(lua_tonumber(L, 1));
    } else if (lua_isstring(L, 1)) {
        const char* name = lua_tostring(L, 1);
        int count = static_cast<int>(lua_objlen(L, -1));
        for (int i = 1; i <= count; i++) {
            lua_rawgeti(L, -1, i);
            lua_getfield(L, -1, "name");
            const char* aName = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (aName && strcmp(aName, name) == 0) { idx = i; lua_pop(L, 1); break; }
            lua_pop(L, 1);
        }
    }
    if (idx < 1) { lua_pop(L, 1); lua_pushnil(L); return 1; }

    const char* key = luaL_checkstring(L, 2);
    lua_rawgeti(L, -1, idx);
    if (!lua_istable(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return 1; }
    lua_getfield(L, -1, "metadata");
    if (!lua_istable(L, -1)) { lua_pop(L, 3); lua_pushnil(L); return 1; }
    lua_getfield(L, -1, key);
    return 1;
}

// UnitBuff(unitId, index) / UnitDebuff(unitId, index)
// Returns: name, rank, icon, count, debuffType, duration, expirationTime, caster, isStealable, shouldConsolidate, spellId

static int lua_GetLocale(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* profile = svc && svc->expansionRegistry
        ? svc->expansionRegistry->getActive() : nullptr;
    lua_pushstring(L, profile ? profile->locale.c_str() : "enUS");
    return 1;
}

static int lua_GetBuildInfo(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* profile = svc && svc->expansionRegistry
        ? svc->expansionRegistry->getActive() : nullptr;
    if (!profile) {
        lua_pushstring(L, "3.3.5a");
        lua_pushnumber(L, 12340);
        lua_pushstring(L, "");
        lua_pushnumber(L, 30300);
        return 4;
    }

    const std::string version = profile->versionString();
    uint32_t tocVersion = 11200;
    if (profile->majorVersion == 2) tocVersion = 20400;
    else if (profile->majorVersion >= 3) tocVersion = 30300;

    lua_pushstring(L, version.c_str());
    lua_pushnumber(L, profile->build);
    lua_pushstring(L, "");
    lua_pushnumber(L, tocVersion);
    return 4;
}

static int lua_GetCurrentMapAreaID(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? gh->getCurrentMapId() : 0);
    return 1;
}

// GetZoneText() / GetRealZoneText() → current zone name
static int lua_GetZoneText(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushstring(L, ""); return 1; }
    uint32_t zoneId = gh->getWorldStateZoneId();
    if (zoneId != 0) {
        std::string name = gh->getWhoAreaName(zoneId);
        if (!name.empty()) { lua_pushstring(L, name.c_str()); return 1; }
    }
    lua_pushstring(L, "");
    return 1;
}

// GetSubZoneText() → subzone name (same as zone for now — server doesn't always send subzone)
static int lua_GetSubZoneText(lua_State* L) {
    return lua_GetZoneText(L);  // Best-effort: zone and subzone often overlap
}

// GetMinimapZoneText() → zone name displayed near minimap
static int lua_GetMinimapZoneText(lua_State* L) {
    return lua_GetZoneText(L);
}

// --- World Map Navigation API ---

// Map ID → continent mapping
static int mapIdToContinent(uint32_t mapId) {
    switch (mapId) {
        case 0:   return 2; // Eastern Kingdoms
        case 1:   return 1; // Kalimdor
        case 530: return 3; // Outland
        case 571: return 4; // Northrend
        default:  return 0; // Instance or unknown
    }
}

// Internal tracked map state (which continent/zone the map UI is viewing)
static int s_mapContinent = 0;
static int s_mapZone = 0;

/// The map view changed, so say so.
///
/// Fired unconditionally, including when the view was already what it is being
/// set to. That is not laziness — watchframe.lua calls SetMapToCurrentZone
/// purely for the side effect, and says so beside the call: "forces WatchFrame
/// event via the WORLD_MAP_UPDATE event, needed to restore the POIs in the
/// tracker to the current zone". Firing only on a change would drop exactly the
/// case that line exists for.
static void fireWorldMapUpdate(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_lua_engine");
    auto* engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (engine) engine->fireEvent("WORLD_MAP_UPDATE", {});
}

// SetMapToCurrentZone() — sets map view to the player's current zone
static int lua_SetMapToCurrentZone(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) {
        s_mapContinent = mapIdToContinent(gh->getCurrentMapId());
        s_mapZone = static_cast<int>(gh->getWorldStateZoneId());
    }
    fireWorldMapUpdate(L);
    return 0;
}

// GetCurrentMapContinent() → continentId (1=Kalimdor, 2=EK, 3=Outland, 4=Northrend)
static int lua_GetCurrentMapContinent(lua_State* L) {
    if (s_mapContinent == 0) {
        auto* gh = getGameHandler(L);
        if (gh) s_mapContinent = mapIdToContinent(gh->getCurrentMapId());
    }
    lua_pushnumber(L, s_mapContinent);
    return 1;
}

// GetCurrentMapZone() → zoneId
static int lua_GetCurrentMapZone(lua_State* L) {
    if (s_mapZone == 0) {
        auto* gh = getGameHandler(L);
        if (gh) s_mapZone = static_cast<int>(gh->getWorldStateZoneId());
    }
    lua_pushnumber(L, s_mapZone);
    return 1;
}

// SetMapZoom(continent [, zone]) — sets map view to continent/zone
static int lua_SetMapZoom(lua_State* L) {
    s_mapContinent = static_cast<int>(luaL_checknumber(L, 1));
    s_mapZone = static_cast<int>(luaL_optnumber(L, 2, 0));
    fireWorldMapUpdate(L);
    return 0;
}

// GetMapContinents() → "Kalimdor", "Eastern Kingdoms", ...
static int lua_GetMapContinents(lua_State* L) {
    lua_pushstring(L, "Kalimdor");
    lua_pushstring(L, "Eastern Kingdoms");
    lua_pushstring(L, "Outland");
    lua_pushstring(L, "Northrend");
    return 4;
}

// GetMapZones(continent) → zone names for that continent
// Returns a basic list; addons mainly need this to not error
static int lua_GetMapZones(lua_State* L) {
    int cont = static_cast<int>(luaL_checknumber(L, 1));
    // Return a minimal representative set per continent
    switch (cont) {
        case 1: // Kalimdor
            lua_pushstring(L, "Durotar"); lua_pushstring(L, "Mulgore");
            lua_pushstring(L, "The Barrens"); lua_pushstring(L, "Teldrassil");
            return 4;
        case 2: // Eastern Kingdoms
            lua_pushstring(L, "Elwynn Forest"); lua_pushstring(L, "Westfall");
            lua_pushstring(L, "Dun Morogh"); lua_pushstring(L, "Tirisfal Glades");
            return 4;
        case 3: // Outland
            lua_pushstring(L, "Hellfire Peninsula"); lua_pushstring(L, "Zangarmarsh");
            return 2;
        case 4: // Northrend
            lua_pushstring(L, "Borean Tundra"); lua_pushstring(L, "Howling Fjord");
            return 2;
        default:
            return 0;
    }
}

// GetNumMapLandmarks() → 0 (no landmark data exposed yet)
static int lua_GetNumMapLandmarks(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

/// GetTrackingTexture() → the icon for what the minimap is tracking, or nil.
///
/// Nothing is tracked here: tracking is a spell effect this client does not
/// model, and GetNumTrackingTypes already answers none. Said explicitly
/// because the missing-API fallback answers with an object, and an object is
/// not nil — MiniMapTrackingIcon:SetTexture(GetTrackingTexture()) would then
/// be handed a table where a path belongs and the button would show the
/// tracking icon for a tracking type that does not exist.
// ── Minimap tracking ───────────────────────────────────────────────────────
//
// The tracking menu is not a fixed list. It is whatever tracking the player
// has learned, which is the known spells applying aura 44 (creatures) or 45
// (resources) — Spell.dbc's EffectApplyAuraName, and the only thing in the
// spell data that tells a tracking spell from any other buff. The effect id
// beside it says an aura is applied but never which one.
//
// All three of these used to be stubs: no texture, no types, and no binding
// at all for the count. GetNumTrackingTypes being absent was the worst of
// them, because a missing global answers nil and the initialiser opens with
// `for id = 1, count`, which raises on nil and took the whole menu down.

/// Aura ids that make a spell a tracking spell.
static constexpr uint32_t kAuraTrackCreatures = 44;
static constexpr uint32_t kAuraTrackResources = 45;

/// The player's tracking spells, ordered by name.
///
/// Sorted rather than left in the known-spell set's own order, which is a hash
/// order: the menu would otherwise list the same spells differently from one
/// session to the next.
static std::vector<uint32_t> trackingSpells(game::GameHandler* gh) {
    std::vector<uint32_t> out;
    if (!gh) return out;
    for (uint32_t sid : gh->getKnownSpells()) {
        // Asking for the name is what fills the cache; the entry is not there
        // to be read until something has.
        gh->getSpellName(sid);
        auto it = gh->spellNameCacheRef().find(sid);
        if (it == gh->spellNameCacheRef().end()) continue;
        for (uint32_t aura : it->second.effectAuraIds) {
            if (aura == kAuraTrackCreatures || aura == kAuraTrackResources) {
                out.push_back(sid);
                break;
            }
        }
    }
    std::sort(out.begin(), out.end(), [gh](uint32_t a, uint32_t b) {
        return gh->getSpellName(a) < gh->getSpellName(b);
    });
    return out;
}

/// Whether that tracking is the one currently running.
static bool trackingActive(game::GameHandler* gh, uint32_t spellId) {
    if (!gh) return false;
    for (const auto& aura : gh->getPlayerAuras()) {
        if (aura.spellId == spellId) return true;
    }
    return false;
}

/// GetTrackingTexture() → the icon on the minimap button.
///
/// The button's own art is empty in the XML and comes entirely from here, so
/// answering nil left a blank square on the minimap. Nothing tracked is not
/// nothing to draw: it is the magnifying glass, which is what a stock client
/// shows and what makes the button look like something to click.
static int lua_GetTrackingTexture(lua_State* L) {
    auto* gh = getGameHandler(L);
    for (uint32_t sid : trackingSpells(gh)) {
        if (!trackingActive(gh, sid)) continue;
        const std::string icon = gh->getSpellIconPath(sid);
        if (!icon.empty()) { lua_pushstring(L, icon.c_str()); return 1; }
    }
    lua_pushstring(L, "Interface\\Minimap\\Tracking\\None");
    return 1;
}

/// GetNumTrackingTypes() → how many the player knows.
static int lua_GetNumTrackingTypes(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(trackingSpells(getGameHandler(L)).size()));
    return 1;
}

/// GetTrackingInfo(index) → name, texture, active, category.
///
/// "spell" for the category, because these are spell icons and the menu uses
/// that to crop the icon's border — the same trim the action bar gives them.
static int lua_GetTrackingInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const auto spells = trackingSpells(gh);
    if (index < 1 || index > static_cast<int>(spells.size())) return 0;
    const uint32_t sid = spells[static_cast<size_t>(index - 1)];
    lua_pushstring(L, gh->getSpellName(sid).c_str());
    lua_pushstring(L, gh->getSpellIconPath(sid).c_str());
    lua_pushboolean(L, trackingActive(gh, sid) ? 1 : 0);
    lua_pushstring(L, "spell");
    return 4;
}

/// SetTracking(index) — casting the spell is how tracking is turned on; there
/// is no separate message for it. A nil index is the menu's "None" entry,
/// which in a stock client cancels the running tracking aura. Cancelling a
/// player's own buff is not wired up here, so that entry does nothing rather
/// than casting something arbitrary.
static int lua_SetTracking(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || lua_isnoneornil(L, 1)) return 0;
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const auto spells = trackingSpells(gh);
    if (index < 1 || index > static_cast<int>(spells.size())) return 0;
    gh->castSpell(spells[static_cast<size_t>(index - 1)], 0);
    return 0;
}


static int lua_GetGameTime(lua_State* L) {
    // Returns server game time as hours, minutes
    auto* gh = getGameHandler(L);
    if (gh) {
        float gt = gh->getGameTime();
        int hours = static_cast<int>(gt) % 24;
        int mins = static_cast<int>((gt - static_cast<int>(gt)) * 60.0f);
        lua_pushnumber(L, hours);
        lua_pushnumber(L, mins);
    } else {
        lua_pushnumber(L, 12);
        lua_pushnumber(L, 0);
    }
    return 2;
}

static int lua_GetServerTime(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(std::time(nullptr)));
    return 1;
}


static int lua_IsInInstance(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushboolean(L, 0); lua_pushstring(L, "none"); return 2; }
    bool inInstance = gh->isInInstance();
    lua_pushboolean(L, inInstance);
    lua_pushstring(L, inInstance ? "party" : "none");  // simplified: "none", "party", "raid", "pvp", "arena"
    return 2;
}

// GetInstanceInfo() → name, type, difficultyIndex, difficultyName, maxPlayers, ...
static int lua_GetInstanceInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) {
        // Seven, and a difficulty of one, so this branch answers in the same
        // shape and the same numbering as the real one below it.
        lua_pushstring(L, ""); lua_pushstring(L, "none"); lua_pushnumber(L, 1);
        lua_pushstring(L, "Normal"); lua_pushnumber(L, 0);
        lua_pushnumber(L, 0); lua_pushboolean(L, 0);
        return 7;
    }
    std::string mapName = gh->getMapName(gh->getCurrentMapId());
    const uint32_t diff = gh->getInstanceDifficulty();
    lua_pushstring(L, mapName.c_str());                    // 1: name
    lua_pushstring(L, gh->isInInstance() ? "party" : "none"); // 2: instanceType
    // Counted from one, which is what the interface compares against.
    //
    // The wire value is zero-based — social_handler reads heroic as
    // difficulty == 1 — and this pushed it straight through. minimap.lua tests
    // `difficulty == 1 and maxPlayers == 5` to decide there is nothing worth
    // showing, and `difficulty == 2` for heroic, so a normal dungeon failed
    // the first test and hung a difficulty banner on the minimap, while a
    // heroic failed the second and had that banner read "Normal".
    //
    // GetInstanceDifficulty beside this already added the one; only this path
    // did not.
    lua_pushnumber(L, diff + 1);                           // 3: difficultyIndex
    static constexpr const char* kDiff[] = {"Normal", "Heroic", "25 Normal", "25 Heroic"};
    lua_pushstring(L, (diff < 4) ? kDiff[diff] : "Normal"); // 4: difficultyName
    lua_pushnumber(L, 5);                                   // 5: maxPlayers (default 5-man)
    // The two the raid branch reads. Neither is tracked here, and both are
    // only consulted for a dynamic-difficulty raid — but nil reaches
    // `playerDifficulty == 1` and `if ( isDynamicInstance )` in minimap.lua,
    // and the interface unpacks all seven on one line.
    lua_pushnumber(L, 0);                                   // 6: playerDifficulty
    lua_pushboolean(L, 0);                                  // 7: isDynamicInstance
    return 7;
}

static int lua_GetInstanceDifficulty(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? (gh->getInstanceDifficulty() + 1) : 1);
    return 1;
}

static int lua_strsplit(lua_State* L) {
    const char* delim = luaL_checkstring(L, 1);
    const char* str = luaL_checkstring(L, 2);
    if (!delim[0]) { lua_pushstring(L, str); return 1; }
    int count = 0;
    std::string s(str);
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t found = s.find(delim[0], pos);
        if (found == std::string::npos) {
            lua_pushstring(L, s.substr(pos).c_str());
            count++;
            break;
        }
        lua_pushstring(L, s.substr(pos, found - pos).c_str());
        count++;
        pos = found + 1;
    }
    return count;
}

// strtrim(str) — remove leading/trailing whitespace
static int lua_strtrim(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    std::string s(str);
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    lua_pushstring(L, (start == std::string::npos) ? "" : s.substr(start, end - start + 1).c_str());
    return 1;
}

/// strlenutf8(s) — the number of characters, where string.len counts bytes.
///
/// Unbound, it answered nil through the fallback, and both callers do
/// arithmetic on the result rather than checking it: autocomplete.lua and
/// chatframe.lua compute an offset as
/// `GetUTF8CursorPosition() - strlenutf8(command) - 1`. A nil there raises, so
/// typing a slash command or a player name took chat autocomplete down.
static int lua_strlenutf8(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_optlstring(L, 1, "", &len);
    int chars = 0;
    for (size_t i = 0; i < len; ++i) {
        // A continuation byte is 10xxxxxx; every other byte opens a character.
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++chars;
    }
    lua_pushnumber(L, chars);
    return 1;
}

// wipe(table) — clear all entries from a table
static int lua_wipe(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    // Remove all integer keys
    int len = static_cast<int>(lua_objlen(L, 1));
    for (int i = len; i >= 1; i--) {
        lua_pushnil(L);
        lua_rawseti(L, 1, i);
    }
    // Remove all string keys
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        lua_pop(L, 1);       // pop value
        lua_pushvalue(L, -1); // copy key
        lua_pushnil(L);
        lua_rawset(L, 1);    // table[key] = nil
    }
    lua_pushvalue(L, 1);
    return 1;
}

// date(format) — safe date function (os.date was removed)
/// date(format, time) — the clock, as WoW exposes it.
///
/// Both shapes FrameXML uses: "*t" for a table of parts, a strftime string
/// otherwise, and an optional timestamp. Formatting "*t" as a strftime string
/// yields the literal "*t", which is what BetterDate then indexed for an hour.
static int lua_wow_date(lua_State* L) {
    const char* fmt = luaL_optstring(L, 1, "%c");
    const std::time_t when = lua_isnumber(L, 2)
        ? static_cast<std::time_t>(lua_tonumber(L, 2))
        : std::time(nullptr);

    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &when);
#else
    localtime_r(&when, &parts);
#endif

    if (std::strcmp(fmt, "*t") == 0 || std::strcmp(fmt, "!*t") == 0) {
        lua_newtable(L);
        auto set = [&](const char* key, int value) {
            lua_pushinteger(L, value);
            lua_setfield(L, -2, key);
        };
        set("year", parts.tm_year + 1900);
        set("month", parts.tm_mon + 1);
        set("day", parts.tm_mday);
        set("hour", parts.tm_hour);
        set("min", parts.tm_min);
        set("sec", parts.tm_sec);
        set("wday", parts.tm_wday + 1);
        set("yday", parts.tm_yday + 1);
        lua_pushboolean(L, parts.tm_isdst > 0);
        lua_setfield(L, -2, "isdst");
        return 1;
    }

    char out[256];
    const size_t n = std::strftime(out, sizeof(out), fmt, &parts);
    lua_pushlstring(L, out, n);
    return 1;
}

// time() — current unix timestamp
static int lua_wow_time(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(time(nullptr)));
    return 1;
}

// GetTime() — returns elapsed seconds since engine start (shared epoch)
static int lua_wow_gettime(lua_State* L) {
    lua_pushnumber(L, luaGetTimeNow());
    return 1;
}

// Names FrameXML reaches for that this client has no state behind yet.
//
// Found by running the load with the missing-API fallback off, which is the
// only way to see them: with it on they answer and the gap is invisible.
// IsThreatWarningEnabled alone was asked 58 times in one load.
//
// Answering falsely is the point. Each returns what the feature being absent
// looks like — no threat warnings, no runes, nobody to pass loot to — so the
// caller takes the branch it would take on a client where that feature is off,
// rather than dividing by a nil.
static int lua_ReturnFalse(lua_State* L) { lua_pushboolean(L, 0); return 1; }
static int lua_ReturnTrue(lua_State* L)  { lua_pushboolean(L, 1); return 1; }
static int lua_ReturnNil(lua_State* L)   { lua_pushnil(L); return 1; }
static int lua_ReturnZero(lua_State* L)  { lua_pushnumber(L, 0.0); return 1; }

/// Which row the skill list has selected, which is UI state rather than
/// anything the game knows. Same shape as selectedFriend and selectedIgnore.
static int& selectedSkill() { static int v = 0; return v; }
static int lua_ReturnNothing(lua_State*) { return 0; }

/// Which channel the channel-list panel has highlighted. Panel state with no
/// counterpart in the game, kept here so the getter and setter agree.
static int& selectedDisplayChannel() { static int selected = 0; return selected; }

/// A cooldown that is not running: start and duration both zero. Two values,
/// because the caller adds them together on the next line —
/// local start, duration = GetSummonFriendCooldown(); start + duration — and
/// one of them missing is arithmetic on nil.
/// The resolutions this client offers, as "WIDTHxHEIGHT" strings, and which of
/// them is current. One entry — the window as it actually is — because this
/// client does not enumerate modes.
///
/// UpdateMenuBarTop reads them together and immediately divides:
///   string.match((({GetScreenResolutions()})[GetCurrentResolution()] or ""),
///                "(%d+).-(%d+)")
/// then tonumber(width) / tonumber(height). An empty list makes both nil.
static int lua_GetScreenResolutions(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* win = svc ? svc->window : nullptr;
    const int w = win ? win->getWidth() : 1920;
    const int h = win ? win->getHeight() : 1080;
    lua_pushstring(L, (std::to_string(w) + "x" + std::to_string(h)).c_str());
    return 1;
}

static int lua_GetCurrentResolution(lua_State* L) {
    lua_pushnumber(L, 1.0);
    return 1;
}

/// A position on the battlefield map for someone who is not there: origin and
/// no name. Three values, because WorldMapFrame_Update multiplies the first
/// two by the map's dimensions on the line after reading them, and its loop is
/// bounded by MAX_RAID_MEMBERS rather than by how many are actually present.
static int lua_GetBattlefieldPosition(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushstring(L, "");
    return 3;
}

// ---- Chat window settings ----
//
// The three getters here answered with fixed numbers and every setter was
// missing, so the interface could read a chat window's settings and never save
// one. Moving a window, resizing it, recolouring it or closing it all went
// through a setter that did nothing, and the next FCF_LoadChatSettings read
// back the same defaults it read the first time.
//
// This is entirely client-side — WoW keeps it in the config, not on the server
// — so a store here is the whole feature rather than a stand-in for one.
struct ChatWindowSettings {
    std::string name;
    float fontSize = 14.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, alpha = 1.0f;
    bool shown = false;
    bool locked = false;
    int  docked = 0;            // 0 = not docked; otherwise its place on the dock
    bool uninteractable = false;
    // Saved geometry. Absent until something saves it, which is not the same as
    // zero: FCF_RestorePositionAndDimensions only restores what it is given,
    // and a zero width is a window with no width.
    bool hasPosition = false;
    std::string point = "TOPLEFT";
    float xOffset = 0.0f, yOffset = 0.0f;
    bool hasDimensions = false;
    float width = 0.0f, height = 0.0f;
};

/// NUM_CHAT_WINDOWS in 3.3.5. Indices are one-based from Lua.
static constexpr int kNumChatWindows = 10;

static std::array<ChatWindowSettings, kNumChatWindows>& chatWindows() {
    static std::array<ChatWindowSettings, kNumChatWindows> windows = [] {
        std::array<ChatWindowSettings, kNumChatWindows> w{};
        // WoW's default layout docks General and the combat log and leaves the
        // rest neither shown nor docked. Which window is being asked about
        // matters: docked is that window's place on the dock, and FCF_DockFrame
        // asserts that whatever claims position one is the dock's primary, so
        // answering one for every window claimed each was first.
        w[0].shown = true; w[0].docked = 1;
        w[1].shown = true; w[1].docked = 2;
        return w;
    }();
    return windows;
}

/// The window a one-based index names, or nullptr if it names none.
static ChatWindowSettings* chatWindow(lua_State* L, int argIndex) {
    const int id = static_cast<int>(luaL_optnumber(L, argIndex, 0));
    if (id < 1 || id > kNumChatWindows) return nullptr;
    return &chatWindows()[static_cast<size_t>(id - 1)];
}

/// A chat window's saved settings. FCF_SetWindowAlpha takes the alpha from
/// here and remembers it as oldAlpha, which the fade handlers then hand to
/// max() on every mouse-over — so a missing alpha is not a cosmetic gap, it is
/// an error every time the cursor crosses the frame.
static int lua_GetChatWindowInfo(lua_State* L) {
    const ChatWindowSettings* w = chatWindow(L, 1);
    if (!w) w = &chatWindows()[0];

    lua_pushstring(L, w->name.c_str());
    lua_pushnumber(L, w->fontSize);
    lua_pushnumber(L, w->r);
    lua_pushnumber(L, w->g);
    lua_pushnumber(L, w->b);
    lua_pushnumber(L, w->alpha);
    // Numbers and nil, not booleans. docked is a dock position, not a flag —
    // FCF_LoadChatSettings hands it straight to FCF_DockFrame as the index to
    // insert at, and that compares it against a count. A boolean there is a
    // comparison between a boolean and a number, which is an error rather than
    // a wrong answer.
    if (w->shown) lua_pushnumber(L, 1.0); else lua_pushnil(L);
    if (w->locked) lua_pushnumber(L, 1.0); else lua_pushnil(L);
    if (w->docked > 0) lua_pushnumber(L, w->docked); else lua_pushnil(L);
    if (w->uninteractable) lua_pushnumber(L, 1.0); else lua_pushnil(L);
    return 10;
}

// GetChatWindowSavedPosition(index) → point, xOffset, yOffset
static int lua_GetChatWindowSavedPosition(lua_State* L) {
    const ChatWindowSettings* w = chatWindow(L, 1);
    if (!w || !w->hasPosition) return luaReturnNil(L);
    lua_pushstring(L, w->point.c_str());
    lua_pushnumber(L, w->xOffset);
    lua_pushnumber(L, w->yOffset);
    return 3;
}

// GetChatWindowSavedDimensions(index) → width, height
static int lua_GetChatWindowSavedDimensions(lua_State* L) {
    const ChatWindowSettings* w = chatWindow(L, 1);
    if (!w || !w->hasDimensions) return luaReturnNil(L);
    lua_pushnumber(L, w->width);
    lua_pushnumber(L, w->height);
    return 2;
}

static int lua_SetChatWindowName(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) w->name = luaL_optstring(L, 2, "");
    return 0;
}
static int lua_SetChatWindowSize(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) w->fontSize = static_cast<float>(luaL_optnumber(L, 2, 14.0));
    return 0;
}
static int lua_SetChatWindowColor(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) {
        w->r = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        w->g = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        w->b = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    }
    return 0;
}
static int lua_SetChatWindowAlpha(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) w->alpha = static_cast<float>(luaL_optnumber(L, 2, 1.0));
    return 0;
}
static int lua_SetChatWindowShown(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) w->shown = lua_toboolean(L, 2) != 0;
    return 0;
}
static int lua_SetChatWindowLocked(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) w->locked = lua_toboolean(L, 2) != 0;
    return 0;
}
/// The dock position, which is a number and not a flag — see GetChatWindowInfo.
/// A false or nil means undocked, which is zero here rather than a missing key.
static int lua_SetChatWindowDocked(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) {
        if (lua_isnumber(L, 2)) w->docked = static_cast<int>(lua_tonumber(L, 2));
        else w->docked = lua_toboolean(L, 2) ? 1 : 0;
    }
    return 0;
}
static int lua_SetChatWindowUninteractable(lua_State* L) {
    if (auto* w = chatWindow(L, 1)) w->uninteractable = lua_toboolean(L, 2) != 0;
    return 0;
}
static int lua_SetChatWindowSavedPosition(lua_State* L) {
    auto* w = chatWindow(L, 1);
    if (!w) return 0;
    // Called with nil to forget the position, which is how a window that has
    // been re-docked stops being restored to where it floated.
    if (lua_isnoneornil(L, 2)) { w->hasPosition = false; return 0; }
    w->point   = luaL_optstring(L, 2, "TOPLEFT");
    w->xOffset = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    w->yOffset = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    w->hasPosition = true;
    return 0;
}
static int lua_SetChatWindowSavedDimensions(lua_State* L) {
    auto* w = chatWindow(L, 1);
    if (!w) return 0;
    if (lua_isnoneornil(L, 2)) { w->hasDimensions = false; return 0; }
    w->width  = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    w->height = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    w->hasDimensions = true;
    return 0;
}

/// ResetChatWindows() — back to the layout a new character starts with.
///
/// Announced, because the settings changing is not something the windows can
/// see. Every floating chat frame answers UPDATE_FLOATING_CHAT_WINDOWS by
/// re-reading its own settings, which is exactly what has just changed
/// underneath it; without the event the store is reset and the windows stay
/// where they were until something else happens to reload them.
static int lua_ResetChatWindows(lua_State* L) {
    auto& windows = chatWindows();
    windows = std::array<ChatWindowSettings, kNumChatWindows>{};
    windows[0].shown = true; windows[0].docked = 1;
    windows[1].shown = true; windows[1].docked = 2;

    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_lua_engine");
    auto* engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (engine) engine->fireEvent("UPDATE_FLOATING_CHAT_WINDOWS", {});
    return 0;
}

/// Whether a chat type colours player names by class. Stored per chat type
/// rather than globally, which is how the chat options panel presents it —
/// a row per type, each with its own tick.
static std::set<std::string>& chatColorByClass() {
    static std::set<std::string> types;
    return types;
}
static int lua_SetChatColorNameByClass(lua_State* L) {
    std::string type(luaL_optstring(L, 1, ""));
    toLowerInPlace(type);
    if (type.empty()) return 0;
    if (lua_toboolean(L, 2)) chatColorByClass().insert(type);
    else chatColorByClass().erase(type);
    return 0;
}
static int lua_GetChatColorNameByClass(lua_State* L) {
    std::string type(luaL_optstring(L, 1, ""));
    toLowerInPlace(type);
    lua_pushboolean(L, chatColorByClass().count(type) ? 1 : 0);
    return 1;
}

/// Names FrameXML reached for and did not find, harvested from a run rather
/// than guessed at: the fallback records each once and reports the list at
/// shutdown. Each answers with what the feature being absent looks like, so
/// the caller takes the branch it would take on a client where that feature is
/// switched off.
static int lua_GetMapInfo(lua_State* L) {
    // mapFileName, textureHeight, textureWidth. WorldMapFrame builds a texture
    // path out of the first and divides by the other two.
    lua_pushstring(L, "");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 3;
}

/// The expansion this client speaks. Two is Wrath, which is what the wire
/// format and the DBC layouts here assume.
static int lua_GetExpansionLevel(lua_State* L) {
    lua_pushnumber(L, 2.0);
    return 1;
}

/// Normal, which is the difficulty a fresh group is on.
static int lua_ReturnOne(lua_State* L) {
    lua_pushnumber(L, 1.0);
    return 1;
}

// The always-up battleground score lines, in the order FrameXML asks for them.
// The server sends world states as bare key/value pairs, so the labels come
// from the shared table this client's own heads-up display also reads.
struct WorldStateLine {
    std::string text;
};

static std::vector<WorldStateLine> worldStateLines(game::GameHandler* gh) {
    std::vector<WorldStateLine> out;
    if (!gh) return out;
    const game::BgScoreDef* def = game::findBgScoreDef(gh->getWorldStateMapId());
    if (!def) return out;

    auto alliance = gh->getWorldState(def->allianceKey);
    auto horde    = gh->getWorldState(def->hordeKey);
    if (!alliance && !horde) return out;

    uint32_t maxScore = def->hardcodedMax;
    if (def->maxKey != 0) {
        if (auto mv = gh->getWorldState(def->maxKey)) maxScore = *mv;
    }
    const bool showMax = maxScore > 0 && def->unit && def->unit[0] != '\0';

    char buf[96];
    for (int side = 0; side < 2; ++side) {
        const char* label = side == 0 ? "Alliance" : "Horde";
        const uint32_t score = (side == 0 ? alliance : horde).value_or(0);
        if (showMax) snprintf(buf, sizeof(buf), "%s: %u/%u", label, score, maxScore);
        else         snprintf(buf, sizeof(buf), "%s: %u", label, score);
        out.push_back({buf});
    }
    return out;
}

// GetNumWorldStateUI() — how many always-up lines there are to draw.
static int lua_GetNumWorldStateUI(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(worldStateLines(getGameHandler(L)).size()));
    return 1;
}

// GetWorldStateUIInfo(index) → uiType, state, text, icon, dynamicIcon, tooltip,
// dynamicTooltip, extendedUI, extendedUIState1, extendedUIState2, extendedUIState3
//
// uiType 0 keeps worldstateframe.lua out of its world-PvP branch, which is the
// one gated on a CVar and on IsSubZonePVPPOI. state 1 means "show, no flash".
static int lua_GetWorldStateUIInfo(lua_State* L) {
    const int index = static_cast<int>(luaL_optinteger(L, 1, 0));
    const auto lines = worldStateLines(getGameHandler(L));
    if (index < 1 || index > static_cast<int>(lines.size())) return 0;

    lua_pushinteger(L, 0);                              // uiType
    lua_pushinteger(L, 1);                              // state
    lua_pushstring(L, lines[index - 1].text.c_str());   // text
    lua_pushstring(L, "");                              // icon
    lua_pushstring(L, "");                              // dynamicIcon
    lua_pushstring(L, "");                              // tooltip
    lua_pushstring(L, "");                              // dynamicTooltip
    lua_pushstring(L, "");                              // extendedUI
    lua_pushinteger(L, 0);                              // extendedUIState1
    lua_pushinteger(L, 0);                              // extendedUIState2
    lua_pushinteger(L, 0);                              // extendedUIState3
    return 11;
}

static int lua_GetDefaultLanguage(lua_State* L) {
    auto* gh = getGameHandler(L);
    static const std::set<uint8_t> kHordeRaces = {2, 5, 6, 8, 10};
    const bool horde = gh && kHordeRaces.count(gh->getPlayerRace()) > 0;
    lua_pushstring(L, horde ? "Orcish" : "Common");
    return 1;
}

/// No enchant on either hand: four values per hand, and MainMenuBar reads the
/// expiry as a number.
/// GetWeaponEnchantInfo() → per hand: hasEnchant, expiration, charges.
///
/// It answered no for both hands unconditionally, so TemporaryEnchantFrame
/// took its early exit and hid itself — a sharpening stone or an oil showed
/// nothing at all. The buff bar is handed over, so this client's own weapon
/// enchant display beside it is suppressed and this was the only one left.
///
/// The enchant is tracked per equipped item; its remaining time is not, and
/// the frame reads expiration only to write a countdown under an icon it has
/// already decided to show. Zero there costs the countdown, not the icon.
static int lua_GetWeaponEnchantInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const game::EquipSlot kHands[2] = {game::EquipSlot::MAIN_HAND,
                                       game::EquipSlot::OFF_HAND};
    for (const game::EquipSlot hand : kHands) {
        bool enchanted = false;
        if (gh) {
            const uint64_t guid = gh->getEquipSlotGuid(static_cast<int>(hand));
            if (guid != 0) enchanted = gh->getItemEnchantIds(guid).second != 0;
        }
        lua_pushboolean(L, enchanted ? 1 : 0);   // hasEnchant
        lua_pushnumber(L, 0.0);                  // expiration, not tracked
        lua_pushnumber(L, 0.0);                  // charges
    }
    return 6;
}

/// Which modifier keys are held. Answered from the real keyboard rather than
/// falsely, because a shift-click means something different from a click and
/// FrameXML asks these on every button press.
static int lua_IsShiftKeyDown(lua_State* L) {
    lua_pushboolean(L, (SDL_GetModState() & KMOD_SHIFT) != 0);
    return 1;
}
static int lua_IsControlKeyDown(lua_State* L) {
    lua_pushboolean(L, (SDL_GetModState() & KMOD_CTRL) != 0);
    return 1;
}
static int lua_IsAltKeyDown(lua_State* L) {
    lua_pushboolean(L, (SDL_GetModState() & KMOD_ALT) != 0);
    return 1;
}
static int lua_IsModifierKeyDown(lua_State* L) {
    const SDL_Keymod m = SDL_GetModState();
    lua_pushboolean(L, (m & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)) != 0);
    return 1;
}

/// Whether an addon is loaded. Real rather than false: the registry holds the
/// addons that were enabled and loaded this session, and FrameXML asks before
/// deciding whether a feature exists — answering no where the answer is yes
/// hides an addon from the interface that is meant to work with it.
static int lua_IsAddOnLoaded(lua_State* L) {
    const char* wanted = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
    if (!wanted) { lua_pushboolean(L, 0); return 1; }

    lua_getfield(L, LUA_REGISTRYINDEX, "wowee_addon_info");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 0); return 1; }
    const int count = static_cast<int>(lua_objlen(L, -1));
    bool found = false;
    for (int i = 1; i <= count && !found; ++i) {
        lua_rawgeti(L, -1, i);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "loadOnDemand");
            const bool lod = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            lua_getfield(L, -1, "name");
            const char* name = lua_tostring(L, -1);
            // Being listed means loaded only for addons that load at startup.
            // A load-on-demand one is listed from the start and loaded later,
            // so its state is the manager's to answer, below.
            found = !lod && name && std::strcmp(name, wanted) == 0;
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    // A load-on-demand addon is not in the list handed to the VM at startup —
    // it is not loaded then — so its loaded state is asked of the manager.
    if (!found) {
        auto* svc = getLuaServices(L);
        if (svc && svc->isAddOnLoaded) found = svc->isAddOnLoaded(wanted);
    }
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}

/// GetTime() → seconds since the client started, as a float.
///
/// The interface's shared reference for anything timed: a cooldown records
/// GetTime() + duration and something else compares against it later. It was
/// never implemented, so every one of those comparisons was against nil —
/// including the ones in this client's own bootstrap.
/// LoadAddOn(name) → loaded, reason.
///
/// The reason is not optional when loaded is false: UIParentLoadAddOn builds
/// an error message out of _G["ADDON_" .. reason], so nil there is a
/// concatenation against nothing. These are Blizzard's own load-on-demand
/// panels — the talent frame and its like — which this client does not ship,
/// and MISSING is the reason string for exactly that.
/// LoadAddOn(name) → loaded, reason
///
/// How the interface reaches half its own panels: the talent tree, the
/// achievement window, the macro editor, the key bindings, the trade skill and
/// glyph frames are all load-on-demand addons that FrameXML asks for the first
/// time one is opened. This answered "MISSING" unconditionally, so none of them
/// ever appeared — and an addon that ships an optional module got the same.
static int lua_LoadAddOn(lua_State* L) {
    const char* name = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
    auto* svc = getLuaServices(L);
    if (!name || !svc || !svc->loadAddOn) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "MISSING");
        return 2;
    }
    std::string reason;
    const bool ok = svc->loadAddOn(name, reason);
    lua_pushboolean(L, ok ? 1 : 0);
    if (ok) lua_pushnil(L);
    else    lua_pushstring(L, reason.empty() ? "MISSING" : reason.c_str());
    return 2;
}

static int lua_ReturnNoCooldown(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 2;
}

/// Alliance or Horde for the player, and the same for anyone else until this
/// client tracks other units' factions. Returns the English tag and the
/// localised name, which is the pair FrameXML expects.
static int lua_UnitFactionGroup(lua_State* L) {
    auto* gh = getGameHandler(L);
    bool horde = false;
    if (gh) {
        // Orc, Undead, Tauren, Troll, Blood Elf.
        static const std::set<uint8_t> kHordeRaces = {2, 5, 6, 8, 10};
        horde = kHordeRaces.count(gh->getPlayerRace()) > 0;
    }
    lua_pushstring(L, horde ? "Horde" : "Alliance");
    lua_pushstring(L, horde ? "Horde" : "Alliance");
    return 2;
}

// RunScript(body) → compiles and runs Lua, the way /script and every macro do
//
// A compile failure is reported the same way a runtime one is, because to
// whoever typed it they are the same mistake. The error goes through the normal
// path, which shows it without telling any script about it.
static int lua_RunScript(lua_State* L) {
    const char* body = luaL_optstring(L, 1, nullptr);
    if (!body || !*body) return 0;
    if (luaL_loadstring(L, body) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_WARNING("RunScript would not compile: ", err ? err : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_WARNING("RunScript failed: ", err ? err : "?");
        lua_pop(L, 1);
    }
    return 0;
}

// IsMouseButtonDown(button) → whether it is held right now
//
// Named as WoW names them — "LeftButton", "RightButton", "MiddleButton" — and
// answers for any button when asked for none, which is what a bare call means.
static int lua_IsMouseButtonDown(lua_State* L) {
    const char* which = luaL_optstring(L, 1, nullptr);
    bool down = false;
    if (!which || !*which) {
        down = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
               ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
               ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    } else {
        std::string name(which);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == "leftbutton" || name == "left")        down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        else if (name == "rightbutton" || name == "right") down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        else if (name == "middlebutton" || name == "middle") down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    }
    lua_pushboolean(L, down ? 1 : 0);
    return 1;
}

// Screenshot() → saves one where the client's own binding puts it
static int lua_Screenshot(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->takeScreenshot) svc->takeScreenshot();
    return 0;
}

// HasLFGRestrictions() → whether the player is in a dungeon-finder group
//
// There is a dungeon finder here — the client tracks the queue, the proposal
// and the dungeon — so this is answered from it rather than declared false the
// way it was when the comment here said no such thing existed.
static int lua_HasLFGRestrictions(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushboolean(L, gh && gh->isLfgInDungeon() ? 1 : 0);
    return 1;
}

// GetLFGProposal() → proposalExists, typeID, id, name, texture, role,
//                    hasResponded, totalEncounters, completedEncounters,
//                    numMembers, isLeader
//
// Eleven values whether or not there is a proposal: lfdframe reads the eighth
// on its own with select(8, GetLFGProposal()), and a short return makes that
// an error rather than an answer of "none".
static int lua_GetLFGProposal(lua_State* L) {
    auto* gh = getGameHandler(L);
    const bool pending = gh && gh->getLfgState() == game::LfgState::Proposal;
    if (!pending) { for (int i = 0; i < 11; ++i) lua_pushnil(L); return 11; }

    lua_pushboolean(L, 1);                                       // 1: proposalExists
    lua_pushnil(L);                                              // 2: typeID
    lua_pushnumber(L, gh->getLfgProposalId());                   // 3: id
    lua_pushstring(L, gh->getCurrentLfgDungeonName().c_str());   // 4: name
    lua_pushnil(L);                                              // 5: texture
    lua_pushnil(L);                                              // 6: role
    lua_pushboolean(L, 0);                                       // 7: hasResponded
    lua_pushnil(L);                                              // 8: totalEncounters
    lua_pushnil(L);                                              // 9: completedEncounters
    lua_pushnil(L);                                              // 10: numMembers
    lua_pushnil(L);                                              // 11: isLeader
    return 11;
}

// GetLFGInfoServer() → inParty, joined, queued, noPartialClear, achievements,
//                      lfgComment, slotCount
static int lua_GetLFGInfoServer(lua_State* L) {
    auto* gh = getGameHandler(L);
    const bool queued = gh && gh->isLfgQueued();
    lua_pushnil(L);                        // 1: inParty
    lua_pushboolean(L, queued ? 1 : 0);    // 2: joined
    lua_pushboolean(L, queued ? 1 : 0);    // 3: queued
    lua_pushnil(L);                        // 4: noPartialClear
    lua_pushnil(L);                        // 5: achievements
    lua_pushnil(L);                        // 6: lfgComment
    lua_pushnil(L);                        // 7: slotCount
    return 7;
}

// GetLFGRoleUpdate() → roleCheckInProgress, slots, members
static int lua_GetLFGRoleUpdate(lua_State* L) {
    auto* gh = getGameHandler(L);
    const bool checking = gh && gh->getLfgState() == game::LfgState::RoleCheck;
    if (checking) lua_pushboolean(L, 1); else lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
}

static int lua_IsListedInLFR(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushboolean(L, gh && gh->getLfgState() == game::LfgState::RaidBrowser ? 1 : 0);
    return 1;
}

// IsPartyLFG() → was this group put together by the dungeon finder
static int lua_IsPartyLFG(lua_State* L) {
    auto* gh = getGameHandler(L);
    bool viaFinder = false;
    if (gh) {
        const auto st = gh->getLfgState();
        viaFinder = (st == game::LfgState::InDungeon ||
                     st == game::LfgState::FinishedDungeon ||
                     st == game::LfgState::Boot);
    }
    lua_pushboolean(L, viaFinder ? 1 : 0);
    return 1;
}

// The two cooldowns that gate re-queuing. Neither is tracked, and both are read
// as `if ( expiration )` — so nil, because a zero would read as a live cooldown
// and park the queue frame behind a countdown that never ends.
static int lua_GetLFGDeserterExpiration(lua_State* L) { lua_pushnil(L); return 1; }
static int lua_GetLFGRandomCooldownExpiration(lua_State* L) { lua_pushnil(L); return 1; }

// RefreshLFGList() — the raid browser's refresh button.
static int lua_RefreshLFGList(lua_State* L) { (void)L; return 0; }

// GetTrackedAchievements() → the achievement ids being watched, as separate
// values. The watch frame counts them by return count.
static int lua_GetTrackedAchievements(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    const auto& tracked = gh->getTrackedAchievements();
    for (uint32_t id : tracked) lua_pushnumber(L, id);
    return static_cast<int>(tracked.size());
}

// The PvP flag's countdown. The client knows whether the flag is set but not
// how long it has left, so the timer is reported as not running and the player
// frame hides the text rather than showing a number it cannot compute.
static int lua_IsPVPTimerRunning(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// Never reached while the above answers false, but playerframe reads it on the
// line after and a missing global there would raise before the branch is taken.
static int lua_GetPVPTimer(lua_State* L) { lua_pushnumber(L, 0); return 1; }

// GetCurrentArenaSeason() → the season number, or NO_ARENA_SEASON.
//
// Zero is the right answer here rather than the usual trap: arenaframe compares
// it with == and ~= against NO_ARENA_SEASON, which is itself 0, so a nil would
// fail both tests instead of meaning "no season".
static int lua_GetCurrentArenaSeason(lua_State* L) { lua_pushnumber(L, 0); return 1; }

// GetPVPRankProgress() → how far through the current rank, 0..1.
// Fed straight to HonorFrameProgressBar:SetValue, which needs a number.
static int lua_GetPVPRankProgress(lua_State* L) { lua_pushnumber(L, 0); return 1; }

// ---- Arena team roster ----
//
// Which team an index names comes from the same list GetArenaTeam reads.
static uint32_t arenaTeamIdAt(game::GameHandler* gh, int index) {
    if (!gh || index < 1) return 0;
    const auto& teams = gh->getArenaTeamStats();
    if (index > static_cast<int>(teams.size())) return 0;
    return teams[static_cast<size_t>(index) - 1].teamId;
}

// ArenaTeamRoster(index) — ask the server for the roster.
static int lua_ArenaTeamRoster(lua_State* L) {
    auto* gh = getGameHandler(L);
    const uint32_t teamId = arenaTeamIdAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
    if (gh && teamId) gh->requestArenaTeamRoster(teamId);
    return 0;
}

// GetArenaTeamRosterInfo(teamIndex, memberIndex) →
//   name, rank, level, class, online, played, win, seasonPlayed, seasonWin, rating
//
// The six counts are numbers rather than nil: the panel subtracts them the line
// after — `loss = played - win` — so a nil raises there. Class stays nil, which
// the panel does test before using, and rank and level are zero because the
// roster carries neither.
static int lua_GetArenaTeamRosterInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const uint32_t teamId = arenaTeamIdAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
    const int member = static_cast<int>(luaL_optnumber(L, 2, 0));
    if (!gh || teamId == 0 || member < 1) return luaReturnNil(L);
    const auto* roster = gh->getArenaTeamRoster(teamId);
    if (!roster || member > static_cast<int>(roster->members.size())) return luaReturnNil(L);
    const auto& m = roster->members[static_cast<size_t>(member) - 1];

    lua_pushstring(L, m.name.c_str());       // 1: name
    lua_pushnumber(L, 0);                    // 2: rank
    lua_pushnumber(L, 0);                    // 3: level
    lua_pushnil(L);                          // 4: class
    lua_pushboolean(L, m.online ? 1 : 0);    // 5: online
    lua_pushnumber(L, m.weekGames);          // 6: played
    lua_pushnumber(L, m.weekWins);           // 7: win
    lua_pushnumber(L, m.seasonGames);        // 8: seasonPlayed
    lua_pushnumber(L, m.seasonWins);         // 9: seasonWin
    lua_pushnumber(L, m.personalRating);     // 10: rating
    return 10;
}

// Which roster row is selected, and closing the roster. Both are the panel's
// own state — nothing is sent for either.
static int lua_GetArenaTeamRosterSelection(lua_State* L) { lua_pushnumber(L, 0); return 1; }
static int lua_SetArenaTeamRosterSelection(lua_State* L) { (void)L; return 0; }
static int lua_CloseArenaTeamRoster(lua_State* L) { (void)L; return 0; }

// Team captaincy is not reported by anything this client parses, so no team
// reads as the player's to run.
static int lua_IsArenaTeamCaptain(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// Closing the battlemaster's window, which is a client-side dismissal.
static int lua_CloseBattlefield(lua_State* L) { (void)L; return 0; }

// Leaving a vehicle, and whether its aim can be raised or lowered. Vehicles
// are not modelled here, so neither is possible.
static int lua_CanExitVehicle(lua_State* L) { lua_pushboolean(L, 0); return 1; }
static int lua_IsVehicleAimAngleAdjustable(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// HasKey() — whether the player carries a key ring at all. The keyring exists
// and holds keys, so the button that opens it is offered.
static int lua_HasKey(lua_State* L) { lua_pushboolean(L, 1); return 1; }

// GetArenaTeam(index) →
//   teamName, teamSize, teamRating, teamPlayed, teamWins, seasonTeamPlayed,
//   seasonTeamWins, playerPlayed, seasonPlayerPlayed, teamRank, playerRating,
//   backgroundR, backgroundG, backgroundB, emblem, emblemR, emblemG, emblemB,
//   border, borderR, borderG, borderB
//
// Twenty-two, because PVPTeam_Update unpacks every one of them on a single line
// and then feeds the colour components straight to SetVertexColor. Answering
// just the name — which is all the promote and kick confirmations need — left
// twenty-one nils behind it and took the team list down.
//
// The tabard is not tracked: SMSG_ARENA_TEAM_QUERY_RESPONSE carries the emblem
// style and colours and nothing here reads them. Those eight answer zero rather
// than nil, so the tabard draws black instead of raising mid-arithmetic.
static int lua_GetArenaTeam(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int idx = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh || idx < 1) return luaReturnNil(L);
    const auto& teams = gh->getArenaTeamStats();
    if (idx > static_cast<int>(teams.size())) return luaReturnNil(L);
    const auto& t = teams[static_cast<size_t>(idx) - 1];

    lua_pushstring(L, t.teamName.c_str());  // 1: teamName
    lua_pushnumber(L, t.teamType);          // 2: teamSize (2, 3 or 5)
    lua_pushnumber(L, t.rating);            // 3: teamRating
    lua_pushnumber(L, t.weekGames);         // 4: teamPlayed
    lua_pushnumber(L, t.weekWins);          // 5: teamWins
    lua_pushnumber(L, t.seasonGames);       // 6: seasonTeamPlayed
    lua_pushnumber(L, t.seasonWins);        // 7: seasonTeamWins
    // Per-player totals come with the roster, not the team summary.
    lua_pushnumber(L, 0);                   // 8: playerPlayed
    lua_pushnumber(L, 0);                   // 9: seasonPlayerPlayed
    lua_pushnumber(L, t.rank);              // 10: teamRank
    lua_pushnumber(L, 0);                   // 11: playerRating
    for (int i = 0; i < 11; ++i) lua_pushnumber(L, 0);  // 12-22: tabard
    return 22;
}

// The daily-win bonus on a random or holiday battleground.
//
// → hasWin, winHonor, winArena, lossHonor, lossArena
//
// Zero rather than nil for the four amounts, and the difference matters: the
// frame writes `if (winHonor ~= 0)` and shows a reward line when that passes.
// nil passes it — nil is not zero — and the line appears with nothing in it.
// Zero says "no bonus" and the line is correctly skipped.
//
// The bonus itself is a per-character daily the server tracks and does not
// volunteer, so "none available" is the honest answer rather than a placeholder
// for one. Reachable, unlike most of this file's absences: the random
// battleground row is drawn from GetBattlegroundInfo, which answers for real.
static int lua_BattlegroundHonorBonusesNone(lua_State* L) {
    lua_pushboolean(L, 0);   // hasWin — the daily is not known to be waiting
    lua_pushnumber(L, 0);    // winHonor
    lua_pushnumber(L, 0);    // winArena
    lua_pushnumber(L, 0);    // lossHonor
    lua_pushnumber(L, 0);    // lossArena
    return 5;
}

// GetBattlefieldInstanceInfo(index) → which numbered instance that row is.
//
// Zero, which the list reads as unnumbered — the same "first available" the
// selection means. The server numbers instances only for a client that asks to
// pick one, and nothing here does.
static int lua_GetBattlefieldInstanceInfo(lua_State* L) {
    (void)L;
    lua_pushnumber(L, 0);
    return 1;
}

// IsInLFGDungeon() — standing inside a dungeon the finder put you in.
//
// The state is already tracked: SocialHandler keeps an LfgState and InDungeon
// is one of its values. Nothing read it, so the minimap's dungeon button could
// not tell inside from outside and offered the wrong direction.
static int lua_IsInLFGDungeon(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushboolean(L, gh && gh->getLfgState() == game::LfgState::InDungeon ? 1 : 0);
    return 1;
}

// LFGTeleport(out) — out to the dungeon's entrance, or back in.
static int lua_LFGTeleport(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->lfgTeleport(lua_toboolean(L, 1) != 0);
    return 0;
}

// Whether the world the player is standing in is an arena.
//
// From BattlemasterList.dbc, which names each row's maps and which this client
// already reads for the queue list — the arena rows are loaded alongside the
// battleground ones and only the battlegrounds are kept in the queue list, so
// the arena maps were sitting there unasked.
//
// Both of these answered nil, which the battlefield frame and the arena frame
// each read as "not an arena". That is right until the player is in one.
static int lua_IsBattlefieldArena(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushboolean(L, gh && gh->isArenaMap(gh->getCurrentMapId()) ? 1 : 0);
    return 1;
}

// IsActiveBattlefieldArena() → isArena, isRegistered
//
// The second is whether the team is a registered one rather than a skirmish,
// which needs the arena team the server never mentions outside a match. Left
// nil, and the frame treats that as unregistered — which a skirmish is.
static int lua_IsActiveBattlefieldArena(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh && gh->isArenaMap(gh->getCurrentMapId())) lua_pushboolean(L, 1);
    else lua_pushnil(L);
    lua_pushnil(L);
    return 2;
}

// CanHearthAndResurrectFromArea() → whether the zone offers the combined
// hearth-and-release button. Only world PvP zones do, and none are tracked.
static int lua_CanHearthAndResurrectFromArea(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// GetWorldPVPQueueStatus(i) → status, mapName, queueID
static int lua_GetWorldPVPQueueStatus(lua_State* L) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
}

// LeaveBattlefield() — walk out of the battleground currently being played.
static int lua_LeaveBattlefield(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) gh->leaveBattlefield();
    return 0;
}

// ---- World map player arrow and ping ----
//
// In the real client these manage a rotating 3D arrow model showing where the
// player is standing and which way they face, plus the ping that plays when a
// party member signals a spot on the map.
//
// This client already draws its own player marker, party dots and quest POIs
// over the map, so the arrow would be a second marker on top of the first.
// They are defined anyway because two of them are called from
// WorldMapFrame_OnLoad: without them that function raised on its fifth line,
// so the black separator never got its colour, the ping never initialised, and
// the WorldMapFrame_Update() call that ends OnLoad never ran at all.
//
// UpdateWorldMapArrowFrames is not among them: it was already registered
// elsewhere in this file, which is why it never appeared as missing.
static int lua_CreateWorldMapArrowFrame(lua_State* L) { (void)L; return 0; }
static int lua_ShowWorldMapArrowFrame(lua_State* L) { (void)L; return 0; }
static int lua_PositionWorldMapArrowFrame(lua_State* L) { (void)L; return 0; }
static int lua_InitWorldMapPing(lua_State* L) { (void)L; return 0; }

// The battlefield minimap's own copy of the arrow, for the same reason and
// with the same answer as the world map's above.
static int lua_CreateMiniWorldMapArrowFrame(lua_State* L) { (void)L; return 0; }
static int lua_PositionMiniWorldMapArrowFrame(lua_State* L) { (void)L; return 0; }
static int lua_ShowMiniWorldMapArrowFrame(lua_State* L) { (void)L; return 0; }

// ---- The stored combat log (Blizzard_CombatLog's refilter) ----
//
// The client keeps no history of combat events — they are handled as they
// arrive — so there is nothing to walk back through and the log rebuilds itself
// from new events only.
//
// CombatLogGetCurrentEntry answers nil rather than zero, and that distinction
// is the whole of it. Blizzard_CombatLog_RefilterUpdate loops
// `while (valid and total < COMBATLOG_LIMIT_PER_FRAME)`, and a zero is true in
// Lua — it would add a line per iteration from an entry that does not exist,
// stop only on the per-frame cap, and be re-armed by the OnUpdate that
// scheduled it, every frame, for as long as the log was open.
static int lua_CombatLogGetNumEntries(lua_State* L) { lua_pushnumber(L, 0); return 1; }
static int lua_CombatLogGetCurrentEntry(lua_State* L) { (void)L; return luaReturnNil(L); }
static int lua_CombatLogAdvanceEntry(lua_State* L) { (void)L; return luaReturnNil(L); }
static int lua_CombatLogSetCurrentEntry(lua_State* L) { (void)L; return 0; }
static int lua_CombatLogAddFilter(lua_State* L) { (void)L; return 0; }
static int lua_CombatLogResetFilter(lua_State* L) { (void)L; return 0; }

// CombatTextSetActiveUnit(unit) — which unit the floating combat text follows.
// It tells the client where to aim the events it already sends; the events do
// not change, so this is recorded by the caller and nothing is needed here.
static int lua_CombatTextSetActiveUnit(lua_State* L) { (void)L; return 0; }

// GetBattlefieldMapIconScale() → what to multiply the map's icon sizes by.
// One is the client's own default; the icons are sized in the frame itself,
// and every use here is a multiply, so a nil would take the arithmetic down.
static int lua_GetBattlefieldMapIconScale(lua_State* L) { lua_pushnumber(L, 1.0); return 1; }

// PlayerIsPVPInactive(unit) → whether a battleground member has gone idle and
// is about to be removed. The server reports this per-player in a battleground
// and nothing here parses it, so nobody reads as idle.
static int lua_PlayerIsPVPInactive(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// ---- The battlemaster's battleground list ----
//
// GetBattlefieldInfo() → mapName, mapDescription, maxGroup
//
// About the battleground the battlemaster being spoken to offers, which is
// what the last SMSG_BATTLEFIELD_LIST described. The frame returns early on a
// nil name, so before this the list drew nothing at all.
//
// The description is nil: BattlemasterList.dbc carries no blurb, and the panel
// treats a missing one as "no description" rather than raising on it.
static int lua_GetBattlefieldInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return luaReturnNil(L);
    const auto& bgs = gh->getAvailableBgs();
    if (bgs.empty()) return luaReturnNil(L);
    const auto* info = gh->getBattlemasterInfo(bgs.back().bgTypeId);
    if (!info || info->name.empty()) return luaReturnNil(L);
    lua_pushstring(L, info->name.c_str());   // 1: mapName
    lua_pushnil(L);                          // 2: mapDescription
    lua_pushnumber(L, info->maxGroupSize);   // 3: maxGroup
    return 3;
}

// GetNumBattlegroundTypes() → how many battlegrounds there are to queue for.
//
// This is the PvP frame's own list, not the battlemaster's offering, and it
// comes from BattlemasterList.dbc — which this client already loads. It was
// answering zero from the counting stub, so the list drew no rows, nothing was
// ever assigned frame.BGindex, and the click handler read that field as nil.
//
// Arenas are excluded: they share the table with battlegrounds and are told
// apart by the instance type, so offering them here would queue the player for
// the wrong thing.
static int lua_GetNumBattlegroundTypes(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? static_cast<double>(gh->getBattlegroundTypes().size()) : 0.0);
    return 1;
}

// GetBattlegroundInfo(index) → localizedName, canEnter, isHoliday, isRandom, id
//
// canEnter is the level range from the row, which is what the client itself
// knows; the server still has the final say when the queue request arrives.
// isHoliday would need the call-to-arms world state, which nothing here parses,
// and claiming a holiday that is not running is worse than not mentioning one.
static int lua_GetBattlegroundInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh) return luaReturnNil(L);
    const auto& list = gh->getBattlegroundTypes();
    if (index < 1 || index > static_cast<int>(list.size())) return luaReturnNil(L);
    const auto& bg = list[static_cast<size_t>(index - 1)];

    // A row naming no level range — the random battleground is the only one —
    // is not a row saying nobody qualifies.
    const uint32_t level = gh->getPlayerLevel();
    const bool hasRange = bg.minLevel != 0 || bg.maxLevel != 0;
    const bool canEnter = !hasRange || (level >= bg.minLevel && level <= bg.maxLevel);

    lua_pushstring(L, bg.name.c_str());
    lua_pushboolean(L, canEnter ? 1 : 0);
    lua_pushboolean(L, 0);                       // isHoliday: world state not parsed
    lua_pushboolean(L, bg.mapCount > 1 ? 1 : 0); // several maps means a pool, i.e. random
    lua_pushnumber(L, bg.id);
    return 5;
}

// RequestBattlegroundInstanceInfo(index) — ask which instances are running.
//
// The reply is SMSG_BATTLEFIELD_LIST, which this client already handled and
// had no way to ask for. The index is a row in the list above, not a
// battleground id, so it is translated before it goes on the wire.
static int lua_RequestBattlegroundInstanceInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    if (!gh) return 0;
    const auto& list = gh->getBattlegroundTypes();
    if (index < 1 || index > static_cast<int>(list.size())) return 0;
    gh->requestBattlefieldList(list[static_cast<size_t>(index - 1)].id);
    return 0;
}

// Which instance of a battleground is picked in the list.
//
// Kept here because the pair has to round-trip: the frame highlights the row
// matching what it last set, so a getter that always answered zero left the
// first row highlighted whatever was clicked.
//
// Zero remains the default and still means "first available", which is what
// the server understands as no preference — the list opens on it, and nothing
// here queues for a specific instance regardless. This is the selection the
// interface is showing, not an instruction to the server.
static int& selectedBattlefield() { static int selected = 0; return selected; }

static int lua_GetSelectedBattlefield(lua_State* L) {
    lua_pushnumber(L, selectedBattlefield());
    return 1;
}

// SetSelectedBattlefield(index) — called unguarded from two places, so it has
// to exist before it has to do anything.
static int lua_SetSelectedBattlefield(lua_State* L) {
    selectedBattlefield() = static_cast<int>(luaL_optnumber(L, 1, 0));
    return 0;
}

// JoinBattlefield(index, asGroup, isArena) — queue for it.
static int lua_JoinBattlefield(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    const auto& bgs = gh->getAvailableBgs();
    if (bgs.empty()) return 0;
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const bool asGroup = lua_toboolean(L, 2) != 0;
    // The index names an instance in the list, and zero means first available.
    const auto& bg = bgs.back();
    uint32_t instanceId = 0;
    if (index > 0 && index <= static_cast<int>(bg.instanceIds.size())) {
        instanceId = bg.instanceIds[static_cast<size_t>(index) - 1];
    }
    gh->joinBattlefield(gh->getCurrentGossip().npcGuid, bg.bgTypeId, instanceId, asGroup);
    return 0;
}

// GetLFGCompletionReward() →
//   name, typeID, textureFilename, moneyBase, moneyVar, experienceBase,
//   experienceVar, numStrangers, numRewards
//
// What a finished dungeon finder run paid out. Nothing here tracks it, so there
// is none — but the six counts answer zero rather than nil, which is the
// opposite of the usual choice and for the opposite reason. The alert frame
// does not test them, it does arithmetic with them:
//
//     local moneyAmount = moneyBase + moneyVar * numStrangers
//
// so a nil raises there rather than reading as absent. The three describing the
// dungeon stay nil, because those are shown rather than added.
static int lua_GetLFGCompletionReward(lua_State* L) {
    lua_pushnil(L);          // 1: name
    lua_pushnil(L);          // 2: typeID
    lua_pushnil(L);          // 3: textureFilename
    for (int i = 0; i < 6; ++i) lua_pushnumber(L, 0);
    return 9;
}

// GetLFGCompletionRewardItem(index) → texturePath, quantity
// Only reached once the call above reports rewards, which it never does.
static int lua_GetLFGCompletionRewardItem(lua_State* L) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 2;
}

// RunMacroText(body) — run a macro body, one command per line.
//
// The same path the action bar takes for a macro button, so a macro run from a
// party frame's click behaves as one run from the bar: /stopmacro is honoured,
// and each line goes through the slash dispatch rather than being sent as chat.
static int lua_RunMacroText(lua_State* L) {
    auto* svc = getLuaServices(L);
    const char* body = luaL_optstring(L, 1, "");
    if (svc && svc->runMacroText && body && *body) svc->runMacroText(body);
    return 0;
}

// RunMacro(id or name) — run a saved macro by which one it is.
static int lua_RunMacro(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* gh = getGameHandler(L);
    if (!svc || !svc->runMacroText || !gh) return 0;
    uint32_t macroId = 0;
    if (lua_isnumber(L, 1)) {
        macroId = static_cast<uint32_t>(lua_tonumber(L, 1));
    } else if (const char* name = lua_tostring(L, 1)) {
        // By name, which is how a macro written into another macro names it.
        for (uint32_t id : gh->getMacroIds()) {
            if (gh->getMacroName(id) == name) { macroId = id; break; }
        }
    }
    if (macroId == 0) return 0;
    const std::string& body = gh->getMacroText(macroId);
    if (!body.empty()) svc->runMacroText(body);
    return 0;
}

// TriggerTutorial(id) — show one of the interface's tutorial pop-outs.
//
// Tutorials are a saved per-account set of which have been seen, and none of
// that is kept here, so nothing is shown. Answered rather than left missing
// because the bag bar fires it whenever a bag is picked up.
static int lua_TriggerTutorial(lua_State* L) { (void)L; return 0; }

// Quit() — leave the game, as the game menu's Exit button does.
//
// The same path /exit takes: a clean logout that ends the process rather than
// dropping to character select.
static int lua_Quit(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) gh->requestLogout(/*exitAfterLogout=*/true);
    return 0;
}

// ReloadUI() — rebuild the interface, as /reload does.
//
// Only asks. The reload shuts this Lua state down and builds a new one, and
// every caller is inside it: a static popup's OnAccept after a setting that
// needs one, or /reload typed at the interface rather than at the client's own
// command handler. Doing the work here would free the state mid-call.
static int lua_ReloadUI(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->requestReloadUI) svc->requestReloadUI();
    return 0;
}

// GetGamma() / SetGamma(value) — screen brightness, as the video options mean
// it. One is neutral. Backed by the client's own brightness setting, so the
// two sliders move together instead of disagreeing.
static int lua_GetGamma(lua_State* L) {
    auto* svc = getLuaServices(L);
    lua_pushnumber(L, (svc && svc->getGamma) ? svc->getGamma() : 1.0);
    return 1;
}

static int lua_SetGamma(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->setGamma) svc->setGamma(static_cast<float>(luaL_optnumber(L, 1, 1.0)));
    return 0;
}

// GetVideoCaps() →
//   anisotropic, pixelShaders, vertexShaders, trilinear, buffering,
//   maxAnisotropy, hardwareCursor
//
// Everything here runs on Vulkan, so the capability questions all answer yes —
// they were written for a Direct3D 9 client that could genuinely lack them.
// maxAnisotropy is the one real number, and sixteen is the ceiling every
// device this client will start on supports; the options panel uses it only to
// bound its own dropdown.
static int lua_GetVideoCaps(lua_State* L) {
    lua_pushboolean(L, 1);   // 1: anisotropic
    lua_pushboolean(L, 1);   // 2: pixelShaders
    lua_pushboolean(L, 1);   // 3: vertexShaders
    lua_pushboolean(L, 1);   // 4: trilinear
    lua_pushboolean(L, 1);   // 5: buffering
    lua_pushnumber(L, 16);   // 6: maxAnisotropy
    lua_pushboolean(L, 1);   // 7: hardwareCursor
    return 7;
}

// GetCVarMin(name) / GetCVarMax(name) → the range a CVar is allowed, if it
// declares one.
//
// Nil, because none of them do here. Every caller is written for that:
// BlizzardOptionsPanel_GetCVarMinSafe passes it through tonumber, the slider
// setup falls back with `or entry.minValue`, and the clamp reads
// `if ( minValue and value < minValue )`. Answering a made-up zero instead
// would clamp every graphics slider in the options panel to it.
static int lua_GetCVarMin(lua_State* L) { (void)L; return luaReturnNil(L); }
static int lua_GetCVarMax(lua_State* L) { (void)L; return luaReturnNil(L); }

// ---- Voice chat ----
//
// There is no voice chat in this client and no plan for one. These are answered
// rather than left missing because the panels that ask are otherwise perfectly
// usable: the audio options page reads the microphone level from an OnUpdate,
// so one absent global there raises every frame the page is open.
static int lua_IsVoiceChatAllowedByServer(lua_State* L) { lua_pushboolean(L, 0); return 1; }
static int lua_VoiceChat_IsRecordingLoopbackSound(lua_State* L) { lua_pushboolean(L, 0); return 1; }
static int lua_VoiceChat_IsPlayingLoopbackSound(lua_State* L) { lua_pushboolean(L, 0); return 1; }
static int lua_VoiceChat_GetCurrentMicrophoneSignalLevel(lua_State* L) { lua_pushnumber(L, 0); return 1; }

// GetVoiceSessionInfo(i) → name, active
static int lua_GetVoiceSessionInfo(lua_State* L) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
}

static int lua_GetNumVoiceSessionMembersBySessionID(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// RequestRaidInfo() — ask the server for saved instance lockouts. The reply is
// SMSG_RAID_INSTANCE_INFO, which the client already parses for
// GetSavedInstanceInfo; nothing was asking for it.
static int lua_RequestRaidInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) gh->requestRaidInfo();
    return 0;
}

// CanShowAchievementUI() → whether the achievement panel may open
static int lua_CanShowAchievementUI(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

// GetAddOnMemoryUsage(index) → kilobytes, and UpdateAddOnMemoryUsage() to
// refresh them
//
// Zero rather than nothing. The performance bar's tooltip adds these up —
// `totalMem = totalMem + mem` for every addon loaded — and nil there is an
// error, on an interface element that is drawn by default. Nothing here
// measures per-addon memory, and zero is what an unmeasured addon costs as far
// as this client knows.
static int lua_GetAddOnMemoryUsage(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// IsXPUserDisabled() → whether the player has turned experience off
//
// Nothing here can turn it off, so this is a definite no rather than an absent
// answer — the reputation panel reads it to decide whether to offer the bar.
static int lua_IsXPUserDisabled(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

/// The taxi nodes the flight map is showing, in a fixed order.
///
/// Every taxi function is asked about a node by position in this list, and the
/// answers only line up if each of them walks the same order. They used to walk
/// `getTaxiNodes()` directly, which is an unordered_map: its order is arbitrary,
/// and rehashing it — which learning a flight path while the window is open
/// does — reorders it under a list already drawn. Clicking a destination would
/// then fly somewhere else. Sorting by node id costs a copy of a few hundred
/// integers and makes the order the same every time it is asked.
///
/// Only the nodes on the map the player is standing on are listed. That is what
/// the flight map shows, and it is what this client's own window already lists.
static std::vector<uint32_t> taxiNodeOrder(game::GameHandler* gh) {
    std::vector<uint32_t> ids;
    if (!gh) return ids;

    const auto& nodes = gh->getTaxiNodes();
    const auto current = nodes.find(gh->getTaxiCurrentNode());
    if (current == nodes.end()) return ids;

    const uint32_t mapId = current->second.mapId;
    ids.reserve(nodes.size());
    for (const auto& [id, node] : nodes) {
        if (node.mapId == mapId) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

/// The node at a one-based position on the flight map, or 0 for no such node.
static uint32_t taxiNodeAt(game::GameHandler* gh, int index) {
    const auto ids = taxiNodeOrder(gh);
    if (index < 1 || index > static_cast<int>(ids.size())) return 0;
    return ids[static_cast<size_t>(index - 1)];
}

/// Where a taxi node sits on the flight map, as a fraction of its width and
/// height.
///
/// The flight map is the continent's map, so this is the same projection the
/// world map does — the one place it lives, in rendering/world_map, reached
/// through the continent rectangle the game handler now reads.
///
/// The order of the two conversions is the whole difficulty. TaxiNodes.dbc
/// holds positions in server order, and feeding those straight to
/// canonicalToRender transposes every marker, which is exactly how the flight
/// map's node markers came out mirrored once before. Server to canonical
/// first, then canonical to render, then project.
static bool taxiNodeMapPos(game::GameHandler* gh, uint32_t nodeId,
                           float& outU, float& outV) {
    if (!gh) return false;
    const auto& nodes = gh->getTaxiNodes();
    const auto it = nodes.find(nodeId);
    if (it == nodes.end()) return false;

    const auto& bounds = gh->getContinentBounds(it->second.mapId);
    if (!bounds.valid) return false;

    const glm::vec3 canonical = core::coords::serverToCanonical(
        glm::vec3(it->second.x, it->second.y, it->second.z));
    const glm::vec3 render = core::coords::canonicalToRender(canonical);

    rendering::world_map::ZoneBounds zb;
    zb.locLeft = bounds.left;   zb.locRight  = bounds.right;
    zb.locTop  = bounds.top;    zb.locBottom = bounds.bottom;
    const glm::vec2 uv =
        rendering::world_map::renderPosToMapUV(render, zb, /*isContinent=*/true);
    outU = uv.x;
    outV = uv.y;
    return true;
}

/// One end of one leg of a flight, as a fraction of the map.
///
/// `End` is 0 for the leg's start and 1 for its finish; `Horizontal` picks x
/// over y. Four names, one body — they differ only in which number they read.
template <int End, bool Horizontal>
static int lua_TaxiLegCoord(lua_State* L) {
    auto* gh = getGameHandler(L);
    const uint32_t dest = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
    const int hop = static_cast<int>(luaL_optnumber(L, 2, 0));
    if (dest == 0 || hop < 1 || !gh) { lua_pushnumber(L, 0); return 1; }

    const auto route = gh->getTaxiRouteTo(dest);
    // Hop N runs from route[N-1] to route[N], so the last hop is size-1.
    if (hop >= static_cast<int>(route.size())) { lua_pushnumber(L, 0); return 1; }

    float u = 0, v = 0;
    if (!taxiNodeMapPos(gh, route[static_cast<size_t>(hop - 1 + End)], u, v)) {
        lua_pushnumber(L, 0);
        return 1;
    }
    lua_pushnumber(L, Horizontal ? u : v);
    return 1;
}

/// The hops of the route the flight map is drawing lines for.
///
/// TaxiNodeSetCurrent names the node the player is hovering, and the frame then
/// asks for each leg of the journey to it in turn. Kept here rather than asked
/// for again per leg because getTaxiRouteTo searches, and the frame asks for the
/// same route once per hop and again for every line it draws.
static std::vector<uint32_t>& taxiRouteShown() {
    static std::vector<uint32_t> route;
    return route;
}

void registerSystemLuaAPI(lua_State* L) {
    static const struct { const char* name; lua_CFunction func; } api[] = {
                {"Screenshot",               lua_Screenshot},
                {"HasLFGRestrictions",       lua_HasLFGRestrictions},
                {"GetLFGProposal",           lua_GetLFGProposal},
                {"GetLFGInfoServer",         lua_GetLFGInfoServer},
                {"GetLFGRoleUpdate",         lua_GetLFGRoleUpdate},
                {"IsListedInLFR",            lua_IsListedInLFR},
                {"IsPartyLFG",               lua_IsPartyLFG},
                {"GetLFGDeserterExpiration", lua_GetLFGDeserterExpiration},
                {"GetLFGRandomCooldownExpiration", lua_GetLFGRandomCooldownExpiration},
                {"RefreshLFGList",           lua_RefreshLFGList},
                {"GetTrackedAchievements",   lua_GetTrackedAchievements},
                {"RequestRaidInfo",          lua_RequestRaidInfo},
                {"IsPVPTimerRunning",        lua_IsPVPTimerRunning},
                {"GetPVPTimer",              lua_GetPVPTimer},
                {"GetCurrentArenaSeason",    lua_GetCurrentArenaSeason},
                {"GetPVPRankProgress",       lua_GetPVPRankProgress},
                {"ArenaTeamRoster",              lua_ArenaTeamRoster},
                {"GetArenaTeamRosterInfo",       lua_GetArenaTeamRosterInfo},
                {"GetArenaTeamRosterSelection",  lua_GetArenaTeamRosterSelection},
                {"SetArenaTeamRosterSelection",  lua_SetArenaTeamRosterSelection},
                {"CloseArenaTeamRoster",         lua_CloseArenaTeamRoster},
                {"IsArenaTeamCaptain",           lua_IsArenaTeamCaptain},
                {"CloseBattlefield",             lua_CloseBattlefield},
                {"CanExitVehicle",               lua_CanExitVehicle},
                {"IsVehicleAimAngleAdjustable",  lua_IsVehicleAimAngleAdjustable},
                {"HasKey",                       lua_HasKey},
                {"GetArenaTeam",             lua_GetArenaTeam},
                {"GetRandomBGHonorCurrencyBonuses",  lua_BattlegroundHonorBonusesNone},
                {"GetHolidayBGHonorCurrencyBonuses", lua_BattlegroundHonorBonusesNone},
                {"GetBattlefieldInstanceInfo",       lua_GetBattlefieldInstanceInfo},
                {"IsInLFGDungeon",           lua_IsInLFGDungeon},
                {"LFGTeleport",              lua_LFGTeleport},
                {"IsBattlefieldArena",       lua_IsBattlefieldArena},
                {"IsActiveBattlefieldArena", lua_IsActiveBattlefieldArena},
                {"CanHearthAndResurrectFromArea", lua_CanHearthAndResurrectFromArea},
                {"GetWorldPVPQueueStatus",   lua_GetWorldPVPQueueStatus},
                {"LeaveBattlefield",         lua_LeaveBattlefield},
                {"CreateWorldMapArrowFrame",   lua_CreateWorldMapArrowFrame},
                {"ShowWorldMapArrowFrame",     lua_ShowWorldMapArrowFrame},
                {"PositionWorldMapArrowFrame", lua_PositionWorldMapArrowFrame},
                {"InitWorldMapPing",           lua_InitWorldMapPing},
                {"CreateMiniWorldMapArrowFrame",   lua_CreateMiniWorldMapArrowFrame},
                {"PositionMiniWorldMapArrowFrame", lua_PositionMiniWorldMapArrowFrame},
                {"ShowMiniWorldMapArrowFrame",     lua_ShowMiniWorldMapArrowFrame},
                {"GetBattlefieldMapIconScale",     lua_GetBattlefieldMapIconScale},
                {"PlayerIsPVPInactive",            lua_PlayerIsPVPInactive},
                {"CombatTextSetActiveUnit",        lua_CombatTextSetActiveUnit},
                {"CombatLogGetNumEntries",         lua_CombatLogGetNumEntries},
                {"CombatLogGetCurrentEntry",       lua_CombatLogGetCurrentEntry},
                {"CombatLogAdvanceEntry",          lua_CombatLogAdvanceEntry},
                {"CombatLogSetCurrentEntry",       lua_CombatLogSetCurrentEntry},
                {"CombatLogAddFilter",             lua_CombatLogAddFilter},
                {"CombatLogResetFilter",           lua_CombatLogResetFilter},
                {"GetBattlefieldInfo",       lua_GetBattlefieldInfo},
                {"SetSelectedBattlefield",   lua_SetSelectedBattlefield},
                {"GetSelectedBattlefield",   lua_GetSelectedBattlefield},
                {"JoinBattlefield",          lua_JoinBattlefield},
                {"GetLFGCompletionReward",     lua_GetLFGCompletionReward},
                {"GetLFGCompletionRewardItem", lua_GetLFGCompletionRewardItem},
                {"RunMacroText",             lua_RunMacroText},
                {"RunMacro",                 lua_RunMacro},
                {"TriggerTutorial",          lua_TriggerTutorial},
                {"Quit",                     lua_Quit},
                {"ReloadUI",                 lua_ReloadUI},
                {"GetGamma",                 lua_GetGamma},
                {"SetGamma",                 lua_SetGamma},
                {"GetVideoCaps",             lua_GetVideoCaps},
                {"GetCVarMin",               lua_GetCVarMin},
                {"GetCVarMax",               lua_GetCVarMax},
                {"IsVoiceChatAllowedByServer", lua_IsVoiceChatAllowedByServer},
                {"VoiceChat_IsRecordingLoopbackSound", lua_VoiceChat_IsRecordingLoopbackSound},
                {"VoiceChat_IsPlayingLoopbackSound",   lua_VoiceChat_IsPlayingLoopbackSound},
                {"VoiceChat_GetCurrentMicrophoneSignalLevel", lua_VoiceChat_GetCurrentMicrophoneSignalLevel},
                {"GetVoiceSessionInfo",      lua_GetVoiceSessionInfo},
                {"GetNumVoiceSessionMembersBySessionID", lua_GetNumVoiceSessionMembersBySessionID},
                {"CanShowAchievementUI",     lua_CanShowAchievementUI},
                {"IsXPUserDisabled",         lua_IsXPUserDisabled},
                {"GetAddOnMemoryUsage",      lua_GetAddOnMemoryUsage},
                {"UpdateAddOnMemoryUsage",   lua_ReturnNothing},
                {"RunScript",                lua_RunScript},
                {"IsMouseButtonDown",        lua_IsMouseButtonDown},
                {"GetCVarDefault",           lua_GetCVar},
                {"IsAddOnLoaded",            lua_IsAddOnLoaded},
                {"LoadAddOn",                lua_LoadAddOn},
                {"UIParentLoadAddOn",        lua_LoadAddOn},
                {"HasCompletedAnyAchievement", lua_ReturnFalse},
                {"TurnInGuildCharter",       lua_ReturnNothing},
                // Nothing is being driven, so aiming it does nothing
                // and there is nothing to climb out of.
                {"VehicleAimUpStart",        lua_ReturnNothing},
                {"VehicleAimUpStop",         lua_ReturnNothing},
                {"VehicleAimDownStart",      lua_ReturnNothing},
                {"VehicleAimDownStop",       lua_ReturnNothing},
                {"VehicleExit",              lua_ReturnNothing},
                {"VehicleAimGetNormAngle",   lua_ReturnZero},
                {"VehicleAimGetNormPower",   lua_ReturnZero},
                {"GetMapInfo",               lua_GetMapInfo},
                {"GetExpansionLevel",        lua_GetExpansionLevel},
                {"GetDungeonDifficulty",     lua_ReturnOne},
                {"GetRaidDifficulty",        lua_ReturnOne},
                {"GetChatTypeIndex",         lua_ReturnOne},
                {"GetDefaultLanguage",       lua_GetDefaultLanguage},
                {"GetWeaponEnchantInfo",     lua_GetWeaponEnchantInfo},
                // The PvP reclaim timer, which this client already tracks.
                // Stubbed to zero this read as "reclaim now" and made the
                // delay text on FrameXML's corpse prompt always empty.
                {"GetCorpseRecoveryDelay", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getCorpseReclaimDelaySec() : 0.0);
            return 1;
        }},
                {"GetAdjustedSkillPoints",   lua_ReturnZero},
                {"GetPartyLeaderIndex",      lua_ReturnZero},
                {"GetNumArenaOpponents",     lua_ReturnZero},
                {"GetCurrentMultisampleFormat", lua_ReturnOne},
                // These hand back a list, not a value: the caller walks it with
                // select("#", ...) and reads it in groups. One number makes the
                // loop run once against nils, which is worse than an empty
                // list — for anything returning a list, nothing is the right
                // way to say there is none.
                {"GetMultisampleFormats",    lua_ReturnNothing},
                {"GetRefreshRates",          lua_ReturnNothing},
                // ---- The options panels behind the game menu ----
                //
                // Every reader here was bound and none of the writers were, so
                // opening Video or Sound and touching anything raised. The
                // panels sit one click inside the game menu, which is a handed-
                // over element, and none of their files was scanned by the
                // readiness report until the question "which FrameXML files
                // does no element reach" was asked.
                //
                // The answers are shaped by what the readers already say.
                // GetScreenResolutions offers exactly one mode -- the window
                // this client is already in -- and GetMultisampleFormats offers
                // none, so choosing from either list can only ever re-choose
                // what is set. These accept the call and change nothing, which
                // is the truth rather than a stub.
                {"SetScreenResolution",      lua_ReturnNothing},
                {"SetMultisampleFormat",     lua_ReturnNothing},
                // Nothing above is settable, so there is nothing to put back.
                {"RestoreVideoResolutionDefaults", lua_ReturnNothing},
                {"RestoreVideoEffectsDefaults",    lua_ReturnNothing},
                {"RestoreVideoStereoDefaults",     lua_ReturnNothing},
                // Applying video settings restarts the graphics device on the
                // real client. This one applies what it can as it goes and has
                // no device to tear down.
                {"RestartGx",                lua_ReturnNothing},
                {"Sound_GameSystem_RestartSoundSystem", lua_ReturnNothing},
                // A separate render scale for the player model, which this
                // client does not have. False disables the control rather than
                // leaving it offering something that would do nothing.
                {"IsPlayerResolutionAvailable", lua_ReturnFalse},
                // Which extra action bars are shown. FrameXML draws the bars
                // and MultiActionBar_Update decides from the SHOW_MULTI_ACTIONBAR_*
                // globals, which are CVars and persist on their own — so the
                // C-side toggle this mirrors has nothing left to do here.
                {"SetActionBarToggles",      lua_ReturnNothing},
                // Voice chat, which this client has none of. The enumerations
                // hand back lists, so they answer with nothing rather than with
                // a zero; IsVoiceChatEnabled already answers false beside them.
                {"VoiceIsDisabledByClient",  lua_ReturnTrue},
                {"VoiceEnumerateCaptureDevices", lua_ReturnNothing},
                {"VoiceEnumerateOutputDevices",  lua_ReturnNothing},
                {"VoiceSelectCaptureDevice", lua_ReturnNothing},
                {"VoiceSelectOutputDevice",  lua_ReturnNothing},
                {"VoiceChat_StopPlayingLoopbackSound",   lua_ReturnNothing},
                {"VoiceChat_StopRecordingLoopbackSound", lua_ReturnNothing},
                {"GetCompanionInfo",         lua_ReturnNil},
                // Counts, and the count is the whole point: each of these is
                // read straight into `for i = 1, X()`, where a nil limit is
                // not an empty loop but an error — "'for' limit must be a
                // number" — that takes down the handler around it. Unbound,
                // the fallback answered nil and the character sheet's title
                // list, the companion tab and the token frame each raised
                // rather than showing nothing.
                //
                // Zero is the honest answer, not a placeholder: no known-title
                // bitmask, companion list or currency list is tracked here, so
                // there is genuinely nothing to count. They stop being zero
                // when something starts reading that data, and the frames
                // above will fill themselves in when it does.
                // GetNumTitles() — how many title *bits* there are to ask
                // about, not how many are owned. paperdollframe.lua walks
                // 1..GetNumTitles() calling IsTitleKnown on each, so this is
                // the size of the space: KNOWN_TITLES_SIZE * 64 in
                // AzerothCore's Player.h, three uint64 fields.
                {"GetNumTitles", [](lua_State* L) -> int {
            lua_pushnumber(L, 192); return 1;
        }},
                {"GetNumCompanions",         lua_ReturnZero},
                // The knowledge base is the server's FAQ, and there is no
                // server here answering for it. Its category dropdown is
                // reached from the "?" micro button beside the action bar, so
                // the raise was one click away rather than in a corner.
                {"KBSetup_GetCategoryCount",    lua_ReturnZero},
                {"KBSetup_GetSubCategoryCount", lua_ReturnZero},
                // The other thirteen, which the note above should have covered
                // and did not: two counts were bound and the rest of the same
                // window was not, so the "?" button still raised — on
                // KBSetup_BeginLoading, which KnowledgeBaseFrame_OnShow calls
                // before anything else.
                //
                // Every caller here guards, and guards on exactly what an
                // absent knowledge base should say. KnowledgeBaseFrame_Search
                // returns early unless KBSetup_IsLoaded, the MOTD and notice
                // are both `if ( x )`, and the article lists are walked by the
                // counts. Never loaded, nothing in it.
                {"KBSetup_IsLoaded",            lua_ReturnFalse},
                {"KBSetup_BeginLoading",        lua_ReturnNothing},
                {"KBQuery_BeginLoading",        lua_ReturnNothing},
                {"KBArticle_BeginLoading",      lua_ReturnNothing},
                {"KBSetup_GetArticleHeaderCount", lua_ReturnZero},
                {"KBSetup_GetTotalArticleCount",  lua_ReturnZero},
                {"KBQuery_GetArticleHeaderCount", lua_ReturnZero},
                {"KBQuery_GetTotalArticleCount",  lua_ReturnZero},
                {"KBSetup_GetCategoryData",     lua_ReturnNil},
                {"KBSetup_GetSubCategoryData",  lua_ReturnNil},
                {"KBArticle_GetData",           lua_ReturnNil},
                {"KBSystem_GetMOTD",            lua_ReturnNil},
                {"KBSystem_GetServerNotice",    lua_ReturnNil},
                // Three more counts read straight into a comparison or a
                // format, with the same result: the socketing window walks
                // `i <= numSockets`, the PvP frame formats the season number
                // into its off-season line, and the achievement comparison
                // concatenates its total. None of the three has data behind it
                // here — no socketing, no arena seasons, and no way to read
                // another player's achievements — so zero is what is true.
                // Three battlefield timers, all read as `X()/1000`. This
                // client has partial battlefield support — status, score,
                // winner and positions are all bound — so these are gaps in a
                // system a player reaches rather than one that does not exist,
                // and each raises the moment a battleground is queued for or
                // entered. Zero reads correctly at every call site: no time in
                // queue, no shutdown pending, no elapsed run time.
                // Both timers are read as X()/1000, so milliseconds. The
                // queue slot carries each in seconds and this answered zero for
                // both — a queue that always read as just-joined with no
                // estimate, which is the whole content of that window.
                {"GetBattlefieldTimeWaited", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_optnumber(L, 1, 1));
            double ms = 0.0;
            if (gh && index >= 1 && index <= 3)
                ms = gh->getBgQueues()[static_cast<size_t>(index - 1)].timeInQueueSec * 1000.0;
            lua_pushnumber(L, ms);
            return 1;
        }},
                {"GetBattlefieldEstimatedWaitTime", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_optnumber(L, 1, 1));
            double ms = 0.0;
            if (gh && index >= 1 && index <= 3)
                ms = gh->getBgQueues()[static_cast<size_t>(index - 1)].avgWaitTimeSec * 1000.0;
            lua_pushnumber(L, ms);
            return 1;
        }},
                // CanJoinBattlefieldAsGroup() — whether the queue button offers
                // to take the party in.
                //
                // The server decides for itself when the request arrives; this
                // only says whether it is worth offering, which is a party the
                // player leads. Solo, there is no group to bring.
                {"CanJoinBattlefieldAsGroup", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushboolean(L, 0); return 1; }
            const auto& pd = gh->getPartyData();
            const bool leadsAParty = !pd.isEmpty() && pd.leaderGuid == gh->getPlayerGuid();
            lua_pushboolean(L, leadsAParty ? 1 : 0);
            return 1;
        }},
                {"GetBattlefieldInstanceExpiration", lua_ReturnZero},
                {"GetBattlefieldInstanceRunTime",    lua_ReturnZero},
                // The aspect ratio the Mac options panel builds its recording
                // resolution from: `"640x"..floor(640*ratio)`. Answering the
                // window's own ratio is both truthful and what makes that read
                // 640x360 on a 16:9 display rather than raising.
                // The rest of the movie recorder, which this client does not
                // have. Not stubs standing in for something — "we are not
                // recording" is simply true, and the two toggles have nothing
                // to toggle.
                //
                // Bound because they hang off keybindings. Those are declared
                // platform="mac" and so should never be reachable here, but a
                // binding is dispatched by name at the moment a key is pressed
                // and answering nothing there raises in the key handler, which
                // is a bad place to find out the filter was not applied.
                {"MovieRecording_IsRecording",   lua_ReturnFalse},
                {"MovieRecording_IsCompressing", lua_ReturnFalse},
                {"MovieRecording_Toggle",        lua_ReturnNothing},
                {"MovieRecording_ToggleGUI",     lua_ReturnNothing},
                {"MovieRecording_Cancel",        lua_ReturnNothing},
                {"MovieRecording_GetAspectRatio", [](lua_State* L) -> int {
            auto* svc = getLuaServices(L);
            auto* win = svc ? svc->window : nullptr;
            const float w = win ? static_cast<float>(win->getWidth())  : 1920.0f;
            const float h = win ? static_cast<float>(win->getHeight()) : 1080.0f;
            lua_pushnumber(L, (w > 0.0f) ? (h / w) : 0.5625);
            return 1;
        }},
                {"GetNumSockets",               lua_ReturnZero},
                {"GetPreviousArenaSeason",      lua_ReturnZero},
                {"GetComparisonCategoryNumAchievements", lua_ReturnZero},
                {"GetMultiCastTotemSpells",  lua_ReturnNil},
                {"GetPossessInfo",           lua_ReturnNil},
                {"GetVoiceStatus",           lua_ReturnFalse},
                {"GetMuteStatus",            lua_ReturnFalse},
                {"GetActiveVoiceChannel",    lua_ReturnNil},
                {"GetVoiceCurrentSessionID", lua_ReturnNil},
                {"GetOptOutOfLoot",          lua_ReturnFalse},
                {"GetPartyMember",           lua_ReturnFalse},
                // GetZonePVPInfo() → pvpType, isSubZonePvP, factionName
                //
                // minimap.lua unpacks three and answered nil for all of them,
                // so the zone name on the minimap was always the default
                // colour and its tooltip never said whose territory it was.
                // Middle value stays nil: it is for a sub-zone that differs
                // from its parent, which this reads at zone granularity.
                {"GetZonePVPInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnil(L); return 1; }
            const auto [type, faction] = gh->getZonePvpInfo(gh->getWorldStateZoneId());
            if (type.empty()) { lua_pushnil(L); return 1; }
            lua_pushstring(L, type.c_str());
            lua_pushnil(L);
            if (faction.empty()) lua_pushnil(L); else lua_pushstring(L, faction.c_str());
            return 3;
        }},
                {"GetMouseButtonClicked",    lua_ReturnNil},
                {"GetChatWindowSavedPosition",   lua_GetChatWindowSavedPosition},
                {"GetChatWindowSavedDimensions", lua_GetChatWindowSavedDimensions},
                {"SetChatWindowSavedPosition",   lua_SetChatWindowSavedPosition},
                {"SetChatWindowSavedDimensions", lua_SetChatWindowSavedDimensions},
                {"SetChatWindowSize",            lua_SetChatWindowSize},
                {"SetChatWindowColor",           lua_SetChatWindowColor},
                {"SetChatWindowAlpha",           lua_SetChatWindowAlpha},
                {"SetChatWindowShown",           lua_SetChatWindowShown},
                {"SetChatWindowLocked",          lua_SetChatWindowLocked},
                {"SetChatWindowDocked",          lua_SetChatWindowDocked},
                {"SetChatWindowUninteractable",  lua_SetChatWindowUninteractable},
                {"ResetChatWindows",             lua_ResetChatWindows},
                {"SetChatColorNameByClass",      lua_SetChatColorNameByClass},
                {"GetChatColorNameByClass",      lua_GetChatColorNameByClass},
                {"GetExistingLocales",       lua_ReturnNil},
                {"GetGuildRosterSelection",  lua_ReturnZero},
                // Read from the real keyboard: a shift-click means something
                // different from a click, and FrameXML asks on every press.
                {"IsShiftKeyDown",           lua_IsShiftKeyDown},
                {"IsLeftShiftKeyDown",       lua_IsShiftKeyDown},
                {"IsRightShiftKeyDown",      lua_IsShiftKeyDown},
                {"IsControlKeyDown",         lua_IsControlKeyDown},
                {"IsLeftControlKeyDown",     lua_IsControlKeyDown},
                {"IsRightControlKeyDown",    lua_IsControlKeyDown},
                {"IsAltKeyDown",             lua_IsAltKeyDown},
                {"IsLeftAltKeyDown",         lua_IsAltKeyDown},
                {"IsRightAltKeyDown",        lua_IsAltKeyDown},
                {"IsModifierKeyDown",        lua_IsModifierKeyDown},
                // The four predicates actionbutton.lua asks about a slot.
                //
                // All answered false, and the pair below the first is why a
                // stack of potions on the bar showed no number: the count is
                // drawn only inside `if ( IsConsumableAction(action) or
                // IsStackableAction(action) )`, and GetActionCount underneath
                // it has been counting across every bag the whole time.
                {"IsAttackAction", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int slot = static_cast<int>(luaL_optnumber(L, 1, 0)) - 1;
            if (!gh || slot < 0) { lua_pushboolean(L, 0); return 1; }
            const auto& bar = gh->getActionBar();
            // 6603 is Auto Attack, the one action that flashes the button red
            // for as long as the swing keeps going.
            constexpr uint32_t kAutoAttack = 6603;
            lua_pushboolean(L, slot < static_cast<int>(bar.size()) &&
                               bar[slot].type == game::ActionBarSlot::SPELL &&
                               bar[slot].id == kAutoAttack);
            return 1;
        }},
                {"IsConsumableAction", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int slot = static_cast<int>(luaL_optnumber(L, 1, 0)) - 1;
            if (!gh || slot < 0) { lua_pushboolean(L, 0); return 1; }
            const auto& bar = gh->getActionBar();
            bool consumable = false;
            if (slot < static_cast<int>(bar.size()) &&
                bar[slot].type == game::ActionBarSlot::ITEM) {
                const auto* info = gh->getItemInfo(bar[slot].id);
                consumable = info && info->valid && info->itemClass == 0;  // Consumable
            }
            lua_pushboolean(L, consumable);
            return 1;
        }},
                {"IsEquippedAction", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int slot = static_cast<int>(luaL_optnumber(L, 1, 0)) - 1;
            if (!gh || slot < 0) { lua_pushboolean(L, 0); return 1; }
            const auto& bar = gh->getActionBar();
            bool worn = false;
            if (slot < static_cast<int>(bar.size()) &&
                bar[slot].type == game::ActionBarSlot::ITEM) {
                const auto& inv = gh->getInventory();
                for (int e = 0; e < game::Inventory::NUM_EQUIP_SLOTS && !worn; ++e) {
                    const auto& s = inv.getEquipSlot(static_cast<game::EquipSlot>(e));
                    worn = !s.empty() && s.item.itemId == bar[slot].id;
                }
            }
            lua_pushboolean(L, worn);
            return 1;
        }},
                {"IsStackableAction", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int slot = static_cast<int>(luaL_optnumber(L, 1, 0)) - 1;
            if (!gh || slot < 0) { lua_pushboolean(L, 0); return 1; }
            const auto& bar = gh->getActionBar();
            bool stackable = false;
            if (slot < static_cast<int>(bar.size()) &&
                bar[slot].type == game::ActionBarSlot::ITEM) {
                const auto* info = gh->getItemInfo(bar[slot].id);
                stackable = info && info->valid && info->maxStack > 1;
            }
            lua_pushboolean(L, stackable);
            return 1;
        }},
                {"IsFlyableArea",            lua_ReturnFalse},
                // The renderer knows whether the camera is inside a WMO, and
                // the macro conditionals [indoors] / [outdoors] have read it
                // all along. These two answered a flat false and a flat true.
                {"IsIndoors", [](lua_State* L) -> int {
            auto* svc = getLuaServices(L);
            lua_pushboolean(L, svc && svc->isPlayerIndoors && svc->isPlayerIndoors());
            return 1;
        }},
                {"IsOutdoors", [](lua_State* L) -> int {
            auto* svc = getLuaServices(L);
            lua_pushboolean(L, !(svc && svc->isPlayerIndoors && svc->isPlayerIndoors()));
            return 1;
        }},
                {"IsHarmfulItem",            lua_ReturnFalse},
                {"IsHelpfulItem",            lua_ReturnFalse},
                {"IsHarmfulSpell",           lua_ReturnFalse},
                {"IsHelpfulSpell",           lua_ReturnFalse},
                {"IsPossessBarVisible",      lua_ReturnFalse},
                // IsRaidOfficer() — whether this player is an assistant.
                //
                // The same question UnitIsRaidOfficer already answers for
                // anyone else, and off the same bit: MEMBER_FLAG_ASSISTANT is
                // 0x01 in AzerothCore's Group.h, and the party members carry
                // their flags. Only the player's own answer was hardcoded no.
                //
                // It gates the assistant-only entries on the unit menus and,
                // with IsPartyLeader beside it, whether the chat frame offers
                // to send a raid warning at all.
                {"IsRaidOfficer", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushboolean(L, 0); return 1; }
            const uint64_t self = gh->getPlayerGuid();
            constexpr uint8_t kMemberFlagAssistant = 0x01;
            for (const auto& mem : gh->getPartyData().members) {
                if (mem.guid == self) {
                    lua_pushboolean(L, (mem.flags & kMemberFlagAssistant) ? 1 : 0);
                    return 1;
                }
            }
            lua_pushboolean(L, 0);
            return 1;
        }},
                {"IsReferAFriendLinked",     lua_ReturnFalse},
                {"IsStereoVideoAvailable",   lua_ReturnFalse},
                {"IsVoiceChatEnabled",       lua_ReturnFalse},
                {"IsZoomOutAvailable",       lua_ReturnFalse},
                {"HasDebugZoneMap",          lua_ReturnFalse},
                {"CanQueueForWintergrasp",   lua_ReturnFalse},
                {"CancelSkillUps",           lua_ReturnNothing},
                {"ConvertToRaid",            lua_ReturnNothing},
                {"FillLocalizedClassList",   lua_ReturnNothing},
                {"QueryGuildEventLog",       lua_ReturnNothing},
                {"RegisterForSave",          lua_ReturnNothing},
                {"RegisterStaticConstants",  lua_ReturnNothing},
                {"SetChatWindowName",        lua_SetChatWindowName},
                {"SetGuildRosterSelection",  lua_ReturnNothing},
                {"SetupFullscreenScale",     lua_ReturnNothing},
                {"DropCursorMoney",          lua_ReturnNothing},
                // AchievementMicroButton_Update() — called by the achievement
                // addon and defined nowhere. mainmenubarmicrobuttons.lua has
                // AchievementMicroButton_OnEvent but not this, so it is a hole
                // in this FrameXML rather than a binding this client owes. A
                // no-op, because what it would do is show a micro button that
                // is hidden, and leaving it hidden is the honest outcome.
                {"AchievementMicroButton_Update", [](lua_State* L) -> int {
            (void)L; return 0; }},
                {"BNFeaturesEnabled",        lua_ReturnFalse},
                {"BNFeaturesEnabledAndConnected", lua_ReturnFalse},
                {"BNGetMaxPlayersInConversation", lua_ReturnZero},
                {"GetSummonFriendCooldown",  lua_ReturnNoCooldown},
                {"GetScreenResolutions",     lua_GetScreenResolutions},
                {"GetCurrentResolution",     lua_GetCurrentResolution},
                // Counts a loop bounds itself with. FrameXML writes
                // "for i = 0, num-1" straight after asking, so nothing is not
                // an answer — it is arithmetic on nil and the file is lost.
                // The channel list panel walks these two, and both answered
                // "there are none" while the client knew exactly which
                // channels the player had joined — GetChannelList reports them
                // from the same vector. A stub saying empty is how a working
                // panel shows nothing.
                {"GetNumDisplayChannels", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? static_cast<double>(gh->getJoinedChannels().size()) : 0.0);
            return 1;
        }},
                {"GetNumMapOverlays",        lua_ReturnZero},
                {"GetNumMapDebugObjects",    lua_ReturnZero},
                {"GetNumBattlefieldPositions", lua_ReturnZero},
                {"GetBattlefieldPosition",   lua_GetBattlefieldPosition},
                {"GetCorpseMapPosition",     lua_GetBattlefieldPosition},
                {"GetDeathReleasePosition",  lua_GetBattlefieldPosition},
                {"GetNumBattlefieldVehicles", lua_ReturnZero},
                {"GetBattlefieldVehicleInfo", lua_ReturnNil},
                {"GetChatWindowInfo",        lua_GetChatWindowInfo},
                {"GetNumBattlefieldFlagPositions", lua_ReturnZero},
                {"GetBattlefieldFlagPosition",     lua_GetBattlefieldPosition},
                {"GuildControlGetNumRanks",  lua_ReturnZero},
                // Time left on a loot roll that is not running, which
                // GroupLootFrame compares against a bar range at once.
                {"GetNumDungeonMapLevels",   lua_ReturnZero},
                // Bar offsets, added to a page number the line they
                // are read on. No bonus or multi-cast bar is showing,
                // and that is zero rather than nothing.
                {"GetMultiCastBarOffset",    lua_ReturnZero},
                {"GetBonusBarOffset",        lua_ReturnZero},
                {"GetNumBattlegroundTypes",  lua_GetNumBattlegroundTypes},
                {"GetBattlegroundInfo",      lua_GetBattlegroundInfo},
                {"RequestBattlegroundInstanceInfo", lua_RequestBattlegroundInstanceInfo},
                {"GetCurrentMapDungeonLevel", lua_ReturnZero},
                {"Sound_GameSystem_GetNumOutputDrivers", lua_ReturnZero},
                {"Sound_ChatSystem_GetNumInputDrivers",  lua_ReturnZero},
                {"Sound_ChatSystem_GetNumOutputDrivers", lua_ReturnZero},
                {"Sound_ChatSystem_GetInputDriverNameByIndex",  lua_ReturnNil},
                {"Sound_ChatSystem_GetOutputDriverNameByIndex", lua_ReturnNil},
                // Nothing selected, which is a number rather than
                // nothing: SkillFrame passes the result straight to
                // GetSkillLineInfo as an index.
                // Which row the skill list has selected. The client has no
                // opinion about it — it is what the player last clicked — so
                // it is held here, the way the friends and ignore lists are.
                //
                // Answering a constant zero meant no row ever matched, because
                // the list is one-based: SkillFrame_SetStatusBar compares each
                // row against this to decide which border to light, so nothing
                // ever looked selected however many times it was clicked.
                {"GetSelectedSkill", [](lua_State* L) -> int {
            lua_pushnumber(L, selectedSkill()); return 1; }},
                {"DungeonUsesTerrainMap",    lua_ReturnFalse},
                // GetChannelDisplayInfo(i) → name, header, collapsed,
                //   channelNumber, count, active, category, voiceEnabled,
                //   voiceActive
                //
                // header is FALSE, not "". An empty string is true in Lua, so
                // returning one marks every channel as a category header:
                // channelframe.lua does `if ( self.header )` and would draw
                // each row as a heading, then call ExpandChannelHeader on it.
                {"GetChannelDisplayInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh || index < 1) { lua_pushnil(L); return 1; }
            const auto& joined = gh->getJoinedChannels();
            if (index > static_cast<int>(joined.size())) { lua_pushnil(L); return 1; }
            lua_pushstring(L, joined[static_cast<size_t>(index) - 1].c_str());
            lua_pushboolean(L, 0);      // header — flat list, no categories
            lua_pushboolean(L, 0);      // collapsed
            lua_pushnumber(L, index);   // channelNumber
            lua_pushnumber(L, 0);       // count — no roster is tracked
            lua_pushboolean(L, 1);      // active
            lua_pushstring(L, "CHANNEL_CATEGORY_CUSTOM");
            lua_pushboolean(L, 0);      // voiceEnabled
            lua_pushboolean(L, 0);      // voiceActive
            return 9;
        }},
                {"IsThreatWarningEnabled",   lua_ReturnFalse},
                // IsAutoRepeatAction(slot) — the button flashes for as long as
                // an auto-repeat is running. There are exactly two in 3.3.5,
                // Auto Shot and the wand's Shoot, which is how IsAttackAction
                // beside it identifies auto-attack: by id rather than by an
                // attribute word this client does not cache.
                {"IsAutoRepeatAction", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int slot = static_cast<int>(luaL_optnumber(L, 1, 0)) - 1;
            bool repeating = false;
            if (gh && slot >= 0) {
                const auto& bar = gh->getActionBar();
                if (slot < static_cast<int>(bar.size()) &&
                    bar[slot].type == game::ActionBarSlot::SPELL) {
                    constexpr uint32_t kAutoShot = 75, kShoot = 5019;
                    repeating = bar[slot].id == kAutoShot || bar[slot].id == kShoot;
                }
            }
            lua_pushboolean(L, repeating);
            return 1;
        }},
                {"IsPetAttackAction",        lua_ReturnFalse},
                {"IsMacClient",              lua_ReturnFalse},
                // IsPartyLeader() — whether *this* player leads the group.
                //
                // The client has known this all along: the party data carries a
                // leader guid and PARTY_LEADER_CHANGED is fired when it moves.
                // Answering a flat false told FrameXML the player never leads,
                // which is what gates the leader-only entries on the unit
                // right-click menus and the loot-method controls.
                {"IsPartyLeader", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushboolean(L, 0); return 1; }
            const uint64_t leader = gh->getPartyData().leaderGuid;
            lua_pushboolean(L, leader != 0 && leader == gh->getPlayerGuid());
            return 1;
        }},
                {"UnitFactionGroup",         lua_UnitFactionGroup},
                // HasPetSpells() → numSpells, petToken
                //
                // Answering nil meant the pet tab was never set up, so a hunter
                // or a warlock with a pet out had no pet spell book at all —
                // SpellBookFrame_Update only calls SpellBookFrame_SetTabType
                // for it when this says there are spells.
                //
                // Both values or neither: the tab's label is built as
                // _G["PET_TYPE_"..token], which raises on a nil token. Only two
                // of those globals exist, DEMON and PET, and WoW picks the
                // first for warlocks and the second for everyone else.
                {"HasPetSpells", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnil(L); return 1; }
            const auto& spells = gh->getPetSpells();
            if (spells.empty()) { lua_pushnil(L); return 1; }
            lua_pushnumber(L, static_cast<lua_Number>(spells.size()));
            constexpr uint8_t kWarlock = 9;
            lua_pushstring(L, gh->getPlayerClass() == kWarlock ? "DEMON" : "PET");
            return 2;
        }},
                // The death knight rune bar, which this client has tracked
                // since it started parsing rune state and never answered for.
                // runeframe.lua is reached through the player frame, handed
                // over by default, so a death knight has been looking at six
                // runes drawn from a nil type and no cooldown at all.
                //
                // RuneType here is Blood, Unholy, Frost, Death from zero;
                // runeframe.lua numbers the same four from one.
                {"GetRuneType", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int id = static_cast<int>(luaL_optinteger(L, 1, 0));
            if (!gh || id < 1 || id > 6) { lua_pushnil(L); return 1; }
            const auto& runes = gh->getPlayerRunes();
            lua_pushinteger(L,
                static_cast<lua_Integer>(runes[static_cast<size_t>(id) - 1].type) + 1);
            return 1;
        }},
                // GetRuneCooldown(id) → start, duration, runeReady
                //
                // The server sends how far along a rune is rather than when it
                // started, so the start is worked back from the fraction. It
                // has to come off the same clock GetTime answers with, or the
                // sweep is drawn against a different origin than it was
                // measured on — CooldownFrame_SetTimer compares the two.
                {"GetRuneCooldown", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int id = static_cast<int>(luaL_optinteger(L, 1, 0));
            if (!gh || id < 1 || id > 6) return 0;
            const auto& rune = gh->getPlayerRunes()[static_cast<size_t>(id) - 1];
            if (rune.ready) {
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
                lua_pushboolean(L, 1);
                return 3;
            }
            constexpr double kRuneCooldownSec = 10.0;  // fixed in WotLK
            const double elapsed = kRuneCooldownSec *
                static_cast<double>(rune.readyFraction);
            lua_pushnumber(L, luaGetTimeNow() - elapsed);
            lua_pushnumber(L, kRuneCooldownSec);
            lua_pushboolean(L, 0);
            return 3;
        }},
                {"GetMasterLootCandidate",   lua_ReturnNil},
                // The selection is the panel's own state and nothing else
                // reads it, so it lives here and round-trips. Answering nil
                // for the getter meant the highlight never moved.
                {"GetSelectedDisplayChannel", [](lua_State* L) -> int {
            lua_pushnumber(L, selectedDisplayChannel());
            return 1;
        }},
                {"SetSelectedDisplayChannel", [](lua_State* L) -> int {
            selectedDisplayChannel() = static_cast<int>(luaL_optnumber(L, 1, 0));
            return 0;
        }},
                // No categories exist to open or close — see the header note
                // on GetChannelDisplayInfo — but the row click handler calls
                // one of these on whatever it was given.
                {"ExpandChannelHeader",      lua_ReturnNothing},
                {"CollapseChannelHeader",    lua_ReturnNothing},
                // Who is in a channel. The server sends a roster only on
                // request and this client never asks, so there is nobody to
                // report; the count above is zero for the same reason.
                {"GetChannelRosterInfo",     lua_ReturnNil},
                {"GetWintergraspWaitTime",   lua_ReturnNil},
                {"GetNumWorldStateUI",       lua_GetNumWorldStateUI},
                {"GetWorldStateUIInfo",      lua_GetWorldStateUIInfo},
                // Only reached from the world-PvP branch, which uiType 0 keeps
                // worldstateframe.lua out of. Bound so the branch is safe if a
                // later entry ever reports uiType 1.
                {"IsSubZonePVPPOI",          lua_ReturnFalse},
                {"GetNumVoiceSessions",      lua_ReturnZero},
                {"RequestBattlefieldPositions", lua_ReturnNothing},
                {"UpdateWorldMapArrowFrames",   lua_ReturnNothing},
                {"SetSelectedSkill", [](lua_State* L) -> int {
            selectedSkill() = static_cast<int>(luaL_optnumber(L, 1, 0)); return 0; }},
                {"Sound_GameSystem_GetOutputDriverNameByIndex", lua_ReturnNil},
                {"PlaySound",           lua_PlaySound},
                {"PlaySoundFile",       lua_PlaySoundFile},
                {"GetPlayerMapPosition", lua_GetPlayerMapPosition},
                {"GetPlayerFacing",     lua_GetPlayerFacing},
                {"GetCVar",             lua_GetCVar},
                {"GetCVarBool",         lua_GetCVarBool},
                {"SetCVar",             lua_SetCVar},
                {"GetLocale",         lua_GetLocale},
                {"GetBuildInfo",      lua_GetBuildInfo},
                {"GetCurrentMapAreaID", lua_GetCurrentMapAreaID},
                {"SetMapToCurrentZone", lua_SetMapToCurrentZone},
                {"GetCurrentMapContinent", lua_GetCurrentMapContinent},
                {"GetCurrentMapZone",   lua_GetCurrentMapZone},
                {"SetMapZoom",          lua_SetMapZoom},
                {"GetMapContinents",    lua_GetMapContinents},
                {"GetMapZones",         lua_GetMapZones},
                {"GetNumMapLandmarks",  lua_GetNumMapLandmarks},
                {"GetTrackingTexture",  lua_GetTrackingTexture},
                {"GetNumTrackingTypes", lua_GetNumTrackingTypes},
                {"GetTrackingInfo",     lua_GetTrackingInfo},
                {"SetTracking",         lua_SetTracking},
                {"GetZoneText",          lua_GetZoneText},
                {"GetRealZoneText",      lua_GetZoneText},
                {"GetSubZoneText",       lua_GetSubZoneText},
                {"GetMinimapZoneText",   lua_GetMinimapZoneText},
                {"GetGameTime",             lua_GetGameTime},
                {"GetServerTime",           lua_GetServerTime},
                {"CombatLog_Object_IsA", lua_CombatLog_Object_IsA},
                {"GetNumAddOns",      lua_GetNumAddOns},
                {"GetAddOnInfo",      lua_GetAddOnInfo},
                {"GetAddOnMetadata",  lua_GetAddOnMetadata},
                {"IsInInstance",         lua_IsInInstance},
                {"GetInstanceInfo",      lua_GetInstanceInfo},
                {"GetInstanceDifficulty", lua_GetInstanceDifficulty},
                {"strsplit",          lua_strsplit},
                {"strtrim",           lua_strtrim},
                {"strlenutf8",        lua_strlenutf8},
                {"wipe",              lua_wipe},
                {"date",              lua_wow_date},
                {"time",              lua_wow_time},
                {"GetTime",           lua_wow_gettime},
                {"IsConnectedToServer", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushboolean(L, gh && gh->isConnected() ? 1 : 0);
            return 1;
        }},
                {"GetRealmName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) {
                const auto* ac = gh->getActiveCharacter();
                lua_pushstring(L, ac ? "WoWee" : "Unknown");
            } else lua_pushstring(L, "Unknown");
            return 1;
        }},
                {"GetNormalizedRealmName", [](lua_State* L) -> int {
            lua_pushstring(L, "WoWee");
            return 1;
        }},
                // ShowHelm(show) and ShowCloak(show) are setters, and these
                // toggled regardless of what they were passed.
                //
                // interfaceoptionspanels.xml drives both from a checkbox:
                // `self:SetChecked(value); ShowHelm(value)`. Toggling on a set
                // inverts the answer whenever the state already matched — the
                // box would tick and the helm would go away — and the panel
                // re-applies its value on every open, so it flipped again each
                // time the options were shown.
                //
                // The client only has a toggle, so this toggles only when that
                // lands on what was asked for. Same shape as SetPVP.
                {"ShowHelm", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const bool want = lua_isnumber(L, 1) ? (lua_tonumber(L, 1) != 0)
                                                 : (lua_toboolean(L, 1) != 0);
            if (want != gh->isHelmVisible()) gh->toggleHelm();
            return 0;
        }},
                {"ShowCloak", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const bool want = lua_isnumber(L, 1) ? (lua_tonumber(L, 1) != 0)
                                                 : (lua_toboolean(L, 1) != 0);
            if (want != gh->isCloakVisible()) gh->toggleCloak();
            return 0;
        }},
                {"TogglePVP", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->togglePvp();
            return 0;
        }},
                {"Minimap_Ping", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            float x = static_cast<float>(luaL_optnumber(L, 1, 0));
            float y = static_cast<float>(luaL_optnumber(L, 2, 0));
            if (gh) gh->sendMinimapPing(x, y);
            return 0;
        }},
                {"RequestTimePlayed", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->requestPlayedTime();
            return 0;
        }},
                {"Logout", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->requestLogout();
            return 0;
        }},
                {"CancelLogout", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->cancelLogout();
            return 0;
        }},
                {"NumTaxiNodes", [](lua_State* L) -> int {
            lua_pushnumber(L, static_cast<double>(taxiNodeOrder(getGameHandler(L)).size()));
            return 1;
        }},
                {"TaxiNodeName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
            if (id == 0) { lua_pushstring(L, ""); return 1; }
            const auto& nodes = gh->getTaxiNodes();
            const auto it = nodes.find(id);
            lua_pushstring(L, it != nodes.end() ? it->second.name.c_str() : "");
            return 1;
        }},
                // TaxiNodeGetType(index) → "CURRENT" | "REACHABLE" | "DISTANT" | "NONE"
                //
                // A name, not a number. The flight map compares this against
                // those four words — to pick the pin's colour, and first of all
                // to decide whether the node is on the map at all. Answering 0
                // or 1 is never equal to any of them, so every node counted as
                // shown, including the ones the player has never been to.
                {"TaxiNodeGetType", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
            if (id == 0) { lua_pushstring(L, "NONE"); return 1; }
            if (id == gh->getTaxiCurrentNode())  { lua_pushstring(L, "CURRENT"); return 1; }
            if (!gh->isKnownTaxiNode(id))        { lua_pushstring(L, "NONE"); return 1; }
            // Known, but nothing flies there from where the player is standing:
            // shown in yellow rather than hidden, so it reads as somewhere they
            // have been rather than somewhere that does not exist.
            lua_pushstring(L, gh->hasTaxiRouteTo(id) ? "REACHABLE" : "DISTANT");
            return 1;
        }},
                {"TakeTaxiNode", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
            if (id != 0) gh->activateTaxi(id);
            return 0;
        }},
                // TaxiNodePosition(index) → x, y as fractions of the map.
                {"TaxiNodePosition", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
            float u = 0, v = 0;
            if (id == 0 || !taxiNodeMapPos(gh, id, u, v)) { return luaReturnNil(L); }
            lua_pushnumber(L, u);
            lua_pushnumber(L, v);
            return 2;
        }},
                // TaxiNodeSetCurrent(index) — the node the map is drawing a
                // route to. Works out the journey once; the frame then asks
                // about each leg of it.
                {"TaxiNodeSetCurrent", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
            taxiRouteShown() = (id != 0 && gh) ? gh->getTaxiRouteTo(id)
                                               : std::vector<uint32_t>{};
            return 0;
        }},
                // GetNumRoutes(index) → how many hops the flight takes.
                //
                // The map draws one line per hop, and reads this to decide how
                // many. It also asks about every node in the list to find the
                // ones a single hop away, so this answers for the node asked
                // about rather than for whatever was last set current.
                {"GetNumRoutes", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
            if (id == 0 || !gh) { lua_pushnumber(L, 0); return 1; }
            const auto route = gh->getTaxiRouteTo(id);
            // A route of N nodes is N-1 hops; one of fewer than two is no
            // journey at all.
            lua_pushnumber(L, route.size() >= 2
                                  ? static_cast<double>(route.size() - 1) : 0.0);
            return 1;
        }},
                // TaxiGetSrcX/Y(index, hop) and TaxiGetDestX/Y(index, hop) —
                // the ends of one leg, on the same scale as TaxiNodePosition.
                //
                // The index names the destination and the hop names the leg, so
                // these route afresh rather than trusting whatever was last set
                // current: DrawOneHopLines walks every node without ever
                // calling TaxiNodeSetCurrent.
                {"TaxiGetSrcX",  lua_TaxiLegCoord<0, true>},
                {"TaxiGetSrcY",  lua_TaxiLegCoord<0, false>},
                {"TaxiGetDestX", lua_TaxiLegCoord<1, true>},
                {"TaxiGetDestY", lua_TaxiLegCoord<1, false>},
                // SetTaxiMap(texture) — the flight map's own picture.
                //
                // Nothing here has continent map artwork to give it, and the
                // texture it is handed keeps whatever its XML set. Named rather
                // than left out so it does not read as a gap that was missed.
                {"SetTaxiMap", [](lua_State* L) -> int { (void)L; return 0; }},
                {"CloseTaxiMap", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->closeTaxi();
            return 0;
        }},
                // TaxiNodeCost(index) → the fare in copper, for the tooltip.
                {"TaxiNodeCost", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const uint32_t id = taxiNodeAt(gh, static_cast<int>(luaL_optnumber(L, 1, 0)));
            lua_pushnumber(L, id != 0 ? static_cast<double>(gh->getTaxiCostTo(id)) : 0.0);
            return 1;
        }},
                {"GetNetStats", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            uint32_t ms = gh ? gh->getLatencyMs() : 0;
            lua_pushnumber(L, 0);   // bandwidthIn
            lua_pushnumber(L, 0);   // bandwidthOut
            lua_pushnumber(L, ms);  // latencyHome
            lua_pushnumber(L, ms);  // latencyWorld
            return 4;
        }},
                {"GetCurrentTitle", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getChosenTitleBit() : -1);
            return 1;
        }},
                {"GetTitleName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            int bit = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || bit < 0) { return luaReturnNil(L); }
            std::string title = gh->getFormattedTitle(static_cast<uint32_t>(bit));
            if (title.empty()) { return luaReturnNil(L); }
            lua_pushstring(L, title.c_str());
            return 1;
        }},
                // SetCurrentTitle(bit) — and the comment that used to sit here
                // saying CMSG_SET_TITLE was not exposed was stale:
                // sendSetTitle builds and sends it, and has for a while.
                //
                // The server validates it. HandleSetTitleOpcode refuses any bit
                // the character does not own and silently sets none, so there
                // is nothing to check here that the server does not check
                // better.
                {"SetCurrentTitle", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->sendSetTitle(static_cast<int32_t>(luaL_optnumber(L, 1, -1)));
            return 0;
        }},
                {"GetInspectSpecialization", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto* ir = gh ? gh->getInspectResult() : nullptr;
            lua_pushnumber(L, ir ? ir->activeTalentGroup : 0);
            return 1;
        }},
                // GetInspectArenaTeamData(index) →
                //   name, size, rating, weekPlayed, weekWins, seasonPlayed,
                //   seasonWins, playerRating
                //
                // Real: the inspect reply carries these and this client already
                // parses them into InspectResult.
                {"GetInspectArenaTeamData", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto* ir = gh ? gh->getInspectResult() : nullptr;
            const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!ir || index < 1 ||
                index > static_cast<int>(ir->arenaTeams.size())) {
                return 0;   // nothing, which is how WoW says "no team here"
            }
            const auto& t = ir->arenaTeams[index - 1];
            // The type is the team size: 2, 3 or 5.
            lua_pushstring(L, t.name.c_str());
            lua_pushnumber(L, t.type);
            lua_pushnumber(L, t.personalRating);
            lua_pushnumber(L, t.weekGames);
            lua_pushnumber(L, t.weekWins);
            lua_pushnumber(L, t.seasonGames);
            lua_pushnumber(L, t.seasonWins);
            lua_pushnumber(L, t.personalRating);
            return 8;
        }},
                // CanInspect(unit [, showError]) — a player other than a
                // corpse, which is as much as this client can judge; the server
                // refuses the rest and the reply simply does not arrive.
                {"CanInspect", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* uid = luaL_optstring(L, 1, "target");
            if (!gh) { lua_pushboolean(L, 0); return 1; }
            std::string uidStr(uid);
            for (char& c : uidStr) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const uint64_t guid = resolveUnitGuid(gh, uidStr);
            if (guid == 0) { lua_pushboolean(L, 0); return 1; }
            // High word 0x0000 marks a player guid in this range; creatures
            // carry 0xF13/0xF14 and cannot be inspected at all.
            const bool isPlayer = ((guid >> 48) & 0xFFFF) == 0;
            lua_pushboolean(L, isPlayer ? 1 : 0);
            return 1;
        }},
                // Honour data for an inspected player is not in the reply this
                // client parses, so it says so plainly rather than reporting
                // zeros as though they were the answer: HasInspectHonorData is
                // false, and the panel's honour section stays empty instead of
                // claiming the player has never won anything.
                {"HasInspectHonorData", [](lua_State* L) -> int {
            lua_pushboolean(L, 0);
            return 1;
        }},
                {"RequestInspectHonorData", [](lua_State* L) -> int {
            (void)L;
            return 0;
        }},
                {"GetInspectHonorData", [](lua_State* L) -> int {
            // Six: today's kills and honour, yesterday's, lifetime kills, and
            // the lifetime *rank* — which inspecthonorframe feeds straight
            // into GetPVPRankInfo. The old rank ladder was retired in this
            // expansion and the server sends nothing for it, so zero is the
            // truthful answer rather than a placeholder; what was missing was
            // returning it at all.
            for (int i = 0; i < 6; ++i) lua_pushnumber(L, 0);
            return 6;
        }},
                {"GetInspectPVPRankProgress", [](lua_State* L) -> int {
            lua_pushnumber(L, 0);
            return 1;
        }},
                // UnitPVPRank(unit) — the old honour rank, which no WotLK
                // server sends; GetPVPRankInfo is fed from it and handles zero.
                {"UnitPVPRank", [](lua_State* L) -> int {
            (void)L;
            lua_pushnumber(L, 0);
            return 1;
        }},
                {"NotifyInspect", [](lua_State* L) -> int {
            (void)L; // Inspect is auto-triggered by the C++ side when targeting a player
            return 0;
        }},
                {"ClearInspectPlayer", [](lua_State* L) -> int {
            (void)L;
            return 0;
        }},
                // Both answer the amount held *and the cap*. staticpopup.lua
                // does `MerchantFrame.honorPoints + currentHonor > maxHonor`
                // when confirming a PvP refund, and comparing a number against
                // a nil raises — so refunding a honour purchase took the
                // confirmation down rather than showing it.
                //
                // The caps are the client's own constants for this expansion,
                // not something the server sends: 75000 honour, 5000 arena
                // points. They are only ever used to warn that a refund would
                // overflow, which is exactly what they are right for.
                {"GetHonorCurrency", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getHonorPoints() : 0);
            lua_pushnumber(L, 75000);
            return 2;
        }},
                {"GetArenaCurrency", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getArenaPoints() : 0);
            lua_pushnumber(L, 5000);
            return 2;
        }},
                {"GetTimePlayed", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
            lua_pushnumber(L, gh->getTotalTimePlayed());
            lua_pushnumber(L, gh->getLevelTimePlayed());
            return 2;
        }},
                {"GetBindLocation", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushstring(L, "Unknown"); return 1; }
            lua_pushstring(L, gh->getWhoAreaName(gh->getHomeBindZoneId()).c_str());
            return 1;
        }},
                {"GetNumSavedInstances", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getInstanceLockouts().size() : 0);
            return 1;
        }},
                {"GetSavedInstanceInfo", [](lua_State* L) -> int {
            // GetSavedInstanceInfo(index) → name, id, reset, difficulty, locked, extended, instanceIDMostSig, isRaid, maxPlayers
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || index < 1) { return luaReturnNil(L); }
            const auto& lockouts = gh->getInstanceLockouts();
            if (index > static_cast<int>(lockouts.size())) { return luaReturnNil(L); }
            const auto& l = lockouts[index - 1];
            lua_pushstring(L, ("Instance " + std::to_string(l.mapId)).c_str()); // name (would need MapDBC for real names)
            lua_pushnumber(L, l.mapId);             // id
            lua_pushnumber(L, static_cast<double>(l.resetTime - static_cast<uint64_t>(time(nullptr)))); // reset (seconds until)
            lua_pushnumber(L, l.difficulty);        // difficulty
            lua_pushboolean(L, l.locked ? 1 : 0);  // locked
            lua_pushboolean(L, l.extended ? 1 : 0); // extended
            lua_pushnumber(L, 0);                   // instanceIDMostSig
            lua_pushboolean(L, l.difficulty >= 2 ? 1 : 0); // isRaid (25-man = raid)
            lua_pushnumber(L, l.difficulty >= 2 ? 25 : (l.difficulty >= 1 ? 10 : 5)); // maxPlayers
            // The difficulty in words, which the raid lockout row prints
            // beside the instance name. It was not returned, so that column
            // was blank on every saved instance.
            static constexpr const char* kNames[] = {"Normal", "Heroic",
                                                     "25 Normal", "25 Heroic"};
            lua_pushstring(L, l.difficulty < 4 ? kNames[l.difficulty] : "Normal");
            return 10;
        }},
                {"CalendarGetDate", [](lua_State* L) -> int {
            // CalendarGetDate() → weekday, month, day, year
            time_t now = time(nullptr);
            struct tm* t = localtime(&now);
            lua_pushnumber(L, t->tm_wday + 1); // weekday (1=Sun)
            lua_pushnumber(L, t->tm_mon + 1);  // month (1-12)
            lua_pushnumber(L, t->tm_mday);     // day
            lua_pushnumber(L, t->tm_year + 1900); // year
            return 4;
        }},
                {"CalendarGetNumPendingInvites", [](lua_State* L) -> int {
            return luaReturnZero(L);
        }},
                {"CalendarGetNumDayEvents", [](lua_State* L) -> int {
            return luaReturnZero(L);
        }},
                // Compared against a number the moment it is called —
                // `CalendarEventGetNumInvites() > MAX_PARTY_MEMBERS + 1` — so
                // nil is not a quiet gap here but an error, and it took the
                // event view down with it. No invite list is tracked, so none
                // is the truth as well as the safe answer.
                {"CalendarEventGetNumInvites", [](lua_State* L) -> int {
            return luaReturnZero(L);
        }},
                {"GetDifficultyInfo", [](lua_State* L) -> int {
            // GetDifficultyInfo(id) → name, groupType, isHeroic, maxPlayers
            int diff = static_cast<int>(luaL_checknumber(L, 1));
            struct DiffInfo { const char* name; const char* group; int heroic; int maxPlayers; };
            static const DiffInfo infos[] = {
                {"5 Player", "party", 0, 5},          // 0: Normal 5-man
                {"5 Player (Heroic)", "party", 1, 5},  // 1: Heroic 5-man
                {"10 Player", "raid", 0, 10},          // 2: 10-man Normal
                {"25 Player", "raid", 0, 25},          // 3: 25-man Normal
                {"10 Player (Heroic)", "raid", 1, 10}, // 4: 10-man Heroic
                {"25 Player (Heroic)", "raid", 1, 25}, // 5: 25-man Heroic
            };
            if (diff >= 0 && diff < 6) {
                lua_pushstring(L, infos[diff].name);
                lua_pushstring(L, infos[diff].group);
                lua_pushboolean(L, infos[diff].heroic);
                lua_pushnumber(L, infos[diff].maxPlayers);
            } else {
                lua_pushstring(L, "Unknown");
                lua_pushstring(L, "party");
                lua_pushboolean(L, 0);
                lua_pushnumber(L, 5);
            }
            return 4;
        }},
                {"GetWeatherInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
            lua_pushnumber(L, gh->getWeatherType());
            lua_pushnumber(L, gh->getWeatherIntensity());
            return 2;
        }},
                {"GetMaxPlayerLevel", [](lua_State* L) -> int {
            auto* svc = getLuaServices(L);
            auto* reg = svc ? svc->expansionRegistry : nullptr;
            auto* prof = reg ? reg->getActive() : nullptr;
            if (prof && prof->id == "wotlk") lua_pushnumber(L, 80);
            else if (prof && prof->id == "tbc") lua_pushnumber(L, 70);
            else lua_pushnumber(L, 60);
            return 1;
        }},
                {"GetAccountExpansionLevel", [](lua_State* L) -> int {
            auto* svc = getLuaServices(L);
            auto* reg = svc ? svc->expansionRegistry : nullptr;
            auto* prof = reg ? reg->getActive() : nullptr;
            if (prof && prof->id == "wotlk") lua_pushnumber(L, 3);
            else if (prof && prof->id == "tbc") lua_pushnumber(L, 2);
            else lua_pushnumber(L, 1);
            return 1;
        }},
    };
    for (const auto& [name, func] : api) {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
    }
}

} // namespace wowee::addons
