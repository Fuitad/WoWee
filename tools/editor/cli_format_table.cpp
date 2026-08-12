#include "cli_format_table.hpp"

#include <cstring>

namespace wowee {
namespace editor {
namespace cli {

namespace {

constexpr FormatMagicEntry kFormats[] = {
    {{'W','O','M',' '}, ".wom",  "asset",     nullptr,               nullptr,          "M2 model"},
    {{'W','O','B',' '}, ".wob",  "asset",     nullptr,               nullptr,          "WMO building"},
    {{'W','H','M',' '}, ".whm",  "world",     nullptr,               nullptr,          "ADT heightmap"},
    {{'W','O','T',' '}, ".wot",  "world",     nullptr,               nullptr,          "ADT textures"},
    {{'W','O','W',' '}, ".wow",  "world",     nullptr,               nullptr,          "Per-zone world manifest"},
    {{'W','I','T','M'}, ".wit",  "items",     "--info-wit",          "itemId",         "Item catalog"},
    {{'W','C','R','T'}, ".wcrt", "creatures", "--info-wcrt",         "creatureId",     "Creature catalog"},
    {{'W','S','P','N'}, ".wspn", "spawns",    "--info-wspn",         nullptr,          "Spawn catalog"},
    {{'W','L','O','T'}, ".wlot", "loot",      "--info-wlot",         "creatureId",     "Loot tables"},
    {{'W','G','O','T'}, ".wgot", "objects",   "--info-wgot",         "objectId",       "GameObject catalog"},
    {{'W','S','N','D'}, ".wsnd", "audio",     "--info-wsnd",         "soundId",        "Sound entries"},
    {{'W','S','P','L'}, ".wspl", "spells",    "--info-wspl",         "spellId",        "Spell catalog"},
    {{'W','Q','T','M'}, ".wqt",  "quests",    "--info-wqt",          "questId",        "Quest catalog"},
    {{'W','M','S','X'}, ".wms",  "maps",      "--info-wms",          nullptr,          "Map / area catalog"},
    {{'W','C','H','C'}, ".wchc", "chars",     "--info-wchc",         nullptr,          "Class + race catalog"},
    {{'W','A','C','H'}, ".wach", "achieve",   "--info-wach",         "achievementId",  "Achievement catalog"},
    {{'W','T','R','N'}, ".wtrn", "trainers",  "--info-wtrn",         "npcId",          "Trainer catalog"},
    {{'W','G','S','P'}, ".wgsp", "gossip",    "--info-wgsp",         "menuId",         "Gossip menu catalog"},
    {{'W','T','A','X'}, ".wtax", "taxi",      "--info-wtax",         nullptr,          "Taxi node catalog"},
    {{'W','T','A','L'}, ".wtal", "talents",   "--info-wtal",         nullptr,          "Talent catalog"},
    {{'W','T','K','N'}, ".wtkn", "tokens",    "--info-wtkn",         "tokenId",        "Token catalog"},
    {{'W','T','R','G'}, ".wtrg", "triggers",  "--info-wtrg",         "triggerId",      "Trigger catalog"},
    {{'W','T','I','T'}, ".wtit", "titles",    "--info-wtit",         "titleId",        "Title catalog"},
    {{'W','S','E','A'}, ".wsea", "events",    "--info-wsea",         "eventId",        "Event catalog"},
    {{'W','M','O','U'}, ".wmou", "mounts",    "--info-wmou",         "mountId",        "Mount catalog"},
    {{'W','B','G','D'}, ".wbgd", "battle",    "--info-wbgd",         "battlegroundId", "Battleground catalog"},
    {{'W','M','A','L'}, ".wmal", "mail",      "--info-wmal",         "templateId",     "Mail catalog"},
    {{'W','G','E','M'}, ".wgem", "gems",      "--info-wgem",         nullptr,          "Gem catalog"},
    {{'W','G','L','D'}, ".wgld", "guilds",    "--info-wgld",         "guildId",        "Guild catalog"},
    {{'W','P','C','D'}, ".wpcd", "cond",      "--info-wpcd",         "conditionId",    "Condition catalog"},
    {{'W','P','E','T'}, ".wpet", "pets",      "--info-wpet",         nullptr,          "Pet catalog"},
    {{'W','A','U','C'}, ".wauc", "auction",   "--info-wauc",         "houseId",        "Auction catalog"},
    {{'W','C','H','N'}, ".wchn", "channels",  "--info-wchn",         "channelId",      "Channel catalog"},
    {{'W','C','M','S'}, ".wcms", "cinematic", "--info-wcms",         "cinematicId",    "Cinematic catalog"},
    {{'W','G','L','Y'}, ".wgly", "glyphs",    "--info-wgly",         "glyphId",        "Glyph catalog"},
    {{'W','V','H','C'}, ".wvhc", "vehicles",  "--info-wvhc",         "vehicleId",      "Vehicle catalog"},
    {{'W','H','O','L'}, ".whol", "holiday",   "--info-whol",         "holidayId",      "Holiday catalog"},
    {{'W','L','I','Q'}, ".wliq", "liquids",   "--info-wliq",         "liquidId",       "Liquid catalog"},
    {{'W','A','N','I'}, ".wani", "anim",      "--info-wani",         "animationId",    "Animation catalog"},
    {{'W','S','V','K'}, ".wsvk", "spellfx",   "--info-wsvk",         "visualKitId",    "Spell visual kit catalog"},
    {{'W','W','U','I'}, ".wwui", "ui",        "--info-wwui",         "worldStateId",   "World-state UI catalog"},
    {{'W','P','C','N'}, ".wpcn", "logic",     "--info-wpcn",         "conditionId",    "Player condition catalog"},
    {{'W','T','S','K'}, ".wtsk", "crafting",  "--info-wtsk",         nullptr,          "Trade skill recipe catalog"},
    {{'W','C','E','Q'}, ".wceq", "creatures", "--info-wceq",         "equipmentId",    "Creature equipment loadout catalog"},
    {{'W','S','E','T'}, ".wset", "items",     "--info-wset",         nullptr,          "Item set / tier-bonus catalog"},
    {{'W','G','T','P'}, ".wgtp", "ui",        "--info-wgtp",         "tipId",          "Game tips / tutorial catalog"},
    {{'W','C','M','P'}, ".wcmp", "pets",      "--info-wcmp",         "companionId",    "Companion / vanity pet catalog"},
    {{'W','S','M','C'}, ".wsmc", "spells",    "--info-wsmc",         "mechanicId",     "Spell mechanic catalog"},
    {{'W','K','B','D'}, ".wkbd", "input",     "--info-wkbd",         "bindingId",      "Keybinding catalog"},
    {{'W','S','C','H'}, ".wsch", "spells",    "--info-wsch",         "schoolId",       "Spell school / damage type catalog"},
    {{'W','L','F','G'}, ".wlfg", "social",    "--info-wlfg",         "dungeonId",      "LFG / Dungeon Finder catalog"},
    {{'W','M','A','C'}, ".wmac", "ui",        "--info-wmac",         "macroId",        "Macro / slash command catalog"},
    {{'W','C','H','F'}, ".wchf", "chars",     "--info-wchf",         "featureId",      "Character hair / face customization catalog"},
    {{'W','P','V','P'}, ".wpvp", "pvp",       "--info-wpvp",         "rankId",         "PvP honor rank + arena tier catalog"},
    {{'W','B','N','K'}, ".wbnk", "items",     "--info-wbnk",         "bagSlotId",      "Bag / bank slot catalog"},
    {{'W','R','U','N'}, ".wrun", "spells",    "--info-wrun",         "runeCostId",     "Death Knight rune cost catalog"},
    {{'W','L','D','S'}, ".wlds", "ui",        "--info-wlds",         "screenId",       "Loading screen catalog"},
    {{'W','S','U','F'}, ".wsuf", "items",     "--info-wsuf",         nullptr,          "Item random-suffix catalog"},
    {{'W','C','R','R'}, ".wcrr", "stats",     "--info-wcrr",         nullptr,          "Combat rating conversion catalog"},
    {{'W','U','M','V'}, ".wumv", "stats",     "--info-wumv",         "moveTypeId",     "Unit movement type catalog"},
    {{'W','Q','S','O'}, ".wqso", "quests",    "--info-wqso",         "sortId",         "Quest sort / category catalog"},
    {{'W','S','R','G'}, ".wsrg", "spells",    "--info-wsrg",         "rangeId",        "Spell range bucket catalog"},
    {{'W','S','C','T'}, ".wsct", "spells",    "--info-wsct",         "castTimeId",     "Spell cast time bucket catalog"},
    {{'W','S','D','R'}, ".wsdr", "spells",    "--info-wsdr",         "durationId",     "Spell duration bucket catalog"},
    {{'W','S','C','D'}, ".wscd", "spells",    "--info-wscd",         "bucketId",       "Spell cooldown category catalog"},
    {{'W','C','E','F'}, ".wcef", "creatures", "--info-wcef",         "familyId",       "Creature / pet family catalog"},
    {{'W','S','P','C'}, ".wspc", "spells",    "--info-wspc",         "powerCostId",    "Spell power cost bucket catalog"},
    {{'W','G','F','S'}, ".wgfs", "glyphs",    "--info-wgfs",         "slotId",         "Glyph slot layout catalog"},
    {{'W','C','D','F'}, ".wcdf", "creatures", "--info-wcdf",         "difficultyId",   "Creature difficulty variant catalog"},
    {{'W','M','A','T'}, ".wmat", "items",     "--info-wmat",         "materialId",     "Item material catalog"},
    {{'W','P','S','P'}, ".wpsp", "chars",     "--info-wpsp",         "profileId",      "Player spawn profile catalog"},
    {{'W','T','L','E'}, ".wtle", "talents",   "--info-wtle",         "tabId",          "Talent tab / tree catalog"},
    {{'W','C','T','R'}, ".wctr", "currency",  "--info-wctr",         "currencyId",     "Currency type catalog"},
    {{'W','S','P','R'}, ".wspr", "spells",    "--info-wspr",         "reagentSetId",   "Spell reagent set catalog"},
    {{'W','A','C','R'}, ".wacr", "achieve",   "--info-wacr",         "criteriaId",     "Achievement criteria catalog"},
    {{'W','S','E','F'}, ".wsef", "spells",    "--info-wsef",         "effectId",       "Spell effect type catalog"},
    {{'W','A','U','R'}, ".waur", "spells",    "--info-waur",         "auraTypeId",     "Spell aura type catalog"},
    {{'W','I','Q','R'}, ".wiqr", "items",     "--info-wiqr",         "qualityId",      "Item quality tier catalog"},
    {{'W','S','C','S'}, ".wscs", "skills",    "--info-wscs",         "costId",         "Skill cost / training tier catalog"},
    {{'W','I','F','S'}, ".wifs", "items",     "--info-wifs",         "flagId",         "Item flag bit catalog"},
    {{'W','B','K','D'}, ".wbkd", "npcs",      "--info-wbkd",         "serviceId",      "NPC service definition catalog"},
    {{'W','T','B','R'}, ".wtbr", "tokens",    "--info-wtbr",         "tokenRewardId",  "Token reward redemption catalog"},
    {{'W','S','P','S'}, ".wsps", "spells",    "--info-wsps",         "procId",         "Spell proc trigger catalog"},
    {{'W','C','M','R'}, ".wcmr", "creatures", "--info-wcmr",         "pathId",         "Creature patrol path catalog"},
    {{'W','B','O','S'}, ".wbos", "raid",      "--info-wbos",         "encounterId",    "Boss encounter definition catalog"},
    {{'W','H','L','D'}, ".whld", "raid",      "--info-whld",         "lockoutId",      "Instance lockout schedule catalog"},
    {{'W','S','T','C'}, ".wstc", "pets",      "--info-wstc",         "slotId",         "Hunter stable slot catalog"},
    {{'W','S','T','M'}, ".wstm", "stats",     "--info-wstm",         "curveId",        "Stat modifier curve catalog"},
    {{'W','A','C','T'}, ".wact", "ui",        "--info-wact",         "bindingId",      "Action bar layout catalog"},
    {{'W','G','R','P'}, ".wgrp", "social",    "--info-wgrp",         "compId",         "Group composition catalog"},
    {{'W','H','R','T'}, ".whrt", "social",    "--info-whrt",         "bindId",         "Hearthstone bind point catalog"},
    {{'W','S','C','B'}, ".wscb", "server",    "--info-wscb",         "broadcastId",    "Server channel broadcast catalog"},
    {{'W','C','M','G'}, ".wcmg", "spells",    "--info-wcmg",         "groupId",        "Combat maneuver group catalog"},
    {{'W','M','S','P'}, ".wmsp", "server",    "--info-wmsp",         "realmId",        "Master server profile / realmlist catalog"},
    {{'W','E','M','O'}, ".wemo", "social",    "--info-wemo",         "emoteId",        "Emote definition catalog"},
    {{'W','B','A','B'}, ".wbab", "spells",    "--info-wbab",         "buffId",         "Buff & Aura book (rank chains)"},
    {{'W','T','B','D'}, ".wtbd", "guilds",    "--info-wtbd",         "tabardId",       "Tabard design / heraldry catalog"},
    {{'W','S','P','M'}, ".wspm", "spellfx",   "--info-wspm",         "markerId",       "Spell persistent marker catalog"},
    {{'W','L','D','N'}, ".wldn", "server",    "--info-wldn",         "notificationId", "Learning notification catalog"},
    {{'W','C','R','E'}, ".wcre", "creatures", "--info-wcre",         "resistId",       "Creature resist + immunity catalog"},
    {{'W','P','T','T'}, ".wptt", "pets",      "--info-wptt",         "talentId",       "Hunter pet talent tree catalog"},
    {{'W','H','R','D'}, ".whrd", "raid",      "--info-whrd",         "scalingId",      "Heroic loot scaling catalog"},
    {{'W','R','P','R'}, ".wrpr", "factions",  "--info-wrpr",         "tierId",         "Reputation reward tier catalog"},
    {{'W','M','N','L'}, ".wmnl", "worldmap",  "--info-wmnl",         "levelId",        "Minimap multi-level catalog"},
    {{'W','P','C','R'}, ".wpcr", "pets",      "--info-wpcr",         "actionId",       "Pet care + action catalog"},
    {{'W','M','V','C'}, ".wmvc", "cinematic", "--info-wmvc",         "rollId",         "Movie credits roll catalog"},
    {{'W','S','P','V'}, ".wspv", "spells",    "--info-wspv",         "variantId",      "Spell variant catalog"},
    {{'W','V','O','X'}, ".wvox", "audio",     "--info-wvox",         "voiceId",        "Voiceover audio catalog"},
    {{'W','T','R','D'}, ".wtrd", "social",    "--info-wtrd",         "ruleId",         "Trade window rules catalog"},
    {{'W','W','F','L'}, ".wwfl", "social",    "--info-wwfl",         "filterId",       "Word filter catalog"},
    {{'W','M','A','R'}, ".wmar", "ui",        "--info-wmar",         "markerId",       "Raid marker catalog"},
    {{'W','L','M','A'}, ".wlma", "loot",      "--info-wlma",         "modeId",         "Loot mode policy catalog"},
    {{'W','S','K','P'}, ".wskp", "world",     "--info-wskp",         "skyId",          "Sky parameters catalog"},
    {{'W','C','F','G'}, ".wcfg", "server",    "--info-wcfg",         "configId",       "Server config catalog"},
    {{'W','A','N','V'}, ".wanv", "events",    "--info-wanv",         "eventId",        "Anniversary & recurring event catalog"},
    {{'W','P','R','G'}, ".wprg", "pvp",       "--info-wprg",         "rankId",         "PvP ranking grades catalog"},
    {{'W','L','A','N'}, ".wlan", "i18n",      "--info-wlan",         "stringId",       "Localization catalog"},
    {{'W','G','C','H'}, ".wgch", "chat",      "--info-wgch",         "channelId",      "Global chat channel catalog"},
    {{'W','M','O','D'}, ".wmod", "addons",    "--info-wmod",         "addonId",        "Addon manifest catalog"},
    {{'W','S','P','K'}, ".wspk", "spells",    "--info-wspk",         "packId",         "Spell pack catalog"},
    {{'W','P','H','M'}, ".wphm", "anim",      "--info-wphm",         nullptr,          "Player movement-to-animation map"},
    {{'W','T','S','C'}, ".wtsc", "transit",   "--info-wtsc",         "routeId",        "Transit schedule catalog"},
    {{'W','P','R','T'}, ".wprt", "portals",   "--info-wprt",         "portalId",       "Mage portal destinations catalog"},
    {{'W','C','S','T'}, ".wcst", "stats",     "--info-wcst",         "statId",         "Combat stats baseline catalog"},
    {{'W','G','B','K'}, ".wgbk", "guild",     "--info-wgbk",         "tabId",          "Guild bank tabs catalog"},
    {{'W','Q','G','R'}, ".wqgr", "quests",    "--info-wqgr",         "questId",        "Quest graph catalog"},
    {{'W','C','R','A'}, ".wcra", "crafting",  "--info-wcra",         "recipeId",       "Crafting recipe catalog"},
    {{'W','L','O','C'}, ".wloc", "world",     "--info-wloc",         "locationId",     "World locations catalog"},
    {{'W','B','N','D'}, ".wbnd", "loot",      "--info-wbnd",         "ruleId",         "Soulbind rules catalog"},
    {{'W','B','H','V'}, ".wbhv", "ai",        "--info-wbhv",         "behaviorId",     "Creature behavior catalog"},
    {{'W','I','R','C'}, ".wirc", "loot",      "--info-wirc",         "poolId",         "Item random-property pool catalog"},
    {{'W','P','R','C'}, ".wprc", "spells",    "--info-wprc",         "procRuleId",     "Spell proc rules catalog"},
    {{'W','A','U','H'}, ".wauh", "economy",   "--info-wauh",         "ahId",           "Auction house config catalog"},
    {{'W','B','R','D'}, ".wbrd", "pvp",       "--info-wbrd",         "rewardId",       "Battleground reward stages catalog"},
    {{'W','S','W','P'}, ".wswp", "audio",     "--info-wswp",         "ruleId",         "Sound swap rules catalog"},
    {{'W','T','U','R'}, ".wtur", "ui",        "--info-wtur",         "tutId",          "Tutorial steps catalog"},
    {{'W','C','M','D'}, ".wcmd", "chat",      "--info-wcmd",         "cmdId",          "Chat slash command catalog"},
    {{'W','C','A','M'}, ".wcam", "camera",    "--info-wcam",         "presetId",       "Camera preset catalog"},
    {{'W','C','F','R'}, ".wcfr", "stats",     "--info-wcfr",         "formulaId",      "Combat formula catalog"},
    {{'W','L','N','K'}, ".wlnk", "chat",      "--info-wlnk",         "linkId",         "Chat hyperlink template catalog"},
    {{'W','F','A','C'}, ".wfac", "factions",  nullptr,               nullptr,          "Faction catalog"},
    {{'W','L','C','K'}, ".wlck", "locks",     nullptr,               nullptr,          "Lock catalog"},
    {{'W','S','K','L'}, ".wskl", "skills",    nullptr,               nullptr,          "Skill catalog"},
    {{'W','O','L','A'}, ".wol",  "light",     "--info-wol",          nullptr,          "Outdoor light catalog"},
    // WoweeWeather writes ".wow", which the per-zone world manifest above
    // already claims. Left as ".wowa" until one of the two formats gives the
    // extension up: a second row claiming ".wow" would be unreachable by name.
    {{'W','O','W','A'}, ".wowa", "weather",   nullptr,               nullptr,          "Weather schedule catalog"},
    {{'W','M','P','X'}, ".womx", "worldmap",  "--info-womx",         nullptr,          "World map catalog"},
};

constexpr size_t kFormatsCount =
    sizeof(kFormats) / sizeof(kFormats[0]);

} // namespace

const FormatMagicEntry* findFormatByExtension(const char* extension) {
    if (!extension || !*extension) return nullptr;
    for (const FormatMagicEntry* row = kFormats;
         row != kFormats + kFormatsCount; ++row) {
        const char* a = row->extension;
        const char* b = extension;
        bool match = true;
        while (*a && *b) {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) { match = false; break; }
            ++a;
            ++b;
        }
        // Both have to have ended: stopping at the shorter would answer .wom
        // for a .womx file.
        if (match && *a == 0 && *b == 0) return row;
    }
    return nullptr;
}

std::string formatFlagSuffix(const char* infoFlag) {
    if (!infoFlag) return {};
    const std::string flag = infoFlag;
    const std::string prefix = "--info-";
    if (flag.size() <= prefix.size() ||
        flag.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return flag.substr(prefix.size());
}

const FormatMagicEntry* findFormatByMagic(const char magic[4]) {
    for (const auto& row : kFormats) {
        if (std::memcmp(row.magic, magic, 4) == 0) return &row;
    }
    return nullptr;
}

const FormatMagicEntry* formatTableBegin() { return kFormats; }
const FormatMagicEntry* formatTableEnd() { return kFormats + kFormatsCount; }
size_t formatTableSize() { return kFormatsCount; }

} // namespace cli
} // namespace editor
} // namespace wowee
