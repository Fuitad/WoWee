#include "quest_editor.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>

namespace wowee {
namespace editor {

void QuestEditor::addQuest(const Quest& q) {
    Quest quest = q;
    quest.id = nextId_++;
    quests_.push_back(quest);
    LOG_INFO("Quest added: [", quest.id, "] ", quest.title);
}

void QuestEditor::removeQuest(int index) {
    if (index >= 0 && index < static_cast<int>(quests_.size()))
        quests_.erase(quests_.begin() + index);
}

Quest* QuestEditor::getQuest(int index) {
    if (index < 0 || index >= static_cast<int>(quests_.size())) return nullptr;
    return &quests_[index];
}

bool QuestEditor::saveToFile(const std::string& path) const {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& q : quests_) {
        nlohmann::json jq;
        jq["id"] = q.id;
        jq["title"] = q.title;
        jq["description"] = q.description;
        jq["completionText"] = q.completionText;
        jq["requiredLevel"] = q.requiredLevel;
        jq["questGiverNpcId"] = q.questGiverNpcId;
        jq["turnInNpcId"] = q.turnInNpcId;
        jq["nextQuestId"] = q.nextQuestId;
        jq["reward"] = {{"xp", q.reward.xp}, {"gold", q.reward.gold},
                        {"silver", q.reward.silver}, {"copper", q.reward.copper}};
        nlohmann::json items = nlohmann::json::array();
        for (const auto& item : q.reward.itemRewards) items.push_back(item);
        jq["reward"]["items"] = items;

        nlohmann::json objs = nlohmann::json::array();
        for (const auto& obj : q.objectives) {
            objs.push_back({{"type", static_cast<int>(obj.type)},
                            {"desc", obj.description},
                            {"target", obj.targetName},
                            {"count", obj.targetCount}});
        }
        jq["objectives"] = objs;
        arr.push_back(jq);
    }

    std::ofstream f(path);
    if (!f) return false;
    f << arr.dump(2) << "\n";

    LOG_INFO("Quests saved: ", path, " (", quests_.size(), " quests)");
    return true;
}

bool QuestEditor::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    try {
        nlohmann::json arr = nlohmann::json::parse(f);
        if (!arr.is_array()) return false;

        quests_.clear();
        uint32_t maxId = 0;

        // Cap total quest count - a stale autosave or hand-edited file
        // could carry thousands of empty quests, each emitting a
        // quest_template INSERT (and queststarter/questender + chain
        // walks) on export. 4096 covers any realistic zone.
        constexpr size_t kMaxQuests = 4096;

        for (const auto& jq : arr) {
            if (quests_.size() >= kMaxQuests) {
                LOG_WARNING("Quest cap reached (", kMaxQuests,
                            ") - remaining entries dropped");
                break;
            }
            Quest q;
            q.id = jq.value("id", 0u);
            q.title = jq.value("title", "Untitled");
            q.description = jq.value("description", "");
            q.completionText = jq.value("completionText", "");
            // AzerothCore quest_template.LogTitle is varchar(200); the
            // Description/QuestCompletionLog are text but practically capped.
            // Truncate edited values so SQL writes don't get rejected by
            // length constraints or bloat the export.
            if (q.title.size() > 200) q.title.resize(200);
            if (q.description.size() > 8192) q.description.resize(8192);
            if (q.completionText.size() > 8192) q.completionText.resize(8192);
            q.requiredLevel = jq.value("requiredLevel", 1u);
            // WoW levels 1-80 (WotLK). Cap to keep AzerothCore happy and
            // catch obvious typos like "999".
            if (q.requiredLevel == 0) q.requiredLevel = 1;
            if (q.requiredLevel > 255) q.requiredLevel = 80;
            q.questGiverNpcId = jq.value("questGiverNpcId", 0u);
            q.turnInNpcId = jq.value("turnInNpcId", 0u);
            q.nextQuestId = jq.value("nextQuestId", 0u);

            if (jq.contains("reward")) {
                const auto& jr = jq["reward"];
                q.reward.xp = jr.value("xp", 100u);
                q.reward.gold = jr.value("gold", 0u);
                q.reward.silver = jr.value("silver", 0u);
                q.reward.copper = jr.value("copper", 0u);
                // Reward sanity caps. Highest WoW quest XP ~50k; gold realistic
                // cap is hundreds. Catches typo entries like "100000000 gold".
                if (q.reward.xp > 1'000'000) q.reward.xp = 1'000'000;
                if (q.reward.gold > 10000) q.reward.gold = 10000;
                if (q.reward.silver > 99) q.reward.silver = 99;
                if (q.reward.copper > 99) q.reward.copper = 99;
                if (jr.contains("items") && jr["items"].is_array()) {
                    for (const auto& item : jr["items"]) {
                        // Cap item reward count to 6 (WoW quest_template
                        // RewardItemId[1..6] slot capacity).
                        if (q.reward.itemRewards.size() >= 6) break;
                        q.reward.itemRewards.push_back(item.get<std::string>());
                    }
                }
            }

            if (jq.contains("objectives") && jq["objectives"].is_array()) {
                for (const auto& jo : jq["objectives"]) {
                    QuestObjective obj;
                    int t = jo.value("type", 0);
                    // Clamp to known QuestObjectiveType range to avoid
                    // garbage enum values from edited JSON.
                    if (t < 0 || t > 5) t = 0;
                    obj.type = static_cast<QuestObjectiveType>(t);
                    obj.description = jo.value("desc", "");
                    obj.targetName = jo.value("target", "");
                    obj.targetCount = jo.value("count", 1u);
                    if (obj.targetCount == 0) obj.targetCount = 1;
                    if (obj.targetCount > 1000) obj.targetCount = 1000;
                    // Cap stored objectives to 10 (matches SQL slot capacity)
                    // - also bounds the per-quest memory.
                    if (q.objectives.size() >= 10) break;
                    q.objectives.push_back(obj);
                }
            }

            if (q.id > maxId) maxId = q.id;
            quests_.push_back(q);
        }

        nextId_ = maxId + 1;
        LOG_INFO("Quests loaded: ", path, " (", quests_.size(), " quests)");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load quests from ", path, ": ", e.what());
        return false;
    }
}

bool QuestEditor::validateChains(std::vector<std::string>& errors) const {
    errors.clear();
    std::unordered_set<uint32_t> validIds;
    std::unordered_map<uint32_t, uint32_t> nextById; // id -> nextId
    for (const auto& q : quests_) {
        validIds.insert(q.id);
        nextById[q.id] = q.nextQuestId;
    }

    for (const auto& q : quests_) {
        if (q.nextQuestId != 0 && validIds.find(q.nextQuestId) == validIds.end()) {
            errors.push_back("Quest [" + std::to_string(q.id) + "] \"" + q.title +
                           "\" chains to non-existent quest " + std::to_string(q.nextQuestId));
        }

        // Quest with no questgiver and no turn-in is unreachable in-game.
        // Common authoring mistake - flag it so the player isn't stuck
        // wondering why a quest never appears.
        if (q.questGiverNpcId == 0 && q.turnInNpcId == 0) {
            errors.push_back("Quest [" + std::to_string(q.id) + "] \"" + q.title +
                           "\" has no questgiver or turn-in NPC (unreachable)");
        }

        // Circular chain detection. Use the precomputed map so the inner
        // lookup is O(1) instead of O(n) - was O(n²) per starting quest.
        if (q.nextQuestId != 0) {
            std::unordered_set<uint32_t> visited;
            uint32_t current = q.id;
            while (current != 0) {
                if (!visited.insert(current).second) {
                    errors.push_back("Circular quest chain detected starting from quest [" +
                                   std::to_string(q.id) + "] \"" + q.title + "\"");
                    break;
                }
                auto it = nextById.find(current);
                current = (it != nextById.end()) ? it->second : 0;
            }
        }
    }
    return errors.empty();
}

} // namespace editor
} // namespace wowee
