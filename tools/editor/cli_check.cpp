#include "cli_check.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleCheckZoneRefs(int& i, int argc, char** argv) {
    // Cross-reference checker: every model path in objects.json
    // must resolve as either an open WOM/WOB sidecar or a
    // proprietary M2/WMO; every quest's giver/turnIn NPC ID must
    // appear in creatures.json (when the zone has creatures).
    // Catches dangling references that --validate doesn't, since
    // --validate only checks open-format file presence.
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "check-zone-refs: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    // Try to find a model on disk in any of the conventional
    // locations (zone-local, output/, custom_zones/, Data/).
    // Strips extension and tries each open + proprietary variant.
    auto stripExt = [](const std::string& p, const char* ext) {
        size_t n = std::strlen(ext);
        if (p.size() >= n) {
            std::string tail = p.substr(p.size() - n);
            std::string lower = tail;
            for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
            if (lower == ext) return p.substr(0, p.size() - n);
        }
        return p;
    };
    auto modelExists = [&](const std::string& path, bool isWMO) {
        std::string base;
        std::vector<std::string> exts;
        if (isWMO) {
            base = stripExt(path, ".wmo");
            exts = {".wob", ".wmo"};
        } else {
            base = stripExt(path, ".m2");
            exts = {".wom", ".m2"};
        }
        std::vector<std::string> roots = {
            "", zoneDir + "/", "output/", "custom_zones/", "Data/"
        };
        for (const auto& root : roots) {
            for (const auto& ext : exts) {
                if (fs::exists(root + base + ext)) return true;
                // Case-fold fallback for case-sensitive filesystems
                // (designers usually type Mixed Case but Linux
                // stores asset paths lowercase after extraction).
                std::string lower = base + ext;
                for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
                if (fs::exists(root + lower)) return true;
            }
        }
        return false;
    };
    std::vector<std::string> errors;
    // Object placements -> models on disk
    wowee::editor::ObjectPlacer op;
    int objectsChecked = 0, objectsMissing = 0;
    if (op.loadFromFile(zoneDir + "/objects.json")) {
        for (size_t k = 0; k < op.getObjects().size(); ++k) {
            const auto& o = op.getObjects()[k];
            objectsChecked++;
            bool isWMO = (o.type == wowee::editor::PlaceableType::WMO);
            if (!modelExists(o.path, isWMO)) {
                objectsMissing++;
                if (errors.size() < 30) {
                    errors.push_back("object[" + std::to_string(k) +
                                     "] missing: " + o.path);
                }
            }
        }
    }
    // Quest NPCs -> creatures.json IDs (only when creatures exist;
    // otherwise NPC IDs may legitimately reference upstream content
    // outside the zone).
    wowee::editor::NpcSpawner sp;
    wowee::editor::QuestEditor qe;
    int questsChecked = 0, questsMissing = 0;
    bool hasCreatures = sp.loadFromFile(zoneDir + "/creatures.json");
    std::unordered_set<uint32_t> creatureIds;
    if (hasCreatures) {
        for (const auto& s : sp.getSpawns()) creatureIds.insert(s.id);
    }
    if (qe.loadFromFile(zoneDir + "/quests.json") && hasCreatures) {
        for (size_t k = 0; k < qe.getQuests().size(); ++k) {
            const auto& q = qe.getQuests()[k];
            questsChecked++;
            bool localGiver = (q.questGiverNpcId != 0 &&
                               creatureIds.count(q.questGiverNpcId) == 0);
            bool localTurn  = (q.turnInNpcId != 0 &&
                               q.turnInNpcId != q.questGiverNpcId &&
                               creatureIds.count(q.turnInNpcId) == 0);
            // Only flag IDs that look 'small' (likely zone-local).
            // Production uses 6-digit IDs that reference upstream
            // content; designers wire those in deliberately.
            if (localGiver && q.questGiverNpcId < 100000) {
                questsMissing++;
                if (errors.size() < 30) {
                    errors.push_back("quest[" + std::to_string(k) + "] '" +
                                     q.title + "' giver " +
                                     std::to_string(q.questGiverNpcId) +
                                     " not in creatures.json");
                }
            }
            if (localTurn && q.turnInNpcId < 100000) {
                questsMissing++;
                if (errors.size() < 30) {
                    errors.push_back("quest[" + std::to_string(k) + "] '" +
                                     q.title + "' turn-in " +
                                     std::to_string(q.turnInNpcId) +
                                     " not in creatures.json");
                }
            }
        }
    }
    int totalErrors = objectsMissing + questsMissing;
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["objectsChecked"] = objectsChecked;
        j["objectsMissing"] = objectsMissing;
        j["questsChecked"] = questsChecked;
        j["questsMissing"] = questsMissing;
        j["errors"] = errors;
        j["passed"] = (totalErrors == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return totalErrors == 0 ? 0 : 1;
    }
    std::printf("Zone refs: %s\n", zoneDir.c_str());
    std::printf("  objects checked  : %d (%d missing)\n",
                objectsChecked, objectsMissing);
    std::printf("  quests checked   : %d (%d bad NPC refs)\n",
                questsChecked, questsMissing);
    if (totalErrors == 0) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED — %d issue(s):\n", totalErrors);
    for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    return 1;
}

int handleCheckZoneContent(int& i, int argc, char** argv) {
    // Sanity-check creature/object/quest fields for plausible
    // values. --check-zone-refs catches dangling references;
    // this catches data-quality issues like creatures with 0 HP,
    // objects with negative scale, quests with no objectives.
    // Both are needed — a quest can have valid NPC IDs (refs OK)
    // AND no objectives (content broken).
    std::string zoneDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "check-zone-content: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    std::vector<std::string> warnings;
    int creatureWarn = 0, objectWarn = 0, questWarn = 0;
    // Creatures
    wowee::editor::NpcSpawner sp;
    if (sp.loadFromFile(zoneDir + "/creatures.json")) {
        for (size_t k = 0; k < sp.spawnCount(); ++k) {
            const auto& s = sp.getSpawns()[k];
            if (s.name.empty()) {
                warnings.push_back("creature[" + std::to_string(k) + "] has empty name");
                creatureWarn++;
            }
            if (s.health == 0) {
                warnings.push_back("creature[" + std::to_string(k) + "] '" +
                                  s.name + "' has 0 health");
                creatureWarn++;
            }
            if (s.level == 0) {
                warnings.push_back("creature[" + std::to_string(k) + "] '" +
                                  s.name + "' has level 0");
                creatureWarn++;
            }
            if (s.minDamage > s.maxDamage) {
                warnings.push_back("creature[" + std::to_string(k) + "] '" +
                                  s.name + "' has minDamage > maxDamage");
                creatureWarn++;
            }
            if (s.scale <= 0.0f || !std::isfinite(s.scale)) {
                warnings.push_back("creature[" + std::to_string(k) + "] '" +
                                  s.name + "' has non-positive or non-finite scale");
                creatureWarn++;
            }
            if (s.displayId == 0) {
                warnings.push_back("creature[" + std::to_string(k) + "] '" +
                                  s.name + "' has displayId=0 (will render invisibly)");
                creatureWarn++;
            }
        }
    }
    // Objects
    wowee::editor::ObjectPlacer op;
    if (op.loadFromFile(zoneDir + "/objects.json")) {
        for (size_t k = 0; k < op.getObjects().size(); ++k) {
            const auto& o = op.getObjects()[k];
            if (o.path.empty()) {
                warnings.push_back("object[" + std::to_string(k) + "] has empty path");
                objectWarn++;
            }
            if (o.scale <= 0.0f || !std::isfinite(o.scale)) {
                warnings.push_back("object[" + std::to_string(k) +
                                  "] has non-positive or non-finite scale");
                objectWarn++;
            }
            if (!std::isfinite(o.position.x) ||
                !std::isfinite(o.position.y) ||
                !std::isfinite(o.position.z)) {
                warnings.push_back("object[" + std::to_string(k) +
                                  "] has non-finite position");
                objectWarn++;
            }
        }
    }
    // Quests
    wowee::editor::QuestEditor qe;
    if (qe.loadFromFile(zoneDir + "/quests.json")) {
        for (size_t k = 0; k < qe.questCount(); ++k) {
            const auto& q = qe.getQuests()[k];
            if (q.title.empty()) {
                warnings.push_back("quest[" + std::to_string(k) + "] has empty title");
                questWarn++;
            }
            if (q.objectives.empty()) {
                warnings.push_back("quest[" + std::to_string(k) + "] '" +
                                  q.title + "' has no objectives (uncompletable)");
                questWarn++;
            }
            if (q.reward.xp == 0 && q.reward.itemRewards.empty() &&
                q.reward.gold == 0 && q.reward.silver == 0 && q.reward.copper == 0) {
                warnings.push_back("quest[" + std::to_string(k) + "] '" +
                                  q.title + "' has no reward at all");
                questWarn++;
            }
            if (q.requiredLevel == 0) {
                warnings.push_back("quest[" + std::to_string(k) + "] '" +
                                  q.title + "' has requiredLevel=0");
                questWarn++;
            }
        }
    }
    int total = creatureWarn + objectWarn + questWarn;
    if (jsonOut) {
        nlohmann::json j;
        j["zone"] = zoneDir;
        j["creatureWarnings"] = creatureWarn;
        j["objectWarnings"] = objectWarn;
        j["questWarnings"] = questWarn;
        j["totalWarnings"] = total;
        j["warnings"] = warnings;
        j["passed"] = (total == 0);
        std::printf("%s\n", j.dump(2).c_str());
        return total == 0 ? 0 : 1;
    }
    std::printf("Zone content: %s\n", zoneDir.c_str());
    std::printf("  creature warnings: %d\n", creatureWarn);
    std::printf("  object warnings  : %d\n", objectWarn);
    std::printf("  quest warnings   : %d\n", questWarn);
    if (total == 0) {
        std::printf("  PASSED\n");
        return 0;
    }
    std::printf("  FAILED — %d total warning(s):\n", total);
    for (const auto& w : warnings) std::printf("    - %s\n", w.c_str());
    return 1;
}

int handleCheckProjectContent(int& i, int argc, char** argv) {
    // Project-level content sanity check. Walks every zone and
    // runs the same per-zone checks that --check-zone-content
    // does, aggregating warnings per zone. Exit 1 if any zone
    // has any warning. Designed for CI gates before --pack-wcp.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "check-project-content: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    // Same per-zone walks as --check-zone-content. Reuse the
    // logic by counting issues directly here (cheaper than
    // shelling out to a sub-invocation per zone).
    struct ZoneRow { std::string name; int creatureWarn, objectWarn, questWarn; };
    std::vector<ZoneRow> rows;
    int projectFailedZones = 0;
    for (const auto& zoneDir : zones) {
        ZoneRow row{fs::path(zoneDir).filename().string(), 0, 0, 0};
        wowee::editor::NpcSpawner sp;
        if (sp.loadFromFile(zoneDir + "/creatures.json")) {
            for (const auto& s : sp.getSpawns()) {
                if (s.name.empty()) row.creatureWarn++;
                if (s.health == 0) row.creatureWarn++;
                if (s.level == 0) row.creatureWarn++;
                if (s.minDamage > s.maxDamage) row.creatureWarn++;
                if (s.scale <= 0.0f || !std::isfinite(s.scale)) row.creatureWarn++;
                if (s.displayId == 0) row.creatureWarn++;
            }
        }
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile(zoneDir + "/objects.json")) {
            for (const auto& o : op.getObjects()) {
                if (o.path.empty()) row.objectWarn++;
                if (o.scale <= 0.0f || !std::isfinite(o.scale)) row.objectWarn++;
                if (!std::isfinite(o.position.x) ||
                    !std::isfinite(o.position.y) ||
                    !std::isfinite(o.position.z)) row.objectWarn++;
            }
        }
        wowee::editor::QuestEditor qe;
        if (qe.loadFromFile(zoneDir + "/quests.json")) {
            for (const auto& q : qe.getQuests()) {
                if (q.title.empty()) row.questWarn++;
                if (q.objectives.empty()) row.questWarn++;
                if (q.reward.xp == 0 && q.reward.itemRewards.empty() &&
                    q.reward.gold == 0 && q.reward.silver == 0 &&
                    q.reward.copper == 0) row.questWarn++;
                if (q.requiredLevel == 0) row.questWarn++;
            }
        }
        int rowTotal = row.creatureWarn + row.objectWarn + row.questWarn;
        if (rowTotal > 0) projectFailedZones++;
        rows.push_back(row);
    }
    int allPassed = (projectFailedZones == 0);
    int totalWarn = 0;
    for (const auto& r : rows) totalWarn += r.creatureWarn + r.objectWarn + r.questWarn;
    if (jsonOut) {
        nlohmann::json j;
        j["projectDir"] = projectDir;
        j["totalZones"] = zones.size();
        j["failedZones"] = projectFailedZones;
        j["totalWarnings"] = totalWarn;
        j["passed"] = bool(allPassed);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({{"zone", r.name},
                            {"creatureWarn", r.creatureWarn},
                            {"objectWarn", r.objectWarn},
                            {"questWarn", r.questWarn}});
        }
        j["zones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return allPassed ? 0 : 1;
    }
    std::printf("check-project-content: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu (%d failed)\n",
                zones.size(), projectFailedZones);
    std::printf("  total warns  : %d\n", totalWarn);
    std::printf("\n  zone                       creat  object   quest  status\n");
    for (const auto& r : rows) {
        int rowTotal = r.creatureWarn + r.objectWarn + r.questWarn;
        std::printf("  %-26s  %5d   %5d   %5d  %s\n",
                    r.name.substr(0, 26).c_str(),
                    r.creatureWarn, r.objectWarn, r.questWarn,
                    rowTotal == 0 ? "PASS" : "FAIL");
    }
    if (allPassed) {
        std::printf("\n  ALL ZONES PASSED\n");
        return 0;
    }
    std::printf("\n  %d zone(s) have content warnings\n",
                projectFailedZones);
    return 1;
}

int handleCheckProjectRefs(int& i, int argc, char** argv) {
    // Project-level cross-reference checker. Walks every zone
    // and runs the same model-path / NPC-id checks as
    // --check-zone-refs. Aggregates per zone with file-level
    // breakdown. Exit 1 if any zone has dangling refs.
    std::string projectDir = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "check-project-refs: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    // What counts as a zone, and the order they are reported in,
    // from one place.
    std::vector<std::string> zones = wowee::editor::projectZoneDirs(projectDir);
    // Same model-resolve logic as --check-zone-refs, applied
    // per zone with the appropriate root list.
    auto stripExt = [](const std::string& p, const char* ext) {
        size_t n = std::strlen(ext);
        if (p.size() >= n) {
            std::string tail = p.substr(p.size() - n);
            std::string lower = tail;
            for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
            if (lower == ext) return p.substr(0, p.size() - n);
        }
        return p;
    };
    struct ZoneRow { std::string name; int objCheck, objMiss, qCheck, qMiss; };
    std::vector<ZoneRow> rows;
    int projectFailedZones = 0;
    for (const auto& zoneDir : zones) {
        ZoneRow row{fs::path(zoneDir).filename().string(), 0, 0, 0, 0};
        auto modelExists = [&](const std::string& path, bool isWMO) {
            std::string base;
            std::vector<std::string> exts;
            if (isWMO) {
                base = stripExt(path, ".wmo");
                exts = {".wob", ".wmo"};
            } else {
                base = stripExt(path, ".m2");
                exts = {".wom", ".m2"};
            }
            std::vector<std::string> roots = {
                "", zoneDir + "/", "output/", "custom_zones/", "Data/"
            };
            for (const auto& root : roots) {
                for (const auto& ext : exts) {
                    if (fs::exists(root + base + ext)) return true;
                    std::string lower = base + ext;
                    for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
                    if (fs::exists(root + lower)) return true;
                }
            }
            return false;
        };
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile(zoneDir + "/objects.json")) {
            for (const auto& o : op.getObjects()) {
                row.objCheck++;
                bool isWMO = (o.type == wowee::editor::PlaceableType::WMO);
                if (!modelExists(o.path, isWMO)) row.objMiss++;
            }
        }
        wowee::editor::NpcSpawner sp;
        wowee::editor::QuestEditor qe;
        bool hasCreatures = sp.loadFromFile(zoneDir + "/creatures.json");
        std::unordered_set<uint32_t> creatureIds;
        if (hasCreatures) {
            for (const auto& s : sp.getSpawns()) creatureIds.insert(s.id);
        }
        if (qe.loadFromFile(zoneDir + "/quests.json") && hasCreatures) {
            for (const auto& q : qe.getQuests()) {
                row.qCheck++;
                bool localGiver = (q.questGiverNpcId != 0 &&
                                    q.questGiverNpcId < 100000 &&
                                    creatureIds.count(q.questGiverNpcId) == 0);
                bool localTurn  = (q.turnInNpcId != 0 &&
                                    q.turnInNpcId < 100000 &&
                                    q.turnInNpcId != q.questGiverNpcId &&
                                    creatureIds.count(q.turnInNpcId) == 0);
                if (localGiver) row.qMiss++;
                if (localTurn) row.qMiss++;
            }
        }
        if (row.objMiss + row.qMiss > 0) projectFailedZones++;
        rows.push_back(row);
    }
    int allPassed = (projectFailedZones == 0);
    int totalMiss = 0;
    for (const auto& r : rows) totalMiss += r.objMiss + r.qMiss;
    if (jsonOut) {
        nlohmann::json j;
        j["projectDir"] = projectDir;
        j["totalZones"] = zones.size();
        j["failedZones"] = projectFailedZones;
        j["totalMissing"] = totalMiss;
        j["passed"] = bool(allPassed);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            arr.push_back({{"zone", r.name},
                            {"objectsChecked", r.objCheck},
                            {"objectsMissing", r.objMiss},
                            {"questsChecked", r.qCheck},
                            {"questsMissing", r.qMiss}});
        }
        j["zones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return allPassed ? 0 : 1;
    }
    std::printf("check-project-refs: %s\n", projectDir.c_str());
    std::printf("  zones        : %zu (%d failed)\n",
                zones.size(), projectFailedZones);
    std::printf("  total missing: %d\n", totalMiss);
    std::printf("\n  zone                       obj_chk obj_miss  q_chk  q_miss  status\n");
    for (const auto& r : rows) {
        int rowMiss = r.objMiss + r.qMiss;
        std::printf("  %-26s   %5d    %5d  %5d   %5d  %s\n",
                    r.name.substr(0, 26).c_str(),
                    r.objCheck, r.objMiss, r.qCheck, r.qMiss,
                    rowMiss == 0 ? "PASS" : "FAIL");
    }
    if (allPassed) {
        std::printf("\n  ALL ZONES PASSED\n");
        return 0;
    }
    std::printf("\n  %d zone(s) have dangling refs\n", projectFailedZones);
    return 1;
}


}  // namespace

bool handleCheck(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--check-zone-refs") == 0 && i + 1 < argc) {
        outRc = handleCheckZoneRefs(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--check-zone-content") == 0 && i + 1 < argc) {
        outRc = handleCheckZoneContent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--check-project-content") == 0 && i + 1 < argc) {
        outRc = handleCheckProjectContent(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--check-project-refs") == 0 && i + 1 < argc) {
        outRc = handleCheckProjectRefs(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
