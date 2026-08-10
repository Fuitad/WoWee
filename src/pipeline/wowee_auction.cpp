#include "pipeline/wowee_auction.hpp"
#include "pipeline/wowee_binary_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'U', 'C'};
constexpr uint32_t kVersion = 1;
constexpr char kExtension[] = ".wauc";

} // namespace

const WoweeAuction::Entry* WoweeAuction::findById(uint32_t houseId) const {
    for (const auto& e : entries) if (e.houseId == houseId) return &e;
    return nullptr;
}

const char* WoweeAuction::factionAccessName(uint8_t f) {
    switch (f) {
        case Alliance: return "alliance";
        case Horde:    return "horde";
        case Neutral:  return "neutral";
        case Both:     return "both";
        default:       return "unknown";
    }
}

bool WoweeAuctionLoader::save(const WoweeAuction& cat,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath, kExtension), std::ios::binary);
    if (!os) return false;
    const uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writeCatalogHeader(os, kMagic, kVersion, cat.name, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.houseId);
        writePOD(os, e.auctioneerNpcId);
        writeStr(os, e.name);
        writePOD(os, e.factionAccess);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.baseDepositRateBp);
        writePOD(os, e.houseCutRateBp);
        writePOD(os, e.maxBidCopper);
        writePOD(os, e.shortHours);
        writePOD(os, e.mediumHours);
        writePOD(os, e.longHours);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.shortMultBp);
        writePOD(os, e.mediumMultBp);
        writePOD(os, e.longMultBp);
        writePOD(os, e.disallowedClassMask);
    }
    return os.good();
}

WoweeAuction WoweeAuctionLoader::load(const std::string& basePath) {
    WoweeAuction out;
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    if (!is) return out;
    uint32_t entryCount = 0;
    if (!readCatalogHeader(is, kMagic, kVersion, out.name, entryCount)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.houseId) ||
            !readPOD(is, e.auctioneerNpcId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.factionAccess)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.baseDepositRateBp) ||
            !readPOD(is, e.houseCutRateBp) ||
            !readPOD(is, e.maxBidCopper) ||
            !readPOD(is, e.shortHours) ||
            !readPOD(is, e.mediumHours) ||
            !readPOD(is, e.longHours)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.shortMultBp) ||
            !readPOD(is, e.mediumMultBp) ||
            !readPOD(is, e.longMultBp) ||
            !readPOD(is, e.disallowedClassMask)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeAuctionLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath, kExtension), std::ios::binary);
    return is.good();
}

WoweeAuction WoweeAuctionLoader::makeStarter(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    {
        WoweeAuction::Entry e;
        e.houseId = 1; e.name = "Starter House";
        e.factionAccess = WoweeAuction::Neutral;
        c.entries.push_back(e);
    }
    return c;
}

WoweeAuction WoweeAuctionLoader::makeFactionPair(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t fac,
                    uint32_t cutBp, uint32_t auctioneerNpc) {
        WoweeAuction::Entry e;
        e.houseId = id; e.name = name;
        e.factionAccess = fac;
        e.houseCutRateBp = cutBp;
        e.auctioneerNpcId = auctioneerNpc;
        c.entries.push_back(e);
    };
    // Faction houses charge 5%; neutral charges the canonical
    // 15% premium for cross-faction trade.
    add(1, "Stormwind",   WoweeAuction::Alliance, 500,  8719);
    add(2, "Orgrimmar",   WoweeAuction::Horde,    500,  8718);
    add(3, "Booty Bay",   WoweeAuction::Neutral,  1500, 2622);
    return c;
}

WoweeAuction WoweeAuctionLoader::makeRestricted(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    {
        WoweeAuction::Entry e;
        e.houseId = 100; e.name = "Restricted House";
        e.factionAccess = WoweeAuction::Both;
        // Disallow Quest items (class 12) and Containers (class 1)
        // and Keys (class 13) — bitmask combination.
        e.disallowedClassMask = (1u << 1) | (1u << 12) | (1u << 13);
        // Tighter durations + lower max bid for testing.
        e.shortHours = 2;
        e.mediumHours = 6;
        e.longHours = 12;
        e.maxBidCopper = 10000000;     // 1000g cap
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
