// SMSG_GUILD_ROSTER, whose layout is written out three times.
//
// ClassicPacketParsers, TbcPacketParsers and the shared GuildRosterParser each
// carry their own copy - 31, 99 and 100 lines - and the three differ in
// exactly three facts:
//
//   ranks    Classic has a fixed ten, each a bare uint32 of rights. TBC and
//            WotLK send a rank count, and each rank carries rights, a gold
//            withdrawal limit, and six pairs of bank tab rights.
//   gender   WotLK writes a uint8 between classId and areaId. Neither of the
//            earlier two does, and reading one where there is none shifts the
//            zone, the last-online time and both notes.
//   bounds   TBC and WotLK check every read against what is left. Classic
//            checks nothing at all.
//
// The WotLK order is the server's, read off AzerothCore's GuildPackets.cpp:
// status, name, rankId, level, classId, gender, areaId. The bytes below are
// written in that order rather than by asking this client's own writer, so a
// drift away from the wire fails rather than agreeing with itself.
//
// Nothing covered any of the three. A field read one step out of place does
// not raise here: the roster simply shows every member in the wrong zone with
// somebody else's note.
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

/// Which of the three shapes to write. These are the only differences.
struct Layout {
    bool rankCount;   ///< a count and per-rank gold limit and bank tabs
    bool gender;      ///< the uint8 between classId and areaId
};

constexpr Layout kClassic{false, false};
constexpr Layout kTbc{true, false};
constexpr Layout kWotlk{true, true};

/// One member, as the server writes it.
void writeMember(Writer& w, uint64_t guid, const std::string& name, bool online,
                 uint32_t rank, uint8_t level, uint8_t classId, uint8_t gender,
                 uint32_t zone, const std::string& note,
                 const std::string& officerNote, const Layout& layout) {
    w.u64(guid);
    w.u8(online ? 1 : 0);
    w.cstr(name);
    w.u32(rank);
    w.u8(level);
    w.u8(classId);
    if (layout.gender) w.u8(gender);
    w.u32(zone);
    if (!online) w.f32(3.5f);   // days since logout, offline members only
    w.cstr(note);
    w.cstr(officerNote);
}

std::vector<uint8_t> buildRoster(const Layout& layout, uint32_t memberCount = 2) {
    Writer w;
    w.u32(memberCount);
    w.cstr("A message of the day");
    w.cstr("Some guild information");

    if (layout.rankCount) {
        w.u32(2);                       // rank count
        for (uint32_t r = 0; r < 2; ++r) {
            w.u32(0xC0DE + r);          // rights
            w.u32(1000 * (r + 1));      // gold withdrawal limit
            for (int t = 0; t < 6; ++t) {
                w.u32(0x100 + t);       // bank tab rights
                w.u32(0x200 + t);       // bank tab slots per day
            }
        }
    } else {
        for (int r = 0; r < 10; ++r) w.u32(0xC0DE + static_cast<uint32_t>(r));
    }

    writeMember(w, 0x1111, "Aerin", true, 0, 80, 5, 1, 1519, "note one", "officer one", layout);
    if (memberCount > 1) {
        writeMember(w, 0x2222, "Borel", false, 1, 74, 9, 0, 12, "note two", "officer two", layout);
    }
    return w.bytes;
}

wowee::network::Packet makePacket(const std::vector<uint8_t>& bytes) {
    return wowee::network::Packet(0x8020, bytes);
}

}  // namespace

TEST_CASE("the WotLK roster reads every member field", "[guild][wire]") {
    auto bytes = buildRoster(kWotlk);
    auto packet = makePacket(bytes);
    GuildRosterData data;
    REQUIRE(GuildRosterParser::parse(packet, data));

    CHECK(data.motd == "A message of the day");
    CHECK(data.guildInfo == "Some guild information");
    REQUIRE(data.members.size() == 2);

    const auto& a = data.members[0];
    CHECK(a.guid == 0x1111);
    CHECK(a.name == "Aerin");
    CHECK(a.online);
    CHECK(a.rankIndex == 0);
    CHECK(a.level == 80);
    CHECK(a.classId == 5);
    CHECK(a.gender == 1);
    CHECK(a.zoneId == 1519);
    CHECK(a.publicNote == "note one");
    CHECK(a.officerNote == "officer one");

    SECTION("and an offline member carries the extra float, which the online one does not") {
        const auto& b = data.members[1];
        CHECK(b.name == "Borel");
        CHECK_FALSE(b.online);
        CHECK(b.zoneId == 12);
        CHECK(b.lastOnline == Catch::Approx(3.5f));
        CHECK(b.publicNote == "note two");
        CHECK(b.officerNote == "officer two");
    }
}

TEST_CASE("the rank block carries the gold limit and both bank tab arrays",
          "[guild][wire]") {
    // Six pairs per rank. They were read to stay aligned with the packet and
    // then thrown away on TBC, so a TBC guild's bank tab permissions were all
    // zero while the same guild's were right on WotLK.
    auto bytes = buildRoster(kWotlk);
    auto packet = makePacket(bytes);
    GuildRosterData data;
    REQUIRE(GuildRosterParser::parse(packet, data));

    REQUIRE(data.ranks.size() == 2);
    CHECK(data.ranks[0].rights == 0xC0DE);
    CHECK(data.ranks[0].goldLimit == 1000);
    CHECK(data.ranks[1].goldLimit == 2000);
    for (int t = 0; t < 6; ++t) {
        INFO("tab " << t);
        CHECK(data.ranks[0].bankTabRights[t] == static_cast<uint32_t>(0x100 + t));
        CHECK(data.ranks[0].bankTabSlotsPerDay[t] == static_cast<uint32_t>(0x200 + t));
    }
}

TEST_CASE("TBC has no gender byte", "[guild][wire]") {
    // The one field that separates the TBC layout from the WotLK one. Reading
    // a byte that is not there does not fail: it shifts the zone, the
    // last-online float and both notes by one, and every member comes out in
    // the wrong zone with somebody else's note.
    auto bytes = buildRoster(kTbc);
    auto packet = makePacket(bytes);
    GuildRosterData data;
    TbcPacketParsers parsers;
    REQUIRE(parsers.parseGuildRoster(packet, data));

    REQUIRE(data.members.size() == 2);
    CHECK(data.members[0].name == "Aerin");
    CHECK(data.members[0].level == 80);
    CHECK(data.members[0].classId == 5);
    CHECK(data.members[0].zoneId == 1519);
    CHECK(data.members[0].publicNote == "note one");
    CHECK(data.members[1].zoneId == 12);
    CHECK(data.members[1].officerNote == "officer two");

    SECTION("and the WotLK reader on the same bytes gets the zone wrong") {
        // The assertion that the byte matters. Not an error - a wrong answer.
        auto again = makePacket(bytes);
        GuildRosterData wrong;
        REQUIRE(GuildRosterParser::parse(again, wrong));
        REQUIRE_FALSE(wrong.members.empty());
        CHECK(wrong.members[0].zoneId != 1519);
    }

    SECTION("TBC keeps the bank tab rights too") {
        REQUIRE(data.ranks.size() == 2);
        CHECK(data.ranks[0].goldLimit == 1000);
        CHECK(data.ranks[0].bankTabRights[0] == 0x100);
        CHECK(data.ranks[0].bankTabSlotsPerDay[5] == 0x205);
    }
}

TEST_CASE("Classic has ten fixed ranks and no gender", "[guild][wire]") {
    // No rank count on the wire at all: ten bare uint32 of rights, and the
    // member block starts straight after the tenth.
    auto bytes = buildRoster(kClassic);
    auto packet = makePacket(bytes);
    GuildRosterData data;
    ClassicPacketParsers parsers;
    REQUIRE(parsers.parseGuildRoster(packet, data));

    REQUIRE(data.ranks.size() == 10);
    CHECK(data.ranks[0].rights == 0xC0DE);
    CHECK(data.ranks[9].rights == 0xC0DE + 9);
    CHECK(data.ranks[0].goldLimit == 0);

    REQUIRE(data.members.size() == 2);
    CHECK(data.members[0].name == "Aerin");
    CHECK(data.members[0].zoneId == 1519);
    CHECK(data.members[0].gender == 0);
    CHECK(data.members[1].publicNote == "note two");
    CHECK(data.members[1].officerNote == "officer two");
}

TEST_CASE("a truncated roster stops rather than reading past the end",
          "[guild][wire]") {
    // Every length the packet can be cut to. None of them may read past the
    // buffer or report success with garbage in it; stopping early with fewer
    // members than the header claimed is the correct answer.
    const auto full = buildRoster(kWotlk);
    for (size_t cut = 4; cut < full.size(); ++cut) {
        std::vector<uint8_t> shortened(full.begin(), full.begin() + static_cast<long>(cut));
        auto packet = makePacket(shortened);
        GuildRosterData data;
        INFO("cut to " << cut << " of " << full.size());
        GuildRosterParser::parse(packet, data);
        CHECK(data.members.size() <= 2);
    }
}
