#include "cli_clone.hpp"

#include "quest_editor.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleCloneQuest(int& i, int argc, char** argv) {
    // Duplicate a quest. Useful for templating: create a base
    // quest with objectives + rewards once, then clone N times
    // for variants ('Slay Wolves', 'Slay Bears' with the same
    // shape). Optional newTitle replaces the cloned copy's title;
    // omit to get '<original> (copy)'.
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string newTitle;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        newTitle = argv[++i];
    }
    std::string path = zoneDir + "/quests.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "clone-quest: %s not found\n", path.c_str());
        return 1;
    }
    int qIdx;
    try { qIdx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "clone-quest: bad questIdx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "clone-quest: failed to load %s\n", path.c_str());
        return 1;
    }
    if (qIdx < 0 || qIdx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "clone-quest: questIdx %d out of range [0, %zu)\n",
            qIdx, qe.questCount());
        return 1;
    }
    // Deep-copy by value via vector iteration; .objectives and
    // .reward are STL containers so the copy is automatic.
    wowee::editor::Quest clone = qe.getQuests()[qIdx];
    // Reset id so the editor's auto-id sequence assigns a fresh
    // one - addQuest does this internally if id==0.
    clone.id = 0;
    // Reset chain link too - copying a chained quest with the
    // same nextQuestId would corrupt the chain semantics.
    clone.nextQuestId = 0;
    clone.title = newTitle.empty()
        ? (clone.title + " (copy)")
        : newTitle;
    qe.addQuest(clone);
    if (!qe.saveToFile(path)) {
        std::fprintf(stderr, "clone-quest: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Cloned quest %d -> '%s' (now %zu total)\n",
                qIdx, clone.title.c_str(), qe.questCount());
    std::printf("  carried %zu objective(s), %zu item reward(s), xp=%u\n",
                clone.objectives.size(),
                clone.reward.itemRewards.size(),
                clone.reward.xp);
    return 0;
}

int handleCloneCreature(int& i, int argc, char** argv) {
    // Duplicate a creature spawn. Common workflow: design one
    // 'patrol guard' archetype, then clone it across spawn points
    // around a town. Preserves stats, faction, behavior, equipment;
    // resets id and offsets position by 5 yards by default so the
    // copy doesn't z-fight with the original.
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string newName;
    float dx = 5.0f, dy = 0.0f, dz = 0.0f;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        newName = argv[++i];
    }
    // Optional 3-axis offset after newName.
    if (i + 3 < argc && argv[i + 1][0] != '-') {
        try {
            dx = std::stof(argv[++i]);
            dy = std::stof(argv[++i]);
            dz = std::stof(argv[++i]);
        } catch (...) {
            std::fprintf(stderr, "clone-creature: bad offset coordinate\n");
            return 1;
        }
    }
    std::string path = zoneDir + "/creatures.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "clone-creature: %s not found\n", path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "clone-creature: bad idx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::NpcSpawner sp;
    if (!sp.loadFromFile(path)) {
        std::fprintf(stderr, "clone-creature: failed to load %s\n", path.c_str());
        return 1;
    }
    if (idx < 0 || idx >= static_cast<int>(sp.spawnCount())) {
        std::fprintf(stderr,
            "clone-creature: idx %d out of range [0, %zu)\n",
            idx, sp.spawnCount());
        return 1;
    }
    // Deep-copy by value; CreatureSpawn is POD-ish (vectors for
    // patrol points copy automatically).
    wowee::editor::CreatureSpawn clone = sp.getSpawns()[idx];
    clone.id = 0;  // addCreature auto-assigns a fresh id
    clone.name = newName.empty()
        ? (clone.name + " (copy)")
        : newName;
    clone.position.x += dx;
    clone.position.y += dy;
    clone.position.z += dz;
    // Patrol path is intentionally NOT offset - patrol points are
    // typically authored as world-space waypoints, not relative to
    // the spawn. Designers re-author the path if needed.
    sp.getSpawns().push_back(clone);
    if (!sp.saveToFile(path)) {
        std::fprintf(stderr, "clone-creature: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Cloned creature %d -> '%s' at (%.1f, %.1f, %.1f) (now %zu total)\n",
                idx, clone.name.c_str(),
                clone.position.x, clone.position.y, clone.position.z,
                sp.spawnCount());
    return 0;
}

int handleCloneObject(int& i, int argc, char** argv) {
    // Symmetric to --clone-creature/--clone-quest. Common
    // workflow: place one tree/lamp/barrel just right, then
    // clone N copies along a path or around a square. Default
    // 5-yard X offset prevents z-fighting; rotation/scale are
    // preserved so a tilted object stays tilted.
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    float dx = 5.0f, dy = 0.0f, dz = 0.0f;
    if (i + 3 < argc && argv[i + 1][0] != '-') {
        try {
            dx = std::stof(argv[++i]);
            dy = std::stof(argv[++i]);
            dz = std::stof(argv[++i]);
        } catch (...) {
            std::fprintf(stderr, "clone-object: bad offset\n");
            return 1;
        }
    }
    std::string path = zoneDir + "/objects.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "clone-object: %s not found\n", path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "clone-object: bad idx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::ObjectPlacer placer;
    if (!placer.loadFromFile(path)) {
        std::fprintf(stderr, "clone-object: failed to load %s\n", path.c_str());
        return 1;
    }
    auto& objs = placer.getObjects();
    if (idx < 0 || idx >= static_cast<int>(objs.size())) {
        std::fprintf(stderr,
            "clone-object: idx %d out of range [0, %zu)\n",
            idx, objs.size());
        return 1;
    }
    // Deep-copy by value. uniqueId is reset so the new object
    // doesn't collide with the source's identifier in any
    // downstream system that dedups by it.
    wowee::editor::PlacedObject clone = objs[idx];
    clone.uniqueId = 0;
    clone.selected = false;
    clone.position.x += dx;
    clone.position.y += dy;
    clone.position.z += dz;
    objs.push_back(clone);
    if (!placer.saveToFile(path)) {
        std::fprintf(stderr, "clone-object: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Cloned object %d -> '%s' at (%.1f, %.1f, %.1f) (now %zu total)\n",
                idx, clone.path.c_str(),
                clone.position.x, clone.position.y, clone.position.z,
                objs.size());
    return 0;
}

}  // namespace

bool handleClone(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--clone-quest") == 0 && i + 2 < argc) {
        outRc = handleCloneQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--clone-creature") == 0 && i + 2 < argc) {
        outRc = handleCloneCreature(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--clone-object") == 0 && i + 2 < argc) {
        outRc = handleCloneObject(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
