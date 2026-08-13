// The movement flag bits, against the values the server uses.
//
// These are a bitfield on every movement packet, read in both directions: the
// server tells the client to start water walking by sending an opcode, the
// client sets the bit, and every movement packet it sends afterwards carries
// it. Get a bit wrong and the client both fails to see the state it was put
// into and asserts a different state it was never in.
//
// FEATHER_FALL was 0x4000 and WATER_WALK 0x8000. Those are PENDING_STOP and
// PENDING_STRAFE_STOP - flags the server sets during ordinary movement - while
// the real bits are 0x20000000 and 0x10000000. So a player under slow fall or
// water walking told the server, in every packet, that they were in a pending
// stop.
//
// The oracle is AzerothCore's UnitDefines.h, read once and recorded here.
// Comparing the whole enum against it by bit found those two of twenty-three.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <map>
#include <string>

#include "game/world_packets.hpp"

using wowee::game::MovementFlags;

namespace {

constexpr uint32_t bits(MovementFlags f) { return static_cast<uint32_t>(f); }

}  // namespace

TEST_CASE("each flag is the bit the server means by it", "[movement-flags]") {
    // Recorded from MOVEMENTFLAG_* in UnitDefines.h. Two names differ in style
    // and are noted where they do; the bit is what matters.
    CHECK(bits(MovementFlags::NONE) == 0x00000000);
    CHECK(bits(MovementFlags::FORWARD) == 0x00000001);
    CHECK(bits(MovementFlags::BACKWARD) == 0x00000002);
    CHECK(bits(MovementFlags::STRAFE_LEFT) == 0x00000004);
    CHECK(bits(MovementFlags::STRAFE_RIGHT) == 0x00000008);
    CHECK(bits(MovementFlags::TURN_LEFT) == 0x00000010);   // LEFT
    CHECK(bits(MovementFlags::TURN_RIGHT) == 0x00000020);  // RIGHT
    CHECK(bits(MovementFlags::PITCH_UP) == 0x00000040);
    CHECK(bits(MovementFlags::PITCH_DOWN) == 0x00000080);
    CHECK(bits(MovementFlags::WALKING) == 0x00000100);
    CHECK(bits(MovementFlags::ONTRANSPORT) == 0x00000200);
    CHECK(bits(MovementFlags::LEVITATING) == 0x00000400);  // DISABLE_GRAVITY
    CHECK(bits(MovementFlags::ROOT) == 0x00000800);
    CHECK(bits(MovementFlags::FALLING) == 0x00001000);
    CHECK(bits(MovementFlags::FALLINGFAR) == 0x00002000);
    CHECK(bits(MovementFlags::SWIMMING) == 0x00200000);
    CHECK(bits(MovementFlags::ASCENDING) == 0x00400000);
    CHECK(bits(MovementFlags::DESCENDING) == 0x00800000);
    CHECK(bits(MovementFlags::CAN_FLY) == 0x01000000);
    CHECK(bits(MovementFlags::FLYING) == 0x02000000);
    CHECK(bits(MovementFlags::HOVER) == 0x40000000);
}

TEST_CASE("water walking and slow fall are the high bits", "[movement-flags]") {
    // The two that were wrong. MOVEMENTFLAG_WATERWALKING is 0x10000000 and
    // MOVEMENTFLAG_FALLING_SLOW - the rogue's safe fall - is 0x20000000.
    CHECK(bits(MovementFlags::WATER_WALK) == 0x10000000);
    CHECK(bits(MovementFlags::FEATHER_FALL) == 0x20000000);
}

TEST_CASE("neither one is a pending-stop bit", "[movement-flags]") {
    // What they were. The server sets PENDING_STOP and PENDING_STRAFE_STOP in
    // the course of normal movement, so a client that puts water walking there
    // is not merely failing to notice a state - it is claiming one.
    constexpr uint32_t kPendingStop = 0x00004000;
    constexpr uint32_t kPendingStrafeStop = 0x00008000;
    CHECK(bits(MovementFlags::FEATHER_FALL) != kPendingStop);
    CHECK(bits(MovementFlags::WATER_WALK) != kPendingStrafeStop);
    CHECK(bits(MovementFlags::WATER_WALK) != kPendingStop);
    CHECK(bits(MovementFlags::FEATHER_FALL) != kPendingStrafeStop);
}

TEST_CASE("no two flags share a bit", "[movement-flags]") {
    // A shared bit makes one of the two unreadable, and every test above would
    // still pass.
    const std::map<std::string, uint32_t> all = {
        {"FORWARD", bits(MovementFlags::FORWARD)},
        {"BACKWARD", bits(MovementFlags::BACKWARD)},
        {"STRAFE_LEFT", bits(MovementFlags::STRAFE_LEFT)},
        {"STRAFE_RIGHT", bits(MovementFlags::STRAFE_RIGHT)},
        {"TURN_LEFT", bits(MovementFlags::TURN_LEFT)},
        {"TURN_RIGHT", bits(MovementFlags::TURN_RIGHT)},
        {"PITCH_UP", bits(MovementFlags::PITCH_UP)},
        {"PITCH_DOWN", bits(MovementFlags::PITCH_DOWN)},
        {"WALKING", bits(MovementFlags::WALKING)},
        {"ONTRANSPORT", bits(MovementFlags::ONTRANSPORT)},
        {"LEVITATING", bits(MovementFlags::LEVITATING)},
        {"ROOT", bits(MovementFlags::ROOT)},
        {"FALLING", bits(MovementFlags::FALLING)},
        {"FALLINGFAR", bits(MovementFlags::FALLINGFAR)},
        {"FEATHER_FALL", bits(MovementFlags::FEATHER_FALL)},
        {"WATER_WALK", bits(MovementFlags::WATER_WALK)},
        {"SWIMMING", bits(MovementFlags::SWIMMING)},
        {"ASCENDING", bits(MovementFlags::ASCENDING)},
        {"DESCENDING", bits(MovementFlags::DESCENDING)},
        {"CAN_FLY", bits(MovementFlags::CAN_FLY)},
        {"FLYING", bits(MovementFlags::FLYING)},
        {"HOVER", bits(MovementFlags::HOVER)},
    };
    std::map<uint32_t, std::string> seen;
    for (const auto& [name, bit] : all) {
        INFO(name);
        CHECK(bit != 0);
        // Exactly one bit: these are flags, not values.
        CHECK((bit & (bit - 1)) == 0);
        const auto found = seen.find(bit);
        if (found != seen.end()) {
            INFO("shares a bit with " << found->second);
            CHECK(false);
        }
        seen[bit] = name;
    }
}
