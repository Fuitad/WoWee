#pragma once

#include "npc_spawner.hpp"
#include "quest_editor.hpp"
#include <string>
#include <cstdint>

namespace wowee {
namespace editor {

class SQLExporter {
public:
    // Export creature spawns as AzerothCore/TrinityCore SQL
    static bool exportCreatures(const std::vector<CreatureSpawn>& spawns,
                                 const std::string& path,
                                 uint32_t mapId = 9000,
                                 uint32_t startEntry = 100000);

    // Export quest definitions as AzerothCore quest_template SQL.
    // `spawns` is used to translate the editor's per-spawn .id values stored
    // in quest links (questGiverNpcId, turnInNpcId, KillCreature targetName)
    // into the matching SQL `entry` (which is `creatureStartEntry + index`).
    // Pass an empty spawns vector if no translation is needed.
    static bool exportQuests(const std::vector<Quest>& quests,
                              const std::string& path,
                              uint32_t startEntry = 100000,
                              const std::vector<CreatureSpawn>* spawns = nullptr,
                              uint32_t creatureStartEntry = 100000);

    // Export everything to a single SQL file
    static bool exportAll(const std::vector<CreatureSpawn>& spawns,
                           const std::vector<Quest>& quests,
                           const std::string& path,
                           uint32_t mapId = 9000,
                           uint32_t startEntry = 100000);

    // Escape a string for safe inclusion inside single-quoted SQL literal.
    // Doubles single quotes and escapes backslashes - matches MySQL/MariaDB
    // string literal rules used by AzerothCore/TrinityCore. Use whenever you
    // emit user-provided text into SQL outside of this class.
    static std::string escape(const std::string& s);
};

} // namespace editor
} // namespace wowee
