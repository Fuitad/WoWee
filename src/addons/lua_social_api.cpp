// lua_social_api.cpp — Chat, guild, friends, ignore, gossip, party management, and emotes Lua API bindings.
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#include "addons/lua_api_helpers.hpp"

namespace wowee::addons {

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

/// Which standing a raw reputation value falls in, and the band it sits in.
/// standingId is 1..8 as the interface numbers them.
struct Standing {
    int id = 4;        // Neutral, for a faction the player has not moved
    int32_t barMin = 0;
    int32_t barMax = 3000;
};

Standing standingFor(int32_t value) {
    // Bottom of each standing, in order; the top of one is the bottom of the
    // next. Checked against the tier table this client's own reputation panel
    // carries, which agrees to the number — two tables of the same thresholds
    // that disagreed would put the same faction in different standings
    // depending on which window was open.
    static const int32_t kBottoms[8] = {
        -42000, -6000, -3000, 0, 3000, 9000, 21000, 42000
    };
    // Exalted is a thousand wide and stays there: the bar reads out of a
    // thousand rather than filling from forty-two to sixty-three.
    constexpr int32_t kExaltedWidth = 1000;
    Standing s;
    for (int i = 7; i >= 0; --i) {
        if (value >= kBottoms[i]) {
            s.id = i + 1;
            s.barMin = kBottoms[i];
            s.barMax = (i < 7) ? kBottoms[i + 1] : kBottoms[7] + kExaltedWidth;
            return s;
        }
    }
    // Below hated is still hated; the server does not send lower.
    s.id = 1;
    s.barMin = kBottoms[0];
    s.barMax = kBottoms[1];
    return s;
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

void registerSocialLuaAPI(lua_State* L) {
    static const struct { const char* name; lua_CFunction func; } api[] = {
                {"BNGetNumFriends",     lua_BNGetNumFriends},
                {"GetGMTicket",         lua_GetGMTicket},
                {"GetNumMacroIcons",    lua_GetNumMacroIcons},
                {"GetMacroIconInfo",    lua_GetMacroIconInfo},
                // The guild bank tab dialog picks from the same icons under a
                // name of its own.
                {"GetNumMacroItemIcons", lua_GetNumMacroIcons},
                {"GetMacroItemIconInfo", lua_GetMacroIconInfo},
                {"NewGMTicket",         lua_NewGMTicket},
                {"DeleteGMTicket",      lua_DeleteGMTicket},
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
            (void)L; // Follow requires movement system integration
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
