// SMSG_MONSTER_MOVE's move-type byte and the facing that follows it.
//
// This was written out three times - once in the WotLK parser, once in the
// vanilla one and once in the TBC one - and every field after it depends on
// having consumed exactly the right number of bytes for the answer. A length
// wrong here is not a wrong facing, it is a misread spline and a creature that
// walks somewhere it was never sent.
//
// Nothing covered it before, in any of the three copies.
#include <catch_amalgamated.hpp>

#include "game/spline_packet.hpp"
#include "network/packet.hpp"

using wowee::game::MonsterMoveData;
using wowee::game::parseMonsterMoveFacing;
using wowee::network::Packet;

namespace {

/// A packet holding just the move-type byte and whatever should follow it.
Packet withFacing(uint8_t moveType, const std::vector<float>& floats = {},
                  bool withGuid = false) {
    Packet p(0xDD);
    p.writeUInt8(moveType);
    if (withGuid) p.writeUInt64(0x0123456789ABCDEFull);
    for (float f : floats) p.writeFloat(f);
    return p;
}

}  // namespace

TEST_CASE("Stop ends the packet where the creature already is", "[monstermove]") {
    // Move type 1 carries nothing after it, and the caller must not go looking
    // for a spline - there is none. The destination is the current position,
    // and hasDest stays false so nothing treats it as somewhere to walk to.
    Packet p = withFacing(1);
    MonsterMoveData data;
    data.x = 10.0f;
    data.y = 20.0f;
    data.z = 30.0f;
    bool stopped = false;

    REQUIRE(parseMonsterMoveFacing(p, data, stopped));
    CHECK(stopped);
    CHECK(data.destX == 10.0f);
    CHECK(data.destY == 20.0f);
    CHECK(data.destZ == 30.0f);
    CHECK_FALSE(data.hasDest);
    CHECK_FALSE(p.hasData());
}

TEST_CASE("each facing kind consumes its own length", "[monstermove]") {
    SECTION("FacingSpot is three floats, read past and not kept") {
        Packet p = withFacing(2, {1.0f, 2.0f, 3.0f});
        MonsterMoveData data;
        bool stopped = true;
        REQUIRE(parseMonsterMoveFacing(p, data, stopped));
        CHECK_FALSE(stopped);
        // Twelve bytes gone, and nothing left behind for the spline reader.
        CHECK_FALSE(p.hasData());
        CHECK(data.facingTarget == 0);
        CHECK(data.facingAngle == 0.0f);
    }

    SECTION("FacingTarget is a guid, and it is kept") {
        Packet p = withFacing(3, {}, /*withGuid=*/true);
        MonsterMoveData data;
        bool stopped = true;
        REQUIRE(parseMonsterMoveFacing(p, data, stopped));
        CHECK_FALSE(stopped);
        CHECK(data.facingTarget == 0x0123456789ABCDEFull);
        CHECK_FALSE(p.hasData());
    }

    SECTION("FacingAngle is one float, and it is kept") {
        Packet p = withFacing(4, {1.75f});
        MonsterMoveData data;
        bool stopped = true;
        REQUIRE(parseMonsterMoveFacing(p, data, stopped));
        CHECK_FALSE(stopped);
        CHECK(data.facingAngle == 1.75f);
        CHECK_FALSE(p.hasData());
    }

    SECTION("Normal carries no facing at all") {
        // Move type 0 falls through every branch and takes nothing, which is
        // what leaves the splineFlags the caller reads next in the right place.
        Packet p = withFacing(0, {9.0f});
        MonsterMoveData data;
        bool stopped = true;
        REQUIRE(parseMonsterMoveFacing(p, data, stopped));
        CHECK_FALSE(stopped);
        CHECK(p.getRemainingSize() == 4);
    }
}

TEST_CASE("a facing that runs off the end is refused, not guessed", "[monstermove]") {
    // Each of these promises more than the packet holds. Answering true would
    // hand the caller a half-read facing and a read position inside a field.
    SECTION("no move type byte") {
        Packet p(0xDD);
        MonsterMoveData data;
        bool stopped = false;
        CHECK_FALSE(parseMonsterMoveFacing(p, data, stopped));
    }

    SECTION("FacingSpot with two floats instead of three") {
        Packet p = withFacing(2, {1.0f, 2.0f});
        MonsterMoveData data;
        bool stopped = false;
        CHECK_FALSE(parseMonsterMoveFacing(p, data, stopped));
    }

    SECTION("FacingTarget with half a guid") {
        Packet p(0xDD);
        p.writeUInt8(3);
        p.writeUInt32(0xDEADBEEF);
        MonsterMoveData data;
        bool stopped = false;
        CHECK_FALSE(parseMonsterMoveFacing(p, data, stopped));
    }

    SECTION("FacingAngle with nothing after it") {
        Packet p = withFacing(4);
        MonsterMoveData data;
        bool stopped = false;
        CHECK_FALSE(parseMonsterMoveFacing(p, data, stopped));
    }
}
