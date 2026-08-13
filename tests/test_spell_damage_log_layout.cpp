// SMSG_SPELLNONMELEEDAMAGELOG, as the pre-WotLK servers send it.
//
// Three parsers read this packet: one per expansion. The WotLK one carries an
// overkill field after the damage; the Classic and TBC ones do not, and set
// overkill to zero themselves. That is the whole of the difference between the
// expansions - and Classic and TBC read byte for byte alike, which is what
// this pins before they were merged into one pre-WotLK reader.
//
// A misread here is silent in the ordinary way: every field is a number, so a
// parser one step out of line reports a plausible hit for a plausible spell
// and nothing raises. The layout is the oracle, taken from the byte counts the
// parsers themselves document and checked against the field order the WotLK
// reader uses with its extra field removed.
#include <catch_amalgamated.hpp>

#include <cstdint>

#include "core/application.hpp"
#include "game/packet_parsers.hpp"
#include "game/world_packets.hpp"

namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}  // namespace wowee

using namespace wowee::game;

namespace {

constexpr uint64_t kTarget = 0xF130000ABC000123ull;
constexpr uint64_t kAttacker = 0x0000000000004567ull;

/// One packet in the pre-WotLK layout: two packed GUIDs, then the fields in
/// the order both readers take them, with no overkill.
wowee::network::Packet damageLogPacket(uint32_t spellId, uint32_t damage,
                                uint8_t schoolMask, uint32_t absorbed,
                                uint32_t resisted, uint32_t flags) {
    wowee::network::Packet p(0);
    p.writePackedGuid(kTarget);
    p.writePackedGuid(kAttacker);
    p.writeUInt32(spellId);
    p.writeUInt32(damage);
    p.writeUInt8(schoolMask);
    p.writeUInt32(absorbed);
    p.writeUInt32(resisted);
    p.writeUInt8(0);        // periodicLog
    p.writeUInt8(0);        // unused
    p.writeUInt32(0);       // blocked
    p.writeUInt32(flags);
    return p;
}

}  // namespace

TEST_CASE("the pre-WotLK fields land where they are read", "[damage-log]") {
    ClassicPacketParsers classic;
    auto packet = damageLogPacket(133, 412, 4, 30, 12, 0);
    SpellDamageLogData data;
    REQUIRE(classic.parseSpellDamageLog(packet, data));
    CHECK(data.targetGuid == kTarget);
    CHECK(data.attackerGuid == kAttacker);
    CHECK(data.spellId == 133);
    CHECK(data.damage == 412);
    CHECK(data.schoolMask == 4);
    CHECK(data.absorbed == 30);
    CHECK(data.resisted == 12);
    CHECK_FALSE(data.isCrit);
}

TEST_CASE("there is no overkill field before WotLK", "[damage-log]") {
    // The one real difference between the expansions. Reading four bytes that
    // are not there would take the school mask from the middle of the absorbed
    // amount and every field after it would be wrong by four.
    ClassicPacketParsers classic;
    auto packet = damageLogPacket(133, 412, 4, 30, 12, 0);
    SpellDamageLogData data;
    REQUIRE(classic.parseSpellDamageLog(packet, data));
    CHECK(data.overkill == 0);
    CHECK(data.damage == 412);
    CHECK(data.absorbed == 30);
}

TEST_CASE("the crit bit is the second one of the flags", "[damage-log]") {
    ClassicPacketParsers classic;
    SpellDamageLogData crit, plain;
    auto critPacket = damageLogPacket(133, 412, 4, 0, 0, 0x02);
    auto plainPacket = damageLogPacket(133, 412, 4, 0, 0, 0x01);
    REQUIRE(classic.parseSpellDamageLog(critPacket, crit));
    REQUIRE(classic.parseSpellDamageLog(plainPacket, plain));
    CHECK(crit.isCrit);
    CHECK_FALSE(plain.isCrit);
}

TEST_CASE("Classic and TBC read this packet identically", "[damage-log]") {
    // Which is what lets one reader serve both. If an expansion ever needs its
    // own, this is what says so.
    ClassicPacketParsers classic;
    TbcPacketParsers tbc;
    SpellDamageLogData a, b;
    auto pa = damageLogPacket(686, 999, 32, 7, 3, 0x02);
    auto pb = damageLogPacket(686, 999, 32, 7, 3, 0x02);
    REQUIRE(classic.parseSpellDamageLog(pa, a));
    REQUIRE(tbc.parseSpellDamageLog(pb, b));
    CHECK(a.targetGuid == b.targetGuid);
    CHECK(a.attackerGuid == b.attackerGuid);
    CHECK(a.spellId == b.spellId);
    CHECK(a.damage == b.damage);
    CHECK(a.overkill == b.overkill);
    CHECK(a.schoolMask == b.schoolMask);
    CHECK(a.absorbed == b.absorbed);
    CHECK(a.resisted == b.resisted);
    CHECK(a.isCrit == b.isCrit);
}

TEST_CASE("a short packet is refused rather than half read", "[damage-log]") {
    // Truncated after the GUIDs. Reading on would take whatever follows in the
    // buffer as a spell id.
    ClassicPacketParsers classic;
    wowee::network::Packet p(0);
    p.writePackedGuid(kTarget);
    p.writePackedGuid(kAttacker);
    p.writeUInt32(133);
    SpellDamageLogData data;
    CHECK_FALSE(classic.parseSpellDamageLog(p, data));

    wowee::network::Packet empty(0);
    SpellDamageLogData other;
    CHECK_FALSE(classic.parseSpellDamageLog(empty, other));
}
