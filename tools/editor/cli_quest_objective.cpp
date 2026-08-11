#include "cli_quest_objective.hpp"

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

int handleAddQuest(int& i, int argc, char** argv) {
    // Append a single quest to a zone's quests.json.
    // Args: <zoneDir> <title> [giverId] [turnInId] [xp] [level]
    std::string zoneDir = argv[++i];
    std::string title = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr, "add-quest: zone '%s' does not exist\n",
                     zoneDir.c_str());
        return 1;
    }
    wowee::editor::Quest q;
    q.title = title;
    // Optional positional args after title. Each is read in order;
    // an empty string or '-' stops consumption so users can omit
    // later fields.
    auto tryReadUint = [&](uint32_t& target) {
        if (i + 1 >= argc || argv[i + 1][0] == '-') return false;
        try {
            target = static_cast<uint32_t>(std::stoul(argv[i + 1]));
            ++i;
            return true;
        } catch (...) { return false; }
    };
    tryReadUint(q.questGiverNpcId);
    tryReadUint(q.turnInNpcId);
    tryReadUint(q.reward.xp);
    tryReadUint(q.requiredLevel);
    wowee::editor::QuestEditor qe;
    std::string path = zoneDir + "/quests.json";
    if (fs::exists(path)) qe.loadFromFile(path);
    qe.addQuest(q);
    if (!qe.saveToFile(path)) {
        std::fprintf(stderr, "add-quest: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Added quest '%s' to %s (now %zu total)\n",
                title.c_str(), path.c_str(), qe.questCount());
    return 0;
}

int handleAddQuestObjective(int& i, int argc, char** argv) {
    // Append a single objective to an existing quest. The quest
    // must already exist (use --add-quest first); index is 0-based
    // and matches --list-quests output.
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string typeStr = argv[++i];
    std::string targetName = argv[++i];
    std::string path = zoneDir + "/quests.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "add-quest-objective: %s not found — run --add-quest first\n",
                     path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "add-quest-objective: bad questIdx '%s'\n", idxStr.c_str());
        return 1;
    }
    using OT = wowee::editor::QuestObjectiveType;
    // The words, the mapping and the list in the refusal all come from beside
    // the enum. They were three separate spellings of one vocabulary, and the
    // one in the message is the copy that goes stale first.
    OT type;
    if (!wowee::editor::questObjectiveTypeFromName(typeStr, type)) {
        std::fprintf(stderr, "add-quest-objective: type must be %s, got '%s'\n",
                     wowee::editor::questObjectiveTypeNames().c_str(),
                     typeStr.c_str());
        return 1;
    }
    uint32_t count = 1;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try {
            count = static_cast<uint32_t>(std::stoul(argv[++i]));
            if (count == 0) count = 1;
        } catch (...) {}
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "add-quest-objective: failed to load %s\n", path.c_str());
        return 1;
    }
    if (idx < 0 || idx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "add-quest-objective: questIdx %d out of range [0, %zu)\n",
            idx, qe.questCount());
        return 1;
    }
    wowee::editor::QuestObjective obj;
    obj.type = type;
    obj.targetName = targetName;
    obj.targetCount = count;
    // Auto-generate a description from type+name+count so addons
    // and tooltips have something useful by default. The user can
    // edit quests.json directly if they want bespoke prose.
    const char* verb = "complete";
    switch (type) {
        case OT::KillCreature: verb = "Slay"; break;
        case OT::CollectItem:  verb = "Collect"; break;
        case OT::TalkToNPC:    verb = "Talk to"; break;
        case OT::ExploreArea:  verb = "Explore"; break;
        case OT::EscortNPC:    verb = "Escort"; break;
        case OT::UseObject:    verb = "Use"; break;
    }
    obj.description = std::string(verb) + " " +
                      (count > 1 ? std::to_string(count) + " " : "") +
                      targetName;
    // Quest is stored by value in the editor's vector; mutate via
    // the non-const getter, which gives us a pointer we can write
    // through.
    wowee::editor::Quest* q = qe.getQuest(idx);
    if (!q) {
        std::fprintf(stderr, "add-quest-objective: getQuest(%d) returned null\n", idx);
        return 1;
    }
    q->objectives.push_back(obj);
    if (!qe.saveToFile(path)) {
        std::fprintf(stderr, "add-quest-objective: failed to write %s\n",
                     path.c_str());
        return 1;
    }
    std::printf("Added objective '%s' to quest %d ('%s'), now %zu objective(s)\n",
                obj.description.c_str(), idx, q->title.c_str(),
                q->objectives.size());
    return 0;
}

int handleRemoveQuestObjective(int& i, int argc, char** argv) {
    // Symmetric counterpart to --add-quest-objective. Removes the
    // objective at <objIdx> within quest <questIdx>. Pair with
    // --info-quests / --list-quests to find the right indices.
    std::string zoneDir = argv[++i];
    std::string qIdxStr = argv[++i];
    std::string oIdxStr = argv[++i];
    std::string path = zoneDir + "/quests.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "remove-quest-objective: %s not found\n", path.c_str());
        return 1;
    }
    int qIdx, oIdx;
    try {
        qIdx = std::stoi(qIdxStr);
        oIdx = std::stoi(oIdxStr);
    } catch (...) {
        std::fprintf(stderr, "remove-quest-objective: bad index\n");
        return 1;
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "remove-quest-objective: failed to load %s\n",
                     path.c_str());
        return 1;
    }
    if (qIdx < 0 || qIdx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "remove-quest-objective: questIdx %d out of range [0, %zu)\n",
            qIdx, qe.questCount());
        return 1;
    }
    wowee::editor::Quest* q = qe.getQuest(qIdx);
    if (!q) return 1;
    if (oIdx < 0 || oIdx >= static_cast<int>(q->objectives.size())) {
        std::fprintf(stderr,
            "remove-quest-objective: objIdx %d out of range [0, %zu)\n",
            oIdx, q->objectives.size());
        return 1;
    }
    std::string removedDesc = q->objectives[oIdx].description;
    q->objectives.erase(q->objectives.begin() + oIdx);
    if (!qe.saveToFile(path)) {
        std::fprintf(stderr, "remove-quest-objective: failed to write %s\n",
                     path.c_str());
        return 1;
    }
    std::printf("Removed objective '%s' (was index %d) from quest %d ('%s'), now %zu remaining\n",
                removedDesc.c_str(), oIdx, qIdx, q->title.c_str(),
                q->objectives.size());
    return 0;
}

}  // namespace

bool handleQuestObjective(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--add-quest") == 0 && i + 2 < argc) {
        outRc = handleAddQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--add-quest-objective") == 0 && i + 4 < argc) {
        outRc = handleAddQuestObjective(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--remove-quest-objective") == 0 && i + 3 < argc) {
        outRc = handleRemoveQuestObjective(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
