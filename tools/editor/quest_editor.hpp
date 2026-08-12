#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

enum class QuestObjectiveType {
    KillCreature,
    CollectItem,
    TalkToNPC,
    ExploreArea,
    EscortNPC,
    UseObject
};

/// The word an objective type is written as in a JSON sidecar and typed as on
/// the command line.
///
/// Five identical copies of this switch existed across four files that emit
/// quest JSON, plus a sixth spelling of the same vocabulary as an if-chain in
/// the handler that reads it back, plus a seventh in that handler's error
/// message listing what it accepts. The words are the format's contract: a
/// file written with one spelling and read with another does not fail, it
/// reads as a different kind of objective.
///
/// No default case, so an objective type added to the list above is a warning
/// here rather than a sidecar full of "unknown".
inline const char* questObjectiveTypeName(QuestObjectiveType type) {
    switch (type) {
        case QuestObjectiveType::KillCreature: return "kill";
        case QuestObjectiveType::CollectItem:  return "collect";
        case QuestObjectiveType::TalkToNPC:    return "talk";
        case QuestObjectiveType::ExploreArea:  return "explore";
        case QuestObjectiveType::EscortNPC:    return "escort";
        case QuestObjectiveType::UseObject:    return "use";
    }
    return "unknown";
}

/// The type behind that word. False when it is not one of them, so a caller
/// can say so rather than guess - which is why this answers into an out
/// parameter instead of picking KillCreature for a typo.
inline bool questObjectiveTypeFromName(const std::string& name, QuestObjectiveType& out) {
    if (name == "kill")    { out = QuestObjectiveType::KillCreature; return true; }
    if (name == "collect") { out = QuestObjectiveType::CollectItem;  return true; }
    if (name == "talk")    { out = QuestObjectiveType::TalkToNPC;    return true; }
    if (name == "explore") { out = QuestObjectiveType::ExploreArea;  return true; }
    if (name == "escort")  { out = QuestObjectiveType::EscortNPC;    return true; }
    if (name == "use")     { out = QuestObjectiveType::UseObject;    return true; }
    return false;
}

/// Every word this accepts, for the message that says so when it refuses.
/// Built from the same list, so a type added cannot go unmentioned.
inline std::string questObjectiveTypeNames() {
    std::string all;
    for (auto type : {QuestObjectiveType::KillCreature, QuestObjectiveType::CollectItem,
                      QuestObjectiveType::TalkToNPC, QuestObjectiveType::ExploreArea,
                      QuestObjectiveType::EscortNPC, QuestObjectiveType::UseObject}) {
        if (!all.empty()) all += "/";
        all += questObjectiveTypeName(type);
    }
    return all;
}

struct QuestObjective {
    QuestObjectiveType type = QuestObjectiveType::KillCreature;
    std::string description;
    std::string targetName;
    uint32_t targetCount = 1;
};

struct QuestReward {
    uint32_t xp = 100;
    uint32_t gold = 0;
    uint32_t silver = 0;
    uint32_t copper = 0;
    std::vector<std::string> itemRewards;
};

struct Quest {
    uint32_t id = 0;
    std::string title = "New Quest";
    std::string description;
    std::string completionText;
    uint32_t requiredLevel = 1;
    uint32_t questGiverNpcId = 0;
    uint32_t turnInNpcId = 0;
    std::vector<QuestObjective> objectives;
    QuestReward reward;
    uint32_t nextQuestId = 0; // chain link
};

class QuestEditor {
public:
    void addQuest(const Quest& q);
    void removeQuest(int index);
    Quest* getQuest(int index);
    const std::vector<Quest>& getQuests() const { return quests_; }
    size_t questCount() const { return quests_.size(); }

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    void clear() { quests_.clear(); nextId_ = 1; }

    // Quest chain validation
    bool validateChains(std::vector<std::string>& errors) const;

    Quest& getTemplate() { return template_; }

private:
    std::vector<Quest> quests_;
    Quest template_;
    uint32_t nextId_ = 1;
};

} // namespace editor
} // namespace wowee
