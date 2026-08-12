#include "npc_spawner.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <random>
#include <algorithm>
#include <filesystem>

namespace wowee {
namespace editor {

uint32_t NpcSpawner::nextId() { return idCounter_++; }

void NpcSpawner::placeCreature(const CreatureSpawn& spawn) {
    CreatureSpawn s = spawn;
    s.id = nextId();
    s.selected = false;
    spawns_.push_back(s);
    LOG_INFO("Creature placed: ", s.name, " (id=", s.id, ") at (",
             s.position.x, ",", s.position.y, ",", s.position.z, ")");
}

void NpcSpawner::removeCreature(int index) {
    if (index < 0 || index >= static_cast<int>(spawns_.size())) return;
    spawns_.erase(spawns_.begin() + index);
    if (selectedIdx_ == index) selectedIdx_ = -1;
    else if (selectedIdx_ > index) selectedIdx_--;
}

int NpcSpawner::selectAt(const glm::vec3& worldPos, float maxDist) {
    clearSelection();
    if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.y) ||
        !std::isfinite(worldPos.z) || !std::isfinite(maxDist)) return -1;
    float bestDist = maxDist;
    int bestIdx = -1;
    for (int i = 0; i < static_cast<int>(spawns_.size()); i++) {
        const auto& p = spawns_[i].position;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
        float dist = glm::length(p - worldPos);
        if (dist < bestDist) { bestDist = dist; bestIdx = i; }
    }
    if (bestIdx >= 0) {
        selectedIdx_ = bestIdx;
        spawns_[bestIdx].selected = true;
    }
    return bestIdx;
}

void NpcSpawner::clearSelection() {
    if (selectedIdx_ >= 0 && selectedIdx_ < static_cast<int>(spawns_.size()))
        spawns_[selectedIdx_].selected = false;
    selectedIdx_ = -1;
}

CreatureSpawn* NpcSpawner::getSelected() {
    if (selectedIdx_ < 0 || selectedIdx_ >= static_cast<int>(spawns_.size())) return nullptr;
    return &spawns_[selectedIdx_];
}

bool NpcSpawner::saveToFile(const std::string& path) const {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);

    // nlohmann::json throws on NaN/inf serialization. Scrub on the way
    // out so a stale in-memory state can't take down the whole save.
    auto san = [](float x) { return std::isfinite(x) ? x : 0.0f; };
    auto sanPos = [&](float x, float def) { return std::isfinite(x) ? x : def; };
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : spawns_) {
        nlohmann::json js;
        js["id"] = s.id;
        js["name"] = s.name;
        js["model"] = s.modelPath;
        js["displayId"] = s.displayId;
        js["position"] = {san(s.position.x), san(s.position.y), san(s.position.z)};
        js["orientation"] = san(s.orientation);
        js["scale"] = sanPos(s.scale, 1.0f);
        js["level"] = s.level;
        js["health"] = s.health;
        js["mana"] = s.mana;
        js["minDamage"] = s.minDamage;
        js["maxDamage"] = s.maxDamage;
        js["armor"] = s.armor;
        js["faction"] = s.faction;
        js["behavior"] = static_cast<int>(s.behavior);
        js["wanderRadius"] = san(s.wanderRadius);
        js["aggroRadius"] = san(s.aggroRadius);
        js["leashRadius"] = san(s.leashRadius);
        js["respawnTimeMs"] = s.respawnTimeMs;
        js["hostile"] = s.hostile;
        js["questgiver"] = s.questgiver;
        js["vendor"] = s.vendor;
        js["flightmaster"] = s.flightmaster;
        js["innkeeper"] = s.innkeeper;
        js["trainer"] = s.trainer;
        js["auctioneer"] = s.auctioneer;
        js["banker"] = s.banker;
        js["repair"] = s.repair;

        nlohmann::json patrol = nlohmann::json::array();
        for (const auto& p : s.patrolPath) {
            patrol.push_back({san(p.position.x), san(p.position.y),
                              san(p.position.z), sanPos(p.waitTimeMs, 2000.0f)});
        }
        js["patrol"] = patrol;
        arr.push_back(js);
    }

    std::ofstream f(path);
    if (!f) { LOG_ERROR("Failed to write NPC file: ", path); return false; }
    f << arr.dump(2) << "\n";

    LOG_INFO("NPC spawns saved: ", path, " (", spawns_.size(), " creatures)");
    return true;
}

void NpcSpawner::scatter(const CreatureSpawn& base, const glm::vec3& center,
                          float radius, int count) {
    // Defensive bounds - UI sliders cap these, but the function is also
    // callable programmatically. radius<=0 would either throw from the
    // uniform distribution constructor or divide by zero on the sqrt
    // line; an absurd count would freeze the editor and OOM.
    if (count <= 0 || count > 10000) return;
    if (!std::isfinite(radius) || radius <= 0.0f) return;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(center.z)) return;

    std::mt19937 rng(static_cast<uint32_t>(center.x * 100 + center.y * 37));
    std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> distDist(0.0f, radius);
    std::uniform_real_distribution<float> distRot(0.0f, 360.0f);

    for (int i = 0; i < count; i++) {
        float angle = distAngle(rng);
        float dist = std::sqrt(distDist(rng) / radius) * radius;
        CreatureSpawn s = base;
        s.position = center + glm::vec3(std::cos(angle) * dist, std::sin(angle) * dist, 0.0f);
        s.orientation = distRot(rng);
        placeCreature(s);
    }
}

bool NpcSpawner::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { LOG_ERROR("Failed to open NPC file: ", path); return false; }

    try {
        auto arr = nlohmann::json::parse(f);
        if (!arr.is_array()) return false;

        spawns_.clear();
        selectedIdx_ = -1;
        idCounter_ = 1;

        // Cap NPC count - same defense pattern as QuestEditor / ObjectPlacer.
        // 50k creatures is generous; each emits a creature_template +
        // creature INSERT, plus optional addon/waypoint rows.
        constexpr size_t kMaxSpawns = 50'000;

        for (const auto& js : arr) {
            if (spawns_.size() >= kMaxSpawns) {
                LOG_WARNING("NPC cap reached (", kMaxSpawns,
                            ") - remaining entries dropped");
                break;
            }
            CreatureSpawn s;
            s.name = js.value("name", "");
            s.modelPath = js.value("model", "");
            // creature_template.name is varchar(100); modelPath is internal
            // but capped to keep export sane. Trim instead of dropping.
            if (s.name.size() > 100) s.name.resize(100);
            if (s.modelPath.size() > 1024) s.modelPath.resize(1024);
            s.displayId = js.value("displayId", 0u);
            s.orientation = js.value("orientation", 0.0f);
            // Normalise orientation to [0, 360) for consistent gizmo behaviour.
            if (std::isfinite(s.orientation)) {
                s.orientation = std::fmod(s.orientation, 360.0f);
                if (s.orientation < 0.0f) s.orientation += 360.0f;
            } else {
                s.orientation = 0.0f;
            }
            s.scale = js.value("scale", 1.0f);
            if (!std::isfinite(s.scale) || s.scale < 0.1f) s.scale = 1.0f;
            s.level = js.value("level", 1u);
            // WoW level cap is 80 (WotLK) but allow up to 255 for special
            // bosses; 0 is invalid.
            if (s.level == 0) s.level = 1;
            if (s.level > 255) s.level = 255;
            s.health = js.value("health", 100u);
            if (s.health == 0) s.health = 1;
            s.mana = js.value("mana", 0u);
            s.minDamage = js.value("minDamage", 5u);
            s.maxDamage = js.value("maxDamage", 10u);
            // maxDmg should be >= minDmg.
            if (s.maxDamage < s.minDamage) s.maxDamage = s.minDamage;
            s.armor = js.value("armor", 0u);
            s.faction = js.value("faction", 0u);
            int beh = js.value("behavior", 0);
            if (beh < 0 || beh > 3) beh = 0;
            s.behavior = static_cast<CreatureBehavior>(beh);
            s.wanderRadius = js.value("wanderRadius", 0.0f);
            if (!std::isfinite(s.wanderRadius) || s.wanderRadius < 0.0f) s.wanderRadius = 0.0f;
            if (s.wanderRadius > 1000.0f) s.wanderRadius = 1000.0f;
            s.aggroRadius = js.value("aggroRadius", 15.0f);
            if (!std::isfinite(s.aggroRadius) || s.aggroRadius < 0.0f) s.aggroRadius = 0.0f;
            s.leashRadius = js.value("leashRadius", 40.0f);
            if (!std::isfinite(s.leashRadius) || s.leashRadius < 0.0f) s.leashRadius = 40.0f;
            s.respawnTimeMs = js.value("respawnTimeMs", 60000u);
            // Cap respawn at 24h - typo guard, also matches AzerothCore.
            if (s.respawnTimeMs > 86'400'000u) s.respawnTimeMs = 86'400'000u;
            if (s.respawnTimeMs < 1000u) s.respawnTimeMs = 1000u;
            s.hostile = js.value("hostile", false);
            s.questgiver = js.value("questgiver", false);
            s.vendor = js.value("vendor", false);
            s.flightmaster = js.value("flightmaster", false);
            s.innkeeper = js.value("innkeeper", false);
            s.trainer = js.value("trainer", false);
            s.auctioneer = js.value("auctioneer", false);
            s.banker = js.value("banker", false);
            s.repair = js.value("repair", false);

            if (js.contains("position") && js["position"].is_array() && js["position"].size() >= 3) {
                s.position = glm::vec3(js["position"][0].get<float>(),
                                       js["position"][1].get<float>(),
                                       js["position"][2].get<float>());
                // Reject NaN/inf positions - they crash the M2 renderer's
                // matrix math and produce invisible / chaos-shaped instances.
                if (!std::isfinite(s.position.x) || !std::isfinite(s.position.y) ||
                    !std::isfinite(s.position.z)) {
                    s.position = glm::vec3(0.0f);
                }
            }

            if (js.contains("patrol") && js["patrol"].is_array()) {
                // Cap patrol size at 256 waypoints - covers any realistic
                // route and keeps SQL export size bounded for malformed
                // input (a stale autosave that grew unbounded).
                constexpr size_t kMaxPatrolPoints = 256;
                for (const auto& pt : js["patrol"]) {
                    if (s.patrolPath.size() >= kMaxPatrolPoints) break;
                    if (pt.is_array() && pt.size() >= 4) {
                        PatrolPoint pp;
                        pp.position = glm::vec3(pt[0].get<float>(), pt[1].get<float>(), pt[2].get<float>());
                        // Skip waypoints with NaN/inf - would produce a path
                        // that warps the creature to garbage coords.
                        if (!std::isfinite(pp.position.x) || !std::isfinite(pp.position.y) ||
                            !std::isfinite(pp.position.z))
                            continue;
                        // Read waitTime as int64 then clamp to uint32_t range.
                        // pt[3].get<uint32_t> would throw json::type_error on
                        // a negative or out-of-range integer and abort the
                        // whole NPC file load on a single bad waypoint.
                        int64_t rawWait = pt[3].is_number_float()
                            ? static_cast<int64_t>(pt[3].get<double>())
                            : pt[3].get<int64_t>();
                        if (rawWait < 0) rawWait = 0;
                        if (rawWait > 600000) rawWait = 600000;  // 10-min cap
                        pp.waitTimeMs = static_cast<uint32_t>(rawWait);
                        s.patrolPath.push_back(pp);
                    }
                }
            }

            if (!s.name.empty()) {
                // Preserve original id from JSON if present so quest hooks
                // (questGiverNpcId, turnInNpcId, KillCreature targetName)
                // remain stable across save/load. Bump idCounter past any
                // loaded value to avoid collisions with future placements.
                if (js.contains("id")) {
                    s.id = js["id"].get<uint32_t>();
                    if (s.id >= idCounter_) idCounter_ = s.id + 1;
                } else {
                    s.id = nextId();
                }
                spawns_.push_back(s);
            }
        }

        LOG_INFO("NPC spawns loaded: ", path, " (", spawns_.size(), " creatures)");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse NPC file: ", e.what());
        return false;
    }
}

} // namespace editor
} // namespace wowee
