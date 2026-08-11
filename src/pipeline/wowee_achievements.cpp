#include "pipeline/wowee_achievements.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'C', 'H'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wach";

} // namespace

const WoweeAchievement::Entry* WoweeAchievement::findById(uint32_t achievementId) const {
    for (const auto& e : entries) {
        if (e.achievementId == achievementId) return &e;
    }
    return nullptr;
}

const char* WoweeAchievement::criteriaKindName(uint8_t k) {
    switch (k) {
        case KillCreature:        return "kill";
        case CompleteQuest:       return "quest";
        case LootItem:            return "loot";
        case ReachLevel:          return "level";
        case EarnReputation:      return "rep";
        case CastSpell:           return "cast";
        case ReachSkillLevel:     return "skill";
        case VisitArea:           return "visit";
        case CompleteAchievement: return "meta";
        default:                  return "unknown";
    }
}

const char* WoweeAchievement::factionName(uint8_t f) {
    switch (f) {
        case FactionBoth:     return "both";
        case FactionAlliance: return "alliance";
        case FactionHorde:    return "horde";
        default:              return "unknown";
    }
}

bool WoweeAchievementLoader::save(const WoweeAchievement& cat,
                     const std::string& basePath) {
    return saveCatalog(cat, basePath, kMagic, kVersion, kExtension,
                       [](std::ofstream& os, const WoweeAchievement::Entry& e) {
        writePOD(os, e.achievementId);
        writePOD(os, e.categoryId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writeStr(os, e.titleReward);
        writePOD(os, e.points);
        writePOD(os, e.minLevel);
        writePOD(os, e.faction);
        uint8_t critCount = static_cast<uint8_t>(
            e.criteria.size() > 255 ? 255 : e.criteria.size());
        writePOD(os, critCount);
        writePOD(os, e.flags);
        for (uint8_t k = 0; k < critCount; ++k) {
            const auto& cr = e.criteria[k];
            writePOD(os, cr.criteriaId);
            writePOD(os, cr.kind);
            writePadding(os, 3);
            writePOD(os, cr.targetId);
            writePOD(os, cr.quantity);
            writeStr(os, cr.description);
        }
                       });
}

WoweeAchievement WoweeAchievementLoader::load(
    const std::string& basePath) {
    return loadCatalog<WoweeAchievement>(basePath, kMagic, kVersion, kExtension,
                              [](std::ifstream& is, WoweeAchievement::Entry& e) {
        if (!readPOD(is, e.achievementId) ||
            !readPOD(is, e.categoryId)) { return false; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath) || !readStr(is, e.titleReward)) { return false; }
        if (!readPOD(is, e.points) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.faction)) { return false; }
        uint8_t critCount = 0;
        if (!readPOD(is, critCount)) { return false; }
        if (!readPOD(is, e.flags)) { return false; }
        e.criteria.resize(critCount);
        for (uint8_t k = 0; k < critCount; ++k) {
            auto& cr = e.criteria[k];
            if (!readPOD(is, cr.criteriaId) ||
                !readPOD(is, cr.kind)) { return false; }
            if (!skipPadding(is, 3)) { return false; }
            if (!readPOD(is, cr.targetId) ||
                !readPOD(is, cr.quantity) ||
                !readStr(is, cr.description)) { return false; }
        }
                                  return true;
                              });
}

bool WoweeAchievementLoader::exists(const std::string& basePath) {
    return catalogExists(basePath, kExtension);
}

WoweeAchievement WoweeAchievementLoader::makeStarter(const std::string& catalogName) {
    WoweeAchievement c;
    c.name = catalogName;
    {
        WoweeAchievement::Entry e;
        e.achievementId = 1;
        e.name = "First Blood";
        e.description = "Kill your first hostile creature.";
        e.points = 5;
        e.criteria.push_back({1, WoweeAchievement::KillCreature,
                               1000, 1, "Kill any hostile creature"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 2;
        e.name = "Helping Hand";
        e.description = "Complete your first quest.";
        e.points = 5;
        e.criteria.push_back({2, WoweeAchievement::CompleteQuest,
                               1, 1, "Complete the Bandit Trouble quest"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 3;
        e.name = "Coming of Age";
        e.description = "Reach character level 10.";
        e.points = 10;
        e.criteria.push_back({3, WoweeAchievement::ReachLevel,
                               0, 10, "Reach level 10"});
        c.entries.push_back(e);
    }
    return c;
}

WoweeAchievement WoweeAchievementLoader::makeBandit(const std::string& catalogName) {
    WoweeAchievement c;
    c.name = catalogName;
    {
        WoweeAchievement::Entry e;
        e.achievementId = 100;
        e.name = "Bandit Hunter";
        e.description = "Slay 50 Defias Bandits.";
        e.points = 10;
        // creatureId 1000 matches WCRT.makeBandit + WSPN.makeCamp
        // + WLOT.makeBandit + WQT.makeStarter target.
        e.criteria.push_back({100, WoweeAchievement::KillCreature,
                               1000, 50, "Defias Bandits slain"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 101;
        e.name = "Strongbox Cracked";
        e.description = "Loot the Bandit Strongbox.";
        e.points = 5;
        // objectId 2000 matches WGOT.makeDungeon's bandit chest.
        e.criteria.push_back({101, WoweeAchievement::LootItem,
                               2000, 1, "Loot the Bandit Strongbox"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 102;
        e.name = "Brotherhood Down";
        e.description = "Complete the Bandit Trouble quest line.";
        e.points = 15;
        e.criteria.push_back({102, WoweeAchievement::CompleteQuest,
                               1, 1, "Quest 1: Bandit Trouble"});
        c.entries.push_back(e);
    }
    return c;
}

WoweeAchievement WoweeAchievementLoader::makeMeta(const std::string& catalogName) {
    WoweeAchievement c;
    c.name = catalogName;
    {
        WoweeAchievement::Entry e;
        e.achievementId = 200; e.name = "Mining Apprentice";
        e.description = "Reach 100 in Mining.";
        e.points = 10;
        // skillId 186 matches WSKL.makeProfessions + WGOT.makeGather.
        e.criteria.push_back({200, WoweeAchievement::ReachSkillLevel,
                               186, 100, "Mining at rank 100"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 201; e.name = "Lockbreaker";
        e.description = "Reach 100 in Lockpicking.";
        e.points = 10;
        // skillId 633 matches WSKL.makeStarter + WLCK.makeDungeon.
        e.criteria.push_back({201, WoweeAchievement::ReachSkillLevel,
                               633, 100, "Lockpicking at rank 100"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 202; e.name = "Frostbinder";
        e.description = "Cast Frostbolt 100 times.";
        e.points = 5;
        // spellId 116 matches WSPL.makeMage's Frostbolt.
        e.criteria.push_back({202, WoweeAchievement::CastSpell,
                               116, 100, "Frostbolt cast count"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 250; e.name = "Jack of All Trades";
        e.description = "Complete all 3 sub-achievements.";
        e.points = 25;
        e.titleReward = "the Versatile";
        e.flags = WoweeAchievement::HiddenUntilEarned;
        e.criteria.push_back({250, WoweeAchievement::CompleteAchievement,
                               200, 1, "Mining Apprentice"});
        e.criteria.push_back({251, WoweeAchievement::CompleteAchievement,
                               201, 1, "Lockbreaker"});
        e.criteria.push_back({252, WoweeAchievement::CompleteAchievement,
                               202, 1, "Frostbinder"});
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
