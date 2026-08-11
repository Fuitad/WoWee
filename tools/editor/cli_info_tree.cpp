#include "cli_info_tree.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoZoneTree(int& i, int /*argc*/, char** argv) {
    // Pretty `tree`-style hierarchical view of a zone's contents.
    // Designed for at-a-glance comprehension — what creatures,
    // what objects, what quests, what tiles, what files. No
    // --json flag because the structured equivalent is just
    // running --info-* per category and concatenating.
    std::string zoneDir = argv[++i];
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "info-zone-tree: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "info-zone-tree: parse failed\n");
        return 1;
    }
    wowee::editor::NpcSpawner sp;
    sp.loadFromFile(zoneDir + "/creatures.json");
    wowee::editor::ObjectPlacer op;
    op.loadFromFile(zoneDir + "/objects.json");
    wowee::editor::QuestEditor qe;
    qe.loadFromFile(zoneDir + "/quests.json");
    // Walk on-disk files for the 'Files' branch.
    std::vector<std::string> diskFiles;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(zoneDir, ec)) {
        if (e.is_regular_file()) {
            diskFiles.push_back(e.path().filename().string());
        }
    }
    std::sort(diskFiles.begin(), diskFiles.end());
    // Tree-drawing helpers — Unix box characters since most
    // terminals support UTF-8 by default. Pre-compute prefix
    // strings so leaf vs branch alignment looks right.
    auto branch = [](bool last) { return last ? "└─ " : "├─ "; };
    auto cont   = [](bool last) { return last ? "   " : "│  "; };
    std::printf("%s/\n",
                zm.displayName.empty() ? zm.mapName.c_str()
                                        : zm.displayName.c_str());
    // Manifest section
    std::printf("├─ Manifest\n");
    std::printf("│  ├─ mapName     : %s\n", zm.mapName.c_str());
    std::printf("│  ├─ mapId       : %u\n", zm.mapId);
    std::printf("│  ├─ baseHeight  : %.1f\n", zm.baseHeight);
    std::printf("│  ├─ biome       : %s\n",
                zm.biome.empty() ? "(unset)" : zm.biome.c_str());
    std::printf("│  └─ flags       : %s%s%s%s\n",
                zm.allowFlying ? "fly " : "",
                zm.pvpEnabled  ? "pvp " : "",
                zm.isIndoor    ? "indoor " : "",
                zm.isSanctuary ? "sanctuary " : "");
    // Tiles
    std::printf("├─ Tiles (%zu)\n", zm.tiles.size());
    for (size_t k = 0; k < zm.tiles.size(); ++k) {
        bool last = (k == zm.tiles.size() - 1);
        std::printf("│  %s(%d, %d)\n", branch(last),
                    zm.tiles[k].first, zm.tiles[k].second);
    }
    // Creatures
    std::printf("├─ Creatures (%zu)\n", sp.spawnCount());
    for (size_t k = 0; k < sp.spawnCount(); ++k) {
        bool last = (k == sp.spawnCount() - 1);
        const auto& s = sp.getSpawns()[k];
        std::printf("│  %slvl %u  %s%s\n",
                    branch(last), s.level, s.name.c_str(),
                    s.hostile ? " [hostile]" : "");
    }
    // Objects
    std::printf("├─ Objects (%zu)\n", op.getObjects().size());
    for (size_t k = 0; k < op.getObjects().size(); ++k) {
        bool last = (k == op.getObjects().size() - 1);
        const auto& o = op.getObjects()[k];
        std::printf("│  %s%s  %s\n", branch(last),
                    o.type == wowee::editor::PlaceableType::M2 ? "m2 " : "wmo",
                    o.path.c_str());
    }
    // Quests with sub-tree of objectives
    std::printf("├─ Quests (%zu)\n", qe.questCount());
    using OT = wowee::editor::QuestObjectiveType;
        // The word is the format's, from beside the enum.
        auto typeName = wowee::editor::questObjectiveTypeName;
    for (size_t k = 0; k < qe.questCount(); ++k) {
        bool lastQ = (k == qe.questCount() - 1);
        const auto& q = qe.getQuests()[k];
        std::printf("│  %s[%u] %s (lvl %u, %u XP)\n",
                    branch(lastQ), q.id, q.title.c_str(),
                    q.requiredLevel, q.reward.xp);
        // Objectives indented under the quest. Use 'cont' for
        // the prior column so vertical bars align.
        for (size_t o = 0; o < q.objectives.size(); ++o) {
            bool lastO = (o == q.objectives.size() - 1 &&
                          q.reward.itemRewards.empty());
            const auto& obj = q.objectives[o];
            std::printf("│  %s%s%s ×%u %s\n",
                        cont(lastQ), branch(lastO),
                        typeName(obj.type), obj.targetCount,
                        obj.targetName.c_str());
        }
        for (size_t r = 0; r < q.reward.itemRewards.size(); ++r) {
            bool lastR = (r == q.reward.itemRewards.size() - 1);
            std::printf("│  %s%sreward: %s\n",
                        cont(lastQ), branch(lastR),
                        q.reward.itemRewards[r].c_str());
        }
    }
    // Files (last top-level branch — uses └─)
    std::printf("└─ Files (%zu)\n", diskFiles.size());
    for (size_t k = 0; k < diskFiles.size(); ++k) {
        bool last = (k == diskFiles.size() - 1);
        std::printf("   %s%s\n", branch(last), diskFiles[k].c_str());
    }
    return 0;
}

int handleInfoProjectTree(int& i, int /*argc*/, char** argv) {
    // Project-level tree view: every zone with quick counts +
    // bake/viewer status. --info-zone-tree drills into one zone;
    // this gives the bird's-eye view across the whole project.
    std::string projectDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "info-project-tree: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    struct ZE {
        std::string name, dir, mapName;
        int tiles = 0, creatures = 0, objects = 0, quests = 0;
        bool hasGlb = false, hasObj = false, hasStl = false;
        bool hasHtml = false, hasZoneMd = false;
    };
    std::vector<ZE> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        wowee::editor::ZoneManifest zm;
        if (!zm.load((entry.path() / "zone.json").string())) continue;
        ZE z;
        z.name = zm.displayName.empty() ? zm.mapName : zm.displayName;
        z.dir = entry.path().filename().string();
        z.mapName = zm.mapName;
        z.tiles = static_cast<int>(zm.tiles.size());
        wowee::editor::NpcSpawner sp;
        if (sp.loadFromFile((entry.path() / "creatures.json").string())) {
            z.creatures = static_cast<int>(sp.spawnCount());
        }
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile((entry.path() / "objects.json").string())) {
            z.objects = static_cast<int>(op.getObjects().size());
        }
        wowee::editor::QuestEditor qe;
        if (qe.loadFromFile((entry.path() / "quests.json").string())) {
            z.quests = static_cast<int>(qe.questCount());
        }
        z.hasGlb  = fs::exists(entry.path() / (zm.mapName + ".glb"));
        z.hasObj  = fs::exists(entry.path() / (zm.mapName + ".obj"));
        z.hasStl  = fs::exists(entry.path() / (zm.mapName + ".stl"));
        z.hasHtml = fs::exists(entry.path() / (zm.mapName + ".html"));
        z.hasZoneMd = fs::exists(entry.path() / "ZONE.md");
        zones.push_back(std::move(z));
    }
    std::sort(zones.begin(), zones.end(),
              [](const ZE& a, const ZE& b) { return a.name < b.name; });
    int totalTiles = 0, totalCreat = 0, totalObj = 0, totalQuest = 0;
    for (const auto& z : zones) {
        totalTiles += z.tiles; totalCreat += z.creatures;
        totalObj += z.objects; totalQuest += z.quests;
    }
    std::printf("%s/  (%zu zones, %d tiles, %d creatures, %d objects, %d quests)\n",
                projectDir.c_str(), zones.size(),
                totalTiles, totalCreat, totalObj, totalQuest);
    for (size_t k = 0; k < zones.size(); ++k) {
        bool lastZ = (k == zones.size() - 1);
        const auto& z = zones[k];
        const char* zBranch = lastZ ? "└─ " : "├─ ";
        const char* zCont   = lastZ ? "   " : "│  ";
        std::printf("%s%s/  (tiles=%d, creat=%d, obj=%d, quest=%d)\n",
                    zBranch, z.dir.c_str(),
                    z.tiles, z.creatures, z.objects, z.quests);
        // Artifact status row — quick visual of what's been baked.
        std::printf("%s├─ name      : %s\n", zCont, z.name.c_str());
        std::printf("%s├─ mapName   : %s\n", zCont, z.mapName.c_str());
        std::printf("%s├─ artifacts : %s%s%s%s%s%s\n", zCont,
                    z.hasGlb  ? ".glb "  : "",
                    z.hasObj  ? ".obj "  : "",
                    z.hasStl  ? ".stl "  : "",
                    z.hasHtml ? ".html " : "",
                    z.hasZoneMd ? "ZONE.md " : "",
                    (!z.hasGlb && !z.hasObj && !z.hasStl &&
                     !z.hasHtml && !z.hasZoneMd) ? "(none)" : "");
        std::printf("%s└─ status    : %s\n", zCont,
                    (z.creatures || z.objects || z.quests) ?
                        "populated" : "empty (only terrain)");
    }
    return 0;
}

}  // namespace

bool handleInfoTree(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-zone-tree") == 0 && i + 1 < argc) {
        outRc = handleInfoZoneTree(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-project-tree") == 0 && i + 1 < argc) {
        outRc = handleInfoProjectTree(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
