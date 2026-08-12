// The socket block of SMSG_ITEM_QUERY_SINGLE_RESPONSE.
//
// The server writes the three sockets one at a time, colour and content
// together - `for (s) { << Socket[s].Color; << Socket[s].Content; }` - and this
// client read three colours followed by three contents. Six reads either way,
// so the socket bonus landed correctly and nothing downstream looked wrong: the
// second socket took the first socket's *content*, which a template leaves
// empty, and an item with three sockets drew one.
//
// tools/packet_layout_check.py structurally cannot see this. It compares the
// fixed prefix a packet opens with, and this one reaches a string by its fourth
// field, so the whole socket block is past where it can look.
//
// The packet here is built field for field the way AzerothCore's
// WorldSession::HandleItemQuerySingleOpcode writes it, so the test also pins
// the fifty-odd fields the parser walks to reach the sockets - including the
// BuyCount that AzerothCore does not send, which the parser scores for.
#include <catch_amalgamated.hpp>

#include "core/application.hpp"
#include "game/world_packets.hpp"
#include "network/packet.hpp"

// The parser's translation unit inlines isActiveExpansion(), which reaches
// through the Application singleton. Nothing on this path calls it.
namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}

using namespace wowee::game;
using wowee::network::Packet;

namespace {

constexpr uint32_t kEntry = 40001;
constexpr uint32_t kSocketBonus  = 3729;
constexpr uint32_t kGemProperties = 0;

/// One item template, written the way the server writes it. `sockets` is the
/// three (colour, content) pairs; everything else is a plausible constant,
/// because what is under test is where the reader lands, not what it reads.
Packet buildResponse(const std::array<std::pair<uint32_t, uint32_t>, 3>& sockets) {
    Packet p(0x0058);  // SMSG_ITEM_QUERY_SINGLE_RESPONSE
    p.writeUInt32(kEntry);
    p.writeUInt32(4);            // Class: armour
    p.writeUInt32(4);            // SubClass: plate
    p.writeUInt32(0xFFFFFFFF);   // SoundOverrideSubclass
    p.writeString("Test Helm");
    p.writeUInt8(0);             // Name2..Name4 are empty strings on the wire
    p.writeUInt8(0);
    p.writeUInt8(0);
    p.writeUInt32(55555);        // DisplayInfoID
    p.writeUInt32(4);            // Quality: epic

    p.writeUInt32(0);            // Flags
    p.writeUInt32(0);            // Flags2
    // No BuyCount - AzerothCore does not send one.
    p.writeUInt32(120000);       // BuyPrice
    p.writeUInt32(24000);        // SellPrice
    p.writeUInt32(1);            // InventoryType: head
    p.writeUInt32(0xFFFFFFFF);   // AllowableClass
    p.writeUInt32(0xFFFFFFFF);   // AllowableRace
    p.writeUInt32(200);          // ItemLevel
    p.writeUInt32(80);           // RequiredLevel
    p.writeUInt32(0);            // RequiredSkill
    p.writeUInt32(0);            // RequiredSkillRank
    p.writeUInt32(0);            // RequiredSpell
    p.writeUInt32(0);            // RequiredHonorRank
    p.writeUInt32(0);            // RequiredCityRank
    p.writeUInt32(0);            // RequiredReputationFaction
    p.writeUInt32(0);            // RequiredReputationRank
    p.writeUInt32(0);            // MaxCount
    p.writeUInt32(1);            // Stackable
    p.writeUInt32(0);            // ContainerSlots

    p.writeUInt32(2);            // StatsCount
    p.writeUInt32(7);  p.writeUInt32(90);   // Stamina
    p.writeUInt32(4);  p.writeUInt32(60);   // Strength
    p.writeUInt32(0);            // ScalingStatDistribution
    p.writeUInt32(0);            // ScalingStatValue
    for (int i = 0; i < 2; ++i) {           // MAX_ITEM_PROTO_DAMAGES
        p.writeFloat(0.0f);      // DamageMin
        p.writeFloat(0.0f);      // DamageMax
        p.writeUInt32(0);        // DamageType
    }
    p.writeUInt32(2400);         // Armor
    for (int i = 0; i < 6; ++i) p.writeUInt32(0);  // Holy..Arcane resistance
    p.writeUInt32(0);            // Delay
    p.writeUInt32(0);            // AmmoType
    p.writeFloat(0.0f);          // RangedModRange
    for (int s = 0; s < 5; ++s) {           // MAX_ITEM_PROTO_SPELLS
        p.writeUInt32(0);        // SpellId
        p.writeUInt32(0);        // SpellTrigger
        p.writeUInt32(0);        // SpellCharges
        p.writeUInt32(0xFFFFFFFF);  // SpellCooldown
        p.writeUInt32(0);        // SpellCategory
        p.writeUInt32(0xFFFFFFFF);  // SpellCategoryCooldown
    }
    p.writeUInt32(1);            // Bonding: picked up
    p.writeString("");           // Description
    p.writeUInt32(0);            // PageText
    p.writeUInt32(0);            // LanguageID
    p.writeUInt32(0);            // PageMaterial
    p.writeUInt32(0);            // StartQuest
    p.writeUInt32(0);            // LockID
    p.writeUInt32(0);            // Material
    p.writeUInt32(0);            // Sheath
    p.writeUInt32(0);            // RandomProperty
    p.writeUInt32(0);            // RandomSuffix
    p.writeUInt32(0);            // Block
    p.writeUInt32(1234);         // ItemSet
    p.writeUInt32(165);          // MaxDurability
    p.writeUInt32(0);            // Area
    p.writeUInt32(0xFFFFFFFF);   // Map
    p.writeUInt32(0);            // BagFamily
    p.writeUInt32(0);            // TotemCategory

    for (const auto& [color, content] : sockets) {
        p.writeUInt32(color);
        p.writeUInt32(content);
    }
    p.writeUInt32(kSocketBonus);
    p.writeUInt32(kGemProperties);
    p.writeUInt32(0);            // RequiredDisenchantSkill
    p.writeFloat(0.0f);          // ArmorDamageModifier
    p.writeUInt32(0);            // Duration
    p.writeUInt32(0);            // ItemLimitCategory
    p.writeUInt32(0);            // HolidayId

    p.setReadPos(0);
    return p;
}

}  // namespace

TEST_CASE("item query reads socket colour and content per socket", "[item_query]") {
    SECTION("three sockets: every colour arrives, in order") {
        // Red, Yellow, Blue - the colour masks an item template carries.
        auto packet = buildResponse({{{2, 0}, {4, 0}, {8, 0}}});
        ItemQueryResponseData data;
        REQUIRE(ItemQueryResponseParser::parse(packet, data));

        REQUIRE(data.entry == kEntry);
        REQUIRE(data.name == "Test Helm");
        // Read as three colours then three contents, socketColor[1] takes the
        // first socket's content - zero - and socketColor[2] takes the second
        // socket's colour. This is the assertion that fails on that reading.
        REQUIRE(data.socketColor[0] == 2);
        REQUIRE(data.socketColor[1] == 4);
        REQUIRE(data.socketColor[2] == 8);
        REQUIRE(data.socketBonus == kSocketBonus);
    }

    SECTION("two sockets: the third stays empty rather than borrowing") {
        auto packet = buildResponse({{{2, 0}, {8, 0}, {0, 0}}});
        ItemQueryResponseData data;
        REQUIRE(ItemQueryResponseParser::parse(packet, data));

        REQUIRE(data.socketColor[0] == 2);
        REQUIRE(data.socketColor[1] == 8);
        REQUIRE(data.socketColor[2] == 0);
    }

    SECTION("a socket the template ships a gem in keeps both halves") {
        // Almost nothing does this, but the field exists and the reader must
        // not confuse it with a colour.
        auto packet = buildResponse({{{2, 3521}, {0, 0}, {0, 0}}});
        ItemQueryResponseData data;
        REQUIRE(ItemQueryResponseParser::parse(packet, data));

        REQUIRE(data.socketColor[0] == 2);
        REQUIRE(data.socketContent[0] == 3521);
        REQUIRE(data.socketColor[1] == 0);
    }
}
