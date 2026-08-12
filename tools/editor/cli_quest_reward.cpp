#include "cli_quest_reward.hpp"

#include "quest_editor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleAddQuestRewardItem(int& i, int argc, char** argv) {
    // Append one or more item rewards to a quest. Multiple paths
    // can be passed in a single invocation:
    //   --add-quest-reward-item zone 0 'Item:Sword' 'Item:Shield'
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string path = zoneDir + "/quests.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "add-quest-reward-item: %s not found\n", path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "add-quest-reward-item: bad questIdx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "add-quest-reward-item: failed to load %s\n", path.c_str());
        return 1;
    }
    if (idx < 0 || idx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "add-quest-reward-item: questIdx %d out of range [0, %zu)\n",
            idx, qe.questCount());
        return 1;
    }
    wowee::editor::Quest* q = qe.getQuest(idx);
    if (!q) return 1;
    int added = 0;
    // Greedy-consume any remaining args that don't start with '-'
    // so the caller can batch-add a whole loot table in one shot.
    while (i + 1 < argc && argv[i + 1][0] != '-') {
        q->reward.itemRewards.push_back(argv[++i]);
        added++;
    }
    if (added == 0) {
        std::fprintf(stderr, "add-quest-reward-item: need at least one itemPath\n");
        return 1;
    }
    if (!qe.saveToFile(path)) {
        std::fprintf(stderr, "add-quest-reward-item: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Added %d item reward(s) to quest %d ('%s'), now %zu total\n",
                added, idx, q->title.c_str(), q->reward.itemRewards.size());
    return 0;
}

int handleSetQuestReward(int& i, int argc, char** argv) {
    // Update XP / coin reward fields on an existing quest. Each
    // field is optional - only the ones explicitly passed are
    // changed. This avoids the round-trip-and-clobber footgun of
    // a "replace whole reward" command.
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string path = zoneDir + "/quests.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "set-quest-reward: %s not found\n", path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "set-quest-reward: bad questIdx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "set-quest-reward: failed to load %s\n", path.c_str());
        return 1;
    }
    if (idx < 0 || idx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "set-quest-reward: questIdx %d out of range [0, %zu)\n",
            idx, qe.questCount());
        return 1;
    }
    wowee::editor::Quest* q = qe.getQuest(idx);
    if (!q) return 1;
    int changed = 0;
    auto consumeUint = [&](const char* flag, uint32_t& target) {
        if (i + 2 < argc && std::strcmp(argv[i + 1], flag) == 0) {
            try {
                target = static_cast<uint32_t>(std::stoul(argv[i + 2]));
                i += 2;
                changed++;
                return true;
            } catch (...) {
                std::fprintf(stderr, "set-quest-reward: bad %s value '%s'\n",
                             flag, argv[i + 2]);
            }
        }
        return false;
    };
    // Loop until no more recognised flags consume their value -
    // order-independent, so callers can pass --gold then --xp.
    bool any = true;
    while (any) {
        any = false;
        if (consumeUint("--xp",     q->reward.xp))     any = true;
        if (consumeUint("--gold",   q->reward.gold))   any = true;
        if (consumeUint("--silver", q->reward.silver)) any = true;
        if (consumeUint("--copper", q->reward.copper)) any = true;
    }
    if (changed == 0) {
        std::fprintf(stderr,
            "set-quest-reward: no fields changed - pass --xp / --gold / --silver / --copper\n");
        return 1;
    }
    if (!qe.saveToFile(path)) {
        std::fprintf(stderr, "set-quest-reward: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Updated %d field(s) on quest %d ('%s'): xp=%u gold=%u silver=%u copper=%u\n",
                changed, idx, q->title.c_str(),
                q->reward.xp, q->reward.gold,
                q->reward.silver, q->reward.copper);
    return 0;
}

}  // namespace

bool handleQuestReward(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--add-quest-reward-item") == 0 && i + 3 < argc) {
        outRc = handleAddQuestRewardItem(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--set-quest-reward") == 0 && i + 2 < argc) {
        outRc = handleSetQuestReward(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
