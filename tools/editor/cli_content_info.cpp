#include "cli_content_info.hpp"

#include "npc_spawner.hpp"
#include "npc_presets.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfoCreatures(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::NpcSpawner spawner;
    if (!spawner.loadFromFile(path)) {
        std::fprintf(stderr, "Failed to load creatures.json: %s\n", path.c_str());
        return 1;
    }
    const auto& spawns = spawner.getSpawns();
    int hostile = 0, vendor = 0, questgiver = 0, trainer = 0;
    int patrol = 0, wander = 0, stationary = 0;
    std::unordered_map<uint32_t, int> displayIdHist;
    for (const auto& s : spawns) {
        if (s.hostile) hostile++;
        if (s.vendor) vendor++;
        if (s.questgiver) questgiver++;
        if (s.trainer) trainer++;
        using B = wowee::editor::CreatureBehavior;
        if (s.behavior == B::Patrol) patrol++;
        else if (s.behavior == B::Wander) wander++;
        else if (s.behavior == B::Stationary) stationary++;
        displayIdHist[s.displayId]++;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["total"] = spawns.size();
        j["hostile"] = hostile;
        j["questgiver"] = questgiver;
        j["vendor"] = vendor;
        j["trainer"] = trainer;
        j["behavior"] = {{"stationary", stationary},
                          {"wander", wander},
                          {"patrol", patrol}};
        j["uniqueDisplayIds"] = displayIdHist.size();
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("creatures.json: %s\n", path.c_str());
    std::printf("  total       : %zu\n", spawns.size());
    std::printf("  hostile     : %d\n", hostile);
    std::printf("  questgiver  : %d\n", questgiver);
    std::printf("  vendor      : %d\n", vendor);
    std::printf("  trainer     : %d\n", trainer);
    std::printf("  behavior    : %d stationary, %d wander, %d patrol\n",
                stationary, wander, patrol);
    std::printf("  unique displayIds: %zu\n", displayIdHist.size());
    return 0;
}

int handleInfoCreaturesByFaction(int& i, int argc, char** argv) {
    // Faction histogram for combat balance analysis. AzerothCore
    // factions: 7=human, 14=monster, 16=alliance-friendly, 35=neutral,
    // etc. A zone with all faction=14 is going to be one giant
    // free-for-all; a mixed-faction zone needs combat-tuning.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::NpcSpawner sp;
    if (!sp.loadFromFile(path)) {
        std::fprintf(stderr,
            "info-creatures-by-faction: failed to load %s\n", path.c_str());
        return 1;
    }
    std::map<uint32_t, int> hist;
    for (const auto& s : sp.getSpawns()) hist[s.faction]++;
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["totalCreatures"] = sp.spawnCount();
        j["uniqueFactions"] = hist.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [f, c] : hist) {
            arr.push_back({{"faction", f}, {"count", c}});
        }
        j["factions"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Creatures by faction: %s (%zu total)\n",
                path.c_str(), sp.spawnCount());
    std::printf("  faction    count   share\n");
    for (const auto& [f, c] : hist) {
        double pct = sp.spawnCount() > 0 ? 100.0 * c / sp.spawnCount() : 0.0;
        std::printf("  %7u    %5d   %5.1f%%\n", f, c, pct);
    }
    std::printf("  (factions: 7=human, 14=monster, 35=neutral, etc.)\n");
    return 0;
}

int handleInfoCreaturesByLevel(int& i, int argc, char** argv) {
    // Level distribution for difficulty-curve analysis. Min/max/
    // avg + per-level histogram. A zone with all level-1 spawns
    // is a starter area; one with all 60s is endgame; spikes in
    // the middle suggest content-tuning issues.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::NpcSpawner sp;
    if (!sp.loadFromFile(path)) {
        std::fprintf(stderr,
            "info-creatures-by-level: failed to load %s\n", path.c_str());
        return 1;
    }
    std::map<uint32_t, int> hist;
    uint32_t minL = std::numeric_limits<uint32_t>::max();
    uint32_t maxL = 0;
    uint64_t sumL = 0;
    for (const auto& s : sp.getSpawns()) {
        hist[s.level]++;
        if (s.level < minL) minL = s.level;
        if (s.level > maxL) maxL = s.level;
        sumL += s.level;
    }
    double avgL = sp.spawnCount() > 0 ? double(sumL) / sp.spawnCount() : 0.0;
    if (sp.spawnCount() == 0) minL = 0;
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["totalCreatures"] = sp.spawnCount();
        j["minLevel"] = minL;
        j["maxLevel"] = maxL;
        j["avgLevel"] = avgL;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [l, c] : hist) {
            arr.push_back({{"level", l}, {"count", c}});
        }
        j["levels"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Creatures by level: %s (%zu total)\n",
                path.c_str(), sp.spawnCount());
    std::printf("  range : %u to %u (avg %.1f)\n", minL, maxL, avgL);
    std::printf("\n  level   count  bar\n");
    int maxBarCount = 0;
    for (const auto& [_, c] : hist) maxBarCount = std::max(maxBarCount, c);
    for (const auto& [l, c] : hist) {
        int barLen = maxBarCount > 0 ? (40 * c) / maxBarCount : 0;
        std::printf("  %5u   %5d  ", l, c);
        for (int b = 0; b < barLen; ++b) std::printf("█");
        std::printf("\n");
    }
    return 0;
}

int handleInfoObjectsByPath(int& i, int argc, char** argv) {
    // Most-used model paths with counts. Designers can quickly
    // spot which trees/lamps/walls dominate a zone — helps with
    // both texture-budget audits and 'this looks repetitive,
    // diversify the doodads' design feedback.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::ObjectPlacer placer;
    if (!placer.loadFromFile(path)) {
        std::fprintf(stderr,
            "info-objects-by-path: failed to load %s\n", path.c_str());
        return 1;
    }
    std::map<std::string, int> hist;
    for (const auto& o : placer.getObjects()) hist[o.path]++;
    // Sort by count descending.
    std::vector<std::pair<std::string, int>> sorted(hist.begin(), hist.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    int total = static_cast<int>(placer.getObjects().size());
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["totalObjects"] = total;
        j["uniquePaths"] = hist.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [p, c] : sorted) {
            arr.push_back({{"path", p}, {"count", c}});
        }
        j["paths"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Objects by path: %s (%d total, %zu unique)\n",
                path.c_str(), total, hist.size());
    std::printf("  count   share   path\n");
    for (const auto& [p, c] : sorted) {
        double pct = total > 0 ? 100.0 * c / total : 0.0;
        std::printf("  %5d   %5.1f%%  %s\n", c, pct, p.c_str());
    }
    return 0;
}

int handleInfoObjectsByType(int& i, int argc, char** argv) {
    // M2 vs WMO split + per-type scale stats. Catches scale
    // outliers ('this WMO is at 0.001 scale, did you mean 1.0?')
    // and gives a sense of zone composition (mostly props vs
    // mostly buildings).
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::ObjectPlacer placer;
    if (!placer.loadFromFile(path)) {
        std::fprintf(stderr,
            "info-objects-by-type: failed to load %s\n", path.c_str());
        return 1;
    }
    int m2Count = 0, wmoCount = 0;
    float m2Min = 1e30f, m2Max = -1e30f;
    float wmoMin = 1e30f, wmoMax = -1e30f;
    double m2SumScale = 0, wmoSumScale = 0;
    for (const auto& o : placer.getObjects()) {
        if (o.type == wowee::editor::PlaceableType::M2) {
            m2Count++;
            m2Min = std::min(m2Min, o.scale);
            m2Max = std::max(m2Max, o.scale);
            m2SumScale += o.scale;
        } else {
            wmoCount++;
            wmoMin = std::min(wmoMin, o.scale);
            wmoMax = std::max(wmoMax, o.scale);
            wmoSumScale += o.scale;
        }
    }
    double m2Avg = m2Count > 0 ? m2SumScale / m2Count : 0.0;
    double wmoAvg = wmoCount > 0 ? wmoSumScale / wmoCount : 0.0;
    if (m2Count == 0) { m2Min = 0; m2Max = 0; }
    if (wmoCount == 0) { wmoMin = 0; wmoMax = 0; }
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["totalObjects"] = m2Count + wmoCount;
        j["m2"] = {{"count", m2Count},
                    {"scaleMin", m2Min}, {"scaleMax", m2Max},
                    {"scaleAvg", m2Avg}};
        j["wmo"] = {{"count", wmoCount},
                     {"scaleMin", wmoMin}, {"scaleMax", wmoMax},
                     {"scaleAvg", wmoAvg}};
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Objects by type: %s\n", path.c_str());
    std::printf("  M2  : %d  (scale %.2f-%.2f, avg %.2f)\n",
                m2Count, m2Min, m2Max, m2Avg);
    std::printf("  WMO : %d  (scale %.2f-%.2f, avg %.2f)\n",
                wmoCount, wmoMin, wmoMax, wmoAvg);
    return 0;
}

int handleInfoQuestsByLevel(int& i, int argc, char** argv) {
    // Required-level distribution. Catches difficulty-curve
    // issues where every quest is requiredLevel=1 (player skips
    // the chain) or every quest is requiredLevel=60 (no early
    // game), and outliers (a level-30 quest dropped into a
    // starter zone).
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr,
            "info-quests-by-level: failed to load %s\n", path.c_str());
        return 1;
    }
    std::map<uint32_t, int> hist;
    uint32_t minL = std::numeric_limits<uint32_t>::max();
    uint32_t maxL = 0;
    uint64_t sumL = 0;
    for (const auto& q : qe.getQuests()) {
        hist[q.requiredLevel]++;
        if (q.requiredLevel < minL) minL = q.requiredLevel;
        if (q.requiredLevel > maxL) maxL = q.requiredLevel;
        sumL += q.requiredLevel;
    }
    double avgL = qe.questCount() > 0 ?
        double(sumL) / qe.questCount() : 0.0;
    if (qe.questCount() == 0) minL = 0;
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["totalQuests"] = qe.questCount();
        j["minLevel"] = minL;
        j["maxLevel"] = maxL;
        j["avgLevel"] = avgL;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [l, c] : hist) {
            arr.push_back({{"level", l}, {"count", c}});
        }
        j["levels"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Quests by required level: %s (%zu total)\n",
                path.c_str(), qe.questCount());
    std::printf("  range : %u to %u (avg %.1f)\n", minL, maxL, avgL);
    std::printf("\n  level   count  bar\n");
    int maxBarCount = 0;
    for (const auto& [_, c] : hist) maxBarCount = std::max(maxBarCount, c);
    for (const auto& [l, c] : hist) {
        int barLen = maxBarCount > 0 ? (40 * c) / maxBarCount : 0;
        std::printf("  %5u   %5d  ", l, c);
        for (int b = 0; b < barLen; ++b) std::printf("█");
        std::printf("\n");
    }
    return 0;
}

int handleInfoQuestsByXp(int& i, int argc, char** argv) {
    // XP reward distribution. Bucket into 100-XP groups so a
    // 10000-XP quest doesn't make the histogram unreadable.
    // Catches no-reward quests + cluster analysis (mostly
    // 100-XP smalls vs mostly 5000-XP boss kills).
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr,
            "info-quests-by-xp: failed to load %s\n", path.c_str());
        return 1;
    }
    uint32_t minXp = std::numeric_limits<uint32_t>::max();
    uint32_t maxXp = 0;
    uint64_t sumXp = 0;
    int zeroXp = 0;
    // Bucket size grows with max — keeps the histogram readable
    // for both starter zones (10-100 XP) and endgame (5000+).
    std::map<uint32_t, int> buckets;
    for (const auto& q : qe.getQuests()) {
        if (q.reward.xp < minXp) minXp = q.reward.xp;
        if (q.reward.xp > maxXp) maxXp = q.reward.xp;
        sumXp += q.reward.xp;
        if (q.reward.xp == 0) zeroXp++;
    }
    uint32_t bucketSize = 100;
    if (maxXp > 1000) bucketSize = 250;
    if (maxXp > 5000) bucketSize = 500;
    if (maxXp > 20000) bucketSize = 1000;
    for (const auto& q : qe.getQuests()) {
        buckets[(q.reward.xp / bucketSize) * bucketSize]++;
    }
    double avgXp = qe.questCount() > 0 ?
        double(sumXp) / qe.questCount() : 0.0;
    if (qe.questCount() == 0) minXp = 0;
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["totalQuests"] = qe.questCount();
        j["minXp"] = minXp;
        j["maxXp"] = maxXp;
        j["avgXp"] = avgXp;
        j["zeroXpQuests"] = zeroXp;
        j["bucketSize"] = bucketSize;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [b, c] : buckets) {
            arr.push_back({{"bucket", b}, {"count", c}});
        }
        j["buckets"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Quests by XP reward: %s (%zu total)\n",
                path.c_str(), qe.questCount());
    std::printf("  range : %u to %u (avg %.0f, %d with 0 XP)\n",
                minXp, maxXp, avgXp, zeroXp);
    std::printf("\n  bucket (≥XP)   count  bar\n");
    int maxBarCount = 0;
    for (const auto& [_, c] : buckets) maxBarCount = std::max(maxBarCount, c);
    for (const auto& [b, c] : buckets) {
        int barLen = maxBarCount > 0 ? (40 * c) / maxBarCount : 0;
        std::printf("  %12u   %5d  ", b, c);
        for (int x = 0; x < barLen; ++x) std::printf("█");
        std::printf("\n");
    }
    std::printf("  (bucket size: %u XP)\n", bucketSize);
    return 0;
}

int handleListCreatures(int& i, int argc, char** argv) {
    // Verbose enumeration of every spawn — needed because
    // --remove-creature takes a 0-based index but --info-creatures
    // only shows aggregate counts.
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::NpcSpawner spawner;
    if (!spawner.loadFromFile(path)) {
        std::fprintf(stderr, "Failed to load creatures.json: %s\n", path.c_str());
        return 1;
    }
    const auto& spawns = spawner.getSpawns();
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["total"] = spawns.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < spawns.size(); ++k) {
            const auto& s = spawns[k];
            arr.push_back({
                {"index", k},
                {"name", s.name},
                {"displayId", s.displayId},
                {"level", s.level},
                {"position", {s.position.x, s.position.y, s.position.z}},
                {"hostile", s.hostile},
            });
        }
        j["spawns"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("creatures.json: %s (%zu total)\n", path.c_str(), spawns.size());
    std::printf("  idx  name                            lvl  display  pos (x, y, z)\n");
    for (size_t k = 0; k < spawns.size(); ++k) {
        const auto& s = spawns[k];
        std::printf("  %3zu  %-30s %3u  %7u  (%.1f, %.1f, %.1f)%s\n",
                    k, s.name.substr(0, 30).c_str(), s.level, s.displayId,
                    s.position.x, s.position.y, s.position.z,
                    s.hostile ? " [hostile]" : "");
    }
    return 0;
}

int handleListObjects(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::ObjectPlacer placer;
    if (!placer.loadFromFile(path)) {
        std::fprintf(stderr, "Failed to load objects.json: %s\n", path.c_str());
        return 1;
    }
    const auto& objs = placer.getObjects();
    auto typeStr = [](wowee::editor::PlaceableType t) {
        return t == wowee::editor::PlaceableType::M2 ? "m2" : "wmo";
    };
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["total"] = objs.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < objs.size(); ++k) {
            const auto& o = objs[k];
            arr.push_back({
                {"index", k},
                {"type", typeStr(o.type)},
                {"path", o.path},
                {"position", {o.position.x, o.position.y, o.position.z}},
                {"scale", o.scale},
            });
        }
        j["objects"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("objects.json: %s (%zu total)\n", path.c_str(), objs.size());
    std::printf("  idx  type  scale  path                                    pos (x, y, z)\n");
    for (size_t k = 0; k < objs.size(); ++k) {
        const auto& o = objs[k];
        std::printf("  %3zu  %-4s  %5.2f  %-38s  (%.1f, %.1f, %.1f)\n",
                    k, typeStr(o.type), o.scale,
                    o.path.substr(0, 38).c_str(),
                    o.position.x, o.position.y, o.position.z);
    }
    return 0;
}

int handleListQuests(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "Failed to load quests.json: %s\n", path.c_str());
        return 1;
    }
    const auto& quests = qe.getQuests();
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["total"] = quests.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < quests.size(); ++k) {
            const auto& q = quests[k];
            arr.push_back({
                {"index", k},
                {"title", q.title},
                {"giver", q.questGiverNpcId},
                {"turnIn", q.turnInNpcId},
                {"requiredLevel", q.requiredLevel},
                {"xp", q.reward.xp},
                {"nextQuestId", q.nextQuestId},
            });
        }
        j["quests"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("quests.json: %s (%zu total)\n", path.c_str(), quests.size());
    std::printf("  idx  lvl  giver    turnIn   xp     title\n");
    for (size_t k = 0; k < quests.size(); ++k) {
        const auto& q = quests[k];
        std::printf("  %3zu  %3u  %7u  %7u  %5u  %s%s\n",
                    k, q.requiredLevel, q.questGiverNpcId, q.turnInNpcId,
                    q.reward.xp, q.title.c_str(),
                    q.nextQuestId ? " [chained]" : "");
    }
    return 0;
}

int handleListQuestObjectives(int& i, int argc, char** argv) {
    // Per-quest objective listing — pairs with --remove-quest-objective
    // (which takes objIdx). Tabulates type, target, count, description.
    std::string path = argv[++i];
    std::string idxStr = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    int qIdx;
    try { qIdx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "list-quest-objectives: bad questIdx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "list-quest-objectives: failed to load %s\n", path.c_str());
        return 1;
    }
    if (qIdx < 0 || qIdx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "list-quest-objectives: questIdx %d out of range [0, %zu)\n",
            qIdx, qe.questCount());
        return 1;
    }
    const auto& q = qe.getQuests()[qIdx];
        // The word is the format's, from beside the enum.
        auto typeName = wowee::editor::questObjectiveTypeName;
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["questIdx"] = qIdx;
        j["title"] = q.title;
        j["count"] = q.objectives.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t o = 0; o < q.objectives.size(); ++o) {
            const auto& ob = q.objectives[o];
            arr.push_back({
                {"index", o},
                {"type", typeName(ob.type)},
                {"target", ob.targetName},
                {"count", ob.targetCount},
                {"description", ob.description},
            });
        }
        j["objectives"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Quest %d ('%s'): %zu objective(s)\n",
                qIdx, q.title.c_str(), q.objectives.size());
    std::printf("  idx  type     count  target              description\n");
    for (size_t o = 0; o < q.objectives.size(); ++o) {
        const auto& ob = q.objectives[o];
        std::printf("  %3zu  %-7s  %5u  %-18s  %s\n",
                    o, typeName(ob.type), ob.targetCount,
                    ob.targetName.substr(0, 18).c_str(),
                    ob.description.c_str());
    }
    return 0;
}

int handleListQuestRewards(int& i, int argc, char** argv) {
    // Per-quest reward listing. Shows XP/coin breakdown plus the
    // full itemRewards list (which --info-quests only counts).
    std::string path = argv[++i];
    std::string idxStr = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    int qIdx;
    try { qIdx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "list-quest-rewards: bad questIdx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "list-quest-rewards: failed to load %s\n", path.c_str());
        return 1;
    }
    if (qIdx < 0 || qIdx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "list-quest-rewards: questIdx %d out of range [0, %zu)\n",
            qIdx, qe.questCount());
        return 1;
    }
    const auto& q = qe.getQuests()[qIdx];
    const auto& r = q.reward;
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["questIdx"] = qIdx;
        j["title"] = q.title;
        j["xp"] = r.xp;
        j["gold"] = r.gold;
        j["silver"] = r.silver;
        j["copper"] = r.copper;
        j["items"] = r.itemRewards;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Quest %d ('%s') rewards:\n", qIdx, q.title.c_str());
    std::printf("  xp     : %u\n", r.xp);
    std::printf("  coin   : %ug %us %uc\n", r.gold, r.silver, r.copper);
    std::printf("  items  : %zu\n", r.itemRewards.size());
    for (size_t k = 0; k < r.itemRewards.size(); ++k) {
        std::printf("    [%zu] %s\n", k, r.itemRewards[k].c_str());
    }
    return 0;
}

int handleInfoQuestGraphStats(int& i, int argc, char** argv) {
    // Topology analysis of the quest dependency graph. Where
    // --export-quest-graph visualizes it, this quantifies it:
    //   roots    = quests no one chains TO (entry points)
    //   leaves   = quests with no nextQuestId (terminal)
    //   orphans  = roots that are also leaves (one-shot quests)
    //   cycles   = circular chain detected
    //   maxDepth = longest path from any root
    //   avgDepth = mean path length across all roots
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr,
            "info-quest-graph-stats: failed to load %s\n", path.c_str());
        return 1;
    }
    const auto& quests = qe.getQuests();
    // Build id -> nextId and reverse adjacency.
    std::unordered_map<uint32_t, uint32_t> nextOf;
    std::unordered_set<uint32_t> hasInbound;
    std::unordered_set<uint32_t> validIds;
    for (const auto& q : quests) {
        validIds.insert(q.id);
        nextOf[q.id] = q.nextQuestId;
    }
    for (const auto& q : quests) {
        if (q.nextQuestId != 0 && validIds.count(q.nextQuestId)) {
            hasInbound.insert(q.nextQuestId);
        }
    }
    int roots = 0, leaves = 0, orphans = 0;
    int cycles = 0;
    int maxDepth = 0;
    int sumDepths = 0;
    for (const auto& q : quests) {
        bool isRoot = (hasInbound.count(q.id) == 0);
        bool isLeaf = (q.nextQuestId == 0 ||
                        validIds.count(q.nextQuestId) == 0);
        if (isRoot) roots++;
        if (isLeaf) leaves++;
        if (isRoot && isLeaf) orphans++;
        if (isRoot) {
            // Walk the chain forward, counting depth + cycle-guarding.
            std::unordered_set<uint32_t> visited;
            int depth = 1;
            uint32_t current = q.id;
            while (current != 0 && validIds.count(current)) {
                if (!visited.insert(current).second) {
                    cycles++;
                    break;
                }
                auto it = nextOf.find(current);
                if (it == nextOf.end() || it->second == 0) break;
                current = it->second;
                depth++;
            }
            if (depth > maxDepth) maxDepth = depth;
            sumDepths += depth;
        }
    }
    double avgDepth = (roots > 0) ? double(sumDepths) / roots : 0.0;
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["totalQuests"] = quests.size();
        j["roots"] = roots;
        j["leaves"] = leaves;
        j["orphans"] = orphans;
        j["cycles"] = cycles;
        j["maxDepth"] = maxDepth;
        j["avgDepth"] = avgDepth;
        std::printf("%s\n", j.dump(2).c_str());
        return cycles == 0 ? 0 : 1;
    }
    std::printf("Quest graph: %s\n", path.c_str());
    std::printf("  total quests : %zu\n", quests.size());
    std::printf("  roots        : %d (no inbound chain — entry points)\n", roots);
    std::printf("  leaves       : %d (no outbound chain — terminal)\n", leaves);
    std::printf("  orphans      : %d (root AND leaf — one-shot)\n", orphans);
    std::printf("  cycles       : %d %s\n", cycles,
                cycles == 0 ? "" : "(BROKEN — chains loop back)");
    std::printf("  max depth    : %d\n", maxDepth);
    std::printf("  avg depth    : %.2f (chain length per root)\n", avgDepth);
    return cycles == 0 ? 0 : 1;
}

int handleInfoCreature(int& i, int argc, char** argv) {
    // Single-creature deep dive — every CreatureSpawn field for
    // one entry. Companion to --list-creatures (which is a
    // table view); useful for digging into 'why is this NPC
    // not behaving like I expect?'.
    std::string path = argv[++i];
    std::string idxStr = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "info-creature: bad idx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::NpcSpawner sp;
    if (!sp.loadFromFile(path)) {
        std::fprintf(stderr, "info-creature: failed to load %s\n", path.c_str());
        return 1;
    }
    if (idx < 0 || idx >= static_cast<int>(sp.spawnCount())) {
        std::fprintf(stderr,
            "info-creature: idx %d out of range [0, %zu)\n",
            idx, sp.spawnCount());
        return 1;
    }
    const auto& s = sp.getSpawns()[idx];
    using B = wowee::editor::CreatureBehavior;
    const char* behavior =
        s.behavior == B::Patrol ? "patrol" :
        s.behavior == B::Wander ? "wander" : "stationary";
    if (jsonOut) {
        nlohmann::json j;
        j["index"] = idx;
        j["id"] = s.id;
        j["name"] = s.name;
        j["modelPath"] = s.modelPath;
        j["displayId"] = s.displayId;
        j["position"] = {s.position.x, s.position.y, s.position.z};
        j["orientation"] = s.orientation;
        j["level"] = s.level;
        j["health"] = s.health;
        j["mana"] = s.mana;
        j["minDamage"] = s.minDamage;
        j["maxDamage"] = s.maxDamage;
        j["armor"] = s.armor;
        j["faction"] = s.faction;
        j["scale"] = s.scale;
        j["behavior"] = behavior;
        j["wanderRadius"] = s.wanderRadius;
        j["aggroRadius"] = s.aggroRadius;
        j["leashRadius"] = s.leashRadius;
        j["respawnTimeMs"] = s.respawnTimeMs;
        j["patrolPoints"] = s.patrolPath.size();
        j["hostile"] = s.hostile;
        j["questgiver"] = s.questgiver;
        j["vendor"] = s.vendor;
        j["trainer"] = s.trainer;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Creature [%d] '%s'\n", idx, s.name.c_str());
    std::printf("  id            : %u\n", s.id);
    std::printf("  displayId     : %u\n", s.displayId);
    std::printf("  modelPath     : %s\n",
                s.modelPath.empty() ? "(uses displayId)" : s.modelPath.c_str());
    std::printf("  position      : (%.2f, %.2f, %.2f)\n",
                s.position.x, s.position.y, s.position.z);
    std::printf("  orientation   : %.2f deg\n", s.orientation);
    std::printf("  scale         : %.2f\n", s.scale);
    std::printf("  level         : %u\n", s.level);
    std::printf("  health/mana   : %u / %u\n", s.health, s.mana);
    std::printf("  damage        : %u-%u\n", s.minDamage, s.maxDamage);
    std::printf("  armor         : %u\n", s.armor);
    std::printf("  faction       : %u\n", s.faction);
    std::printf("  behavior      : %s\n", behavior);
    std::printf("  wander rad    : %.1f\n", s.wanderRadius);
    std::printf("  aggro rad     : %.1f\n", s.aggroRadius);
    std::printf("  leash rad     : %.1f\n", s.leashRadius);
    std::printf("  respawn ms    : %u\n", s.respawnTimeMs);
    std::printf("  patrol points : %zu\n", s.patrolPath.size());
    std::printf("  flags         : %s%s%s%s\n",
                s.hostile ? "hostile " : "",
                s.questgiver ? "questgiver " : "",
                s.vendor ? "vendor " : "",
                s.trainer ? "trainer " : "");
    return 0;
}

int handleInfoQuest(int& i, int argc, char** argv) {
    // Single-quest deep dive — combines what --list-quest-objectives
    // and --list-quest-rewards show into one view, plus the chain
    // pointer + descriptions that neither covers.
    std::string path = argv[++i];
    std::string idxStr = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "info-quest: bad idx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "info-quest: failed to load %s\n", path.c_str());
        return 1;
    }
    if (idx < 0 || idx >= static_cast<int>(qe.questCount())) {
        std::fprintf(stderr,
            "info-quest: idx %d out of range [0, %zu)\n",
            idx, qe.questCount());
        return 1;
    }
    const auto& q = qe.getQuests()[idx];
        // The word is the format's, from beside the enum.
        auto typeName = wowee::editor::questObjectiveTypeName;
    if (jsonOut) {
        nlohmann::json j;
        j["index"] = idx;
        j["id"] = q.id;
        j["title"] = q.title;
        j["description"] = q.description;
        j["completionText"] = q.completionText;
        j["requiredLevel"] = q.requiredLevel;
        j["questGiverNpcId"] = q.questGiverNpcId;
        j["turnInNpcId"] = q.turnInNpcId;
        j["nextQuestId"] = q.nextQuestId;
        j["reward"] = {
            {"xp", q.reward.xp},
            {"gold", q.reward.gold},
            {"silver", q.reward.silver},
            {"copper", q.reward.copper},
            {"items", q.reward.itemRewards}
        };
        nlohmann::json objs = nlohmann::json::array();
        for (const auto& obj : q.objectives) {
            objs.push_back({
                {"type", typeName(obj.type)},
                {"target", obj.targetName},
                {"count", obj.targetCount},
                {"description", obj.description}
            });
        }
        j["objectives"] = objs;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Quest [%d] '%s'\n", idx, q.title.c_str());
    std::printf("  id              : %u\n", q.id);
    std::printf("  required level  : %u\n", q.requiredLevel);
    std::printf("  giver NPC id    : %u\n", q.questGiverNpcId);
    std::printf("  turn-in NPC id  : %u\n", q.turnInNpcId);
    std::printf("  next quest id   : %u%s\n", q.nextQuestId,
                q.nextQuestId == 0 ? " (terminal)" : "");
    if (!q.description.empty()) {
        std::printf("  description     : %s\n", q.description.c_str());
    }
    if (!q.completionText.empty()) {
        std::printf("  completion text : %s\n", q.completionText.c_str());
    }
    std::printf("  reward          : %u XP, %ug %us %uc, %zu item(s)\n",
                q.reward.xp, q.reward.gold, q.reward.silver,
                q.reward.copper, q.reward.itemRewards.size());
    for (size_t k = 0; k < q.reward.itemRewards.size(); ++k) {
        std::printf("    item[%zu]      : %s\n", k,
                    q.reward.itemRewards[k].c_str());
    }
    std::printf("  objectives      : %zu\n", q.objectives.size());
    for (size_t k = 0; k < q.objectives.size(); ++k) {
        const auto& o = q.objectives[k];
        std::printf("    [%zu] %-7s ×%u  %s%s%s\n",
                    k, typeName(o.type), o.targetCount,
                    o.targetName.c_str(),
                    o.description.empty() ? "" : "  — ",
                    o.description.c_str());
    }
    return 0;
}

int handleInfoObject(int& i, int argc, char** argv) {
    // Single-object deep dive — every PlacedObject field for one
    // entry. Completes the single-entity inspector trio
    // (creature/quest/object).
    std::string path = argv[++i];
    std::string idxStr = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    int idx;
    try { idx = std::stoi(idxStr); }
    catch (...) {
        std::fprintf(stderr, "info-object: bad idx '%s'\n", idxStr.c_str());
        return 1;
    }
    wowee::editor::ObjectPlacer placer;
    if (!placer.loadFromFile(path)) {
        std::fprintf(stderr, "info-object: failed to load %s\n", path.c_str());
        return 1;
    }
    const auto& objs = placer.getObjects();
    if (idx < 0 || idx >= static_cast<int>(objs.size())) {
        std::fprintf(stderr,
            "info-object: idx %d out of range [0, %zu)\n",
            idx, objs.size());
        return 1;
    }
    const auto& o = objs[idx];
    const char* typeStr =
        o.type == wowee::editor::PlaceableType::M2 ? "m2" : "wmo";
    if (jsonOut) {
        nlohmann::json j;
        j["index"] = idx;
        j["type"] = typeStr;
        j["path"] = o.path;
        j["nameId"] = o.nameId;
        j["uniqueId"] = o.uniqueId;
        j["position"] = {o.position.x, o.position.y, o.position.z};
        j["rotation"] = {o.rotation.x, o.rotation.y, o.rotation.z};
        j["scale"] = o.scale;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("Object [%d]\n", idx);
    std::printf("  type      : %s\n", typeStr);
    std::printf("  path      : %s\n", o.path.c_str());
    std::printf("  nameId    : %u\n", o.nameId);
    std::printf("  uniqueId  : %u%s\n", o.uniqueId,
                o.uniqueId == 0 ? " (unassigned)" : "");
    std::printf("  position  : (%.3f, %.3f, %.3f)\n",
                o.position.x, o.position.y, o.position.z);
    std::printf("  rotation  : (%.2f, %.2f, %.2f) deg\n",
                o.rotation.x, o.rotation.y, o.rotation.z);
    std::printf("  scale     : %.3f\n", o.scale);
    return 0;
}


int handleInfoQuests(int& i, int argc, char** argv) {
    using OT = wowee::editor::QuestObjectiveType;
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr, "Failed to load quests.json: %s\n", path.c_str());
        return 1;
    }
    const auto& quests = qe.getQuests();
    int chained = 0, withReward = 0, withItems = 0;
    int objKill = 0, objCollect = 0, objTalk = 0;
    uint32_t totalXp = 0;
    for (const auto& q : quests) {
        if (q.nextQuestId != 0) chained++;
        if (q.reward.xp > 0 || q.reward.gold > 0 ||
            q.reward.silver > 0 || q.reward.copper > 0) withReward++;
        if (!q.reward.itemRewards.empty()) withItems++;
        totalXp += q.reward.xp;
        for (const auto& obj : q.objectives) {
            if (obj.type == OT::KillCreature) objKill++;
            else if (obj.type == OT::CollectItem) objCollect++;
            else if (obj.type == OT::TalkToNPC) objTalk++;
        }
    }
    std::vector<std::string> errors;
    qe.validateChains(errors);
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["total"] = quests.size();
        j["chained"] = chained;
        j["withReward"] = withReward;
        j["withItems"] = withItems;
        j["totalXp"] = totalXp;
        j["avgXpPerQuest"] = quests.empty() ? 0.0
                                : double(totalXp) / quests.size();
        j["objectives"] = {{"kill", objKill},
                            {"collect", objCollect},
                            {"talk", objTalk}};
        j["chainErrors"] = errors;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("quests.json: %s\n", path.c_str());
    std::printf("  total       : %zu\n", quests.size());
    std::printf("  chained     : %d (have nextQuestId)\n", chained);
    std::printf("  with reward : %d\n", withReward);
    std::printf("  with items  : %d\n", withItems);
    std::printf("  total XP    : %u (avg %.0f per quest)\n", totalXp,
                quests.empty() ? 0.0 : double(totalXp) / quests.size());
    std::printf("  objectives  : %d kill, %d collect, %d talk\n",
                objKill, objCollect, objTalk);
    if (!errors.empty()) {
        std::printf("  chain errors: %zu\n", errors.size());
        for (const auto& e : errors) std::printf("    - %s\n", e.c_str());
    }
    return 0;
}

int handleInfoObjects(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    wowee::editor::ObjectPlacer placer;
    if (!placer.loadFromFile(path)) {
        std::fprintf(stderr, "Failed to load objects.json: %s\n", path.c_str());
        return 1;
    }
    const auto& objs = placer.getObjects();
    int m2Count = 0, wmoCount = 0;
    std::unordered_map<std::string, int> pathHist;
    float minScale = 1e30f, maxScale = -1e30f;
    for (const auto& o : objs) {
        if (o.type == wowee::editor::PlaceableType::M2) m2Count++;
        else if (o.type == wowee::editor::PlaceableType::WMO) wmoCount++;
        pathHist[o.path]++;
        if (o.scale < minScale) minScale = o.scale;
        if (o.scale > maxScale) maxScale = o.scale;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["file"] = path;
        j["total"] = objs.size();
        j["m2"] = m2Count;
        j["wmo"] = wmoCount;
        j["uniquePaths"] = pathHist.size();
        if (!objs.empty()) {
            j["scaleMin"] = minScale;
            j["scaleMax"] = maxScale;
        }
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("objects.json: %s\n", path.c_str());
    std::printf("  total       : %zu\n", objs.size());
    std::printf("  M2 doodads  : %d\n", m2Count);
    std::printf("  WMO buildings: %d\n", wmoCount);
    std::printf("  unique paths: %zu\n", pathHist.size());
    if (!objs.empty()) {
        std::printf("  scale range : [%.2f, %.2f]\n", minScale, maxScale);
    }
    return 0;
}

}  // namespace

bool handleContentInfo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-creatures") == 0 && i + 1 < argc) {
        outRc = handleInfoCreatures(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-creatures-by-faction") == 0 && i + 1 < argc) {
        outRc = handleInfoCreaturesByFaction(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-creatures-by-level") == 0 && i + 1 < argc) {
        outRc = handleInfoCreaturesByLevel(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-objects-by-path") == 0 && i + 1 < argc) {
        outRc = handleInfoObjectsByPath(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-objects-by-type") == 0 && i + 1 < argc) {
        outRc = handleInfoObjectsByType(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-quests-by-level") == 0 && i + 1 < argc) {
        outRc = handleInfoQuestsByLevel(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-quests-by-xp") == 0 && i + 1 < argc) {
        outRc = handleInfoQuestsByXp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-creatures") == 0 && i + 1 < argc) {
        outRc = handleListCreatures(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-objects") == 0 && i + 1 < argc) {
        outRc = handleListObjects(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-quests") == 0 && i + 1 < argc) {
        outRc = handleListQuests(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-quest-objectives") == 0 && i + 2 < argc) {
        outRc = handleListQuestObjectives(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-quest-rewards") == 0 && i + 2 < argc) {
        outRc = handleListQuestRewards(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-quest-graph-stats") == 0 && i + 1 < argc) {
        outRc = handleInfoQuestGraphStats(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-creature") == 0 && i + 2 < argc) {
        outRc = handleInfoCreature(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-quest") == 0 && i + 2 < argc) {
        outRc = handleInfoQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-object") == 0 && i + 2 < argc) {
        outRc = handleInfoObject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-quests") == 0 && i + 1 < argc) {
        outRc = handleInfoQuests(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-objects") == 0 && i + 1 < argc) {
        outRc = handleInfoObjects(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
