// SMSG_CHAR_ENUM, whose layout is written out three times.
//
// The character list is parsed by three separate functions: CharEnumParser
// for WotLK, and overrides in ClassicPacketParsers and TbcPacketParsers. All
// three are about ninety lines, and they read the same fields in the same
// order apart from two facts:
//
//   WotLK   after flags: uint32 customization + uint8 unknown, 23 equipment
//           items of (uint32 displayModel, uint8 invType, uint32 enchantment)
//   TBC     after flags: uint8 firstLogin, 20 items of the same nine bytes
//   Classic after flags: uint8 firstLogin, 20 items of (uint32, uint8) with
//           no enchantment at all
//
// Nothing covered any of them. That matters more here than it looks: this is
// the packet every login depends on, and a field read one step out of place
// does not raise. The fields after it simply hold each other's values, and a
// character list that draws with the wrong race or an empty name is the first
// thing anyone sees.
//
// The bytes are built here in the server's order rather than by asking this
// client's own writer, so a drift away from the wire fails rather than
// agreeing with itself.
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
    void u8(uint8_t v) { bytes.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(v >> (i * 8)));
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

/// What the server puts on the wire for one character, in its order.
///
/// equipmentItems is 23 on WotLK and 20 before it; enchantment says whether
/// each item carries the trailing uint32. Those two are the whole difference
/// between the three expansions.
void writeCharacter(Writer& w, uint64_t guid, const std::string& name,
                    uint8_t race, uint8_t klass, uint8_t gender, uint8_t level,
                    bool wotlkTail, int equipmentItems, bool enchantment) {
    w.u64(guid);
    w.cstr(name);
    w.u8(race);
    w.u8(klass);
    w.u8(gender);
    w.u32(0x04030201);          // appearance bytes: skin, face, hair, colour
    w.u8(7);                    // facial features
    w.u8(level);
    w.u32(1519);                // zone
    w.u32(0);                   // map
    w.f32(-8913.5f);            // x
    w.f32(554.6f);              // y
    w.f32(93.7f);               // z
    w.u32(0);                   // guild
    w.u32(0);                   // flags
    if (wotlkTail) {
        w.u32(0);               // customization
        w.u8(0);                // unknown
    } else {
        w.u8(1);                // firstLogin
    }
    w.u32(0);                   // pet display model
    w.u32(0);                   // pet level
    w.u32(0);                   // pet family
    for (int i = 0; i < equipmentItems; ++i) {
        w.u32(static_cast<uint32_t>(1000 + i));   // display model
        w.u8(static_cast<uint8_t>(i + 1));        // inventory type
        if (enchantment) w.u32(static_cast<uint32_t>(2000 + i));
    }
}

/// The parse, whichever of the three does it.
void checkOneCharacter(CharEnumResponse& response, const std::string& name,
                       uint8_t race, uint8_t klass, uint8_t level,
                       int equipmentItems, bool enchantment) {
    REQUIRE(response.characters.size() == 1);
    const Character& c = response.characters[0];
    CHECK(c.guid == 0x0102030405060708ull);
    CHECK(c.name == name);
    CHECK(static_cast<uint8_t>(c.race) == race);
    CHECK(static_cast<uint8_t>(c.characterClass) == klass);
    CHECK(c.level == level);
    CHECK(c.zoneId == 1519);
    // The position is the tell for a field read out of step: it sits in the
    // middle of the entry, so anything short before it lands here first.
    CHECK(c.x == Catch::Approx(-8913.5f));
    CHECK(c.y == Catch::Approx(554.6f));
    CHECK(c.z == Catch::Approx(93.7f));
    REQUIRE(static_cast<int>(c.equipment.size()) >= equipmentItems);
    CHECK(c.equipment[0].displayModel == 1000);
    CHECK(c.equipment[0].inventoryType == 1);
    CHECK(c.equipment[equipmentItems - 1].displayModel ==
          static_cast<uint32_t>(1000 + equipmentItems - 1));
    // Classic has no enchantment field, and the parser answers zero for it
    // rather than reading four bytes that are not there.
    CHECK(c.equipment[0].enchantment == (enchantment ? 2000u : 0u));
}

}  // namespace

TEST_CASE("WotLK char enum: 23 items with enchantments", "[charenum]") {
    Writer w;
    w.u8(1);
    writeCharacter(w, 0x0102030405060708ull, "Kelsi", 1, 2, /*gender=*/0, 80,
                   /*wotlkTail=*/true, /*equipmentItems=*/23, /*enchantment=*/true);
    wowee::network::Packet packet(0, w.bytes);

    CharEnumResponse response;
    REQUIRE(CharEnumParser::parse(packet, response));
    checkOneCharacter(response, "Kelsi", 1, 2, 80, 23, true);
}

TEST_CASE("TBC char enum: 20 items with enchantments", "[charenum]") {
    Writer w;
    w.u8(1);
    writeCharacter(w, 0x0102030405060708ull, "Kelsi", 1, 2, /*gender=*/0, 70,
                   /*wotlkTail=*/false, /*equipmentItems=*/20, /*enchantment=*/true);
    wowee::network::Packet packet(0, w.bytes);

    TbcPacketParsers parsers;
    CharEnumResponse response;
    REQUIRE(parsers.parseCharEnum(packet, response));
    checkOneCharacter(response, "Kelsi", 1, 2, 70, 20, true);
}

TEST_CASE("Classic char enum: 20 items and no enchantment field", "[charenum]") {
    Writer w;
    w.u8(1);
    writeCharacter(w, 0x0102030405060708ull, "Kelsi", 1, 2, /*gender=*/0, 60,
                   /*wotlkTail=*/false, /*equipmentItems=*/20, /*enchantment=*/false);
    wowee::network::Packet packet(0, w.bytes);

    ClassicPacketParsers parsers;
    CharEnumResponse response;
    REQUIRE(parsers.parseCharEnum(packet, response));
    checkOneCharacter(response, "Kelsi", 1, 2, 60, 20, false);
}

TEST_CASE("a char enum entry cut short is dropped, not half-read", "[charenum]") {
    // Every one of the three checks a minimum size before reading an entry.
    // The number differs per expansion because the entry does, and getting it
    // wrong is invisible: a truncated packet parses into a character whose
    // fields hold each other's bytes.
    Writer w;
    w.u8(2);
    writeCharacter(w, 0x0102030405060708ull, "Kelsi", 1, 2, /*gender=*/0, 80, true, 23, true);
    const size_t completeEntry = w.bytes.size();
    writeCharacter(w, 0x1112131415161718ull, "Second", 1, 2, /*gender=*/0, 80, true, 23, true);
    w.bytes.resize(completeEntry + 20);   // second entry, cut off early

    wowee::network::Packet packet(0, w.bytes);
    CharEnumResponse response;
    REQUIRE(CharEnumParser::parse(packet, response));
    CHECK(response.characters.size() == 1);
    CHECK(response.characters[0].name == "Kelsi");
}
