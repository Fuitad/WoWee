// The body of SMSG_MONSTER_MOVE, after the spline flags.
//
// Two parsers read it, one for WotLK and pre-WotLK and one for Vanilla, and
// they share a head: an optional animation block, the duration, an optional
// parabolic block, then the point count. The head was written out twice and
// neither copy was covered; the existing spline tests exercise the create-
// spline path and the curve maths.
//
// A head misread does not raise. Every field after the mistake shifts, so a
// creature moves for a duration read out of an animation id, or walks to a
// waypoint assembled from a start time, and it reads as the server sending
// nonsense.
//
// The oracle is the server's own writer, AzerothCore's
// Movement::PacketBuilder::WriteCommonMonsterMovePart, which after the flags
// word writes:
//
//     if (animation)  uint8 animationId, uint32 effectStartTime
//     uint32 duration
//     if (parabolic)  float verticalAcceleration, uint32 effectStartTime
//
// and its MoveSplineFlag.h, where Animation is 0x00200000 and Parabolic is
// 0x00000800 for WotLK.
#include <catch_amalgamated.hpp>

#include <cstring>
#include <vector>

#include "core/application.hpp"
#include "game/spline_packet.hpp"

namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}

using namespace wowee::game;

namespace {

struct Writer {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }
    void f32(float v) {
        uint32_t raw;
        std::memcpy(&raw, &v, 4);
        u32(raw);
    }
    void vec3(float x, float y, float z) { f32(x); f32(y); f32(z); }
};

constexpr uint32_t kPreWotlkAnimation = SplineFlag::ANIMATION;        // 0x00400000
constexpr uint32_t kWotlkAnimation = SplineFlagWotlk::ANIMATION;      // 0x00200000
constexpr uint32_t kParabolic = SplineFlag::PARABOLIC_MM;             // 0x00000800

}  // namespace

TEST_CASE("the WotLK head is animation, duration, parabolic", "[splinebody]") {
    Writer w;
    w.u8(7);            // animation id
    w.u32(1500);        // animation start time
    w.u32(4200);        // duration
    w.f32(-19.29f);     // vertical acceleration
    w.u32(2100);        // parabolic start time
    w.u32(1);           // one point
    w.vec3(10.0f, 20.0f, 30.0f);
    wowee::network::Packet packet(0, w.bytes);

    SplineBlockData out;
    REQUIRE(parseMonsterMoveSplineBody(packet, out, kWotlkAnimation | kParabolic,
                                       glm::vec3(0.0f), false, SplineFlagSet::Wotlk));
    CHECK(out.hasAnimation);
    CHECK(out.animationType == 7);
    CHECK(out.animationStartTime == 1500u);
    CHECK(out.duration == 4200u);
    CHECK(out.hasParabolic);
    CHECK(out.verticalAcceleration == Catch::Approx(-19.29f));
    CHECK(out.parabolicStartTime == 2100u);
}

TEST_CASE("with no animation the duration comes first", "[splinebody]") {
    // The whole risk in the head: five bytes present or absent depending on a
    // flag. Read them when they are not there and the duration is assembled
    // from the animation id and the first byte of the real duration.
    Writer w;
    w.u32(4200);        // duration
    w.u32(0);           // no points
    wowee::network::Packet packet(0, w.bytes);

    SplineBlockData out;
    REQUIRE(parseMonsterMoveSplineBody(packet, out, 0, glm::vec3(0.0f), false,
                                       SplineFlagSet::Wotlk));
    CHECK_FALSE(out.hasAnimation);
    CHECK(out.duration == 4200u);
}

TEST_CASE("the animation bit moved between expansions", "[splinebody]") {
    // WotLK reads 0x00200000 and everything before it reads 0x00400000. Each
    // parser must ignore the other's bit, because the bit it ignores means
    // something else in its own generation, and honouring it would eat five
    // bytes that are not there.
    Writer w;
    w.u32(4200);        // duration, straight away
    w.u32(0);
    const std::vector<uint8_t> bytes = w.bytes;

    SECTION("WotLK ignores the pre-WotLK bit") {
        wowee::network::Packet packet(0, bytes);
        SplineBlockData out;
        REQUIRE(parseMonsterMoveSplineBody(packet, out, kPreWotlkAnimation,
                                           glm::vec3(0.0f), false, SplineFlagSet::Wotlk));
        CHECK_FALSE(out.hasAnimation);
        CHECK(out.duration == 4200u);
    }

    SECTION("pre-WotLK ignores the WotLK bit") {
        wowee::network::Packet packet(0, bytes);
        SplineBlockData out;
        REQUIRE(parseMonsterMoveSplineBody(packet, out, kWotlkAnimation,
                                           glm::vec3(0.0f), false, SplineFlagSet::PreWotlk));
        CHECK_FALSE(out.hasAnimation);
        CHECK(out.duration == 4200u);
    }
}

TEST_CASE("Vanilla reads the same head with the pre-WotLK animation bit",
          "[splinebody]") {
    Writer w;
    w.u8(3);
    w.u32(500);
    w.u32(9000);        // duration
    w.u32(1);           // one point, always compressed on Vanilla
    w.vec3(1.0f, 2.0f, 3.0f);
    wowee::network::Packet packet(0, w.bytes);

    SplineBlockData out;
    REQUIRE(parseMonsterMoveSplineBodyVanilla(packet, out, kPreWotlkAnimation,
                                              glm::vec3(0.0f)));
    CHECK(out.hasAnimation);
    CHECK(out.animationType == 3);
    CHECK(out.animationStartTime == 500u);
    CHECK(out.duration == 9000u);
    CHECK(out.hasDest);
    CHECK(out.destination.x == Catch::Approx(1.0f));
}

TEST_CASE("parabolic is read only when its flag is set", "[splinebody]") {
    // Eight bytes this time, and the same shape of failure: the point count
    // would be read out of the vertical acceleration.
    Writer w;
    w.u32(4200);
    w.u32(2);           // point count where the parabolic block would be
    w.vec3(5.0f, 6.0f, 7.0f);
    w.u32(0);           // one packed delta
    wowee::network::Packet packet(0, w.bytes);

    SplineBlockData out;
    REQUIRE(parseMonsterMoveSplineBodyVanilla(packet, out, 0, glm::vec3(0.0f)));
    CHECK_FALSE(out.hasParabolic);
    CHECK(out.duration == 4200u);
    CHECK(out.destination.z == Catch::Approx(7.0f));
}

TEST_CASE("a zero point count is a complete body", "[splinebody]") {
    // The server sends these: a spline that only changes the facing. Refusing
    // it would drop the whole packet.
    Writer w;
    w.u32(4200);
    w.u32(0);
    wowee::network::Packet packet(0, w.bytes);

    SplineBlockData out;
    CHECK(parseMonsterMoveSplineBodyVanilla(packet, out, 0, glm::vec3(0.0f)));
    CHECK(out.duration == 4200u);
    CHECK_FALSE(out.hasDest);
}

TEST_CASE("a truncated head is refused rather than read past", "[splinebody]") {
    SECTION("nothing after the animation flag") {
        Writer w;
        w.u8(1);
        wowee::network::Packet packet(0, w.bytes);
        SplineBlockData out;
        CHECK_FALSE(parseMonsterMoveSplineBody(packet, out, kWotlkAnimation,
                                               glm::vec3(0.0f), false,
                                               SplineFlagSet::Wotlk));
    }
    SECTION("no duration") {
        Writer w;
        w.u8(0);
        wowee::network::Packet packet(0, w.bytes);
        SplineBlockData out;
        CHECK_FALSE(parseMonsterMoveSplineBodyVanilla(packet, out, 0, glm::vec3(0.0f)));
    }
    SECTION("a point count with no points behind it") {
        Writer w;
        w.u32(4200);
        w.u32(4);
        wowee::network::Packet packet(0, w.bytes);
        SplineBlockData out;
        CHECK_FALSE(parseMonsterMoveSplineBodyVanilla(packet, out, 0, glm::vec3(0.0f)));
    }
}

TEST_CASE("an implausible point count is refused", "[splinebody]") {
    // A count read out of the wrong bytes is usually enormous, and reserving
    // for it is how a malformed packet becomes an allocation failure.
    Writer w;
    w.u32(4200);
    w.u32(100000);
    wowee::network::Packet packet(0, w.bytes);

    SplineBlockData out;
    CHECK_FALSE(parseMonsterMoveSplineBodyVanilla(packet, out, 0, glm::vec3(0.0f)));
}
