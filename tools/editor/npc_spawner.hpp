#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

enum class CreatureBehavior {
    Stationary,
    Patrol,
    Wander,
    Scripted
};

struct PatrolPoint {
    glm::vec3 position;
    float waitTimeMs = 2000.0f;
};

struct CreatureSpawn {
    uint32_t id = 0;
    std::string name = "Creature";
    std::string modelPath;
    uint32_t displayId = 0;

    // Position
    glm::vec3 position{0};
    float orientation = 0.0f; // degrees

    // Stats
    uint32_t level = 1;
    uint32_t health = 100;
    uint32_t mana = 0;
    uint32_t minDamage = 5;
    uint32_t maxDamage = 10;
    uint32_t armor = 0;
    uint32_t faction = 0; // 0 = neutral

    // Display - 1.0 matches AzerothCore's default creature scale.
    // Templates can be scaled higher per-NPC if needed.
    float scale = 1.0f;

    // Behavior. Default is Wander with a small radius so newly-placed
    // creatures actually move at runtime - Stationary was the old
    // default and was a frequent "my NPCs don't patrol" complaint.
    // Editor preview doesn't run AI; this kicks in once the zone ships.
    CreatureBehavior behavior = CreatureBehavior::Wander;
    float wanderRadius = 10.0f;
    float aggroRadius = 20.0f;
    float leashRadius = 40.0f;
    uint32_t respawnTimeMs = 300000;
    std::vector<PatrolPoint> patrolPath;

    // Flags
    bool hostile = false;
    bool questgiver = false;
    bool vendor = false;
    bool flightmaster = false;
    bool innkeeper = false;
    bool trainer = false;       // class/profession trainer (SQL npcflag 0x10)
    bool auctioneer = false;    // (SQL npcflag 0x200000)
    bool banker = false;        // (SQL npcflag 0x20000)
    bool repair = false;        // (SQL npcflag 0x1000)

    bool selected = false;
};

class NpcSpawner {
public:
    void placeCreature(const CreatureSpawn& spawn);
    void removeCreature(int index);

    int selectAt(const glm::vec3& worldPos, float maxDist = 30.0f);
    void clearSelection();
    CreatureSpawn* getSelected();
    int getSelectedIndex() const { return selectedIdx_; }

    const std::vector<CreatureSpawn>& getSpawns() const { return spawns_; }
    std::vector<CreatureSpawn>& getSpawns() { return spawns_; }
    size_t spawnCount() const { return spawns_.size(); }
    void clearAll() { spawns_.clear(); selectedIdx_ = -1; idCounter_ = 1; }

    // Serialize to/from JSON
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    // Template creature for placement
    CreatureSpawn& getTemplate() { return template_; }

    // Scatter: place multiple copies in a radius around a point
    void scatter(const CreatureSpawn& base, const glm::vec3& center,
                 float radius, int count);

private:
    uint32_t nextId();
    std::vector<CreatureSpawn> spawns_;
    int selectedIdx_ = -1;
    uint32_t idCounter_ = 1;
    CreatureSpawn template_;
};

} // namespace editor
} // namespace wowee
