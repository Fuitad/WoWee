// SMSG_ITEM_QUERY_SINGLE_RESPONSE as classic and TBC send it.
//
// The two readers are 154 lines each and differ in one field: TBC carries a
// SoundOverrideSubclass after subClass and classic does not. Everything after
// it is the same run in the same order, which is what makes the two easy to
// merge and easy to merge wrongly: a field read one step out of place does not
// raise, it shifts every value after it, and an item's stats, damage and
// bind type all come from that tail.
//
// So this pins what each reader answers for a packet built field by field,
// before either is touched.
//
// What it is not: an independent check that the layout matches a server. The
// bytes here are laid out from what the readers themselves expect, so this
// proves a change preserves behaviour, not that the behaviour was ever right.
// The manifest test has a real outside oracle; this one does not, and the
// distinction is worth keeping straight.
#include <catch_amalgamated.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "core/application.hpp"
#include "game/packet_parsers.hpp"

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

/// One item, in the order these readers expect it.
///
/// soundOverride is the whole difference between the two expansions: TBC sends
/// the field, classic does not.
std::vector<uint8_t> itemBytes(bool soundOverride) {
    Writer w;
    w.u32(6948);                 // entry
    w.u32(15);                   // itemClass
    w.u32(4);                    // subClass
    if (soundOverride) w.u32(0xFFFFFFFFu);
    w.cstr("Hearthstone");
    w.cstr("");
    w.cstr("");
    w.cstr("");
    w.u32(6418);                 // displayInfoId
    w.u32(1);                    // quality
    w.u32(64);                   // flags
    w.u32(1000);                 // buyPrice
    w.u32(250);                  // sellPrice
    w.u32(0);                    // inventoryType

    // readCommonRequirements: fourteen in a row.
    w.u32(0xFFFFFFFFu);          // allowableClass
    w.u32(0xFFFFFFFFu);          // allowableRace
    w.u32(5);                    // itemLevel
    w.u32(3);                    // requiredLevel
    w.u32(0);                    // requiredSkill
    w.u32(0);                    // requiredSkillRank
    w.u32(0);                    // requiredSpell
    w.u32(0);                    // requiredHonorRank
    w.u32(0);                    // requiredCityRank
    w.u32(0);                    // requiredReputationFaction
    w.u32(0);                    // requiredReputationRank
    w.u32(1);                    // maxCount
    w.u32(20);                   // maxStack
    w.u32(0);                    // containerSlots

    // Ten stat pairs, no count prefix on either expansion.
    w.u32(7); w.u32(11);         // stamina
    w.u32(0); w.u32(37);         // mana, which TBC used to drop
    for (int i = 2; i < 10; ++i) { w.u32(0); w.u32(0); }

    // Five damage entries.
    w.f32(12.0f); w.f32(18.0f); w.u32(0);
    for (int i = 1; i < 5; ++i) { w.f32(0.0f); w.f32(0.0f); w.u32(0); }

    w.u32(42);                   // armor
    w.u32(1); w.u32(2); w.u32(3); w.u32(4); w.u32(5); w.u32(6);  // resistances
    w.u32(2900);                 // delay
    w.u32(0);                    // ammoType
    w.f32(0.0f);                 // rangedModRange

    // Five spells, six fields each.
    w.u32(8690); w.u32(0); w.u32(0); w.u32(0); w.u32(0); w.u32(0);
    for (int i = 1; i < 5; ++i) for (int j = 0; j < 6; ++j) w.u32(0);

    w.u32(1);                    // bindType
    return w.bytes;
}

void checkItem(const ItemQueryResponseData& data) {
    CHECK(data.entry == 6948u);
    CHECK(data.name == "Hearthstone");
    CHECK(data.displayInfoId == 6418u);
    CHECK(data.quality == 1u);
    CHECK(data.sellPrice == 250u);
    CHECK(data.itemLevel == 5u);
    CHECK(data.requiredLevel == 3u);
    // maxStack sits at the end of readCommonRequirements, so anything read one
    // step out of place before it lands here first.
    CHECK(data.maxStack == 20);
    CHECK(data.stamina == 11);
    // Stat type 0 is mana. TBC used to skip it outright.
    REQUIRE(data.extraStats.size() == 1);
    CHECK(data.extraStats[0].statType == 0u);
    CHECK(data.extraStats[0].statValue == 37);
    CHECK(data.armor == 42);
    CHECK(data.delayMs == 2900u);
    CHECK(data.holyRes == 1);
    CHECK(data.arcaneRes == 6);
    CHECK(data.spells[0].spellId == 8690u);
    CHECK(data.bindType == 1u);
}

}  // namespace

TEST_CASE("classic item query: no SoundOverrideSubclass", "[item_query]") {
    const auto bytes = itemBytes(/*soundOverride=*/false);
    wowee::network::Packet packet(0, bytes);

    ClassicPacketParsers parsers;
    ItemQueryResponseData data;
    REQUIRE(parsers.parseItemQueryResponse(packet, data));
    checkItem(data);
}

TEST_CASE("TBC item query: SoundOverrideSubclass after subClass", "[item_query]") {
    const auto bytes = itemBytes(/*soundOverride=*/true);
    wowee::network::Packet packet(0, bytes);

    TbcPacketParsers parsers;
    ItemQueryResponseData data;
    REQUIRE(parsers.parseItemQueryResponse(packet, data));
    checkItem(data);
}

TEST_CASE("an entry with the high bit set is an item the server has not got",
          "[item_query]") {
    Writer w;
    w.u32(0x80000000u | 6948u);
    wowee::network::Packet packet(0, w.bytes);

    ClassicPacketParsers parsers;
    ItemQueryResponseData data;
    // Answered, and marked absent rather than filled in with whatever followed.
    parsers.parseItemQueryResponse(packet, data);
    CHECK(data.name.empty());
}
