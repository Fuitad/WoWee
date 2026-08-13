// The three client packets Classic and TBC build the same way.
//
// Each is a WotLK packet with something taken off the front or the back, and
// each failure is silent in the same way: the server reads the bytes it
// expects, finds the wrong values, and does nothing. No error comes back.
//
//   CMSG_MOVE_*                 WotLK writes a packed GUID before the payload.
//                               Pre-WotLK does not, so a client that sent one
//                               would have every field after it read one to
//                               nine bytes late.
//   CMSG_QUESTGIVER_QUERY_QUEST WotLK appends a trailing uint32. Pre-WotLK
//   CMSG_QUESTGIVER_ACCEPT_QUEST does not.
//
// The oracle is the shape itself: these assert what is absent, which is the
// whole of the difference. Pinned here before the two builders were merged
// into one pre-WotLK implementation, so the merge could not quietly change
// either of them.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <vector>

#include "core/application.hpp"
#include "game/packet_parsers.hpp"
#include "game/world_packets.hpp"

// The packet layer reaches the Application singleton; nothing under test does,
// so it stays null. Same stub the other packet-layout tests carry.
namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}  // namespace wowee

using wowee::game::ClassicPacketParsers;
using wowee::game::LogicalOpcode;
using wowee::game::MovementInfo;
using wowee::game::TbcPacketParsers;

namespace {

constexpr uint64_t kNpcGuid = 0xF130000ABC000123ull;
constexpr uint32_t kQuestId = 5261;
constexpr uint64_t kPlayerGuid = 0x0000000000001234ull;

MovementInfo sampleMovement() {
    MovementInfo info{};
    info.flags = 0x00000001;  // FORWARD
    info.flags2 = 0x0000;
    info.time = 0x11223344;
    info.x = 1.0f;
    info.y = 2.0f;
    info.z = 3.0f;
    info.orientation = 0.5f;
    return info;
}

}  // namespace

TEST_CASE("the quest query packet has no trailing field", "[prewotlk-build]") {
    // Eight bytes of GUID and four of quest id. WotLK adds a uint32 after
    // this; sending it to a pre-WotLK server leaves four bytes over.
    ClassicPacketParsers classic;
    auto packet = classic.buildQueryQuestPacket(kNpcGuid, kQuestId);
    CHECK(packet.readUInt64() == kNpcGuid);
    CHECK(packet.readUInt32() == kQuestId);
    CHECK_FALSE(packet.hasData());
}

TEST_CASE("the quest accept packet has no trailing field", "[prewotlk-build]") {
    ClassicPacketParsers classic;
    auto packet = classic.buildAcceptQuestPacket(kNpcGuid, kQuestId);
    CHECK(packet.readUInt64() == kNpcGuid);
    CHECK(packet.readUInt32() == kQuestId);
    CHECK_FALSE(packet.hasData());
}

TEST_CASE("the movement packet has no packed guid in front", "[prewotlk-build]") {
    // The payload starts at the flags. A packed GUID would put a mask byte
    // there instead, and every field after it would be read late.
    ClassicPacketParsers classic;
    const auto info = sampleMovement();
    auto packet = classic.buildMovementPacket(LogicalOpcode::MSG_MOVE_START_FORWARD,
                                              info, kPlayerGuid);
    CHECK(packet.readUInt32() == info.flags);
    // And straight to the timestamp: Classic writes no flags2 at all.
    CHECK(packet.readUInt32() == info.time);
}

TEST_CASE("the flags2 field is a different width in each expansion",
          "[prewotlk-build]") {
    // The reason buildMovementPacket cannot be shared even though the two
    // bodies read identically: each calls its own writeMovementPayload.
    // Vanilla has no second flags field, TBC added a uint8, WotLK widened it
    // to a uint16. One byte either way puts the timestamp and every float
    // after it out of step, and the server reads the packet without
    // complaining.
    ClassicPacketParsers classic;
    TbcPacketParsers tbc;
    const auto info = sampleMovement();

    const auto classicBytes =
        classic.buildMovementPacket(LogicalOpcode::MSG_MOVE_START_FORWARD, info,
                                    kPlayerGuid).getData();
    const auto tbcBytes =
        tbc.buildMovementPacket(LogicalOpcode::MSG_MOVE_START_FORWARD, info,
                                kPlayerGuid).getData();

    CHECK(tbcBytes.size() == classicBytes.size() + 1);

    // TBC's timestamp sits one byte later than Classic's, which is the whole
    // of the difference.
    auto tbcPacket = tbc.buildMovementPacket(LogicalOpcode::MSG_MOVE_START_FORWARD,
                                             info, kPlayerGuid);
    CHECK(tbcPacket.readUInt32() == info.flags);
    CHECK(tbcPacket.readUInt8() == static_cast<uint8_t>(info.flags2 & 0xFF));
    CHECK(tbcPacket.readUInt32() == info.time);
}

TEST_CASE("classic and TBC build both quest packets identically",
          "[prewotlk-build]") {
    // Which is what lets one implementation serve both. If an expansion ever
    // needs its own, this is what says so.
    ClassicPacketParsers classic;
    TbcPacketParsers tbc;

    CHECK(classic.buildQueryQuestPacket(kNpcGuid, kQuestId).getData() ==
          tbc.buildQueryQuestPacket(kNpcGuid, kQuestId).getData());
    CHECK(classic.buildAcceptQuestPacket(kNpcGuid, kQuestId).getData() ==
          tbc.buildAcceptQuestPacket(kNpcGuid, kQuestId).getData());
}

TEST_CASE("the player guid is not written anywhere", "[prewotlk-build]") {
    // buildMovementPacket takes a player GUID because the WotLK one needs it.
    // Pre-WotLK ignores it, and the way to say so is that changing it changes
    // nothing about the bytes.
    ClassicPacketParsers classic;
    const auto info = sampleMovement();
    const auto a = classic.buildMovementPacket(LogicalOpcode::MSG_MOVE_START_FORWARD,
                                               info, 0ull);
    const auto b = classic.buildMovementPacket(LogicalOpcode::MSG_MOVE_START_FORWARD,
                                               info, 0xFFFFFFFFFFFFFFFFull);
    CHECK(a.getData() == b.getData());
}
