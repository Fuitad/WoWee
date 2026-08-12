#include "cli_list_formats.hpp"
#include "cli_arg_parse.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Static catalog of every novel open format the editor can
// currently emit, parse, and round-trip. Adding a new format
// requires appending one row here so --list-formats stays
// authoritative. The list is intentionally kept in
// introduction order so users can correlate against the
// commit history.
struct FormatRow {
    const char* magic;       // 4-char binary magic
    const char* extension;   // file suffix (with dot)
    const char* category;    // grouping label
    const char* replaces;    // proprietary source(s)
    const char* description;
};

constexpr FormatRow kFormats[] = {
    // World / asset / pipeline foundations.
    {"WOM ", ".wom",   "asset",     "M2",                              "M2 model - bones / vertices / animations"},
    {"WOB ", ".wob",   "asset",     "WMO",                             "WMO building - groups / portals / collision"},
    {"WHM ", ".whm",   "world",     "ADT heightmap",                   "ADT terrain heightmap tile"},
    {"WOT ", ".wot",   "world",     "ADT textures",                    "ADT terrain texture splats + alpha layers"},
    {"WOW ", ".wow",   "world",     "WDT/WDL",                         "Per-zone world manifest + weather"},

    // Catalog / DBC replacements.
    {"WITM", ".wit",   "items",     "Item.dbc + item_template",        "Item catalog (gear, consumables, quest items)"},
    {"WCRT", ".wcrt",  "creatures", "creature_template",               "Creature catalog (NPCs, mobs, vendors)"},
    {"WSPN", ".wspn",  "wspn",      "creature SQL",                    "Creature/object spawns by zone+coord"},
    {"WLOT", ".wlot",  "loot",      "creature_loot_template",          "Loot tables and drop chances"},
    {"WGOT", ".wgot",  "objects",   "gameobject_template",             "GameObject catalog (chests / doors)"},
    {"WSND", ".wsnd",  "audio",     "SoundEntries.dbc",                "Sound entry catalog"},
    {"WSPL", ".wspl",  "spells",    "Spell.dbc + spell_template",      "Spell catalog (effects, durations, costs)"},
    {"WQTM", ".wqt",   "quests",    "quest_template + Quest*.dbc",     "Quest catalog (objectives, rewards)"},
    {"WMSX", ".wms",   "maps",      "Map.dbc + AreaTable.dbc",         "Map and area catalog"},
    {"WCHC", ".wchc",  "chars",     "ChrClasses.dbc + ChrRaces.dbc",   "Class + race catalog"},
    {"WACH", ".wach",  "achieve",   "Achievement.dbc + Criteria.dbc",  "Achievement catalog with criteria"},
    {"WTRN", ".wtrn",  "trainers",  "npc_trainer + Spell.dbc",         "Trainer catalog (spell teaching)"},
    {"WGSP", ".wgsp",  "gossip",    "gossip_menu + npc_gossip",        "Gossip menu / dialog tree catalog"},
    {"WTAX", ".wtax",  "taxi",      "TaxiNodes.dbc + TaxiPath.dbc",    "Flight path catalog (taxi network)"},
    {"WTAL", ".wtal",  "talents",   "Talent.dbc + TalentTab.dbc",      "Talent tree catalog"},
    {"WTKN", ".wtkn",  "tokens",    "ItemExtendedCost + currency",     "Token / currency catalog"},
    {"WTRG", ".wtrg",  "triggers",  "AreaTrigger.dbc + areatrigger",   "Area trigger catalog"},
    {"WTIT", ".wtit",  "titles",    "CharTitles.dbc",                  "Player title catalog"},
    {"WSEA", ".wsea",  "events",    "GameEvent + spell_script",        "Scripted event catalog"},
    {"WMOU", ".wmou",  "mounts",    "Mount.dbc + spell_mount",         "Mount catalog (ground+flying)"},
    {"WBGD", ".wbgd",  "battle",    "BattlemasterList.dbc + bg_*",     "Battleground definition catalog"},
    {"WMAL", ".wmal",  "mail",      "mail + mail_external",            "In-game mail message catalog"},
    {"WGEM", ".wgem",  "gems",      "GemProperties.dbc + Enchant.dbc", "Gem + enchantment catalog"},
    {"WGLD", ".wgld",  "guilds",    "guild + guild_member",            "Guild catalog (charters, ranks)"},
    {"WPCD", ".wpcd",  "cond",      "Conditions + spell_proc_event",   "Reusable condition rule catalog"},
    {"WPET", ".wpet",  "pets",      "CreatureFamily.dbc + pet SQL",    "Hunter pet + warlock minion catalog"},
    {"WAUC", ".wauc",  "auction",   "auctionhouse + npc_auctioneer",   "Auction house rules catalog"},
    {"WCHN", ".wchn",  "channels",  "ChatChannels.dbc + chat_channel", "Chat channel catalog"},
    {"WCMS", ".wcms",  "cinematic", "Movie.dbc + CinematicCamera.dbc", "Cinematic catalog (videos, cutscenes)"},
    {"WGLY", ".wgly",  "glyphs",    "GlyphProperties.dbc + GlyphSlot", "WotLK glyph catalog"},
    {"WVHC", ".wvhc",  "vehicles",  "Vehicle.dbc + VehicleSeat.dbc",   "Vehicle + seat-layout catalog"},
    {"WHOL", ".whol",  "holiday",   "Holidays.dbc + game_event",       "Calendar holiday + event catalog"},
    {"WLIQ", ".wliq",  "liquids",   "LiquidType.dbc",                  "Liquid material catalog (water/lava/slime)"},
    {"WANI", ".wani",  "anim",      "AnimationData.dbc",               "Animation ID + fallback + weapon-flag catalog"},
    {"WSVK", ".wsvk",  "spellfx",   "SpellVisualKit.dbc + SpellVisFx",  "Spell visual kit (cast/proj/impact effects)"},
    {"WWUI", ".wwui",  "ui",        "WorldStateUI.dbc + world_state",   "World-state UI (BG scoreboards / siege counters)"},
    {"WPCN", ".wpcn",  "logic",     "PlayerCondition.dbc + conditions", "Player condition (gates, AND/OR/NOT chains)"},
    {"WTSK", ".wtsk",  "crafting",  "SkillLineAbility.dbc + recipes",   "Trade skill recipes (per-profession crafts)"},
    {"WCEQ", ".wceq",  "creatures", "creature_equip_template",          "Creature equipment loadout (visible weapons)"},
    {"WSET", ".wset",  "items",     "ItemSet.dbc + ItemSetSpell.dbc",   "Item set + tier-bonus catalog"},
    {"WGTP", ".wgtp",  "ui",        "GameTips.dbc + tutorial hints",    "Game tips / tutorial / loading-screen catalog"},
    {"WCMP", ".wcmp",  "pets",      "CreatureFamily + companion SQL",   "Companion / vanity pet catalog"},
    {"WSMC", ".wsmc",  "spells",    "SpellMechanic.dbc + DR tables",    "Spell mechanic / CC category catalog"},
    {"WKBD", ".wkbd",  "input",     "KeyBinding.dbc + default binds",   "Default keybinding catalog"},
    {"WSCH", ".wsch",  "spells",    "SpellSchools.dbc + Resistances",   "Spell school / damage type catalog"},
    {"WLFG", ".wlfg",  "social",    "LFGDungeons.dbc + LFG rewards",    "LFG / Dungeon Finder catalog"},
    {"WMAC", ".wmac",  "ui",        "(client-side macro storage)",      "Macro / slash command catalog"},
    {"WCHF", ".wchf",  "chars",     "CharHairGeosets + CharFacialHair", "Character hair / face customization catalog"},
    {"WPVP", ".wpvp",  "pvp",       "honor / arena rank tables",        "PvP honor rank + arena tier catalog"},
    {"WBNK", ".wbnk",  "items",     "ItemBag.dbc + bank slots",         "Bag / bank / special slot catalog"},
    {"WRUN", ".wrun",  "spells",    "RuneCost.dbc + ChrPowerType DK",   "Death Knight rune cost catalog"},
    {"WLDS", ".wlds",  "ui",        "LoadingScreens.dbc",               "Per-zone loading screen catalog"},
    {"WSUF", ".wsuf",  "items",     "ItemRandomProperties + Suffix",    "Item random-suffix bonus catalog"},
    {"WCRR", ".wcrr",  "stats",     "gtCombatRatings.dbc + curves",     "Combat rating conversion catalog"},
    {"WUMV", ".wumv",  "stats",     "UnitMovement.dbc + speed mods",    "Unit movement type / speed catalog"},
    {"WQSO", ".wqso",  "quests",    "QuestSort.dbc + QuestInfo cats",   "Quest sort / category catalog"},
    {"WSRG", ".wsrg",  "spells",    "SpellRange.dbc + per-spell range", "Spell range bucket catalog"},
    {"WSCT", ".wsct",  "spells",    "SpellCastTimes.dbc + cast scaling","Spell cast time bucket catalog"},
    {"WSDR", ".wsdr",  "spells",    "SpellDuration.dbc + per-spell dur","Spell duration bucket catalog"},
    {"WSCD", ".wscd",  "spells",    "SpellCooldown.dbc + shared cd grp","Spell cooldown category catalog"},
    {"WCEF", ".wcef",  "creatures", "CreatureFamily.dbc + pet trees",   "Creature / pet family catalog"},
    {"WSPC", ".wspc",  "spells",    "Spell.dbc power-cost fields",      "Spell power cost bucket catalog"},
    {"WGFS", ".wgfs",  "glyphs",    "GlyphSlot.dbc",                    "Glyph slot layout catalog"},
    {"WCDF", ".wcdf",  "creatures", "CreatureDifficulty.dbc",           "Creature difficulty variant catalog"},
    {"WMAT", ".wmat",  "items",     "Material.dbc + ItemDisplayInfo",   "Item material catalog"},
    {"WPSP", ".wpsp",  "chars",     "playercreateinfo SQL + StartOutfit","Player spawn profile catalog"},
    {"WTLE", ".wtle",  "talents",   "TalentTab.dbc",                    "Talent tab / tree catalog"},
    {"WCTR", ".wctr",  "currency",  "CurrencyTypes.dbc + caps",         "Currency type catalog"},
    {"WSPR", ".wspr",  "spells",    "Spell.dbc Reagent[8]+Count[8]",    "Spell reagent set catalog"},
    {"WACR", ".wacr",  "achieve",   "Achievement_Criteria.dbc",         "Achievement criteria catalog"},
    {"WSEF", ".wsef",  "spells",    "SpellEffect.Effect dispatch",      "Spell effect type catalog"},
    {"WAUR", ".waur",  "spells",    "SpellEffect.EffectAuraType",       "Spell aura type catalog"},
    {"WIQR", ".wiqr",  "items",     "Item quality tier colors+rules",   "Item quality tier catalog"},
    {"WSCS", ".wscs",  "skills",    "SkillCostsData.dbc + train tiers", "Skill cost / training tier catalog"},
    {"WIFS", ".wifs",  "items",     "Item.dbc Flags bit meanings",      "Item flag bit catalog"},
    {"WBKD", ".wbkd",  "npcs",      "npc_vendor + npc_trainer SQL",     "NPC service definition catalog"},
    {"WTBR", ".wtbr",  "tokens",    "currency_token_reward SQL",        "Token reward redemption catalog"},
    {"WSPS", ".wsps",  "spells",    "spell_proc_event SQL + Spell.dbc", "Spell proc trigger catalog"},
    {"WCMR", ".wcmr",  "creatures", "creature_movement waypoints SQL",  "Creature patrol path catalog"},
    {"WBOS", ".wbos",  "raid",      "instance_encounter SQL",           "Boss encounter definition catalog"},
    {"WHLD", ".whld",  "raid",      "InstanceTemplate.dbc reset fields","Instance lockout schedule catalog"},
    {"WSTC", ".wstc",  "pets",      "stable_slot SQL + hunter UI",      "Hunter stable slot catalog"},
    {"WSTM", ".wstm",  "stats",     "gtChanceTo*.dbc + gtRegen*.dbc",   "Stat modifier curve catalog"},
    {"WACT", ".wact",  "ui",        "Hardcoded class default action bar","Action bar layout catalog"},
    {"WGRP", ".wgrp",  "social",    "LFG group-composition rules",     "Group composition catalog (role quotas)"},
    {"WHRT", ".whrt",  "social",    "SMSG_BINDPOINTUPDATE bind list",  "Hearthstone bind point catalog"},
    {"WSCB", ".wscb",  "server",    "MOTD + scheduled SMSG_NOTIFICATION","Server channel broadcast catalog"},
    {"WCMG", ".wcmg",  "spells",    "Stance/Form/Aspect mutex tables",  "Combat maneuver group catalog (mutex spells)"},
    {"WMSP", ".wmsp",  "server",    "realmlist + SMSG_REALM_LIST data",  "Master server profile / realmlist catalog"},
    {"WEMO", ".wemo",  "social",    "EmotesText.dbc + EmotesTextSound", "Emote definition catalog (/dance, /wave, etc.)"},
    {"WBAB", ".wbab",  "spells",    "Spell.dbc nextRank/prevRank ptrs",  "Buff & Aura book - long-duration class buffs with rank chains"},
    {"WTBD", ".wtbd",  "guilds",    "guild_member tabard config blob",   "Tabard design / heraldry catalog (3-color)"},
    {"WSPM", ".wspm",  "spellfx",   "AreaTrigger.dbc + decal blob",      "Spell persistent marker catalog (AoE ground decals)"},
    {"WLDN", ".wldn",  "server",    "TutorialPopup + LevelMilestone msgs","Learning notification catalog (level-up milestones)"},
    {"WCRE", ".wcre",  "creatures", "creature_template resist + immunity","Creature resist + CC-immunity profile catalog"},
    {"WPTT", ".wptt",  "pets",      "PetTalent.dbc + PetTalentTab.dbc",   "Hunter pet talent tree catalog (3 trees, grid+graph)"},
    {"WHRD", ".whrd",  "raid",      "implicit Heroic-mode loot scaling",  "Heroic loot scaling catalog (per instance+difficulty)"},
    {"WRPR", ".wrpr",  "factions",  "npc_vendor reqstanding + rep gates", "Reputation reward tier catalog (per faction)"},
    {"WMNL", ".wmnl",  "worldmap",  "WorldMapTransforms.dbc + Overlay",   "Minimap multi-level catalog (vertical zones)"},
    {"WPCR", ".wpcr",  "pets",      "Spell.dbc pet ops + npc_text stable","Pet care + action catalog (Hunter / Warlock / stable mgmt)"},
    {"WMVC", ".wmvc",  "cinematic", "embedded cinematic credit-roll blob","Movie credits roll catalog (per-cinematic)"},
    {"WSPV", ".wspv",  "spells",    "implicit Spell.dbc context overrides","Spell variant catalog (stance/talent/racial substitution)"},
    {"WVOX", ".wvox",  "audio",     "CreatureTextSounds + per-quest voice","Voiceover audio catalog (per-NPC, per-event clips)"},
    {"WTRD", ".wtrd",  "social",    "trade-window state machine policy",  "Trade window rules catalog (P2P trade policy)"},
    {"WWFL", ".wwfl",  "social",    "chat preprocessor bad-word matcher", "Word filter catalog (spam/RMT/all-caps/URL)"},
    {"WMAR", ".wmar",  "ui",        "raid-target icon set (8 fixed)",     "Raid marker catalog (8 raid + map pins + party roles)"},
    {"WLMA", ".wlma",  "loot",      "GroupLoot CMSG_LOOT_METHOD policy",  "Loot mode policy catalog (FFA / RR / Master / NBG / Personal)"},
    {"WSKP", ".wskp",  "world",     "LightParams.dbc + Light.dbc diurnal","Sky parameters catalog (per-zone keyframes)"},
    {"WCFG", ".wcfg",  "server",    "worldserver.conf flat-text config",  "Server config catalog (polymorphic Float/Int/Bool/String values)"},
    {"WANV", ".wanv",  "events",    "GameEvent SQL + per-holiday script", "Anniversary & recurring event catalog (cron-like scheduling)"},
    {"WPRG", ".wprg",  "pvp",       "vanilla 14-rank PvP ladder ladder",  "PvP ranking grades catalog (faction + tier + honor thresholds)"},
    {"WLAN", ".wlan",  "i18n",      "Locale_*.MPQ + DBC trailing strings","Localization catalog (per-language string overlay)"},
    {"WGCH", ".wgch",  "chat",      "ChatChannels.dbc + zone-default joins","Global chat channel catalog (access policy + zone auto-join)"},
    {"WMOD", ".wmod",  "addons",    "per-addon TOC text + load-order rules","Addon manifest catalog (deps + cycle detection)"},
    {"WSPK", ".wspk",  "spells",    "SkillLineAbility + per-spec tab order","Spell pack catalog (per-class spellbook tab layout)"},
    {"WPHM", ".wphm",  "anim",      "implicit M2 movementState->anim map","Player movement-to-animation map (per race/gender/state)"},
    {"WTSC", ".wtsc",  "transit",   "TaxiNodes + zeppelin GO scripts",   "Transit schedule catalog (taxi/zeppelin/boat scheduled departures)"},
    {"WPRT", ".wprt",  "portals",   "SpellEffect TELEPORT_UNITS + AreaTrigger","Mage portal destinations catalog (spellId -> coords binding)"},
    {"WCST", ".wcst",  "stats",     "CharBaseInfo + GtChanceTo*.dbc + StatSystem","Combat stats baseline catalog (per-class per-level base stats)"},
    {"WGBK", ".wgbk",  "guild",     "(absent in vanilla, TBC GuildBankTab)","Guild bank tabs catalog (per-rank withdrawal limits)"},
    {"WQGR", ".wqgr",  "quests",    "QuestRelations.dbc + per-quest scripts","Quest graph catalog (chain prereq DAG + cycle detection)"},
    {"WCRA", ".wcra",  "crafting",  "SpellReagents.dbc + Spell.dbc effect-24","Crafting recipe catalog (trade-skill recipe expansion)"},
    {"WLOC", ".wloc",  "world",     "AreaPOI + GO spawns + AreaTrigger.dbc","World locations catalog (POI/RareSpawn/HerbNode/MineralVein/FishingSpot/Trigger/PortalLand)"},
    {"WBND", ".wbnd",  "loot",      "ItemTemplate.bondingType + LootMgr",  "Soulbind rules catalog (BoP/BoE/BoU/BoA + raid-trade window)"},
    {"WBHV", ".wbhv",  "ai",        "creature_template.AIName + ScriptMgr","Creature behavior catalog (combat AI archetypes + special abilities)"},
    {"WIRC", ".wirc",  "loot",      "ItemRandomProperties.dbc + LootMgr",  "Item random-property pool catalog (weighted suffix enchants)"},
    {"WPRC", ".wprc",  "spells",    "SpellProcEvents + per-spell procFlags","Spell proc rules catalog (event triggers + ICD + self-loop guard)"},
    {"WAUH", ".wauh",  "economy",   "AuctionHouse.dbc + AuctionMgr",       "Auction house config catalog (deposit/cut rates + duration tiers)"},
    {"WBRD", ".wbrd",  "pvp",       "BattlemasterList.dbc + BattlegroundMgr","Battleground reward stages catalog (per-bracket honor + marks)"},
    {"WSWP", ".wswp",  "audio",     "(absent in vanilla - patch-level edits)","Sound swap rules catalog (priority + condition-gated substitution)"},
    {"WTUR", ".wtur",  "ui",        "TutorialFrame.xml + Tutorial.lua",    "Tutorial steps catalog (event-triggered first-time-player popup sequences)"},
    {"WCMD", ".wcmd",  "chat",      "ChatFrame.lua + per-command CommandHandler","Chat slash command catalog (security-gated registry + aliases + throttle)"},
    {"WCAM", ".wcam",  "camera",    "CameraMgr hard-coded camera profiles","Camera preset catalog (per-purpose FOV/distance/pitch/damping)"},
    {"WCFR", ".wcfr",  "stats",     "gtChanceTo*.dbc + StatSystem constants","Combat formula catalog (per-class stat conversion ratios in fixed-point)"},
    {"WLNK", ".wlnk",  "chat",      "ChatFrame_OnHyperlinkClick LUA",      "Chat hyperlink template catalog (sprintf-style %d/%s link composition)"},

    // Additional pipeline catalogs without the alternating
    // gen/info/validate CLI surface (loaded by the engine
    // directly).
    {"WFAC", ".wfac",  "factions",  "Faction.dbc + FactionTemplate",   "Faction + reputation catalog"},
    {"WLCK", ".wlck",  "locks",     "Lock.dbc",                        "Lock + key requirement catalog"},
    {"WSKL", ".wskl",  "skills",    "SkillLine.dbc + SkillLineAbility","Skill / profession catalog"},
    {"WOLA", ".wola",  "light",     "Light.dbc + LightParams.dbc",     "Outdoor lighting / sky color catalog"},
    {"WOWA", ".wowa",  "weather",   "weather + LightParams",           "Per-zone weather schedule catalog"},
    {"WMPX", ".wmpx",  "worldmap",  "WorldMapArea.dbc",                "World map / minimap zone catalog"},
};

constexpr size_t kFormatsCount =
    sizeof(kFormats) / sizeof(kFormats[0]);

int handleList(int& i, int argc, char** argv) {
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (jsonOut) {
        nlohmann::json j;
        j["count"] = kFormatsCount;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& row : kFormats) {
            arr.push_back({
                {"magic", row.magic},
                {"extension", row.extension},
                {"category", row.category},
                {"replaces", row.replaces},
                {"description", row.description},
            });
        }
        j["formats"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Wowee open formats: %zu total\n", kFormatsCount);
    std::printf("\n");
    std::printf("  magic    ext      category     replaces                          description\n");
    std::printf("  -------  -------  -----------  --------------------------------  -----------\n");
    for (const auto& row : kFormats) {
        std::printf("  %-7s  %-7s  %-11s  %-32s  %s\n",
                    row.magic, row.extension, row.category,
                    row.replaces, row.description);
    }
    return 0;
}

} // namespace

bool handleListFormats(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-formats") == 0) {
        outRc = handleList(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
