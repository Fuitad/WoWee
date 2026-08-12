#include "sql_exporter.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include <fstream>
#include <filesystem>
#include <ctime>
#include <chrono>
#include <unordered_map>
#include <cmath>

namespace wowee {
namespace editor {

std::string SQLExporter::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        // MySQL/MariaDB string-literal escape rules. The backslash sequences
        // are the canonical way; doubled single quote works inside single-
        // quoted strings either way (matches AzerothCore's import scripts).
        // Stripping NUL prevents premature string termination in clients
        // that don't fully respect length-prefixed strings; \r/\n keep
        // each INSERT on its own line for human-readable export files.
        switch (c) {
            case '\'': out += "''"; break;
            case '\\': out += "\\\\"; break;
            case '\0': /* drop NUL */ break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case 26:   out += "\\Z"; break; // Ctrl-Z
            default:   out += c; break;
        }
    }
    return out;
}

static std::string escapeSql(const std::string& s) { return SQLExporter::escape(s); }

bool SQLExporter::exportCreatures(const std::vector<CreatureSpawn>& spawns,
                                   const std::string& path,
                                   uint32_t mapId,
                                   uint32_t startEntry) {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());

    std::ofstream f(path);
    if (!f) return false;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&time));

    f << "-- Wowee World Editor - Creature Spawn Export\n";
    f << "-- Generated: " << timeBuf << "\n";
    f << "-- Target: AzerothCore / TrinityCore 3.3.5a\n";
    f << "-- Map ID: " << mapId << "\n";
    f << "-- Creatures: " << spawns.size() << "\n\n";

    // creature_template entries
    f << "-- =============================================\n";
    f << "-- creature_template (NPC definitions)\n";
    f << "-- =============================================\n\n";

    for (size_t i = 0; i < spawns.size(); i++) {
        const auto& s = spawns[i];
        uint32_t entry = startEntry + static_cast<uint32_t>(i);

        // AzerothCore creature_template.npcflag bits.
        uint32_t npcFlags = 0;
        if (s.questgiver) npcFlags |= 0x02;
        if (s.trainer) npcFlags |= 0x10;
        if (s.vendor) npcFlags |= 0x80;
        if (s.repair) npcFlags |= 0x1000;
        if (s.innkeeper) npcFlags |= 0x10000;
        if (s.banker) npcFlags |= 0x20000;
        if (s.auctioneer) npcFlags |= 0x200000;
        if (s.flightmaster) npcFlags |= 0x02000000;

        uint32_t unitFlags = 0;
        if (!s.hostile) unitFlags |= 0x02; // NON_ATTACKABLE

        // displayId=0 results in an invisible NPC in-game. Fall back to
        // 11707 (a generic humanoid) so the export is at least usable;
        // the user can fix it after if they meant something else.
        uint32_t displayId = s.displayId == 0 ? 11707 : s.displayId;

        f << "INSERT INTO `creature_template` "
          << "(`entry`, `name`, `minlevel`, `maxlevel`, `minhealth`, `maxhealth`, "
          << "`mana`, `armor`, `mindmg`, `maxdmg`, `faction`, `npcflag`, "
          << "`unit_flags`, `modelid1`, `scale`) VALUES ("
          << entry << ", "
          << "'" << escapeSql(s.name) << "', "
          << s.level << ", " << s.level << ", "
          << s.health << ", " << s.health << ", "
          << s.mana << ", " << s.armor << ", "
          << s.minDamage << ", " << s.maxDamage << ", "
          << s.faction << ", " << npcFlags << ", "
          << unitFlags << ", "
          << displayId << ", "
          << s.scale
          << ") ON DUPLICATE KEY UPDATE `name`='" << escapeSql(s.name) << "';\n";
    }

    // creature spawn entries
    f << "\n-- =============================================\n";
    f << "-- creature (spawn positions)\n";
    f << "-- =============================================\n\n";

    for (size_t i = 0; i < spawns.size(); i++) {
        const auto& s = spawns[i];
        uint32_t entry = startEntry + static_cast<uint32_t>(i);
        uint32_t guid = startEntry + static_cast<uint32_t>(i);

        // movementType: 0=stationary, 1=wander, 2=waypoint (patrol).
        // Patrol with no waypoints or Wander with zero radius would either
        // log an error or spawn a creature that pretends to wander but
        // never moves. Downgrade to stationary in both cases.
        uint8_t movementType = 0;
        if (s.behavior == CreatureBehavior::Wander && s.wanderRadius > 0.01f)
            movementType = 1;
        if (s.behavior == CreatureBehavior::Patrol && !s.patrolPath.empty())
            movementType = 2;

        // Editor stores positions in render coords; AzerothCore expects WoW
        // canonical (X=north, Y=west). renderToCanonical handles the swap.
        // Sanitize each component - ostream prints NaN as "nan" which
        // AzerothCore's SQL import will reject.
        glm::vec3 wow = core::coords::renderToCanonical(s.position);
        if (!std::isfinite(wow.x)) wow.x = 0.0f;
        if (!std::isfinite(wow.y)) wow.y = 0.0f;
        if (!std::isfinite(wow.z)) wow.z = 0.0f;
        const float wowX = wow.x;
        const float wowY = wow.y;
        const float wowZ = wow.z;
        // AzerothCore expects orientation in radians from +X (north). Editor's
        // orientation is in degrees from +renderX (west). Convert via:
        //   wowYaw = π/2 - editorYaw
        constexpr float kPi = 3.14159265358979323846f;
        float orientation = std::isfinite(s.orientation) ? s.orientation : 0.0f;
        const float editorYawRad = orientation * kPi / 180.0f;
        float orientRad = kPi * 0.5f - editorYawRad;
        while (orientRad < 0.0f) orientRad += 2.0f * kPi;
        while (orientRad >= 2.0f * kPi) orientRad -= 2.0f * kPi;
        f << "INSERT INTO `creature` "
          << "(`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, "
          << "`orientation`, `spawntimesecs`, `wander_distance`, `MovementType`) VALUES ("
          << guid << ", " << entry << ", " << mapId << ", "
          << wowX << ", " << wowY << ", " << wowZ << ", "
          << orientRad << ", "
          << (s.respawnTimeMs / 1000) << ", "
          // wander_distance only meaningful for Wander behaviour; Patrol uses
          // waypoint_data and Stationary doesn't move at all.
          << (s.behavior == CreatureBehavior::Wander ? s.wanderRadius : 0.0f) << ", "
          << static_cast<int>(movementType)
          << ") ON DUPLICATE KEY UPDATE `position_x`=" << wowX << ";\n";
    }

    // Patrol waypoints
    bool hasPatrols = false;
    for (const auto& s : spawns) {
        if (!s.patrolPath.empty()) { hasPatrols = true; break; }
    }
    if (hasPatrols) {
        f << "\n-- =============================================\n";
        f << "-- creature_addon + waypoint_data (patrol paths)\n";
        f << "-- =============================================\n\n";

        for (size_t i = 0; i < spawns.size(); i++) {
            const auto& s = spawns[i];
            if (s.patrolPath.empty()) continue;
            uint32_t guid = startEntry + static_cast<uint32_t>(i);
            uint32_t pathId = guid;

            f << "INSERT INTO `creature_addon` (`guid`, `path_id`) VALUES ("
              << guid << ", " << pathId
              << ") ON DUPLICATE KEY UPDATE `path_id`=" << pathId << ";\n";

            for (size_t pi = 0; pi < s.patrolPath.size(); pi++) {
                const auto& wp = s.patrolPath[pi];
                glm::vec3 wpWow = core::coords::renderToCanonical(wp.position);
                if (!std::isfinite(wpWow.x)) wpWow.x = 0.0f;
                if (!std::isfinite(wpWow.y)) wpWow.y = 0.0f;
                if (!std::isfinite(wpWow.z)) wpWow.z = 0.0f;
                const float wpWowX = wpWow.x;
                const float wpWowY = wpWow.y;
                const float wpWowZ = wpWow.z;
                f << "INSERT INTO `waypoint_data` "
                  << "(`id`, `point`, `position_x`, `position_y`, `position_z`, `delay`) VALUES ("
                  << pathId << ", " << (pi + 1) << ", "
                  << wpWowX << ", " << wpWowY << ", " << wpWowZ << ", "
                  << wp.waitTimeMs
                  << ") ON DUPLICATE KEY UPDATE `position_x`=" << wpWowX << ";\n";
            }
        }
    }

    LOG_INFO("SQL exported: ", path, " (", spawns.size(), " creatures)");
    return true;
}

bool SQLExporter::exportQuests(const std::vector<Quest>& quests,
                                const std::string& path,
                                uint32_t startEntry,
                                const std::vector<CreatureSpawn>* spawns,
                                uint32_t creatureStartEntry) {
    // Build a spawn.id -> SQL creature entry map. Allows quest hooks defined
    // by the editor's UI (which uses spawn.id) to point at the matching
    // creature_template row (which uses creatureStartEntry + index).
    std::unordered_map<uint32_t, uint32_t> spawnIdToEntry;
    if (spawns) {
        for (size_t i = 0; i < spawns->size(); i++) {
            spawnIdToEntry[(*spawns)[i].id] = creatureStartEntry + static_cast<uint32_t>(i);
        }
    }
    auto resolveCreatureEntry = [&](uint32_t spawnId) {
        if (!spawns || spawns->empty()) return spawnId;
        auto it = spawnIdToEntry.find(spawnId);
        return it != spawnIdToEntry.end() ? it->second : spawnId;
    };
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());

    std::ofstream f(path, std::ios::app);
    if (!f) return false;

    if (quests.empty()) return true;

    f << "\n-- =============================================\n";
    f << "-- quest_template (quest definitions)\n";
    f << "-- =============================================\n\n";

    // Pre-scan for unsupported objective types and emit a single header
    // comment so users know which quests will need manual server-side
    // scripting after import.
    bool hasUnsupportedObj = false;
    for (const auto& q : quests) {
        for (const auto& obj : q.objectives) {
            if (obj.type == QuestObjectiveType::ExploreArea ||
                obj.type == QuestObjectiveType::EscortNPC ||
                obj.type == QuestObjectiveType::UseObject) {
                hasUnsupportedObj = true;
                break;
            }
        }
        if (hasUnsupportedObj) break;
    }
    if (hasUnsupportedObj) {
        f << "-- NOTE: some quests use objective types (ExploreArea, EscortNPC,\n"
          << "--       UseObject) that have no direct quest_template column.\n"
          << "--       Implement them via AzerothCore script_quest hooks.\n\n";
    }

    for (const auto& q : quests) {
        uint32_t entry = startEntry + q.id;
        uint32_t rewardMoney = q.reward.gold * 10000 + q.reward.silver * 100 + q.reward.copper;

        // Map up to 4 KillCreature objectives to RequiredNpcOrGo slots
        // and report objective counts in matching slots. Item objectives go
        // to RequiredItemId/Count. Editor stores the M2 path in targetName,
        // so creature objectives can only be linked when the user has filled
        // a numeric ID into targetName (rare); leave 0 otherwise.
        uint32_t reqNpcOrGo[4] = {0, 0, 0, 0};
        uint32_t reqNpcOrGoCount[4] = {0, 0, 0, 0};
        uint32_t reqItemId[6] = {0, 0, 0, 0, 0, 0};
        uint32_t reqItemCount[6] = {0, 0, 0, 0, 0, 0};
        size_t npcSlot = 0, itemSlot = 0;
        for (const auto& obj : q.objectives) {
            uint32_t id = 0;
            try { id = static_cast<uint32_t>(std::stoul(obj.targetName)); } catch (...) {}
            if (obj.type == QuestObjectiveType::KillCreature && npcSlot < 4) {
                // Editor UI fills targetName with a spawn.id for Kill objectives;
                // resolve to the matching SQL creature entry.
                reqNpcOrGo[npcSlot] = resolveCreatureEntry(id);
                reqNpcOrGoCount[npcSlot] = obj.targetCount;
                npcSlot++;
            } else if (obj.type == QuestObjectiveType::TalkToNPC && npcSlot < 4) {
                // AzerothCore reuses RequiredNpcOrGo for talk objectives -
                // count=1 indicates an interaction rather than a kill.
                reqNpcOrGo[npcSlot] = resolveCreatureEntry(id);
                reqNpcOrGoCount[npcSlot] = 1;
                npcSlot++;
            } else if (obj.type == QuestObjectiveType::CollectItem && itemSlot < 6) {
                reqItemId[itemSlot] = id;
                reqItemCount[itemSlot] = obj.targetCount;
                itemSlot++;
            }
            // ExploreArea / EscortNPC / UseObject have no direct
            // quest_template column; they need server scripts. Silently
            // skipped here (validateChains warns if a quest has no SQL-
            // representable objectives).
        }

        // Reward items - itemRewards entries are item IDs as strings;
        // try to parse as numeric. Anything unparseable becomes 0 and is
        // skipped by the count=0 check below.
        uint32_t rewardItemId[4] = {0, 0, 0, 0};
        uint32_t rewardItemCount[4] = {0, 0, 0, 0};
        for (size_t k = 0; k < q.reward.itemRewards.size() && k < 4; k++) {
            try {
                rewardItemId[k] = static_cast<uint32_t>(std::stoul(q.reward.itemRewards[k]));
                if (rewardItemId[k] != 0) rewardItemCount[k] = 1;
            } catch (...) { /* leave as 0 */ }
        }

        // Resolve quest-chain link to the matching SQL quest entry.
        // q.nextQuestId is editor-relative; the SQL row id is startEntry + id.
        uint32_t nextQuestSqlId = q.nextQuestId != 0
            ? startEntry + q.nextQuestId : 0;

        f << "INSERT INTO `quest_template` "
          << "(`ID`, `LogTitle`, `LogDescription`, `QuestCompletionLog`, "
          << "`MinLevel`, `RewardXP`, `RewardMoney`, `NextQuestInChain`, "
          << "`RequiredNpcOrGo1`, `RequiredNpcOrGoCount1`, "
          << "`RequiredNpcOrGo2`, `RequiredNpcOrGoCount2`, "
          << "`RequiredNpcOrGo3`, `RequiredNpcOrGoCount3`, "
          << "`RequiredNpcOrGo4`, `RequiredNpcOrGoCount4`, "
          << "`RequiredItemId1`, `RequiredItemCount1`, "
          << "`RequiredItemId2`, `RequiredItemCount2`, "
          << "`RequiredItemId3`, `RequiredItemCount3`, "
          << "`RequiredItemId4`, `RequiredItemCount4`, "
          << "`RequiredItemId5`, `RequiredItemCount5`, "
          << "`RequiredItemId6`, `RequiredItemCount6`, "
          << "`RewardItem1`, `RewardItemCount1`, "
          << "`RewardItem2`, `RewardItemCount2`, "
          << "`RewardItem3`, `RewardItemCount3`, "
          << "`RewardItem4`, `RewardItemCount4`) VALUES ("
          << entry << ", "
          << "'" << escapeSql(q.title) << "', "
          << "'" << escapeSql(q.description) << "', "
          << "'" << escapeSql(q.completionText) << "', "
          << q.requiredLevel << ", "
          << q.reward.xp << ", " << rewardMoney << ", " << nextQuestSqlId;
        for (int k = 0; k < 4; k++) f << ", " << reqNpcOrGo[k] << ", " << reqNpcOrGoCount[k];
        for (int k = 0; k < 6; k++) f << ", " << reqItemId[k] << ", " << reqItemCount[k];
        for (int k = 0; k < 4; k++) f << ", " << rewardItemId[k] << ", " << rewardItemCount[k];
        f << ") ON DUPLICATE KEY UPDATE `LogTitle`='" << escapeSql(q.title) << "';\n";
    }

    // creature_queststarter / creature_questender link quests to NPCs.
    // Without these the player can pick up the quest from no one.
    f << "\n-- =============================================\n";
    f << "-- creature_queststarter / _questender (quest links)\n";
    f << "-- =============================================\n\n";
    for (const auto& q : quests) {
        uint32_t entry = startEntry + q.id;
        if (q.questGiverNpcId > 0) {
            f << "INSERT IGNORE INTO `creature_queststarter` (`id`, `quest`) VALUES ("
              << resolveCreatureEntry(q.questGiverNpcId) << ", " << entry << ");\n";
        }
        if (q.turnInNpcId > 0) {
            f << "INSERT IGNORE INTO `creature_questender` (`id`, `quest`) VALUES ("
              << resolveCreatureEntry(q.turnInNpcId) << ", " << entry << ");\n";
        }
    }

    LOG_INFO("SQL quests exported: ", quests.size(), " quests");
    return true;
}

bool SQLExporter::exportAll(const std::vector<CreatureSpawn>& spawns,
                             const std::vector<Quest>& quests,
                             const std::string& path,
                             uint32_t mapId,
                             uint32_t startEntry) {
    if (!exportCreatures(spawns, path, mapId, startEntry)) return false;
    if (!quests.empty()) exportQuests(quests, path, startEntry, &spawns, startEntry);
    return true;
}

} // namespace editor
} // namespace wowee
