#include "pipeline/wowee_events.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'E', 'A'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wsea";

} // namespace

const WoweeEvent::Entry* WoweeEvent::findById(uint32_t eventId) const {
    for (const auto& e : entries) if (e.eventId == eventId) return &e;
    return nullptr;
}

const char* WoweeEvent::holidayKindName(uint8_t k) {
    switch (k) {
        case Combat:      return "combat";
        case Collection:  return "collection";
        case Racial:      return "racial";
        case Anniversary: return "anniversary";
        case Fishing:     return "fishing";
        case Cosmetic:    return "cosmetic";
        case WorldEvent:  return "world-event";
        default:          return "unknown";
    }
}

const char* WoweeEvent::factionGroupName(uint8_t f) {
    switch (f) {
        case FactionBoth:     return "both";
        case FactionAlliance: return "alliance";
        case FactionHorde:    return "horde";
        default:              return "unknown";
    }
}

bool WoweeEventLoader::save(const WoweeEvent& cat,
                            const std::string& basePath) {
    std::ofstream os(normalizePath(basePath, kExtension), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.eventId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writeStr(os, e.announceMessage);
        writePOD(os, e.startDate);
        writePOD(os, e.duration_seconds);
        writePOD(os, e.recurrenceDays);
        writePOD(os, e.holidayKind);
        writePOD(os, e.factionGroup);
        writePOD(os, e.bonusXpPercent);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        writePOD(os, e.tokenIdReward);
    }
    return os.good();
}

WoweeEvent WoweeEventLoader::load(const std::string& basePath) {
    WoweeEvent out;
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version) || version != kVersion) return out;
    if (!readStr(is, out.name)) return out;
    uint32_t entryCount = 0;
    if (!readPOD(is, entryCount)) return out;
    if (entryCount > (1u << 20)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.eventId)) { out.entries.clear(); return out; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath) || !readStr(is, e.announceMessage)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.startDate) ||
            !readPOD(is, e.duration_seconds) ||
            !readPOD(is, e.recurrenceDays) ||
            !readPOD(is, e.holidayKind) ||
            !readPOD(is, e.factionGroup) ||
            !readPOD(is, e.bonusXpPercent)) {
            out.entries.clear(); return out;
        }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.tokenIdReward)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeEventLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    return is.good();
}

WoweeEvent WoweeEventLoader::makeStarter(const std::string& catalogName) {
    WoweeEvent c;
    c.name = catalogName;
    {
        WoweeEvent::Entry e;
        e.eventId = 1; e.name = "Brawl Week";
        e.description = "Double honor gains in all PvP combat.";
        e.holidayKind = WoweeEvent::Combat;
        e.duration_seconds = 7 * 24 * 3600;       // 1 week
        e.bonusXpPercent = 200;
        c.entries.push_back(e);
    }
    {
        WoweeEvent::Entry e;
        e.eventId = 2; e.name = "Stranglethorn Fishing Extravaganza";
        e.description = "Catch the rare Tastyfish for prizes.";
        e.holidayKind = WoweeEvent::Fishing;
        e.duration_seconds = 4 * 3600;            // 4 hours
        e.recurrenceDays = 7;                     // weekly
        c.entries.push_back(e);
    }
    {
        WoweeEvent::Entry e;
        e.eventId = 3; e.name = "Anniversary";
        e.description = "Celebrate the server's launch anniversary.";
        e.holidayKind = WoweeEvent::Anniversary;
        e.duration_seconds = 3 * 24 * 3600;       // 3 days
        e.recurrenceDays = 365;                   // yearly
        e.bonusXpPercent = 50;
        c.entries.push_back(e);
    }
    return c;
}

WoweeEvent WoweeEventLoader::makeYearly(const std::string& catalogName) {
    WoweeEvent c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t durDays, uint32_t tokenId,
                    const char* announce) {
        WoweeEvent::Entry e;
        e.eventId = id; e.name = name;
        e.holidayKind = kind;
        e.duration_seconds = durDays * 24 * 3600;
        e.recurrenceDays = 365;
        e.tokenIdReward = tokenId;
        e.announceMessage = announce;
        c.entries.push_back(e);
    };
    // tokenIds (200/201/202/203) match WTKN.makeSeasonal.
    add(100, "Hallow's End", WoweeEvent::Collection, 14, 200,
        "The Headless Horseman rides! Tricky Treats await...");
    add(101, "Brewfest", WoweeEvent::Cosmetic, 14, 201,
        "Brewfest is here. Mount up and ride for the big kegs!");
    add(102, "Lunar Festival", WoweeEvent::Anniversary, 14, 202,
        "Visit the elders to receive Coins of Ancestry.");
    add(103, "Winter's Veil", WoweeEvent::Cosmetic, 21, 203,
        "Snow falls across the realm. Greatfather Winter awaits.");
    return c;
}

WoweeEvent WoweeEventLoader::makeBonusWeekends(const std::string& catalogName) {
    WoweeEvent c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t bonus, const char* desc) {
        WoweeEvent::Entry e;
        e.eventId = id; e.name = name; e.description = desc;
        e.holidayKind = WoweeEvent::Combat;
        e.duration_seconds = 3 * 24 * 3600;       // Fri-Sun
        e.recurrenceDays = 30;                    // monthly
        e.bonusXpPercent = bonus;
        c.entries.push_back(e);
    };
    add(300, "Quest XP Bonus",       50,  "+50% experience from quests.");
    add(301, "Combat XP Bonus",     100,  "Double experience from kills.");
    add(302, "Refer-A-Friend",      200,  "Triple experience while grouped with a recruit.");
    return c;
}

} // namespace pipeline
} // namespace wowee
