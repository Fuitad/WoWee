// SMSG_GAMEOBJECT_QUERY_RESPONSE, whose layout is written out three times.
//
// ClassicPacketParsers, TbcPacketParsers and the shared parser each carry
// their own copy of forty-odd lines. The three differ in exactly one number:
// how many strings sit between the four names and the twenty-four
// type-specific data fields.
//
//   Classic  none
//   TBC      two - iconName, castBarCaption
//   WotLK    three - iconName, castBarCaption, unk1
//
// The order is the server's, off AzerothCore's QueryHandler.cpp:
// entry, type, displayId, Name, name2, name3, name4, IconName, CastBarCaption,
// unk1, raw[24], size. The bytes below are written in that order rather than by
// asking this client's own writer.
//
// Getting the count wrong does not raise. The strings are usually empty, so
// each one costs a single NUL, and reading one too few starts data[0] on that
// NUL: every type-specific field shifts by a byte and a chest reports a lock
// it does not have. The count is asserted here for all three expansions
// precisely because nothing else would notice.
#include <catch_amalgamated.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "core/application.hpp"
#include "game/packet_parsers.hpp"
#include "game/world_packets.hpp"

namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}

using namespace wowee::game;

namespace {

struct Writer {
    std::vector<uint8_t> bytes;
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }
    void f32(float v) {
        uint32_t raw;
        std::memcpy(&raw, &v, 4);
        u32(raw);
    }
    void cstr(const std::string& s) {
        bytes.insert(bytes.end(), s.begin(), s.end());
        bytes.push_back(0);
    }
};

/// The packet as the server writes it, with `extraStrings` of the 2.0.3 block.
std::vector<uint8_t> buildResponse(uint32_t entry, uint32_t type,
                                   const std::string& name, int extraStrings) {
    Writer w;
    w.u32(entry);
    w.u32(type);
    w.u32(0xD15);                  // displayId
    w.cstr(name);
    w.cstr("");                    // name2
    w.cstr("");                    // name3
    w.cstr("");                    // name4
    const char* extras[] = {"Attack", "Collecting", ""};
    for (int i = 0; i < extraStrings; ++i) w.cstr(extras[i]);
    for (uint32_t i = 0; i < 24; ++i) w.u32(0xA000 + i);
    w.f32(1.5f);                   // size, which this client does not read
    return w.bytes;
}

wowee::network::Packet makePacket(const std::vector<uint8_t>& bytes) {
    return wowee::network::Packet(0x005F, bytes);
}

}  // namespace

TEST_CASE("the WotLK gameobject query reads three strings before the data",
          "[gameobject][wire]") {
    auto bytes = buildResponse(1731, 3, "Copper Vein", 3);
    auto packet = makePacket(bytes);
    GameObjectQueryResponseData data;
    REQUIRE(GameObjectQueryResponseParser::parse(packet, data));

    CHECK(data.entry == 1731);
    CHECK(data.type == 3);
    CHECK(data.displayId == 0xD15);
    CHECK(data.name == "Copper Vein");
    REQUIRE(data.hasData);
    // The whole point of the count: data[0] is the first field after the
    // strings, and one string too few or too many puts something else there.
    CHECK(data.data[0] == 0xA000);
    CHECK(data.data[23] == 0xA017);
}

TEST_CASE("TBC reads two, Classic reads none", "[gameobject][wire]") {
    SECTION("TBC") {
        auto bytes = buildResponse(180, 0, "Door", 2);
        auto packet = makePacket(bytes);
        GameObjectQueryResponseData data;
        TbcPacketParsers parsers;
        REQUIRE(parsers.parseGameObjectQueryResponse(packet, data));
        CHECK(data.name == "Door");
        CHECK(data.data[0] == 0xA000);
        CHECK(data.data[23] == 0xA017);
    }
    SECTION("Classic") {
        auto bytes = buildResponse(180, 0, "Door", 0);
        auto packet = makePacket(bytes);
        GameObjectQueryResponseData data;
        ClassicPacketParsers parsers;
        REQUIRE(parsers.parseGameObjectQueryResponse(packet, data));
        CHECK(data.name == "Door");
        CHECK(data.data[0] == 0xA000);
        CHECK(data.data[23] == 0xA017);
    }
}

TEST_CASE("one string too few shifts every data field", "[gameobject][wire]") {
    // Not an error - a wrong answer, and the reason the count is asserted at
    // all. A WotLK packet read by the TBC reader leaves an empty string's NUL
    // in front of data[0].
    auto bytes = buildResponse(1731, 3, "Copper Vein", 3);
    auto packet = makePacket(bytes);
    GameObjectQueryResponseData data;
    TbcPacketParsers parsers;
    REQUIRE(parsers.parseGameObjectQueryResponse(packet, data));
    CHECK(data.name == "Copper Vein");
    CHECK(data.data[0] != 0xA000);
}

TEST_CASE("an entry the server does not know stops at the flag",
          "[gameobject][wire]") {
    // The high bit means "no such gameobject" and nothing follows it. Reading
    // on would take whatever the next packet's bytes happen to be.
    Writer w;
    w.u32(1731u | 0x80000000u);
    auto packet = makePacket(w.bytes);
    GameObjectQueryResponseData data;
    REQUIRE(GameObjectQueryResponseParser::parse(packet, data));
    CHECK(data.entry == 1731);
    CHECK(data.name.empty());
    CHECK_FALSE(data.hasData);
}

TEST_CASE("a truncated response stops rather than reading past the end",
          "[gameobject][wire]") {
    const auto full = buildResponse(1731, 3, "Copper Vein", 3);
    for (size_t cut = 4; cut < full.size(); ++cut) {
        std::vector<uint8_t> shortened(full.begin(), full.begin() + static_cast<long>(cut));
        auto packet = makePacket(shortened);
        GameObjectQueryResponseData data;
        INFO("cut to " << cut << " of " << full.size());
        GameObjectQueryResponseParser::parse(packet, data);
    }
    SUCCEED("no read past the end at any length");
}
