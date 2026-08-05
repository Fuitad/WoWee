#pragma once

#include "game/world_packets.hpp"
#include "game/opcode_table.hpp"
#include "game/group_defines.hpp"
#include "game/handler_types.hpp"
#include "network/packet.hpp"
#include <array>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wowee {
namespace game {

class GameHandler;

class SocialHandler {
public:
    using PacketHandler = std::function<void(network::Packet&)>;
    using DispatchTable = std::unordered_map<LogicalOpcode, PacketHandler>;

    explicit SocialHandler(GameHandler& owner);

    void registerOpcodes(DispatchTable& table);

    // ---- Structs (aliased from handler_types.hpp) ----

    using InspectArenaTeam = game::InspectArenaTeam;

    using InspectResult = game::InspectResult;

    using WhoEntry = game::WhoEntry;

    using BgQueueSlot = game::BgQueueSlot;

    using AvailableBgInfo = game::AvailableBgInfo;

    using BgPlayerScore = game::BgPlayerScore;

    using ArenaTeamScore = game::ArenaTeamScore;

    using BgScoreboardData = game::BgScoreboardData;

    using BgPlayerPosition = game::BgPlayerPosition;

    using PetitionSignature = game::PetitionSignature;

    using PetitionInfo = game::PetitionInfo;

    using ReadyCheckResult = game::ReadyCheckResult;

    using InstanceLockout = game::InstanceLockout;

    using LfgState = game::LfgState;

    using ArenaTeamStats = game::ArenaTeamStats;

    using ArenaTeamMember = game::ArenaTeamMember;

    using ArenaTeamRoster = game::ArenaTeamRoster;

    // ---- Public API ----

    // Inspection
    void inspectTarget();
    /// Inspect any player by guid; inspectTarget is this with the current target.
    void inspectUnit(uint64_t guid);
    const InspectResult* getInspectResult() const {
        return inspectResult_.guid ? &inspectResult_ : nullptr;
    }

    // Server info / who
    void queryServerTime();
    void requestPlayedTime();
    void queryWho(const std::string& playerName = "");
    uint32_t getTotalTimePlayed() const { return totalTimePlayed_; }
    uint32_t getLevelTimePlayed() const { return levelTimePlayed_; }
    const std::vector<WhoEntry>& getWhoResults() const { return whoResults_; }
    uint32_t getWhoOnlineCount() const { return whoOnlineCount_; }
    std::string getWhoAreaName(uint32_t zoneId) const;

    // Social commands
    void addFriend(const std::string& playerName, const std::string& note = "");
    void removeFriend(const std::string& playerName);
    void setFriendNote(const std::string& playerName, const std::string& note);
    void addIgnore(const std::string& playerName);
    void removeIgnore(const std::string& playerName);

    // Random roll
    void randomRoll(uint32_t minRoll = 1, uint32_t maxRoll = 100);

    // Battleground
    bool hasPendingBgInvite() const;
    void acceptBattlefield(uint32_t queueSlot = 0xFFFFFFFF);
    void declineBattlefield(uint32_t queueSlot = 0xFFFFFFFF);
    void leaveBattlefield();
    /// Ask the server which instances of one battleground are running.
    void requestBattlefieldList(uint32_t bgTypeId);
    /// Report a player in this battleground as not participating.
    void reportPvpAfk(uint64_t playerGuid);
    void joinBattlefield(uint64_t battlemasterGuid, uint32_t bgTypeId,
                         uint32_t instanceId, bool asGroup);
    const std::array<BgQueueSlot, 3>& getBgQueues() const { return bgQueues_; }
    const std::vector<AvailableBgInfo>& getAvailableBgs() const { return availableBgs_; }
    void requestPvpLog();
    const BgScoreboardData* getBgScoreboard() const {
        return bgScoreboard_.players.empty() ? nullptr : &bgScoreboard_;
    }
    const std::vector<BgPlayerPosition>& getBgPlayerPositions() const { return bgPlayerPositions_; }

    // Logout. exitAfterLogout distinguishes /quit and /exit, which leave the game
    // entirely, from /logout and /camp, which drop back to character select.
    void requestLogout(bool exitAfterLogout = false);
    void cancelLogout();
    bool  isLoggingOut()        const { return loggingOut_; }
    float getLogoutCountdown()  const { return logoutCountdown_; }
    bool  isLogoutToExit()      const { return exitAfterLogout_; }

    // Guild
    void requestGuildInfo();
    void requestGuildRoster();
    /// Ask for the guild's event log. The reply is the same opcode.
    void requestGuildEventLog();
    /// Ask for one bank tab's log. Tab six is the money log, which is what the
    /// server means by GUILD_BANK_MAX_TABS.
    void requestGuildBankLog(uint8_t tab);
    static constexpr uint8_t kGuildBankMoneyTab = 6;
    const std::vector<GuildBankLogEntry>& getGuildBankLog(uint8_t tab) const {
        static const std::vector<GuildBankLogEntry> empty;
        return (tab < guildBankLogs_.size()) ? guildBankLogs_[tab] : empty;
    }
    const std::vector<GuildEventLogEntry>& getGuildEventLog() const { return guildEventLog_; }
    void setGuildInfoText(const std::string& text);
    void takeInboxTextItem(uint32_t mailId);
    void setGuildMotd(const std::string& motd);
    void promoteGuildMember(const std::string& playerName);
    void demoteGuildMember(const std::string& playerName);
    void leaveGuild();
    void inviteToGuild(const std::string& playerName);
    void kickGuildMember(const std::string& playerName);
    void disbandGuild();
    void setGuildLeader(const std::string& name);
    void setGuildPublicNote(const std::string& name, const std::string& note);
    void setGuildOfficerNote(const std::string& name, const std::string& note);
    void acceptGuildInvite();
    void declineGuildInvite();
    void queryGuildInfo(uint32_t guildId);
    void createGuild(const std::string& guildName);
    void addGuildRank(const std::string& rankName);
    void delGuildRank();
    void saveGuildRank(uint32_t rankId, uint32_t rights, const std::string& rankName,
                       uint32_t goldLimit, const uint32_t* tabRights, const uint32_t* tabSlots);
    void deleteGuildRank();
    void requestPetitionShowlist(uint64_t npcGuid);
    void buyPetition(uint64_t npcGuid, const std::string& guildName);

    // Guild state accessors
    bool isInGuild() const;
    const std::string& getGuildName() const { return guildName_; }
    const GuildRosterData& getGuildRoster() const { return guildRoster_; }
    bool hasGuildRoster() const { return hasGuildRoster_; }
    const std::vector<std::string>& getGuildRankNames() const { return guildRankNames_; }
    /// The rights bitmask of the player's own guild rank, or 0 when it is not
    /// known. The roster carries one per rank and the member row names which
    /// rank each member holds; both were parsed and stored and neither was
    /// being read.
    uint32_t getPlayerGuildRankRights() const;
    bool hasPendingGuildInvite() const { return pendingGuildInvite_; }
    const std::string& getPendingGuildInviterName() const { return pendingGuildInviterName_; }
    const std::string& getPendingGuildInviteGuildName() const { return pendingGuildInviteGuildName_; }
    const GuildInfoData& getGuildInfoData() const { return guildInfoData_; }
    const GuildQueryResponseData& getGuildQueryData() const { return guildQueryData_; }
    bool hasGuildInfoData() const { return guildInfoData_.isValid(); }

    // Petition
    bool hasPetitionShowlist() const { return showPetitionDialog_; }
    void clearPetitionDialog() { showPetitionDialog_ = false; }
    /// Shuts the vendor session and tells the interface. Both windows read a
    /// different thing, so closing one has to close the other.
    void closePetitionVendor();
    uint32_t getPetitionCost() const { return petitionCost_; }
    uint64_t getPetitionNpcGuid() const { return petitionNpcGuid_; }
    const PetitionInfo& getPetitionInfo() const { return petitionInfo_; }
    bool hasPetitionSignaturesUI() const { return petitionInfo_.showUI; }
    void clearPetitionSignaturesUI() { petitionInfo_.showUI = false; }
    void signPetition(uint64_t petitionGuid);
    void turnInPetition(uint64_t petitionGuid);
    /// Offer the charter to another player so they can sign it.
    void offerPetition(uint64_t petitionGuid, uint64_t targetGuid);

    // Guild name lookup
    const std::string& lookupGuildName(uint32_t guildId);
    uint32_t getEntityGuildId(uint64_t guid) const;

    // Ready check
    void initiateReadyCheck();
    void respondToReadyCheck(bool ready);
    bool hasPendingReadyCheck() const { return pendingReadyCheck_; }
    void dismissReadyCheck() { pendingReadyCheck_ = false; }
    const std::string& getReadyCheckInitiator() const { return readyCheckInitiator_; }
    const std::vector<ReadyCheckResult>& getReadyCheckResults() const { return readyCheckResults_; }

    // Duel
    void acceptDuel();
    void forfeitDuel();
    void proposeDuel(uint64_t targetGuid);
    void reportPlayer(uint64_t targetGuid, const std::string& reason);
    bool hasPendingDuelRequest() const { return pendingDuelRequest_; }
    const std::string& getDuelChallengerName() const { return duelChallengerName_; }
    float getDuelCountdownRemaining() const {
        if (duelCountdownMs_ == 0) return 0.0f;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - duelCountdownStartedAt_).count();
        float rem = (static_cast<float>(duelCountdownMs_) - static_cast<float>(elapsed)) / 1000.0f;
        return rem > 0.0f ? rem : 0.0f;
    }

    // Party/Raid
    void inviteToGroup(const std::string& playerName);
    void acceptGroupInvite();
    void declineGroupInvite();

    // ---- Arena team invitations ----
    // Accept and decline carry no payload: the server answers from the invite
    // it is already holding against this character.
    void acceptArenaTeamInvite();
    void declineArenaTeamInvite();
    void disbandArenaTeam(uint32_t teamId);
    /// Report a mail as spam. Type 0 of CMSG_COMPLAIN; the three values after
    /// the sender are a zero, the mail's own id, and another zero.
    void reportMailSpam(uint64_t senderGuid, uint32_t mailId);
    void leaveGroup();
    void convertToRaid();
    void sendSetLootMethod(uint32_t method, uint32_t threshold, uint64_t masterLooterGuid);
    bool isInGroup() const { return !partyData.isEmpty(); }
    const GroupListData& getPartyData() const { return partyData; }
    bool hasPendingGroupInvite() const { return pendingGroupInvite; }
    const std::string& getPendingInviterName() const { return pendingInviterName; }
    void setGuildBankTabText(uint8_t tab, const std::string& text);
    void channelModeration(Opcode op, const std::string& channelName,
                           const std::string& targetName);
    void promoteToLeader(uint64_t guid);
    void uninvitePlayer(const std::string& playerName);
    void setLootMethod(uint8_t method, uint64_t masterGuid, uint8_t threshold);
    void setPartyAssignment(uint8_t assignment, uint64_t guid, bool apply);
    void setRaidSubgroup(const std::string& playerName, uint8_t group);
    void swapRaidSubgroup(const std::string& firstName, const std::string& secondName);
    void leaveParty();
    void setMainTank(uint64_t targetGuid);
    void setMainAssist(uint64_t targetGuid);
    void clearMainTank();
    void clearMainAssist();
    void setRaidMark(uint64_t guid, uint8_t icon);
    // Apply a mark without the server round-trip, used when the player has no
    // group (the server would drop the request). Grouped marking stays
    // server-authoritative so every member sees the same icons.
    void setRaidMarkLocally(uint64_t guid, uint8_t icon);
    void requestRaidInfo();
    void setSavedInstanceExtend(uint32_t mapId, uint32_t difficulty, bool extend);
    /// The roles last offered to the dungeon finder. lfgSetRoles used to send
    /// them and keep nothing, so the panel reading its own checkboxes back, and
    /// the ready dialog naming the role it found you a group for, had no source.
    uint8_t getLfgOfferedRoles() const { return lfgOfferedRoles_; }
    int32_t  getLfgWaitTank() const   { return lfgWaitTank_; }
    int32_t  getLfgWaitHealer() const { return lfgWaitHealer_; }
    int32_t  getLfgWaitDps() const    { return lfgWaitDps_; }
    uint8_t  getLfgNeedTank() const   { return lfgNeedTank_; }
    uint8_t  getLfgNeedHealer() const { return lfgNeedHealer_; }
    uint8_t  getLfgNeedDps() const    { return lfgNeedDps_; }
    uint64_t getLfgBootVictimGuid() const { return lfgBootVictimGuid_; }
    const std::unordered_map<uint32_t, uint32_t>& getLfgLocks() const { return lfgLocks_; }
    const std::vector<LfgReward>& getLfgRewards() const { return lfgRewards_; }
    void handleLfgPlayerInfo(network::Packet& packet);
    const std::vector<LfgProposalMember>& getLfgProposalMembers() const {
        return lfgProposalMembers_;
    }

    // Instance lockouts
    const std::vector<InstanceLockout>& getInstanceLockouts() const { return instanceLockouts_; }

    // Instance difficulty
    uint32_t getInstanceDifficulty() const { return instanceDifficulty_; }
    bool isInstanceHeroic() const { return instanceIsHeroic_; }
    bool isInInstance() const { return inInstance_; }

    // Minimap ping
    void sendMinimapPing(float wowX, float wowY);

    // Summon request
    void handleSummonRequest(network::Packet& packet);
    /// CMSG_SUMMON_RESPONSE: the summoner's guid, then yes or no.
    void sendSummonResponse(bool accept);
    void acceptSummon();
    void declineSummon();

    // Battlefield Manager
    void acceptBfMgrInvite();
    void declineBfMgrInvite();
    void respondBfMgrQueueInvite(uint32_t battleId, bool accept);
    void requestBfMgrExit(uint32_t battleId);

    // Calendar
    void requestCalendar();

    // ---- Methods moved from GameHandler ----
    void sendSetDifficulty(uint32_t difficulty, bool raid = false);
    void toggleHelm();
    void toggleCloak();
    void setStandState(uint8_t standState);
    void sendAlterAppearance(uint32_t hairStyleEntry, uint32_t hairColor,
                             uint32_t facialHairEntry, uint32_t skinColorEntry);
    void deleteGmTicket();
    void requestGmTicket();
    /// Ask whether the GM ticket queue is accepting tickets.
    void requestGmSystemStatus();

    // Utility methods for delegation from GameHandler
    void updateLogoutCountdown(float deltaTime);
    void resetTransferState();
    GroupListData& mutablePartyData() { return partyData; }
    InspectResult& mutableInspectResult() { return inspectResult_; }
    void setRaidTargetGuid(uint8_t icon, uint64_t guid) {
        if (icon < kRaidMarkCount) raidTargetGuids_[icon] = guid;
    }
    void setEncounterUnitGuid(uint32_t slot, uint64_t guid) {
        if (slot < kMaxEncounterSlots) encounterUnitGuids_[slot] = guid;
    }

    // The bind-or-leave question the server asks on entering an instance that
    // is already under way. It carries a countdown, and the answer is the
    // player's: accept and be saved, decline and go to the graveyard. Nothing
    // happens until one of the two is sent, so this is held rather than
    // answered here.
    struct InstanceLockPrompt {
        bool active = false;
        float secondsLeft = 0.0f;
        bool previouslySaved = false;
        uint32_t completedEncounterMask = 0;
    };
    const InstanceLockPrompt& getInstanceLockPrompt() const { return instanceLock_; }
    void respondInstanceLock(bool accept);

    // Encounter unit tracking
    static constexpr uint32_t kMaxEncounterSlots = 5;
    uint64_t getEncounterUnitGuid(uint32_t slot) const {
        return (slot < kMaxEncounterSlots) ? encounterUnitGuids_[slot] : 0;
    }

    // Raid target markers (0-7: Star, Circle, Diamond, Triangle, Moon, Square, Cross, Skull)
    static constexpr uint32_t kRaidMarkCount = 8;
    uint64_t getRaidMarkGuid(uint32_t icon) const {
        return (icon < kRaidMarkCount) ? raidTargetGuids_[icon] : 0;
    }
    uint8_t getEntityRaidMark(uint64_t guid) const {
        if (guid == 0) return 0xFF;
        for (uint32_t i = 0; i < kRaidMarkCount; ++i)
            if (raidTargetGuids_[i] == guid) return static_cast<uint8_t>(i);
        return 0xFF;
    }

    // LFG / Dungeon Finder
    void lfgJoin(uint32_t dungeonId, uint8_t roles);
    void lfgLeave();
    void lfgSetRoles(uint8_t roles);
    void lfgAcceptProposal(uint32_t proposalId, bool accept);
    void lfgSetBootVote(bool vote);
    void lfgTeleport(bool toLfgDungeon = true);
    LfgState getLfgState()           const { return lfgState_; }
    bool isLfgQueued()               const { return lfgState_ == LfgState::Queued; }
    bool isLfgInDungeon()            const { return lfgState_ == LfgState::InDungeon; }
    uint32_t getLfgDungeonId()       const { return lfgDungeonId_; }
    std::string getCurrentLfgDungeonName() const;
    uint32_t getLfgProposalId()      const { return lfgProposalId_; }
    int32_t  getLfgAvgWaitSec()      const { return lfgAvgWaitSec_; }
    uint32_t getLfgTimeInQueueMs()   const { return lfgTimeInQueueMs_; }
    uint32_t getLfgBootVotes()       const { return lfgBootVotes_; }
    uint32_t getLfgBootTotal()       const { return lfgBootTotal_; }
    uint32_t getLfgBootTimeLeft()    const { return lfgBootTimeLeft_; }
    uint32_t getLfgBootNeeded()      const { return lfgBootNeeded_; }
    const std::string& getLfgBootTargetName() const { return lfgBootTargetName_; }
    const std::string& getLfgBootReason()     const { return lfgBootReason_; }

    // Arena
    const std::vector<ArenaTeamStats>& getArenaTeamStats() const { return arenaTeamStats_; }
    void requestArenaTeamRoster(uint32_t teamId);
    const ArenaTeamRoster* getArenaTeamRoster(uint32_t teamId) const {
        for (const auto& r : arenaTeamRosters_)
            if (r.teamId == teamId) return &r;
        return nullptr;
    }

private:
    // ---- Packet handlers ----
    void handleInspectResults(network::Packet& packet);
    void handleGuildBankLog(network::Packet& packet);
    void sendBfMgrResponse(Opcode op, uint32_t battleId, bool accept, bool withFlag);
    void handleQueryTimeResponse(network::Packet& packet);
    void handlePlayedTime(network::Packet& packet);
    void handleWho(network::Packet& packet);
    void handleFriendList(network::Packet& packet);
    void handleContactList(network::Packet& packet);
    void handleFriendStatus(network::Packet& packet);
    void handleRandomRoll(network::Packet& packet);
    void handleLogoutResponse(network::Packet& packet);
    void handleLogoutComplete(network::Packet& packet);
    void handleGroupInvite(network::Packet& packet);
    void handleGroupDecline(network::Packet& packet);
    void handleGroupList(network::Packet& packet);
    void handleGroupUninvite(network::Packet& packet);
    void handlePartyCommandResult(network::Packet& packet);
    void handlePartyMemberStats(network::Packet& packet, bool isFull);
    void handleGuildInfo(network::Packet& packet);
    void handleGuildRoster(network::Packet& packet);
    void handleGuildQueryResponse(network::Packet& packet);
    void handleGuildEvent(network::Packet& packet);
    // Updates a roster member's cached online flag; returns true only on a real change so
    // the login-time SIGNED_ON flood (state unchanged) is suppressed from guild chat.
    bool guildMemberOnlineTransition(const std::string& name, bool nowOnline);
    void handleGuildInvite(network::Packet& packet);
    void handleGuildCommandResult(network::Packet& packet);
    void handlePetitionShowlist(network::Packet& packet);
    void handlePetitionQueryResponse(network::Packet& packet);
    void handleGuildEventLog(network::Packet& packet);
    void handlePetitionShowSignatures(network::Packet& packet);
    void handlePetitionSignResults(network::Packet& packet);
    void handleTurnInPetitionResults(network::Packet& packet);
    void handleBattlefieldStatus(network::Packet& packet);
    void handleBattlefieldList(network::Packet& packet);
    void handleRaidInstanceInfo(network::Packet& packet);
    void handleInstanceDifficulty(network::Packet& packet);
    void handleDuelRequested(network::Packet& packet);
    void handleDuelComplete(network::Packet& packet);
    void handleDuelWinner(network::Packet& packet);
    void handleLfgJoinResult(network::Packet& packet);
    void handleLfgQueueStatus(network::Packet& packet);
    void handleLfgProposalUpdate(network::Packet& packet);
    void handleLfgRoleCheckUpdate(network::Packet& packet);
    void handleLfgUpdatePlayer(network::Packet& packet);
    void handleLfgPlayerReward(network::Packet& packet);
    void handleLfgBootProposalUpdate(network::Packet& packet);
    void handleLfgTeleportDenied(network::Packet& packet);
    void handleArenaTeamCommandResult(network::Packet& packet);
    void handleArenaTeamQueryResponse(network::Packet& packet);
    void handleArenaTeamRoster(network::Packet& packet);
    void handleArenaTeamInvite(network::Packet& packet);
    void handleArenaTeamEvent(network::Packet& packet);
    void handleArenaTeamStats(network::Packet& packet);
    void handleArenaError(network::Packet& packet);
    void handlePvpLogData(network::Packet& packet);
    void handleInitializeFactions(network::Packet& packet);
    void handleSetFactionStanding(network::Packet& packet);
    void handleSetFactionAtWar(network::Packet& packet);
    void handleSetFactionVisible(network::Packet& packet);
    void handleGroupSetLeader(network::Packet& packet);
    void handleTalentsInfo(network::Packet& packet);
    void loadGuildNameCache();
    void saveGuildNameCache() const;
    void rememberGuildName(uint32_t guildId, const std::string& guildName);

    GameHandler& owner_;

    // ---- State ----

    // Inspect
    InspectResult inspectResult_;

    // Logout
    bool  loggingOut_        = false;
    bool  exitAfterLogout_   = false;
    float logoutCountdown_   = 0.0f;

    // Time played
    uint32_t totalTimePlayed_ = 0;
    uint32_t levelTimePlayed_ = 0;

    // Who results
    std::vector<WhoEntry> whoResults_;
    uint32_t whoOnlineCount_ = 0;

    // Duel
    bool pendingDuelRequest_    = false;
    uint64_t duelChallengerGuid_= 0;
    uint64_t duelFlagGuid_      = 0;
    std::string duelChallengerName_;
    uint32_t duelCountdownMs_   = 0;
    std::chrono::steady_clock::time_point duelCountdownStartedAt_{};

    // Guild
    std::string guildName_;
    std::vector<std::string> guildRankNames_;
    GuildRosterData guildRoster_;
    GuildInfoData guildInfoData_;
    GuildQueryResponseData guildQueryData_;
    bool hasGuildRoster_ = false;
    std::unordered_map<uint32_t, std::string> guildNameCache_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> pendingGuildNameQueries_;
    bool pendingGuildInvite_ = false;
    std::string pendingGuildInviterName_;
    std::string pendingGuildInviteGuildName_;
    bool showPetitionDialog_ = false;
    uint32_t petitionCost_ = 0;
    uint64_t petitionNpcGuid_ = 0;
    PetitionInfo petitionInfo_;

    // Group
    GroupListData partyData;
    bool pendingGroupInvite = false;
    std::string pendingInviterName;

    // Ready check
    bool        pendingReadyCheck_       = false;
    uint32_t    readyCheckReadyCount_    = 0;
    uint32_t    readyCheckNotReadyCount_ = 0;
    std::string readyCheckInitiator_;
    std::vector<ReadyCheckResult> readyCheckResults_;

    // Instance
    std::vector<InstanceLockout> instanceLockouts_;
    std::vector<GuildEventLogEntry> guildEventLog_;
    /// Six item tabs and the money log after them.
    std::array<std::vector<GuildBankLogEntry>, 7> guildBankLogs_;
    uint32_t instanceDifficulty_ = 0;
    bool instanceIsHeroic_ = false;
    bool inInstance_ = false;

    // Raid marks
    std::array<uint64_t, kRaidMarkCount> raidTargetGuids_ = {};

    // Encounter units
    std::array<uint64_t, kMaxEncounterSlots> encounterUnitGuids_ = {};
    InstanceLockPrompt instanceLock_;

    // Arena
    std::vector<ArenaTeamStats>  arenaTeamStats_;
    std::vector<ArenaTeamRoster> arenaTeamRosters_;

    // Battleground
    std::array<BgQueueSlot, 3> bgQueues_{};
    std::vector<AvailableBgInfo> availableBgs_;
    BgScoreboardData bgScoreboard_;
    std::vector<BgPlayerPosition> bgPlayerPositions_;

    // LFG / Dungeon Finder
    LfgState lfgState_        = LfgState::None;
    uint32_t lfgDungeonId_    = 0;
    uint32_t lfgProposalId_   = 0;
    /// The proposal the ready dialog has already been opened for. The server
    /// resends the update as each member answers, and opening again would
    /// reset the countdown and the ticks beside every name.
    uint32_t shownProposalId_ = 0;
    uint8_t  lfgOfferedRoles_ = 0;
    // The queue's own numbers, which never arrived while the length check was
    // longer than the packet.
    int32_t  lfgWaitTank_ = -1;
    int32_t  lfgWaitHealer_ = -1;
    int32_t  lfgWaitDps_ = -1;
    uint8_t  lfgNeedTank_ = 0;
    uint8_t  lfgNeedHealer_ = 0;
    uint8_t  lfgNeedDps_ = 0;
    uint64_t lfgBootVictimGuid_ = 0;
    /// Why the server will not let this character queue for a dungeon, by
    /// dungeon id. Empty means nothing is locked, which is also what it meant
    /// while SMSG_LFG_PLAYER_INFO was being skipped — the difference is that
    /// now it is an answer rather than an absence.
    std::unordered_map<uint32_t, uint32_t> lfgLocks_;
    std::vector<LfgReward> lfgRewards_;
    /// The group a proposal is offering, in the order the server lists it.
    std::vector<LfgProposalMember> lfgProposalMembers_;
    int32_t  lfgAvgWaitSec_   = -1;
    uint32_t lfgTimeInQueueMs_= 0;
    uint32_t lfgBootVotes_    = 0;
    uint32_t lfgBootTotal_    = 0;
    uint32_t lfgBootTimeLeft_ = 0;
    uint32_t lfgBootNeeded_   = 0;
    std::string lfgBootTargetName_;
    std::string lfgBootReason_;
};

} // namespace game
} // namespace wowee
