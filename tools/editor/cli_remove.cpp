#include "cli_remove.hpp"

#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleRemoveCreature(int& i, int /*argc*/, char** argv) {
    // Remove a creature spawn by 0-based index. Pair with
    // --info-creatures (or your editor) to find the right index
    // first; nothing identifies entries reliably across reloads.
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string path = zoneDir + "/creatures.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "remove-creature: %s not found\n", path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "remove-creature: bad index '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::NpcSpawner sp;
    sp.loadFromFile(path);
    if (idx < 0 || idx >= static_cast<int>(sp.spawnCount())) {
        std::fprintf(stderr, "remove-creature: index %d out of range [0, %zu)\n",
                     idx, sp.spawnCount());
        return 1;
    }
    std::string removedName = sp.getSpawns()[idx].name;
    sp.removeCreature(idx);
    if (!sp.saveToFile(path)) {
        std::fprintf(stderr, "remove-creature: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Removed creature '%s' (was index %d) from %s (now %zu total)\n",
                removedName.c_str(), idx, path.c_str(), sp.spawnCount());
    return 0;
}

int handleRemoveObject(int& i, int /*argc*/, char** argv) {
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string path = zoneDir + "/objects.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "remove-object: %s not found\n", path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "remove-object: bad index '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::ObjectPlacer placer;
    placer.loadFromFile(path);
    auto& objs = placer.getObjects();
    if (idx < 0 || idx >= static_cast<int>(objs.size())) {
        std::fprintf(stderr, "remove-object: index %d out of range [0, %zu)\n",
                     idx, objs.size());
        return 1;
    }
    std::string removedPath = objs[idx].path;
    objs.erase(objs.begin() + idx);
    if (!placer.saveToFile(path)) {
        std::fprintf(stderr, "remove-object: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Removed object '%s' (was index %d) from %s (now %zu total)\n",
                removedPath.c_str(), idx, path.c_str(), objs.size());
    return 0;
}

int handleRemoveQuest(int& i, int /*argc*/, char** argv) {
    std::string zoneDir = argv[++i];
    std::string idxStr = argv[++i];
    std::string path = zoneDir + "/quests.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "remove-quest: %s not found\n", path.c_str());
        return 1;
    }
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "remove-quest: bad index '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::QuestEditor qe;
    qe.loadFromFile(path);
    if (idx < 0 || idx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr, "remove-quest: index %d out of range [0, %zu)\n",
                     idx, qe.questCount());
        return 1;
    }
    std::string removedTitle = qe.getQuests()[idx].title;
    qe.removeQuest(idx);
    if (!qe.saveToFile(path)) {
        std::fprintf(stderr, "remove-quest: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("Removed quest '%s' (was index %d) from %s (now %zu total)\n",
                removedTitle.c_str(), idx, path.c_str(), qe.questCount());
    return 0;
}

int handleRemoveItem(int& i, int /*argc*/, char** argv) {
    // Remove the item at given 0-based index from <zoneDir>/
    // items.json. Mirrors --remove-creature/--remove-object/
    // --remove-quest semantics - bounds-checked, file rewrites
    // on success, exit 1 on out-of-range.
    std::string zoneDir = argv[++i];
    int idx = -1;
    try { idx = std::stoi(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "remove-item: index must be an integer\n");
        return 1;
    }
    namespace fs = std::filesystem;
    std::string path = zoneDir + "/items.json";
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "remove-item: %s has no items.json\n", zoneDir.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        std::ifstream in(path);
        in >> doc;
    } catch (...) {
        std::fprintf(stderr,
            "remove-item: %s is not valid JSON\n", path.c_str());
        return 1;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        std::fprintf(stderr,
            "remove-item: %s has no 'items' array\n", path.c_str());
        return 1;
    }
    auto& items = doc["items"];
    if (idx < 0 || static_cast<size_t>(idx) >= items.size()) {
        std::fprintf(stderr,
            "remove-item: index %d out of range (have %zu)\n",
            idx, items.size());
        return 1;
    }
    std::string removedName = items[idx].value("name", std::string("(unnamed)"));
    uint32_t removedId = items[idx].value("id", 0u);
    items.erase(items.begin() + idx);
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr,
            "remove-item: failed to write %s\n", path.c_str());
        return 1;
    }
    out << doc.dump(2);
    out.close();
    std::printf("Removed item '%s' (id=%u) from %s (now %zu total)\n",
                removedName.c_str(), removedId,
                path.c_str(), items.size());
    return 0;
}

}  // namespace

bool handleRemove(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--remove-creature") == 0 && i + 2 < argc) {
        outRc = handleRemoveCreature(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--remove-object") == 0 && i + 2 < argc) {
        outRc = handleRemoveObject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--remove-quest") == 0 && i + 2 < argc) {
        outRc = handleRemoveQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--remove-item") == 0 && i + 2 < argc) {
        outRc = handleRemoveItem(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
