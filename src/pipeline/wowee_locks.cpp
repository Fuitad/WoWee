#include "pipeline/wowee_locks.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'C', 'K'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wlck";
// SkillLine canonical ID for lockpicking (matches AzerothCore).
constexpr uint32_t kLockpickingSkill = 633;

} // namespace

const WoweeLock::Entry* WoweeLock::findById(uint32_t lockId) const {
    for (const auto& e : entries) {
        if (e.lockId == lockId) return &e;
    }
    return nullptr;
}

const char* WoweeLock::channelKindName(uint8_t k) {
    switch (k) {
        case ChannelNone:     return "-";
        case ChannelItem:     return "item";
        case ChannelLockpick: return "lockpick";
        case ChannelSpell:    return "spell";
        case ChannelDamage:   return "damage";
        default:              return "?";
    }
}

bool WoweeLockLoader::save(const WoweeLock& cat,
                           const std::string& basePath) {
    std::ofstream os(normalizePath(basePath, kExtension), std::ios::binary);
    if (!os) return false;
    const uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writeCatalogHeader(os, kMagic, kVersion, cat.name, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.lockId);
        writeStr(os, e.name);
        writePOD(os, e.flags);
        for (int k = 0; k < WoweeLock::kChannelSlots; ++k) {
            const auto& ch = e.channels[k];
            writePOD(os, ch.kind);
            uint8_t pad = 0;
            writePOD(os, pad);
            writePOD(os, ch.skillRequired);
            writePOD(os, ch.targetId);
        }
    }
    return os.good();
}

WoweeLock WoweeLockLoader::load(const std::string& basePath) {
    WoweeLock out;
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    if (!is) return out;
    uint32_t entryCount = 0;
    if (!readCatalogHeader(is, kMagic, kVersion, out.name, entryCount)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.lockId)) { out.entries.clear(); return out; }
        if (!readStr(is, e.name)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.flags)) { out.entries.clear(); return out; }
        for (int k = 0; k < WoweeLock::kChannelSlots; ++k) {
            auto& ch = e.channels[k];
            if (!readPOD(is, ch.kind)) {
                out.entries.clear(); return out;
            }
            uint8_t pad = 0;
            if (!readPOD(is, pad)) {
                out.entries.clear(); return out;
            }
            if (!readPOD(is, ch.skillRequired) ||
                !readPOD(is, ch.targetId)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeLockLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    return is.good();
}

WoweeLock WoweeLockLoader::makeStarter(const std::string& catalogName) {
    WoweeLock c;
    c.name = catalogName;
    {
        // lockId 1 matches the WGOT.makeDungeon Iron Door lock.
        WoweeLock::Entry e;
        e.lockId = 1; e.name = "Iron Door Lock";
        e.channels[0] = {WoweeLock::ChannelItem, 0, 5001};   // requires key item 5001
        e.channels[1] = {WoweeLock::ChannelDamage, 0, 0};    // OR force open
        c.entries.push_back(e);
    }
    {
        WoweeLock::Entry e;
        e.lockId = 100; e.name = "Wooden Chest Lock";
        e.channels[0] = {WoweeLock::ChannelDamage, 0, 0};   // forceable
        c.entries.push_back(e);
    }
    return c;
}

WoweeLock WoweeLockLoader::makeDungeon(const std::string& catalogName) {
    WoweeLock c;
    c.name = catalogName;
    {
        // lockId 2 matches WGOT.makeDungeon's bandit strongbox.
        WoweeLock::Entry e;
        e.lockId = 2; e.name = "Light Bandit Strongbox";
        e.channels[0] = {WoweeLock::ChannelLockpick,
                          1, kLockpickingSkill};   // any skill rank
        c.entries.push_back(e);
    }
    {
        WoweeLock::Entry e;
        e.lockId = 200; e.name = "Steel Chest Lock";
        // Either heavy lockpick OR a specific key.
        e.channels[0] = {WoweeLock::ChannelLockpick,
                          175, kLockpickingSkill};
        e.channels[1] = {WoweeLock::ChannelItem, 0, 5101};
        c.entries.push_back(e);
    }
    {
        WoweeLock::Entry e;
        e.lockId = 300; e.name = "Boss Vault Seal";
        e.flags = WoweeLock::DestructOnOpen;
        // Quest key only — no lockpick option (story-gated).
        e.channels[0] = {WoweeLock::ChannelItem, 0, 5200};
        c.entries.push_back(e);
    }
    return c;
}

WoweeLock WoweeLockLoader::makeProfessions(const std::string& catalogName) {
    WoweeLock c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint16_t skillReq) {
        WoweeLock::Entry e;
        e.lockId = id; e.name = name;
        e.channels[0] = {WoweeLock::ChannelLockpick,
                          skillReq, kLockpickingSkill};
        c.entries.push_back(e);
    };
    add(401, "Battered Junkbox",     1);
    add(402, "Worn Junkbox",       100);
    add(403, "Sturdy Junkbox",     175);
    add(404, "Heavy Junkbox",      250);
    return c;
}

} // namespace pipeline
} // namespace wowee
