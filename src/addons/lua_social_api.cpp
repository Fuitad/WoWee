// lua_social_api.cpp — Chat, guild, friends, ignore, gossip, party management, and emotes Lua API bindings.
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#include <vector>
#include "addons/lua_api_helpers.hpp"
#include "game/reputation_standing.hpp"

namespace wowee::addons {

// The languages a character can speak: their racial one, plus the faction
// tongue every member of that faction is taught. Human and Orc are the two
// where those are the same, so they know exactly one.
//
// Returned as (name, id) pairs rather than read from Languages.dbc: the wire
// ids are fixed across every expansion this client speaks, and the dbc adds
// nothing but a localised name we do not have a table for.
struct LanguageEntry { const char* name; int id; };

static void collectKnownLanguages(uint8_t raceId, std::vector<LanguageEntry>& out) {
    const bool horde = (raceId == 2 || raceId == 5 || raceId == 6 ||
                        raceId == 8 || raceId == 10);
    // Racial language first — that is the order the dropdown shows them in.
    switch (raceId) {
        case 1:  out.push_back({"Common", 7});      break;
        case 2:  out.push_back({"Orcish", 1});      break;
        case 3:  out.push_back({"Dwarvish", 6});    break;
        case 4:  out.push_back({"Darnassian", 2});  break;
        case 5:  out.push_back({"Gutterspeak", 33}); break;
        case 6:  out.push_back({"Taurahe", 3});     break;
        case 7:  out.push_back({"Gnomish", 13});    break;
        case 8:  out.push_back({"Troll", 14});      break;
        case 10: out.push_back({"Thalassian", 10}); break;
        case 11: out.push_back({"Draenei", 35});    break;
        default: break;
    }
    const LanguageEntry faction = horde ? LanguageEntry{"Orcish", 1}
                                        : LanguageEntry{"Common", 7};
    // Skipped when it is the racial one, so Human does not list Common twice.
    if (out.empty() || out[0].id != faction.id) out.push_back(faction);
}

/// Whether the player's guild rank holds one right. Shared by the nine Can*
/// queries, which differ only in which bit they ask about.
static int luaGuildRight(lua_State* L, uint32_t bit) {
    auto* gh = getGameHandler(L);
    lua_pushboolean(L, (gh && (gh->getPlayerGuildRankRights() & bit)) ? 1 : 0);
    return 1;
}

static int lua_GetNumLanguages(lua_State* L) {
    auto* gh = getGameHandler(L);
    std::vector<LanguageEntry> langs;
    if (gh) collectKnownLanguages(gh->getPlayerRace(), langs);
    if (langs.empty()) langs.push_back({"Common", 7});
    lua_pushnumber(L, static_cast<double>(langs.size()));
    return 1;
}

static int lua_GetLanguageByIndex(lua_State* L) {
    auto* gh = getGameHandler(L);
    std::vector<LanguageEntry> langs;
    if (gh) collectKnownLanguages(gh->getPlayerRace(), langs);
    if (langs.empty()) langs.push_back({"Common", 7});

    const int idx = static_cast<int>(luaL_optnumber(L, 1, 1));
    if (idx < 1 || idx > static_cast<int>(langs.size())) return 0;
    lua_pushstring(L, langs[static_cast<size_t>(idx) - 1].name);
    lua_pushnumber(L, langs[static_cast<size_t>(idx) - 1].id);
    return 2;
}

static int lua_SendChatMessage(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    const char* msg = luaL_checkstring(L, 1);
    const char* chatType = luaL_optstring(L, 2, "SAY");
    // language arg (3) ignored — server determines language
    const char* target = luaL_optstring(L, 4, "");

    std::string typeStr(chatType);
    for (char& c : typeStr) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    game::ChatType ct = game::ChatType::SAY;
    if (typeStr == "SAY")            ct = game::ChatType::SAY;
    else if (typeStr == "YELL")      ct = game::ChatType::YELL;
    else if (typeStr == "PARTY")     ct = game::ChatType::PARTY;
    else if (typeStr == "GUILD")     ct = game::ChatType::GUILD;
    else if (typeStr == "OFFICER")   ct = game::ChatType::OFFICER;
    else if (typeStr == "RAID")      ct = game::ChatType::RAID;
    else if (typeStr == "WHISPER")   ct = game::ChatType::WHISPER;
    else if (typeStr == "BATTLEGROUND") ct = game::ChatType::BATTLEGROUND;

    std::string targetStr(target && *target ? target : "");
    gh->sendChatMessage(ct, msg, targetStr);
    return 0;
}

// SendAddonMessage(prefix, text, chatType, target) — send addon message
static int lua_SendAddonMessage(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    const char* prefix = luaL_checkstring(L, 1);
    const char* text = luaL_checkstring(L, 2);
    const char* chatType = luaL_optstring(L, 3, "PARTY");
    const char* target = luaL_optstring(L, 4, "");

    // Build addon message: prefix + TAB + text, send via the appropriate channel
    std::string typeStr(chatType);
    for (char& c : typeStr) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    game::ChatType ct = game::ChatType::PARTY;
    if (typeStr == "PARTY")           ct = game::ChatType::PARTY;
    else if (typeStr == "RAID")       ct = game::ChatType::RAID;
    else if (typeStr == "GUILD")      ct = game::ChatType::GUILD;
    else if (typeStr == "OFFICER")    ct = game::ChatType::OFFICER;
    else if (typeStr == "BATTLEGROUND") ct = game::ChatType::BATTLEGROUND;
    else if (typeStr == "WHISPER")    ct = game::ChatType::WHISPER;

    // Encode as prefix\ttext (WoW addon message format)
    std::string encoded = std::string(prefix) + "\t" + text;
    std::string targetStr(target && *target ? target : "");
    gh->sendAddonMessage(ct, encoded, targetStr);
    return 0;
}

// RegisterAddonMessagePrefix(prefix) — register prefix for receiving addon messages
static int lua_RegisterAddonMessagePrefix(lua_State* L) {
    const char* prefix = luaL_checkstring(L, 1);
    // Store in a global Lua table for filtering
    lua_getglobal(L, "__WoweeAddonPrefixes");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "__WoweeAddonPrefixes");
    }
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, prefix);
    lua_pop(L, 1);
    lua_pushboolean(L, 1); // success
    return 1;
}

// IsAddonMessagePrefixRegistered(prefix) → boolean
static int lua_IsAddonMessagePrefixRegistered(lua_State* L) {
    const char* prefix = luaL_checkstring(L, 1);
    lua_getglobal(L, "__WoweeAddonPrefixes");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, prefix);
        lua_pushboolean(L, lua_toboolean(L, -1));
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int lua_GetNumFriends(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    // Two values. FrameXML reads the second and adds it to another count on
    // the same line — local _, numWoWOnline = GetNumFriends() — so returning
    // only the total leaves that arithmetic against nil.
    int count = 0, online = 0;
    for (const auto& c : gh->getContacts()) {
        if (!c.isFriend()) continue;
        ++count;
        if (c.status != 0) ++online;   // 0 is offline; 1/2/3 are on, AFK, DND
    }
    lua_pushnumber(L, count);
    lua_pushnumber(L, online);
    return 2;
}

// GetFriendInfo(index) → name, level, class, area, connected, status, note
static int lua_GetFriendInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) {
        return luaReturnNil(L);
    }
    int found = 0;
    for (const auto& c : gh->getContacts()) {
        if (!c.isFriend()) continue;
        if (++found == index) {
            lua_pushstring(L, c.name.c_str());      // 1: name
            lua_pushnumber(L, c.level);              // 2: level

            lua_pushstring(L, c.classId < 12 ? kLuaClasses[c.classId] : "Unknown"); // 3: class
            std::string area;
            if (c.areaId != 0) area = gh->getWhoAreaName(c.areaId);
            lua_pushstring(L, area.c_str());         // 4: area
            lua_pushboolean(L, c.isOnline());        // 5: connected
            lua_pushstring(L, c.status == 2 ? "<AFK>" : (c.status == 3 ? "<DND>" : "")); // 6: status
            lua_pushstring(L, c.note.c_str());       // 7: note
            return 7;
        }
    }
    lua_pushnil(L);
    return 1;
}

// --- Guild API ---

// IsInGuild() → boolean
static int lua_IsInGuild(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushboolean(L, gh && gh->isInGuild());
    return 1;
}

// GetGuildInfo("player") → guildName, guildRankName, guildRankIndex
static int lua_GetGuildInfoFunc(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh || !gh->isInGuild()) { return luaReturnNil(L); }
    lua_pushstring(L, gh->getGuildName().c_str());
    // Get rank name for the player
    const auto& roster = gh->getGuildRoster();
    std::string rankName;
    uint32_t rankIndex = 0;
    for (const auto& m : roster.members) {
        if (m.guid == gh->getPlayerGuid()) {
            rankIndex = m.rankIndex;
            const auto& rankNames = gh->getGuildRankNames();
            if (rankIndex < rankNames.size()) rankName = rankNames[rankIndex];
            break;
        }
    }
    lua_pushstring(L, rankName.c_str());
    lua_pushnumber(L, rankIndex);
    return 3;
}

// GetNumGuildMembers() → totalMembers, onlineMembers
static int lua_GetNumGuildMembers(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    const auto& roster = gh->getGuildRoster();
    int online = 0;
    for (const auto& m : roster.members)
        if (m.online) online++;
    lua_pushnumber(L, roster.members.size());
    lua_pushnumber(L, online);
    return 2;
}

// GetGuildRosterInfo(index) → name, rank, rankIndex, level, class, zone, note, officerNote, online, status, classId
static int lua_GetGuildRosterInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) { return luaReturnNil(L); }
    const auto& roster = gh->getGuildRoster();
    if (index > static_cast<int>(roster.members.size())) { return luaReturnNil(L); }
    const auto& m = roster.members[index - 1];

    lua_pushstring(L, m.name.c_str());                      // 1: name
    const auto& rankNames = gh->getGuildRankNames();
    lua_pushstring(L, m.rankIndex < rankNames.size()
        ? rankNames[m.rankIndex].c_str() : "");              // 2: rank name
    lua_pushnumber(L, m.rankIndex);                          // 3: rankIndex
    lua_pushnumber(L, m.level);                              // 4: level
    lua_pushstring(L, m.classId < 12 ? kLuaClasses[m.classId] : "Unknown"); // 5: class
    std::string zone;
    if (m.zoneId != 0 && m.online) zone = gh->getWhoAreaName(m.zoneId);
    lua_pushstring(L, zone.c_str());                         // 6: zone
    lua_pushstring(L, m.publicNote.c_str());                 // 7: note
    lua_pushstring(L, m.officerNote.c_str());                // 8: officerNote
    lua_pushboolean(L, m.online);                            // 9: online
    lua_pushnumber(L, 0);                                    // 10: status (0=online, 1=AFK, 2=DND)
    lua_pushnumber(L, m.classId);                            // 11: classId (numeric)
    return 11;
}

// GetGuildRosterMOTD() → motd
static int lua_GetGuildRosterMOTD(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { lua_pushstring(L, ""); return 1; }
    lua_pushstring(L, gh->getGuildRoster().motd.c_str());
    return 1;
}

// GetNumIgnores() → count
static int lua_GetNumIgnores(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) { return luaReturnZero(L); }
    int count = 0;
    for (const auto& c : gh->getContacts())
        if (c.isIgnored()) count++;
    lua_pushnumber(L, count);
    return 1;
}

// GetIgnoreName(index) → name
static int lua_GetIgnoreName(lua_State* L) {
    auto* gh = getGameHandler(L);
    int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) { return luaReturnNil(L); }
    int found = 0;
    for (const auto& c : gh->getContacts()) {
        if (!c.isIgnored()) continue;
        if (++found == index) {
            lua_pushstring(L, c.name.c_str());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// --- Talent API ---

// GetNumTalentTabs() → count (usually 3)

/// Battle.net friends: none, and none online. Two values, because FriendsFrame
/// reads them together and does arithmetic on the second the line it is read —
/// numBNetTotal - numBNetOnline.
static int lua_BNGetNumFriends(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 2;
}


// GetAutoCompleteResults(text, include, exclude, max, cursorPos) → names
//
// What the box under a whisper or a mail address offers as you type. Returns
// the names themselves, one value each, because AutoComplete_UpdateResults
// reads them with select rather than out of a table.
//
// The sources are the ones this client actually knows: the group, the guild
// roster and the friends list. Battle.net has none here, so its flag matches
// nothing rather than pretending.
static int lua_GetAutoCompleteResults(lua_State* L) {
    auto* gh = getGameHandler(L);
    std::string prefix = luaL_optstring(L, 1, "");
    const uint32_t include = static_cast<uint32_t>(luaL_optnumber(L, 2, 0xFFFFFFFFu));
    const uint32_t exclude = static_cast<uint32_t>(luaL_optnumber(L, 3, 0));
    const int wanted = static_cast<int>(luaL_optnumber(L, 4, 5));
    if (!gh || wanted <= 0) return 0;

    constexpr uint32_t kInGroup = 0x01, kInGuild = 0x02, kFriend = 0x04, kOnline = 0x20;

    auto lower = [](std::string v) {
        for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return v;
    };
    const std::string want = lower(prefix);

    // One name may be a guildmate and a friend both, so what it is comes first
    // and the flags are merged before anything is decided about it.
    std::map<std::string, uint32_t> byName;
    auto note = [&](const std::string& name, uint32_t flag, bool online) {
        if (name.empty()) return;
        if (!want.empty() && lower(name).compare(0, want.size(), want) != 0) return;
        byName[name] |= flag | (online ? kOnline : 0u);
    };

    for (const auto& mbr : gh->getPartyData().members) note(mbr.name, kInGroup, mbr.isOnline != 0);
    for (const auto& mbr : gh->getGuildRoster().members) note(mbr.name, kInGuild, mbr.online);
    for (const auto& c : gh->getContacts()) {
        if (c.isFriend()) note(c.name, kFriend, c.isOnline());
    }

    int pushed = 0;
    for (const auto& [name, flags] : byName) {
        if (pushed >= wanted) break;
        if ((flags & exclude) != 0) continue;
        if (include != 0 && (flags & include) == 0) continue;
        lua_pushstring(L, name.c_str());
        ++pushed;
    }
    return pushed;
}



/// Which row the friends and ignore lists have selected. The client has no
/// opinion about either; they are what the player last clicked.
static int& selectedFriend() { static int v = 0; return v; }
static int& selectedIgnore() { static int v = 0; return v; }

// SendSystemMessage(text) — put a line in the chat as the client itself would
static int lua_SendSystemMessage(lua_State* L) {
    auto* gh = getGameHandler(L);
    const char* msg = luaL_optstring(L, 1, "");
    if (gh && msg && *msg) gh->addSystemChatMessage(msg);
    return 0;
}

// AddOrRemoveFriend(name, note) / AddOrDelIgnore(name) — the slash commands
//
// /friend and /ignore both toggle: naming someone already on the list takes
// them off it. That is what the "OrRemove" and "OrDel" in the names mean, and
// getting it backwards would remove the friend the player was trying to add.
static int lua_AddOrRemoveFriend(lua_State* L) {
    auto* gh = getGameHandler(L);
    const std::string name = luaL_optstring(L, 1, "");
    const std::string note = luaL_optstring(L, 2, "");
    if (!gh || name.empty()) return 0;
    for (const auto& c : gh->getContacts()) {
        if (c.isFriend() && c.name == name) { gh->removeFriend(name); return 0; }
    }
    gh->addFriend(name, note);
    return 0;
}

static int lua_AddOrDelIgnore(lua_State* L) {
    auto* gh = getGameHandler(L);
    const std::string name = luaL_optstring(L, 1, "");
    if (!gh || name.empty()) return 0;
    for (const auto& c : gh->getContacts()) {
        if (c.isIgnored() && c.name == name) { gh->removeIgnore(name); return 0; }
    }
    gh->addIgnore(name);
    return 0;
}

// --- The confirmations that answer another player ---
//
// Every one of these popups already appears: the client fires
// PARTY_INVITE_REQUEST, GUILD_INVITE_REQUEST, RESURRECT_REQUEST and
// DUEL_REQUESTED, and StaticPopup draws them. None of the buttons did anything,
// because the functions behind them were never bound — so an invitation could
// be seen and not taken.
static int lua_AcceptGroup(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->acceptGroupInvite();
    return 0;
}
static int lua_DeclineGroup(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->declineGroupInvite();
    return 0;
}
static int lua_AcceptGuild(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->acceptGuildInvite();
    return 0;
}
static int lua_DeclineGuild(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->declineGuildInvite();
    return 0;
}
static int lua_AcceptResurrect(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->acceptResurrect();
    return 0;
}
static int lua_DeclineResurrect(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->declineResurrect();
    return 0;
}
static int lua_AcceptDuel(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->acceptDuel();
    return 0;
}
// A duel is refused by forfeiting it, which is the only way this client has to
// say no and is what the server reads as a decline before it starts.
static int lua_CancelDuel(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->forfeitDuel();
    return 0;
}

// --- Reputation ---
//
// The panel is a list of factions with a bar showing how far through the
// current standing the player is. The bands are the game's own and fixed:
// hated starts at -42000 and exalted at 42000, with the rest between.
namespace {

/// The standing a value falls in, as the interface wants it: the band's number
/// and the ends of the bar drawn for it. The thresholds themselves live in
/// game/reputation_standing.hpp, shared with the panel this client draws.
struct Standing {
    int id = 4;
    int32_t barMin = 0;
    int32_t barMax = 3000;
};

Standing standingFor(int32_t value) {
    const auto& band = game::reputationStandingFor(value);
    // The bar's top is one past the last value still at this standing, so a
    // faction sitting at the ceiling reads as full rather than as over.
    return {band.id, band.floor, band.ceiling + 1};
}

/// Which row the panel has selected. The client has no opinion — it is what
/// the player last clicked — so it lives here rather than being invented.
int& selectedFaction() {
    static int selected = 1;
    return selected;
}

}  // namespace

// --- Gossip quest lists ---
//
// Which of an NPC's quests are on offer and which are already taken is decided
// by the icon the server sent, not by anything the client tracks. The values
// are the server's QUEST_STATUS enum and are the same ones quest_handler.cpp
// classifies by — kept in step with it deliberately, since a quest sorted into
// the wrong list is offered twice or not at all.
namespace {

/// Daily quests take a different icon in the list. Protocol-defined, WotLK.
constexpr uint32_t kQuestFlagsDaily = 0x1000;

bool gossipQuestIsAvailable(uint32_t icon)   { return icon == 2 || icon == 7 || icon == 8; }
bool gossipQuestIsCompletable(uint32_t icon) { return icon == 5 || icon == 6 || icon == 10; }
bool gossipQuestIsIncomplete(uint32_t icon)  { return icon == 3 || icon == 4; }
/// Taken already, finished or not — which is one list in the gossip window.
bool gossipQuestIsActive(uint32_t icon) {
    return gossipQuestIsIncomplete(icon) || gossipQuestIsCompletable(icon);
}

/// The i-th quest of one kind, or null past the end. One walk shared by
/// everything that indexes these lists, so they cannot disagree about which
/// quest is second.
const game::GossipQuestItem* gossipQuestAt(lua_State* L, bool available, int index) {
    auto* gh = getGameHandler(L);
    if (!gh || index < 1) return nullptr;
    int seen = 0;
    for (const auto& q : gh->getCurrentGossip().quests) {
        const bool matches = available ? gossipQuestIsAvailable(q.questIcon)
                                       : gossipQuestIsActive(q.questIcon);
        if (matches && ++seen == index) return &q;
    }
    return nullptr;
}

int countGossipQuests(lua_State* L, bool available) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    int n = 0;
    for (const auto& q : gh->getCurrentGossip().quests) {
        if (available ? gossipQuestIsAvailable(q.questIcon)
                      : gossipQuestIsActive(q.questIcon)) ++n;
    }
    return n;
}

int pushGossipQuestTitle(lua_State* L, bool available) {
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    const auto* q = gossipQuestAt(L, available, index);
    lua_pushstring(L, q ? q->title.c_str() : "");
    return 1;
}

/// Index into whichever of the two lists the caller means, then ask for that
/// quest by id — the position in the filtered list is not the position in the
/// packet, so the id is the only thing safe to send.
int selectGossipQuestAt(lua_State* L, bool available) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || index < 1) return 0;
    int seen = 0;
    for (const auto& q : gh->getCurrentGossip().quests) {
        const bool matches = available ? gossipQuestIsAvailable(q.questIcon)
                                       : gossipQuestIsActive(q.questIcon);
        if (!matches) continue;
        if (++seen != index) continue;
        gh->selectGossipQuest(q.questId);
        return 0;
    }
    return 0;
}

}  // namespace

// --- GM tickets ---
//
// GetGMTicket asks; it does not answer. The reply arrives as UPDATE_TICKET,
// fired from the SMSG_GMTICKET_GETTICKET handler, with no arguments at all when
// there is no ticket — which is how the help frame tells the two apart.

// GetGMTicket() → asks the server what ticket this player has open
static int lua_GetGMTicket(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->requestGmTicket();
    return 0;
}

// NewGMTicket(text, needResponse) → opens one
static int lua_NewGMTicket(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return 0;
    const char* text = luaL_checkstring(L, 1);
    // needResponse is not carried by the packet this client sends, so it is
    // read and dropped rather than quietly changing what is submitted.
    gh->submitGmTicket(text);
    return 0;
}

// DeleteGMTicket() → withdraws it
static int lua_DeleteGMTicket(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->deleteGmTicket();
    return 0;
}

// --- Icon pickers ---
//
// The macro frame and the guild bank tab dialog are both grids over the same
// list of icons: how many, then one at a time by index. WoW numbers them from
// one, and the first is the question mark every unset macro shows.

static const std::vector<std::string>* iconList(lua_State* L) {
    auto* services = getLuaServices(L);
    if (!services || !services->listIconTextures) return nullptr;
    return &services->listIconTextures();
}

static int lua_GetNumMacroIcons(lua_State* L) {
    const auto* icons = iconList(L);
    lua_pushnumber(L, icons ? static_cast<double>(icons->size()) : 0.0);
    return 1;
}

// GetMacroIconInfo(index) → a texture path, or nil past the end
static int lua_GetMacroIconInfo(lua_State* L) {
    const auto* icons = iconList(L);
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!icons || index < 1 || index > static_cast<int>(icons->size())) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, (*icons)[static_cast<size_t>(index - 1)].c_str());
    return 1;
}

// ── Guild and arena charters ──────────────────────────────────────────
//
// Every one of these was missing while the client underneath had the whole
// flow: it parses the signature list, tracks who has signed, and its own popup
// signs and turns in. SMSG_PETITION_SHOW_SIGNATURES already fires PETITION_SHOW,
// so the interface's frame opened on cue and then raised on the first call it
// made, which is worse than never opening.
//
// This client only ever holds a guild charter — arena charters are bought
// through a registrar it does not implement — so the type is reported as
// "guild" rather than guessed from the signature count. Saying "arena" wrongly
// would relabel the whole frame and ask for a team size that is not there.

/// The petition currently being shown, or nullptr when there is none.
static const game::PetitionInfo* shownPetition(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (!gh) return nullptr;
    const auto& info = gh->getPetitionInfo();
    return info.petitionGuid != 0 ? &info : nullptr;
}

// GetPetitionInfo() → type, title, bodyText, maxSignatures, originatorName,
//                     isOriginator, minSignatures
static int lua_GetPetitionInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto* info = shownPetition(L);
    if (!gh || !info) return 0;
    const bool isOwner = info->ownerGuid == gh->getPlayerGuid();
    const std::string& owner = gh->lookupName(info->ownerGuid);
    lua_pushstring(L, "guild");
    lua_pushstring(L, info->guildName.c_str());
    lua_pushstring(L, "");                                   // body text: not sent
    lua_pushnumber(L, info->signaturesRequired);
    lua_pushstring(L, owner.empty() ? "Unknown" : owner.c_str());
    lua_pushboolean(L, isOwner ? 1 : 0);
    lua_pushnumber(L, info->signaturesRequired);
    return 7;
}

// CanSignPetition() → whether the Sign button should be live.
//
// The originator cannot sign their own charter, and neither can someone who
// already has. Both are answered from the signature list rather than assumed,
// so the button greys out the moment the server confirms the signature.
static int lua_CanSignPetition(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto* info = shownPetition(L);
    if (!gh || !info) { lua_pushboolean(L, 0); return 1; }
    const uint64_t me = gh->getPlayerGuid();
    if (info->ownerGuid == me) { lua_pushboolean(L, 0); return 1; }
    for (const auto& sig : info->signatures) {
        if (sig.playerGuid == me) { lua_pushboolean(L, 0); return 1; }
    }
    lua_pushboolean(L, info->signatureCount < info->signaturesRequired ? 1 : 0);
    return 1;
}

static int lua_GetNumPetitionNames(lua_State* L) {
    const auto* info = shownPetition(L);
    lua_pushnumber(L, info ? static_cast<double>(info->signatures.size()) : 0.0);
    return 1;
}

// GetPetitionNameInfo(index) → the signer's name.
//
// Only GUIDs come down the wire, so the name is resolved the same way this
// client's own popup resolves it. A signer who has never been seen has no name
// to give, and the row says so rather than going blank.
static int lua_GetPetitionNameInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto* info = shownPetition(L);
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh || !info || index < 1 || index > static_cast<int>(info->signatures.size())) {
        lua_pushnil(L);
        return 1;
    }
    const std::string& name = gh->lookupName(info->signatures[static_cast<size_t>(index - 1)].playerGuid);
    lua_pushstring(L, name.empty() ? "Unknown" : name.c_str());
    return 1;
}

static int lua_SignPetition(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (const auto* info = shownPetition(L); gh && info) gh->signPetition(info->petitionGuid);
    return 0;
}

// OfferPetition() → hand the charter to the current target to sign.
//
// The interface offers this only to the originator, which matches the server:
// nobody else can present someone else's charter.
static int lua_OfferPetition(lua_State* L) {
    auto* gh = getGameHandler(L);
    const auto* info = shownPetition(L);
    if (!gh || !info) return 0;
    const uint64_t target = gh->getTargetGuid();
    if (target == 0) {
        gh->addUIError("You have no target.");
        return 0;
    }
    gh->offerPetition(info->petitionGuid, target);
    return 0;
}

static int lua_ClosePetition(lua_State* L) {
    if (auto* gh = getGameHandler(L)) gh->clearPetitionSignaturesUI();
    return 0;
}

// GetNumGuildEvents() / GetGuildEventInfo(i) → the guild's event log.
//
// The log arrives as MSG_GUILD_EVENT_LOG_QUERY and is parsed against
// AzerothCore's own writer rather than guessed — see handleGuildEventLog.
//
// GetGuildEventInfo answers type, player1, player2, rank, year, month, day,
// hour. The wire sends an age in seconds, not a date, so the four time fields
// are derived from it the way the interface expects them: year and month count
// backwards from now in whole units, day is days-ago within the month, hour is
// hours-ago within the day. That is what FriendsFrame formats, and it is why a
// log entry reads "3 days ago" rather than a calendar date.
static int lua_GetNumGuildEvents(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? static_cast<double>(gh->getGuildEventLog().size()) : 0.0);
    return 1;
}

static int lua_GetGuildEventInfo(lua_State* L) {
    auto* gh = getGameHandler(L);
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    if (!gh) return luaReturnNil(L);
    const auto& log = gh->getGuildEventLog();
    if (index < 1 || index > static_cast<int>(log.size())) return luaReturnNil(L);
    const auto& e = log[static_cast<size_t>(index - 1)];

    static const char* kTypes[] = {
        "", "invite", "join", "promote", "demote", "remove", "quit"
    };
    lua_pushstring(L, (e.type < 7) ? kTypes[e.type] : "");
    lua_pushstring(L, gh->lookupName(e.playerGuid).c_str());
    lua_pushstring(L, e.otherGuid ? gh->lookupName(e.otherGuid).c_str() : "");
    lua_pushnumber(L, e.newRank);

    // Whole units of age, largest first, each within its parent.
    const uint32_t secs = e.secondsAgo;
    lua_pushnumber(L, secs / (365u * 86400u));            // years ago
    lua_pushnumber(L, (secs / (30u * 86400u)) % 12u);     // months within the year
    lua_pushnumber(L, (secs / 86400u) % 30u);             // days within the month
    lua_pushnumber(L, (secs / 3600u) % 24u);              // hours within the day
    return 8;
}

void registerSocialLuaAPI(lua_State* L) {
    static const struct { const char* name; lua_CFunction func; } api[] = {
                {"BNGetNumFriends",     lua_BNGetNumFriends},
                {"GetGMTicket",         lua_GetGMTicket},
                // UpdateGMTicket(text) — rewrite the ticket already open.
                // Creating one carries the player's position because a new
                // ticket records where it was raised; editing does not move it,
                // and the request is the text alone.
                {"UpdateGMTicket", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* text = luaL_optstring(L, 1, "");
            if (gh && text && *text) gh->updateGmTicket(text);
            return 0;
        }},
                {"GetNumMacroIcons",    lua_GetNumMacroIcons},
                {"GetMacroIconInfo",    lua_GetMacroIconInfo},
                {"GetNumGuildEvents",   lua_GetNumGuildEvents},
                {"GetGuildEventInfo",   lua_GetGuildEventInfo},
                {"GetPetitionInfo",     lua_GetPetitionInfo},
                {"CanSignPetition",     lua_CanSignPetition},
                {"GetNumPetitionNames", lua_GetNumPetitionNames},
                {"GetPetitionNameInfo", lua_GetPetitionNameInfo},
                {"SignPetition",        lua_SignPetition},
                {"OfferPetition",       lua_OfferPetition},
                {"ClosePetition",       lua_ClosePetition},
                // The guild bank tab dialog picks from the same icons under a
                // name of its own.
                {"GetNumMacroItemIcons", lua_GetNumMacroIcons},
                {"GetMacroItemIconInfo", lua_GetMacroIconInfo},
                {"NewGMTicket",         lua_NewGMTicket},
                {"DeleteGMTicket",      lua_DeleteGMTicket},
                // The long-form guild information, which is a different field
                // from the message of the day and has its own opcode. The
                // roster already carries the current text.
                {"GetGuildInfoText", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return luaReturnNil(L);
            lua_pushstring(L, gh->getGuildRoster().guildInfo.c_str());
            return 1;
        }},
                {"SetGuildInfoText", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* text = luaL_optstring(L, 1, "");
            if (gh && text) gh->setGuildInfoText(text);
            return 0;
        }},
                // Whether the roster lists members who are offline. A display
                // choice the client does not keep, and the roster it is given
                // holds everyone either way — so the list shows them, which is
                // what answering true says.
                {"GetGuildRosterShowOffline", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
                {"SetGuildRosterShowOffline", [](lua_State* L) -> int { (void)L; return 0; }},
                // The who list arrives in the server's order and is shown in
                // it; there is no second order to sort into.
                {"SortWho", [](lua_State* L) -> int { (void)L; return 0; }},
                // Voice again: nothing can be muted, so nothing is added to the
                // list and there is no list to update.
                // The last two the social frame reaches for, both belonging to
                // systems this server has no counterpart to: Battle.net
                // presence, and picking which voice channel is live.
                {"GetAutoCompletePresenceID", [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"SetActiveVoiceChannelBySessionID", [](lua_State* L) -> int { (void)L; return 0; }},
                {"AddMute",         [](lua_State* L) -> int { (void)L; return 0; }},
                {"MutedList_Update", [](lua_State* L) -> int { (void)L; return 0; }},

                // ---- Guild rank permissions ----
                //
                // SMSG_GUILD_ROSTER carries a rights bitmask per rank and names
                // which rank each member holds. Both were already parsed and
                // stored; nothing read them, so every one of these answered
                // through the missing-API fallback and the guild panel offered
                // actions the player may not have.
                //
                // The bits are the server's own: invite 0x10, remove 0x20,
                // promote 0x80, demote 0x100, set MOTD 0x1000, edit public note
                // 0x2000, view officer note 0x4000, edit officer note 0x8000.
                {"CanGuildInvite",      [](lua_State* L) -> int { return luaGuildRight(L, 0x00000010u); }},
                {"CanGuildRemove",      [](lua_State* L) -> int { return luaGuildRight(L, 0x00000020u); }},
                {"CanGuildPromote",     [](lua_State* L) -> int { return luaGuildRight(L, 0x00000080u); }},
                {"CanGuildDemote",      [](lua_State* L) -> int { return luaGuildRight(L, 0x00000100u); }},
                {"CanEditMOTD",         [](lua_State* L) -> int { return luaGuildRight(L, 0x00001000u); }},
                {"CanEditPublicNote",   [](lua_State* L) -> int { return luaGuildRight(L, 0x00002000u); }},
                {"CanViewOfficerNote",  [](lua_State* L) -> int { return luaGuildRight(L, 0x00004000u); }},
                {"CanEditOfficerNote",  [](lua_State* L) -> int { return luaGuildRight(L, 0x00008000u); }},
                {"CanEditGuildInfo",    [](lua_State* L) -> int { return luaGuildRight(L, 0x00010000u); }},

                // GetGuildRosterLastOnline(index) → how long since that member
                // was last seen, as years, months, days, hours.
                {"GetGuildRosterLastOnline", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int idx = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh || idx < 1) return luaReturnNil(L);
            const auto& roster = gh->getGuildRoster();
            if (idx > static_cast<int>(roster.members.size())) return luaReturnNil(L);
            // The roster reports it in days, fractionally.
            const float days = roster.members[static_cast<size_t>(idx) - 1].lastOnline;
            const int totalDays = static_cast<int>(days);
            lua_pushnumber(L, totalDays / 365);          // years
            lua_pushnumber(L, (totalDays % 365) / 30);   // months
            lua_pushnumber(L, (totalDays % 365) % 30);   // days
            lua_pushnumber(L, static_cast<int>((days - totalDays) * 24.0f)); // hours
            return 4;
        }},
                // The guild event log is a separate request this client never
                // makes, so there are no entries to describe.
                {"GetGuildEventInfo", [](lua_State* L) -> int { return luaReturnNil(L); }},
                // One realm, so everyone is on it.
                {"UnitIsSameServer", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
                // Recruit-a-Friend summoning, which needs a linked account.
                {"CanSummonFriend", luaReturnFalse},
                {"SummonFriend",    [](lua_State* L) -> int { (void)L; return 0; }},
                // The voice mute list, which cannot have entries without voice.
                {"GetSelectedMute", [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"SetSelectedMute", [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetMuteName",     [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"DelMute",         [](lua_State* L) -> int { (void)L; return 0; }},
                // GetGuildBankTabPermissions(tab) → canView, canDeposit,
                //   canUpdateText, withdrawPerDay
                //
                // The roster carries these per rank per tab; they were read
                // past to stay aligned with the packet and discarded. The bits
                // are view 0x01, deposit 0x02, update text 0x04.
                {"GetGuildBankTabPermissions", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int tab = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh || tab < 1 || tab > 6) return luaReturnNil(L);
            const auto& roster = gh->getGuildRoster();
            const int sel = gh->getSelectedGuildRank();
            if (sel < 1 || sel > static_cast<int>(roster.ranks.size())) return luaReturnNil(L);
            const auto& rank = roster.ranks[static_cast<size_t>(sel) - 1];
            const uint32_t flags = rank.bankTabRights[static_cast<size_t>(tab) - 1];
            lua_pushboolean(L, (flags & 0x01u) ? 1 : 0);   // 1: canView
            lua_pushboolean(L, (flags & 0x02u) ? 1 : 0);   // 2: canDeposit
            lua_pushboolean(L, (flags & 0x04u) ? 1 : 0);   // 3: canUpdateText
            lua_pushnumber(L, rank.bankTabSlotsPerDay[static_cast<size_t>(tab) - 1]);
            return 4;
        }},
                // GetGuildBankWithdrawLimit() → gold per day for the selected
                // rank. The roster calls it goldLimit and it was already parsed.
                {"GetGuildBankWithdrawLimit",  [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnumber(L, 0); return 1; }
            const auto& roster = gh->getGuildRoster();
            const int sel = gh->getSelectedGuildRank();
            if (sel < 1 || sel > static_cast<int>(roster.ranks.size())) { lua_pushnumber(L, 0); return 1; }
            lua_pushnumber(L, roster.ranks[static_cast<size_t>(sel) - 1].goldLimit);
            return 1;
        }},
                // The three that stage the bank half of a rank edit. Bits are
                // view 0x01, deposit 0x02, update text 0x04 — the same the
                // roster reports.
                {"SetGuildBankTabPermissions", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int tab  = static_cast<int>(luaL_optnumber(L, 1, 0));
            const int perm = static_cast<int>(luaL_optnumber(L, 2, 0));
            const bool on  = lua_toboolean(L, 3) != 0;
            if (!gh || tab < 1 || tab > 6 || perm < 1 || perm > 3) return 0;
            const uint32_t bit = 1u << (perm - 1);
            auto& p = gh->pendingGuildRankRef();
            if (on) p.tabRights[static_cast<size_t>(tab) - 1] |= bit;
            else    p.tabRights[static_cast<size_t>(tab) - 1] &= ~bit;
            return 0;
        }},
                {"SetGuildBankTabWithdraw", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int tab = static_cast<int>(luaL_optnumber(L, 1, 0));
            const auto n  = static_cast<uint32_t>(luaL_optnumber(L, 2, 0));
            if (!gh || tab < 1 || tab > 6) return 0;
            gh->pendingGuildRankRef().tabSlots[static_cast<size_t>(tab) - 1] = n;
            return 0;
        }},
                {"SetGuildBankWithdrawLimit", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->pendingGuildRankRef().goldLimit =
                static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            return 0;
        }},

                // ---- Mail: invoices, stationery, and the spam report ----
                //
                // GetInboxInvoiceInfo(id) → invoiceType, itemName, playerName,
                //   bid, buyout, deposit, consignment, moneyDelay, etaHour, etaMin
                //
                // Ten nils. The auction house sends these as mail and nothing
                // here parses the invoice body, so there is no invoice to
                // describe — and the caller compares the first against a string,
                // which nil fails cleanly.
                {"GetInboxInvoiceInfo", [](lua_State* L) -> int {
            for (int i = 0; i < 10; ++i) lua_pushnil(L);
            return 10;
        }},
                // Stationery is the letterhead a mail is written on. The list
                // is empty — GetNumStationeries answers zero — so the picker
                // has no rows and these are the calls around it.
                {"GetStationeryInfo", [](lua_State* L) -> int { return luaReturnNil(L); }},
                {"SelectStationery", [](lua_State* L) -> int { (void)L; return 0; }},
                // Guarded with `if ( texture )` before being pasted into a
                // path, so nil leaves the default parchment rather than
                // building a texture name out of nothing.
                {"GetSelectedStationeryTexture", [](lua_State* L) -> int { return luaReturnNil(L); }},
                // Reporting a mail as spam needs a GM channel this client does
                // not have, so no mail can be complained about.
                {"CanComplainInboxItem", luaReturnFalse},
                // Turning a letter into a keepable item. Called from the mail
                // frame's XML rather than its Lua, which is why a scan of the
                // Lua alone never reported it.
                {"TakeInboxTextItem", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const auto id = static_cast<uint32_t>(luaL_optnumber(L, 1, 0));
            if (gh && id) gh->takeInboxTextItem(id);
            return 0;
        }},
                // Tells the client the send-mail tab is open, so it can hold a
                // draft. Nothing here holds one.
                {"SetSendMailShowing", [](lua_State* L) -> int { (void)L; return 0; }},

                // ---- The help frame's GM requests ----
                //
                // There is no GM to reach: tickets are submitted through
                // NewGMTicket, and these three are the paths beside it — asking
                // for a lag report, saying an answer did not help, and polling
                // whether a GM is available. Answered rather than left missing
                // because the help frame calls the last one from its OnLoad.
                // Stuck() — the help frame's "I'm stuck" button.
                //
                // In WoW this casts the Stuck spell, which the server answers
                // by moving the character to a graveyard. Cast rather than
                // stubbed, because the button hides the help frame straight
                // after and a no-op would look like the request was made.
                {"Stuck", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->castSpell(7355);
            return 0;
        }},
                {"GMReportLag", [](lua_State* L) -> int { (void)L; return 0; }},
                {"GMResponseNeedMoreHelp", [](lua_State* L) -> int { (void)L; return 0; }},
                // GetGMStatus() — ask whether the ticket queue is open.
                //
                // A request, not a getter: the answer comes back as
                // UPDATE_GM_STATUS. The help frame calls this from OnShow and
                // waits for the event, so a no-op here meant it waited forever
                // and kept whatever it was last told.
                {"GetGMStatus", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->requestGmSystemStatus();
            return 0;
        }},

                // ---- Guild rank editing ----
                {"GuildControlGetRankName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int idx = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (!gh || idx < 1) return luaReturnNil(L);
            const auto& ranks = gh->getGuildRankNames();
            if (idx > static_cast<int>(ranks.size())) return luaReturnNil(L);
            lua_pushstring(L, ranks[static_cast<size_t>(idx) - 1].c_str());
            return 1;
        }},
                // GuildControlSetRank(index) — which rank the panel is editing.
                // Remembered rather than sent: GuildControlGetRankFlags below
                // takes no argument, so this is the only thing that says which
                // rank it is being asked about.
                {"GuildControlSetRank", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int idx = static_cast<int>(luaL_optnumber(L, 1, 0));
            if (gh && idx >= 1) gh->setSelectedGuildRank(idx);
            return 0;
        }},
                // GuildControlGetRankFlags() → one boolean per permission
                // checkbox, in the panel's order.
                //
                // That order is not the bit order and the two must not be
                // confused: the panel lists Promote and Demote fifth and sixth
                // while their bits are 0x80 and 0x100, above Invite's 0x10 and
                // Remove's 0x20 — so walking the mask in order would tick the
                // wrong four boxes.
                //
                // Thirteen, not seventeen. Fourteen through seventeen are guild
                // event and guild bank rights carried in a mask this client
                // does not parse, and the consumer loops select('#', ...), so a
                // short answer leaves those boxes untouched rather than
                // claiming they are clear.
                {"GuildControlGetRankFlags", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const auto& roster = gh->getGuildRoster();
            const int sel = gh->getSelectedGuildRank();
            if (sel < 1 || sel > static_cast<int>(roster.ranks.size())) return 0;
            const uint32_t rights = roster.ranks[static_cast<size_t>(sel) - 1].rights;
            static constexpr uint32_t kOrder[13] = {
                0x00000001u,  // 1  Guildchat Listen
                0x00000002u,  // 2  Guildchat Speak
                0x00000004u,  // 3  Officerchat Listen
                0x00000008u,  // 4  Officerchat Speak
                0x00000080u,  // 5  Promote
                0x00000100u,  // 6  Demote
                0x00000010u,  // 7  Invite Member
                0x00000020u,  // 8  Remove Member
                0x00001000u,  // 9  Set MOTD
                0x00002000u,  // 10 Edit Public Note
                0x00004000u,  // 11 View Officer Note
                0x00008000u,  // 12 Edit Officer Note
                0x00010000u,  // 13 Modify Guild Info
            };
            for (uint32_t bit : kOrder) lua_pushboolean(L, (rights & bit) ? 1 : 0);
            return 13;
        }},
                {"GuildControlAddRank", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_optstring(L, 1, "");
            if (gh && name && *name) gh->addGuildRank(name);
            return 0;
        }},
                {"GuildControlDelRank", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->delGuildRank();
            return 0;
        }},
                // GuildControlSetRankFlag(checkboxIndex, checked) — stage one
                // permission. Called from the checkbox's own OnClick in the
                // XML, which is why it never appeared in a scan of the Lua.
                //
                // Same order as GuildControlGetRankFlags reads them, and for
                // the same reason: the panel's order is not the bit order.
                {"GuildControlSetRankFlag", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int idx = static_cast<int>(luaL_optnumber(L, 1, 0));
            const bool on = lua_toboolean(L, 2) != 0;
            if (!gh || idx < 1 || idx > 13) return 0;
            static constexpr uint32_t kOrder[13] = {
                0x00000001u, 0x00000002u, 0x00000004u, 0x00000008u,
                0x00000080u, 0x00000100u, 0x00000010u, 0x00000020u,
                0x00001000u, 0x00002000u, 0x00004000u, 0x00008000u,
                0x00010000u,
            };
            const uint32_t bit = kOrder[static_cast<size_t>(idx) - 1];
            auto& p = gh->pendingGuildRankRef();
            if (on) p.rights |= bit; else p.rights &= ~bit;
            return 0;
        }},
                // GuildControlSaveRank(name) — commit the staged rank.
                //
                // One packet rewrites the rank whole: rights, name, gold per
                // day and all six bank tabs. A field left at zero is not
                // "unchanged", it is revoked — which is why the staging copy is
                // seeded from the rank as it stands when the panel selects it,
                // and only what the panel edits moves.
                {"GuildControlSaveRank", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_optstring(L, 1, "");
            if (gh && name && *name) gh->saveGuildRank(name);
            return 0;
        }},
                // JoinPermanentChannel(name, password, frameId, permanent)
                //   → zoneChannel, channelName
                //
                // The same join as JoinChannelByName, plus the chat frame the
                // channel should show in. The frame binding is handled entirely
                // in FrameXML, so only the join happens here.
                //
                // The first value says whether this is one of the automatic
                // zone channels; nothing joined by name is, so it is nil rather
                // than a zero that would read as true.
                {"JoinPermanentChannel", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_optstring(L, 1, "");
            const char* pass = luaL_optstring(L, 2, "");
            if (!gh || !name || !*name) return luaReturnNil(L);
            gh->joinChannel(name, pass ? pass : "");
            lua_pushnil(L);
            lua_pushstring(L, name);
            return 2;
        }},
                // GetChannelList() → id, name, disabled, repeated per channel.
                {"GetChannelList", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const auto& joined = gh->getJoinedChannels();
            for (size_t i = 0; i < joined.size(); ++i) {
                lua_pushnumber(L, static_cast<lua_Number>(i + 1));
                lua_pushstring(L, joined[i].c_str());
                lua_pushboolean(L, 0);   // disabled
            }
            return static_cast<int>(joined.size() * 3);
        }},
                // Channel moderation is not modelled, so the player owns none.
                {"IsDisplayChannelOwner", luaReturnFalse},
                // ChangeChatColor(type, r, g, b) — recolour one kind of chat
                // message.
                //
                // The colour lives in FrameXML's ChatTypeInfo table, which its
                // UPDATE_CHAT_COLOR handler fills from the event's own
                // arguments, so firing the event is the whole of the change.
                //
                // Three decimal places, because an event argument is only read
                // back as a number when it is under twelve characters — a full
                // float would arrive as a string and be assigned straight into
                // a colour field.
                {"ChangeChatColor", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* type = luaL_optstring(L, 1, "");
            if (!gh || !type || !*type) return 0;
            char r[16], g[16], b[16];
            std::snprintf(r, sizeof(r), "%.3f", luaL_optnumber(L, 2, 1.0));
            std::snprintf(g, sizeof(g), "%.3f", luaL_optnumber(L, 3, 1.0));
            std::snprintf(b, sizeof(b), "%.3f", luaL_optnumber(L, 4, 1.0));
            gh->fireAddonEvent("UPDATE_CHAT_COLOR", {type, r, g, b});
            return 0;
        }},
                // UnitIsRaidOfficer(unit) → whether that unit is an assistant.
                // The bit rides along with the group list, same as the main
                // tank and main assist flags read above.
                {"UnitIsRaidOfficer", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* unit = luaL_optstring(L, 1, "");
            if (!gh || !unit || !*unit) return luaReturnFalse(L);
            std::string uid(unit);
            toLowerInPlace(uid);
            const uint64_t guid = resolveUnitGuid(gh, uid);
            if (guid == 0) return luaReturnFalse(L);
            for (const auto& mem : gh->getPartyData().members) {
                if (mem.guid == guid) { lua_pushboolean(L, (mem.flags & 0x01) ? 1 : 0); return 1; }
            }
            return luaReturnFalse(L);
        }},
                // ---- Loot rules and party assignments ----
                //
                // SetLootMethod(method, masterPlayer) and SetLootThreshold(q)
                // both write CMSG_LOOT_METHOD, which carries all three settings
                // at once — so each resends the other two as they stand rather
                // than clearing them.
                {"SetLootMethod", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            std::string method(luaL_optstring(L, 1, ""));
            toLowerInPlace(method);
            uint8_t m = 0;
            if (method == "roundrobin")           m = 1;
            else if (method == "master")          m = 2;
            else if (method == "group")           m = 3;
            else if (method == "needbeforegreed") m = 4;

            const auto& pd = gh->getPartyData();
            uint64_t masterGuid = 0;
            if (m == 2) {
                // The second argument is a unit id or a name, depending on who
                // is calling — the loot dropdown passes a name.
                const char* who = luaL_optstring(L, 2, nullptr);
                if (who && *who) {
                    for (const auto& mem : pd.members) {
                        if (mem.name == who) { masterGuid = mem.guid; break; }
                    }
                    if (masterGuid == 0) masterGuid = gh->getPlayerGuid();
                }
            }
            gh->setLootMethod(m, masterGuid, pd.lootThreshold);
            return 0;
        }},
                {"SetLootThreshold", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const auto quality = static_cast<uint8_t>(luaL_optnumber(L, 1, 2));
            const auto& pd = gh->getPartyData();
            gh->setLootMethod(pd.lootMethod, pd.looterGuid, quality);
            return 0;
        }},
                // GetPartyAssignment(assignment, unit) → whether that unit
                // holds it. The flags come with the group list.
                {"GetPartyAssignment", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            std::string what(luaL_optstring(L, 1, ""));
            const char* unit = luaL_optstring(L, 2, "");
            toLowerInPlace(what);
            if (!gh || !unit || !*unit) return luaReturnFalse(L);
            const uint8_t bit = (what == "maintank") ? 0x02
                              : (what == "mainassist") ? 0x04 : 0x00;
            if (bit == 0) return luaReturnFalse(L);
            std::string uid(unit);
            toLowerInPlace(uid);
            const uint64_t guid = resolveUnitGuid(gh, uid);
            if (guid == 0) return luaReturnFalse(L);
            for (const auto& mem : gh->getPartyData().members) {
                if (mem.guid == guid) { lua_pushboolean(L, (mem.flags & bit) ? 1 : 0); return 1; }
            }
            return luaReturnFalse(L);
        }},
                {"SetPartyAssignment", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            std::string what(luaL_optstring(L, 1, ""));
            const char* unit = luaL_optstring(L, 2, "");
            toLowerInPlace(what);
            if (!gh || !unit || !*unit) return 0;
            const int assignment = (what == "maintank") ? 0 : (what == "mainassist") ? 1 : -1;
            if (assignment < 0) return 0;
            std::string uid(unit);
            toLowerInPlace(uid);
            const uint64_t guid = resolveUnitGuid(gh, uid);
            if (guid) gh->setPartyAssignment(static_cast<uint8_t>(assignment), guid, true);
            return 0;
        }},
                {"ClearPartyAssignment", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            std::string what(luaL_optstring(L, 1, ""));
            const char* unit = luaL_optstring(L, 2, "");
            toLowerInPlace(what);
            if (!gh || !unit || !*unit) return 0;
            const int assignment = (what == "maintank") ? 0 : (what == "mainassist") ? 1 : -1;
            if (assignment < 0) return 0;
            std::string uid(unit);
            toLowerInPlace(uid);
            const uint64_t guid = resolveUnitGuid(gh, uid);
            if (guid) gh->setPartyAssignment(static_cast<uint8_t>(assignment), guid, false);
            return 0;
        }},
                // ---- Raid group management ----
                // SetRaidSubgroup(raidIndex, group) — move a member into a
                // different group of eight. SwapRaidSubgroup exchanges two,
                // which is what the raid UI uses when the destination is full.
                {"SetRaidSubgroup", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int idx   = static_cast<int>(luaL_optnumber(L, 1, 0));
            const int group = static_cast<int>(luaL_optnumber(L, 2, 0));
            if (!gh || idx < 1) return 0;
            const auto& members = gh->getPartyData().members;
            if (idx > static_cast<int>(members.size())) return 0;
            gh->setRaidSubgroup(members[static_cast<size_t>(idx) - 1].name,
                                static_cast<uint8_t>(group));
            return 0;
        }},
                {"SwapRaidSubgroup", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int a = static_cast<int>(luaL_optnumber(L, 1, 0));
            const int b = static_cast<int>(luaL_optnumber(L, 2, 0));
            if (!gh || a < 1 || b < 1) return 0;
            const auto& members = gh->getPartyData().members;
            const int n = static_cast<int>(members.size());
            if (a > n || b > n) return 0;
            gh->swapRaidSubgroup(members[static_cast<size_t>(a) - 1].name,
                                 members[static_cast<size_t>(b) - 1].name);
            return 0;
        }},
                // Which row the raid roster has highlighted. Purely local —
                // the raid UI reads it back through its own frames, and
                // nothing is sent for it.
                {"SetRaidRosterSelection", [](lua_State* L) -> int { (void)L; return 0; }},
                // Voice chat again: there is none, so nobody is muted or
                // silenced and asking to mute them does nothing.
                {"IsMuted",             luaReturnFalse},
                {"UnitIsSilenced",      luaReturnFalse},
                {"AddOrDelMute",        [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelSilenceVoice", [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelUnSilenceVoice", [](lua_State* L) -> int { (void)L; return 0; }},
                // Vehicles are not modelled in the raid frames.
                {"UnitTargetsVehicleInRaidUI", luaReturnFalse},
                {"GetNumLanguages",   lua_GetNumLanguages},
                {"GetLanguageByIndex", lua_GetLanguageByIndex},
                {"SendChatMessage",   lua_SendChatMessage},
                {"SendAddonMessage",  lua_SendAddonMessage},
                {"RegisterAddonMessagePrefix", lua_RegisterAddonMessagePrefix},
                {"IsAddonMessagePrefixRegistered", lua_IsAddonMessagePrefixRegistered},
                {"IsInGuild",               lua_IsInGuild},
                {"GetGuildInfo",            lua_GetGuildInfoFunc},
                {"GetNumGuildMembers",      lua_GetNumGuildMembers},
                {"GuildRoster", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->requestGuildRoster();
            return 0;
        }},
                {"SortGuildRoster", [](lua_State* L) -> int {
            (void)L; // Sorting is client-side display only
            return 0;
        }},
                {"GetGuildRosterInfo",      lua_GetGuildRosterInfo},
                {"GetGuildRosterMOTD",      lua_GetGuildRosterMOTD},
                {"GetNumFriends",           lua_GetNumFriends},
                {"GetFriendInfo",           lua_GetFriendInfo},
                {"GetNumIgnores",           lua_GetNumIgnores},
                {"GetIgnoreName",           lua_GetIgnoreName},
                {"GuildInvite", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->inviteToGuild(luaL_checkstring(L, 1));
            return 0;
        }},
                {"GuildUninvite", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->kickGuildMember(luaL_checkstring(L, 1));
            return 0;
        }},
                {"GuildPromote", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->promoteGuildMember(luaL_checkstring(L, 1));
            return 0;
        }},
                {"GuildDemote", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->demoteGuildMember(luaL_checkstring(L, 1));
            return 0;
        }},
                {"GuildLeave", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->leaveGuild();
            return 0;
        }},
                {"GuildSetPublicNote", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->setGuildPublicNote(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
            return 0;
        }},
                {"DoEmote", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* token = luaL_checkstring(L, 1);
            if (!gh) return 0;
            std::string t(token);
            for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            // Map common emote tokens to DBC TextEmote IDs
            static const std::unordered_map<std::string, uint32_t> emoteMap = {
                {"WAVE", 67}, {"BOW", 2}, {"DANCE", 10}, {"CHEER", 5},
                {"CHICKEN", 6}, {"CRY", 8}, {"EAT", 14}, {"DRINK", 13},
                {"FLEX", 16}, {"KISS", 22}, {"LAUGH", 23}, {"POINT", 30},
                {"ROAR", 34}, {"RUDE", 36}, {"SALUTE", 37}, {"SHY", 40},
                {"SILLY", 41}, {"SIT", 42}, {"SLEEP", 43}, {"SPIT", 44},
                {"THANK", 52}, {"CLAP", 7}, {"KNEEL", 21}, {"LAY", 24},
                {"NO", 28}, {"YES", 70}, {"BEG", 1}, {"ANGRY", 64},
                {"FAREWELL", 15}, {"HELLO", 18}, {"WELCOME", 68},
            };
            auto it = emoteMap.find(t);
            uint64_t target = gh->hasTarget() ? gh->getTargetGuid() : 0;
            if (it != emoteMap.end()) {
                gh->sendTextEmote(it->second, target);
            }
            return 0;
        }},
                {"AddFriend", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_checkstring(L, 1);
            const char* note = luaL_optstring(L, 2, "");
            if (gh) gh->addFriend(name, note);
            return 0;
        }},
                {"RemoveFriend", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_checkstring(L, 1);
            if (gh) gh->removeFriend(name);
            return 0;
        }},
                {"AddIgnore", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_checkstring(L, 1);
            if (gh) gh->addIgnore(name);
            return 0;
        }},
                {"DelIgnore", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_checkstring(L, 1);
            if (gh) gh->removeIgnore(name);
            return 0;
        }},
                {"ShowFriends", [](lua_State* L) -> int {
            (void)L; // Friends panel is shown via ImGui, not Lua
            return 0;
        }},
                {"GetNumWhoResults", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
            lua_pushnumber(L, gh->getWhoResults().size());
            lua_pushnumber(L, gh->getWhoOnlineCount());
            return 2;
        }},
                {"GetWhoInfo", [](lua_State* L) -> int {
            // GetWhoInfo(index) → name, guild, level, race, class, zone, classFileName
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || index < 1) { return luaReturnNil(L); }
            const auto& results = gh->getWhoResults();
            if (index > static_cast<int>(results.size())) { return luaReturnNil(L); }
            const auto& w = results[index - 1];


            const char* raceName = (w.raceId < 12) ? kLuaRaces[w.raceId] : "Unknown";
            const char* className = (w.classId < 12) ? kLuaClasses[w.classId] : "Unknown";
            static constexpr const char* kClassFiles[] = {"","WARRIOR","PALADIN","HUNTER","ROGUE","PRIEST","DEATHKNIGHT","SHAMAN","MAGE","WARLOCK","","DRUID"};
            const char* classFile = (w.classId < 12) ? kClassFiles[w.classId] : "WARRIOR";
            lua_pushstring(L, w.name.c_str());
            lua_pushstring(L, w.guildName.c_str());
            lua_pushnumber(L, w.level);
            lua_pushstring(L, raceName);
            lua_pushstring(L, className);
            lua_pushstring(L, ""); // zone name (would need area lookup)
            lua_pushstring(L, classFile);
            return 7;
        }},
                {"SendWho", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* query = luaL_optstring(L, 1, "");
            if (gh) gh->queryWho(query);
            return 0;
        }},
                {"SetWhoToUI", [](lua_State* L) -> int {
            (void)L; return 0; // Stub
        }},
                {"GetNumGossipOptions", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getCurrentGossip().options.size() : 0);
            return 1;
        }},
                // The gossip window's two quest lists. Both are flat runs of
                // values rather than tables — five per available quest and four
                // per active one, which is the stride GossipFrame reads them at.
                {"GetGossipAvailableQuests", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            int n = 0;
            for (const auto& q : gh->getCurrentGossip().quests) {
                if (!gossipQuestIsAvailable(q.questIcon)) continue;
                lua_pushstring(L, q.title.c_str());
                lua_pushnumber(L, q.questLevel);
                // Trivial greys the title out. Nothing here knows a quest's
                // green range, and guessing it would grey quests that are not.
                lua_pushboolean(L, 0);
                lua_pushboolean(L, (q.questFlags & kQuestFlagsDaily) ? 1 : 0);
                lua_pushboolean(L, q.isRepeatable ? 1 : 0);
                n += 5;
            }
            return n;
        }},
                {"GetGossipActiveQuests", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            int n = 0;
            for (const auto& q : gh->getCurrentGossip().quests) {
                if (!gossipQuestIsActive(q.questIcon)) continue;
                lua_pushstring(L, q.title.c_str());
                lua_pushnumber(L, q.questLevel);
                lua_pushboolean(L, 0);
                lua_pushboolean(L, gossipQuestIsCompletable(q.questIcon) ? 1 : 0);
                n += 4;
            }
            return n;
        }},
                {"SelectGossipAvailableQuest", [](lua_State* L) -> int {
            return selectGossipQuestAt(L, /*available=*/true);
        }},
                {"SelectGossipActiveQuest", [](lua_State* L) -> int {
            return selectGossipQuestAt(L, /*available=*/false);
        }},
                // The greeting panel, shown when a quest giver has several
                // quests and nothing else to say. It reads the same list as the
                // gossip window — this client routes both through one — but
                // asks for it a quest at a time rather than all at once.
                {"GetGreetingText", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushstring(L, gh ? gh->getQuestGreeting().c_str() : "");
            return 1;
        }},
                {"GetNumAvailableQuests", [](lua_State* L) -> int {
            lua_pushnumber(L, countGossipQuests(L, /*available=*/true));
            return 1;
        }},
                {"GetNumActiveQuests", [](lua_State* L) -> int {
            lua_pushnumber(L, countGossipQuests(L, /*available=*/false));
            return 1;
        }},
                {"GetAvailableTitle", [](lua_State* L) -> int {
            return pushGossipQuestTitle(L, /*available=*/true);
        }},
                {"GetActiveTitle", [](lua_State* L) -> int {
            return pushGossipQuestTitle(L, /*available=*/false);
        }},
                {"SelectAvailableQuest", [](lua_State* L) -> int {
            return selectGossipQuestAt(L, /*available=*/true);
        }},
                {"SelectActiveQuest", [](lua_State* L) -> int {
            return selectGossipQuestAt(L, /*available=*/false);
        }},
                // Only greys a title, and nothing here knows a quest's green
                // range — a guess would grey quests that are not trivial.
                {"IsActiveQuestTrivial", [](lua_State* L) -> int {
            lua_pushboolean(L, 0);
            return 1;
        }},
                // isTrivial, isDaily, isRepeatable — which icon the greeting
                // panel puts beside an offered quest.
                {"GetAvailableQuestInfo", [](lua_State* L) -> int {
            const int index = static_cast<int>(luaL_checknumber(L, 1));
            const auto* q = gossipQuestAt(L, /*available=*/true, index);
            lua_pushboolean(L, 0);
            lua_pushboolean(L, q && (q->questFlags & kQuestFlagsDaily) ? 1 : 0);
            lua_pushboolean(L, q && q->isRepeatable ? 1 : 0);
            return 3;
        }},
                // Whether the gossip window must be shown even when there is
                // one thing to click. The server sends no such flag here, and
                // the frame reads it as "not ForceGossip()" to decide whether
                // to go straight to a lone vendor or flight master — which is
                // what the real client does, so a definite no keeps that.
                // GuildSetMOTD(text) — what /gmotd does
                //
                // The rest of the guild commands were already bound; this one
                // was not, and the server refuses it from a rank without the
                // right, which is where that decision belongs while nothing
                // here reads rank rights.
                {"GuildSetMOTD", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* motd = luaL_optstring(L, 1, "");
            if (gh && motd) gh->setGuildMotd(motd);
            return 0;
        }},
                {"SendSystemMessage",   lua_SendSystemMessage},
                {"GetSelectedFriend", [](lua_State* L) -> int {
            lua_pushnumber(L, selectedFriend()); return 1; }},
                {"SetSelectedFriend", [](lua_State* L) -> int {
            selectedFriend() = static_cast<int>(luaL_optnumber(L, 1, 0)); return 0; }},
                {"GetSelectedIgnore", [](lua_State* L) -> int {
            lua_pushnumber(L, selectedIgnore()); return 1; }},
                {"SetSelectedIgnore", [](lua_State* L) -> int {
            selectedIgnore() = static_cast<int>(luaL_optnumber(L, 1, 0)); return 0; }},
                {"AddOrRemoveFriend",   lua_AddOrRemoveFriend},
                {"AddOrDelIgnore",      lua_AddOrDelIgnore},
                // DoReadyCheck() — what /readycheck does
                {"DoReadyCheck", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->initiateReadyCheck();
            return 0;
        }},
                // The buttons on FrameXML's summon and talent-wipe popups.
                // Both prompts are raised — uiparent.lua answers CONFIRM_SUMMON
                // and CONFIRM_TALENT_WIPE, and this client fires both — so
                // without these the popup appeared and neither button did
                // anything. The client's own dialogs were the only way to
                // answer either, which is the shape this branch keeps finding:
                // a capability reachable from one of this client's own windows
                // and from nowhere else.
                {"ConfirmSummon", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->acceptSummon();
            return 0;
        }},
                {"CancelSummon", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->declineSummon();
            return 0;
        }},
                // The popup's own OnCancel only hides the talent frame, so
                // there is no DeclineTalentWipe to bind — cancelTalentWipe is
                // this client's bookkeeping and runs when the popup goes.
                {"ConfirmTalentWipe", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->confirmTalentWipe();
            return 0;
        }},
                {"AcceptGroup",         lua_AcceptGroup},
                {"DeclineGroup",        lua_DeclineGroup},
                {"AcceptGuild",         lua_AcceptGuild},
                {"DeclineGuild",        lua_DeclineGuild},
                {"AcceptResurrect",     lua_AcceptResurrect},
                {"DeclineResurrect",    lua_DeclineResurrect},
                {"AcceptDuel",          lua_AcceptDuel},
                {"CancelDuel",          lua_CancelDuel},
                {"GetAutoCompleteResults", lua_GetAutoCompleteResults},
                {"GetNumFactions", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? static_cast<double>(gh->getReputationList().size()) : 0.0);
            return 1;
        }},
                // name, description, standingID, barMin, barMax, barValue,
                // atWarWith, canToggleAtWar, isHeader, isCollapsed, hasRep,
                // isWatched, isChild
                //
                // Flat: every row is a faction. The real client groups these
                // under collapsible headers built from each faction's parent,
                // which this does not read — so the list is complete and in the
                // server's order, but not divided into categories.
                {"GetFactionInfo", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || index < 1) { return luaReturnNil(L); }
            const auto& list = gh->getReputationList();
            if (index > static_cast<int>(list.size())) { return luaReturnNil(L); }
            const auto& f = list[index - 1];

            const int32_t value = gh->getFactionStanding(f.factionId);
            const Standing s = standingFor(value);
            const bool atWar = (f.flags & game::GameHandler::FACTION_FLAG_AT_WAR) != 0;
            const bool peaceForced =
                (f.flags & game::GameHandler::FACTION_FLAG_PEACE_FORCED) != 0;

            lua_pushstring(L, f.name.c_str());
            lua_pushstring(L, "");            // description: not in the data here
            lua_pushnumber(L, s.id);
            lua_pushnumber(L, s.barMin);
            lua_pushnumber(L, s.barMax);
            lua_pushnumber(L, value);
            lua_pushboolean(L, atWar ? 1 : 0);
            lua_pushboolean(L, peaceForced ? 0 : 1);   // canToggleAtWar
            lua_pushboolean(L, 0);            // isHeader
            lua_pushboolean(L, 0);            // isCollapsed
            lua_pushboolean(L, 1);            // hasRep
            lua_pushboolean(L, 0);            // isWatched
            lua_pushboolean(L, 0);            // isChild
            return 13;
        }},
                {"IsFactionInactive", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const int index = static_cast<int>(luaL_checknumber(L, 1));
            const auto* list = gh ? &gh->getReputationList() : nullptr;
            const bool inactive = list && index >= 1 &&
                index <= static_cast<int>(list->size()) &&
                ((*list)[index - 1].flags & game::GameHandler::FACTION_FLAG_INACTIVE) != 0;
            lua_pushboolean(L, inactive ? 1 : 0);
            return 1;
        }},
                {"GetSelectedFaction", [](lua_State* L) -> int {
            lua_pushnumber(L, selectedFaction());
            return 1;
        }},
                {"SetSelectedFaction", [](lua_State* L) -> int {
            selectedFaction() = static_cast<int>(luaL_checknumber(L, 1));
            return 0;
        }},
                {"ForceGossip", [](lua_State* L) -> int {
            lua_pushboolean(L, 0);
            return 1;
        }},
                {"GetGossipOptions", [](lua_State* L) -> int {
            // Returns pairs of (text, type) for each option
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const auto& opts = gh->getCurrentGossip().options;
            int n = 0;
            static constexpr const char* kIcons[] = {"gossip","vendor","taxi","trainer","spiritguide","innkeeper","banker","petition","tabard","battlemaster","auctioneer"};
            for (const auto& o : opts) {
                lua_pushstring(L, o.text.c_str());
                lua_pushstring(L, o.icon < 11 ? kIcons[o.icon] : "gossip");
                n += 2;
            }
            return n;
        }},
                {"SelectGossipOption", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || index < 1) return 0;
            const auto& opts = gh->getCurrentGossip().options;
            if (index <= static_cast<int>(opts.size()))
                gh->selectGossipOption(opts[index - 1].id);
            return 0;
        }},
                {"GetNumGossipAvailableQuests", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { return luaReturnZero(L); }
            int count = 0;
            for (const auto& q : gh->getCurrentGossip().quests)
                if (q.questIcon != 4) ++count; // 4 = active/in-progress
            lua_pushnumber(L, count);
            return 1;
        }},
                {"GetNumGossipActiveQuests", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) { return luaReturnZero(L); }
            int count = 0;
            for (const auto& q : gh->getCurrentGossip().quests)
                if (q.questIcon == 4) ++count;
            lua_pushnumber(L, count);
            return 1;
        }},
                {"CloseGossip", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->closeGossip();
            return 0;
        }},
                {"InviteUnit", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->inviteToGroup(luaL_checkstring(L, 1));
            return 0;
        }},
                {"UninviteUnit", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->uninvitePlayer(luaL_checkstring(L, 1));
            return 0;
        }},
                {"LeaveParty", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (gh) gh->leaveGroup();
            return 0;
        }},
                {"FollowUnit", [](lua_State* L) -> int {
            // Still nothing. followTarget exists and works, but it follows
            // whatever is targeted and this is called with a *name* — a party
            // member who is not the target cannot be followed without giving
            // the movement handler a guid to aim at. A no-op is the honest
            // answer until then, and unlike a missing global it does not raise.
            (void)L;
            return 0;
        }},
                // Inspect and duel, both of which unitpopup.lua calls straight
                // out of the unit right-click menu — and neither was bound at
                // all, so choosing either raised. A missing global is worse
                // than a stub: an unanswered call throws where an unfired event
                // only goes unheard.
                {"InspectUnit", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* unit = luaL_optstring(L, 1, "target");
            if (!gh || !unit) return 0;
            std::string uid(unit);
            toLowerInPlace(uid);
            if (const uint64_t guid = resolveUnitGuid(gh, uid)) gh->inspectUnit(guid);
            return 0;
        }},
                // The achievement comparison the same menu offers. The query
                // goes out with the inspect above, and there is no comparison
                // window to show it in — but it is bound rather than absent so
                // that picking it does nothing instead of throwing.
                {"InspectAchievements", [](lua_State* L) -> int { (void)L; return 0; }},

                // ---- The rest of what the unit right-click menu calls ----
                //
                // UnitPopup_ShowMenu asks several of these while deciding which
                // entries to show, so a missing one does not skip an entry — it
                // throws part way through building the menu and there is no
                // menu at all. Right-clicking the player's own frame reached
                // GetPVPDesired that way.
                //
                // Everything here is either answered properly or answered
                // safely. A conservative answer hides a menu entry; a missing
                // global breaks the whole menu.
                {"GetPVPDesired", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            bool on = false;
            if (gh) {
                if (auto e = gh->getEntityManager().getEntity(gh->getPlayerGuid())) {
                    // UNIT_FIELD_FLAGS index 59, bit 0x1000 = UNIT_FLAG_PVP,
                    // read the same way CombatHandler::togglePvp reads it.
                    on = (e->getField(59) & 0x00001000u) != 0;
                }
            }
            lua_pushnumber(L, on ? 1 : 0);   // the menu compares it against 1 and 0
            return 1;
        }},
                {"SetPVP", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            if (!gh) return 0;
            const bool want = lua_isnumber(L, 1) ? (lua_tonumber(L, 1) != 0)
                                                 : (lua_toboolean(L, 1) != 0);
            bool on = false;
            if (auto e = gh->getEntityManager().getEntity(gh->getPlayerGuid())) {
                on = (e->getField(59) & 0x00001000u) != 0;
            }
            // The client can only toggle, so only toggle when it would land on
            // what was asked for — otherwise SetPVP(1) while already flagged
            // would turn it off.
            if (want != on) gh->togglePvp();
            return 0;
        }},
                {"UnitIsInMyGuild", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_optstring(L, 1, "");
            if (!gh || !name || !*name) { lua_pushboolean(L, 0); return 1; }
            bool found = false;
            for (const auto& m : gh->getGuildRoster().members) {
                if (m.name == name) { found = true; break; }
            }
            lua_pushboolean(L, found);
            return 1;
        }},
                // Answered no rather than left missing. Each hides one menu
                // entry, which is what should happen for something this client
                // cannot do — party promotion and demotion, channel moderation,
                // granting levels, reporting chat, and the recruit-a-friend and
                // Battle.net pieces have no client support behind them.
                {"PetCanBeRenamed",           [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"CanGrantLevel",             [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"CanComplainChat",           [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"CanChangePlayerDifficulty", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"IsSilenced",                [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"IsDisplayChannelModerator", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"PromoteToLeader",           [](lua_State* L) -> int { (void)L; return 0; }},
                {"PromoteToAssistant",        [](lua_State* L) -> int { (void)L; return 0; }},
                {"DemoteAssistant",           [](lua_State* L) -> int { (void)L; return 0; }},
                {"GrantLevel",                [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetOptOutOfLoot",           [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetDungeonDifficulty",      [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetRaidDifficulty",         [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelBan",                [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelKick",               [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelModerator",          [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelUnmoderator",        [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetChannelOwner",           [](lua_State* L) -> int { (void)L; return 0; }},
                {"BNSetToonBlocked",          [](lua_State* L) -> int { (void)L; return 0; }},

                // ---- What the summon and resurrect popups read ----
                //
                // These two prompts were handed to FrameXML so they would stop
                // being asked twice, and that put their text and timer
                // functions on a live path for the first time. Both were
                // missing, so the summon popup threw in its OnShow and
                // ShowResurrectRequest threw before it could show anything —
                // the handover fixed a duplicate and replaced it with a raise.
                {"GetSummonConfirmSummoner", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushstring(L, gh ? gh->getSummonerName().c_str() : "");
            return 1;
        }},
                {"GetSummonConfirmTimeLeft", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            lua_pushnumber(L, gh ? gh->getSummonTimeoutSec() : 0.0f);
            return 1;
        }},
                // The destination is not carried in what this client parses,
                // and the popup prints it into a sentence — so an empty string
                // rather than nil, which would concatenate into a raise.
                {"GetSummonConfirmAreaName", [](lua_State* L) -> int {
            lua_pushstring(L, ""); return 1;
        }},
                // Asked every frame by the popup's OnUpdate to grey the accept
                // button. The server refuses a summon it will not honour, so
                // the client does not second-guess it.
                {"PlayerCanTeleport", [](lua_State* L) -> int {
            lua_pushboolean(L, 1); return 1;
        }},
                // Which of three resurrect popups to raise. Both false picks
                // RESURRECT_NO_TIMER, the plain "accept?" prompt, which is the
                // right one for a player casting a resurrect — sickness and the
                // release timer belong to the spirit healer path, which this
                // client answers itself.
                {"ResurrectHasSickness", [](lua_State* L) -> int {
            lua_pushboolean(L, 0); return 1;
        }},
                {"ResurrectHasTimer", [](lua_State* L) -> int {
            lua_pushboolean(L, 0); return 1;
        }},

                // ---- What chatframe.lua's slash commands call ----
                //
                // Forty-eight names in that file were unbound, and unlike the
                // Battle.net set they sit behind no feature flag: each is a
                // slash command handler, so each is a raise the moment someone
                // types that command. That is what kept "chat" out of the
                // candidate list, and binding them is what lets it back in.
                //
                // Implemented where this client can do the thing, answered
                // safely where it cannot. A safe answer makes the command do
                // nothing, which is the honest outcome for a feature that is
                // not there; a missing name takes the chat frame down with it.

                // The conditional parser every secure slash command runs its
                // argument through: "/cast [mod:shift] A; B". With no
                // conditional support the whole argument is the action and
                // there is no target clause, which is what an unconditional
                // command means anyway.
                {"SecureCmdOptionParse", [](lua_State* L) -> int {
            const char* msg = luaL_optstring(L, 1, "");
            lua_pushstring(L, msg ? msg : "");
            lua_pushnil(L);
            return 2;
        }},
                {"GuildInfo", [](lua_State* L) -> int {
            if (auto* gh = getGameHandler(L)) gh->requestGuildInfo();
            return 0;
        }},
                {"GuildSetLeader", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_optstring(L, 1, "");
            if (gh && name && *name) gh->setGuildLeader(name);
            return 0;
        }},
                // Not logging, and there is nothing to turn on — false is the
                // true answer rather than a placeholder.
                {"LoggingChat",   [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"LoggingCombat", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"BNIsFriend",      [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"BNIsToonBlocked", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
                {"BNGetNumFriendInvites", [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
                {"GetChatWindowChannels", [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetChatWindowMessages", [](lua_State* L) -> int { (void)L; return 0; }},
                {"GetClickFrame",         [](lua_State* L) -> int { lua_pushnil(L); return 1; }},
                // Pet autocast is left alone deliberately rather than wired to
                // togglePetSpellAutocast: these two say enable and disable, the
                // client can only toggle, and nothing reads back which state a
                // pet spell is in — so "enable" on an already-autocasting spell
                // would turn it off. Doing nothing beats doing the opposite.
                {"EnableSpellAutocast",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"DisableSpellAutocast", [](lua_State* L) -> int { (void)L; return 0; }},
                {"PetAggressiveMode",    [](lua_State* L) -> int { (void)L; return 0; }},
                // Channel moderation, arena teams, the addon list, the console
                // and the rest: no client support behind any of them.
                {"AddChatWindowChannel",     [](lua_State* L) -> int { (void)L; return 0; }},
                {"RemoveChatWindowChannel",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"AddChatWindowMessages",    [](lua_State* L) -> int { (void)L; return 0; }},
                {"RemoveChatWindowMessages", [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelInvite",            [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelMute",              [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelUnmute",            [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelUnban",             [](lua_State* L) -> int { (void)L; return 0; }},
                {"ChannelToggleAnnouncements", [](lua_State* L) -> int { (void)L; return 0; }},
                {"DisplayChannelOwner",      [](lua_State* L) -> int { (void)L; return 0; }},
                {"ListChannels",             [](lua_State* L) -> int { (void)L; return 0; }},
                {"ListChannelByName",        [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetChannelPassword",       [](lua_State* L) -> int { (void)L; return 0; }},
                {"ArenaTeamInviteByName",    [](lua_State* L) -> int { (void)L; return 0; }},
                {"ArenaTeamLeave",           [](lua_State* L) -> int { (void)L; return 0; }},
                {"ArenaTeamSetLeaderByName", [](lua_State* L) -> int { (void)L; return 0; }},
                {"ArenaTeamUninviteByName",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"BNGetConversationInfo",    [](lua_State* L) -> int { (void)L; return 0; }},
                {"BNInviteToConversation",   [](lua_State* L) -> int { (void)L; return 0; }},
                {"BNLeaveConversation",      [](lua_State* L) -> int { (void)L; return 0; }},
                {"BNListConversation",       [](lua_State* L) -> int { (void)L; return 0; }},
                {"BNSendConversationMessage",[](lua_State* L) -> int { (void)L; return 0; }},
                {"BNSendWhisper",            [](lua_State* L) -> int { (void)L; return 0; }},
                {"ConsoleExec",              [](lua_State* L) -> int { (void)L; return 0; }},
                {"DisableAllAddOns",         [](lua_State* L) -> int { (void)L; return 0; }},
                {"EnableAllAddOns",          [](lua_State* L) -> int { (void)L; return 0; }},
                {"SetTaxiBenchmarkMode",     [](lua_State* L) -> int { (void)L; return 0; }},
                {"StopMacro",                [](lua_State* L) -> int { (void)L; return 0; }},
                // The targeting variants this client has no equivalent for.
                // targetEnemy and targetFriend do not filter to players, and
                // one last-target is tracked rather than one per side — so
                // wiring these to the nearest thing would target the wrong
                // unit, which is worse than not answering the command.
                {"TargetLastEnemy",           [](lua_State* L) -> int { (void)L; return 0; }},
                {"TargetLastFriend",          [](lua_State* L) -> int { (void)L; return 0; }},
                {"TargetNearestEnemyPlayer",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"TargetNearestFriendPlayer", [](lua_State* L) -> int { (void)L; return 0; }},
                {"TargetNearestPartyMember",  [](lua_State* L) -> int { (void)L; return 0; }},
                {"TargetNearestRaidMember",   [](lua_State* L) -> int { (void)L; return 0; }},
                {"StartDuel", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* unit = luaL_optstring(L, 1, "target");
            if (!gh || !unit) return 0;
            std::string uid(unit);
            toLowerInPlace(uid);
            if (const uint64_t guid = resolveUnitGuid(gh, uid)) gh->proposeDuel(guid);
            return 0;
        }},
                {"RandomRoll", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            int mn = static_cast<int>(luaL_optnumber(L, 1, 1));
            int mx = static_cast<int>(luaL_optnumber(L, 2, 100));
            if (gh) gh->randomRoll(mn, mx);
            return 0;
        }},
                {"JoinChannelByName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_checkstring(L, 1);
            const char* pw = luaL_optstring(L, 2, "");
            if (gh) gh->joinChannel(name, pw);
            return 0;
        }},
                {"LeaveChannelByName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            const char* name = luaL_checkstring(L, 1);
            if (gh) gh->leaveChannel(name);
            return 0;
        }},
                {"GetChannelName", [](lua_State* L) -> int {
            auto* gh = getGameHandler(L);
            int index = static_cast<int>(luaL_checknumber(L, 1));
            if (!gh || index < 1) { return luaReturnNil(L); }
            std::string name = gh->getChannelByIndex(index - 1);
            if (!name.empty()) {
                lua_pushstring(L, name.c_str());
                lua_pushstring(L, ""); // header
                lua_pushboolean(L, 0); // collapsed
                lua_pushnumber(L, index); // channelNumber
                lua_pushnumber(L, 0); // count
                lua_pushboolean(L, 1); // active
                lua_pushstring(L, "CHANNEL_CATEGORY_CUSTOM"); // category
                return 7;
            }
            lua_pushnil(L);
            return 1;
        }},
    };
    for (const auto& [name, func] : api) {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
    }
}

} // namespace wowee::addons
