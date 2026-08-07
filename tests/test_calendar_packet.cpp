// SMSG_CALENDAR_SEND_CALENDAR, read against the server that writes it.
//
// Six lists back to back, no length prefix on any of them, and four of the six
// rows carry a packed guid or a string — so a row read one field wrong does
// not fail. It slides everything after it, and the next count is read out of
// the middle of a string. Nothing in the packet says so, and nothing in the
// client would either: the calendar would simply be full of plausible nonsense.
//
// The bytes below are laid out exactly as WorldSession::HandleCalendarGetCalendar
// writes them (CalendarHandler.cpp:53), so a change on either side shows up
// here rather than on screen.

#include <catch_amalgamated.hpp>

#include "core/application.hpp"
#include "game/calendar_data.hpp"
#include "network/packet.hpp"

namespace wowee::core {
Application* Application::instance = nullptr;
}

using namespace wowee::game;
using wowee::network::Packet;

namespace {

/// A holiday row: five fields, then 26 dates, 10 durations, 10 flags, then the
/// texture name. The three array lengths are the server's own constants and
/// getting any of them wrong misreads every holiday after the first.
void writeHoliday(Packet& p, uint32_t id, const std::string& texture,
                  uint32_t firstDate) {
    p.writeUInt32(id);
    p.writeUInt32(2);    // region
    p.writeUInt32(1);    // looping
    p.writeUInt32(7);    // priority
    p.writeUInt32(3);    // calendarFilterType
    for (int i = 0; i < CalendarHoliday::kNumDates; ++i) {
        p.writeUInt32(i == 0 ? firstDate : 0);
    }
    for (int i = 0; i < CalendarHoliday::kNumDurations; ++i) {
        p.writeUInt32(i == 0 ? 24u : 0u);
    }
    for (int i = 0; i < CalendarHoliday::kNumFlags; ++i) {
        p.writeUInt32(i == 0 ? 0x10u : 0u);
    }
    p.writeString(texture);
}

/// One packet carrying something in every list, so no list can be skipped
/// without the ones after it landing wrong.
Packet fullCalendar() {
    Packet p(0x436);

    p.writeUInt32(2);                 // invites
    p.writeUInt64(1001); p.writeUInt64(5001);
    p.writeUInt8(1); p.writeUInt8(2); p.writeUInt8(1);
    p.writePackedGuid(0x070000000000ABCDull);
    p.writeUInt64(1002); p.writeUInt64(5002);
    p.writeUInt8(0); p.writeUInt8(0); p.writeUInt8(0);
    p.writePackedGuid(0x42ull);

    p.writeUInt32(2);                 // events
    p.writeUInt64(1001);
    p.writeString("Naxxramas");
    p.writeUInt32(1);
    p.writeUInt32(0x09E1C000u);       // packed time
    p.writeUInt32(0x0100u);
    p.writeUInt32(533);
    p.writePackedGuid(0x070000000000ABCDull);
    p.writeUInt64(1002);
    p.writeString("");                // an untitled event is still a row
    p.writeUInt32(4);
    p.writeUInt32(0);
    p.writeUInt32(0);
    p.writeUInt32(-1);
    p.writePackedGuid(0x42ull);

    p.writeUInt32(1300000000u);       // server time
    p.writeUInt32(0x09E1C000u);       // zone time

    p.writeUInt32(1);                 // lockouts
    p.writeUInt32(533); p.writeUInt32(1); p.writeUInt32(86400);
    p.writeUInt64(0x0010000000000007ull);

    p.writeUInt32(1135814400u);       // reset relation time
    p.writeUInt32(2);                 // resets
    p.writeUInt32(533); p.writeUInt32(604800); p.writeUInt32(0);
    p.writeUInt32(615); p.writeUInt32(604800); p.writeUInt32(3600);

    p.writeUInt32(2);                 // holidays
    writeHoliday(p, 141, "Calendar_HallowsEnd", 0x09E1C000u);
    writeHoliday(p, 181, "Calendar_WinterVeil", 0x0A21C000u);

    return p;
}

}  // namespace

TEST_CASE("A whole calendar reads back field for field", "[calendar]") {
    Packet p = fullCalendar();
    CalendarData cal;
    REQUIRE(parseCalendarSendCalendar(p, cal));

    // Nothing left over. A parser that stopped a field early would still fill
    // everything above and only show itself here.
    CHECK(p.getRemainingSize() == 0);

    REQUIRE(cal.invites.size() == 2);
    CHECK(cal.invites[0].eventId == 1001);
    CHECK(cal.invites[0].inviteId == 5001);
    CHECK(cal.invites[0].status == 1);
    CHECK(cal.invites[0].rank == 2);
    CHECK(cal.invites[0].isGuildEvent);
    CHECK(cal.invites[0].creatorGuid == 0x070000000000ABCDull);
    CHECK_FALSE(cal.invites[1].isGuildEvent);
    CHECK(cal.invites[1].creatorGuid == 0x42ull);

    REQUIRE(cal.events.size() == 2);
    CHECK(cal.events[0].title == "Naxxramas");
    CHECK(cal.events[0].type == 1);
    CHECK(cal.events[0].flags == 0x0100u);
    CHECK(cal.events[0].dungeonId == 533);
    CHECK(cal.events[0].creatorGuid == 0x070000000000ABCDull);
    // Kept raw as well as unpacked: the interface sorts events on the packed
    // value and re-sending it is how an edit keeps the same time.
    CHECK(cal.events[0].eventTimePacked == 0x09E1C000u);
    // A dungeon id of -1 is "no dungeon", which only reads correctly as
    // signed — as a uint32 it is four billion and every guard on it inverts.
    CHECK(cal.events[1].dungeonId == -1);
    CHECK(cal.events[1].title.empty());

    CHECK(cal.serverTimeUnix == 1300000000u);

    REQUIRE(cal.lockouts.size() == 1);
    CHECK(cal.lockouts[0].mapId == 533);
    CHECK(cal.lockouts[0].difficulty == 1);
    CHECK(cal.lockouts[0].secondsRemaining == 86400);
    // A full guid, not a packed one — the server writes the instance guid
    // straight into the buffer while the two creator fields above are packed.
    CHECK(cal.lockouts[0].instanceGuid == 0x0010000000000007ull);

    CHECK(cal.resetRelationTime == 1135814400u);
    REQUIRE(cal.resets.size() == 2);
    CHECK(cal.resets[1].mapId == 615);
    CHECK(cal.resets[1].periodSeconds == 604800);
    CHECK(cal.resets[1].offsetSeconds == 3600);

    REQUIRE(cal.holidays.size() == 2);
    CHECK(cal.holidays[0].id == 141);
    CHECK(cal.holidays[0].textureFilename == "Calendar_HallowsEnd");
    CHECK(cal.holidays[0].dates[0] == 0x09E1C000u);
    CHECK(cal.holidays[0].durations[0] == 24);
    CHECK(cal.holidays[0].calendarFlags[0] == 0x10u);
    // The second holiday is the one that proves the array lengths. Read 25
    // dates instead of 26 and this row is four bytes out, so its id is the
    // tail of the previous row's texture name rather than 181.
    CHECK(cal.holidays[1].id == 181);
    CHECK(cal.holidays[1].textureFilename == "Calendar_WinterVeil");
    CHECK(cal.holidays[1].dates[0] == 0x0A21C000u);
}

TEST_CASE("The event time unpacks as a date", "[calendar]") {
    Packet p = fullCalendar();
    CalendarData cal;
    REQUIRE(parseCalendarSendCalendar(p, cal));

    // Same helper the achievement and guild parsers use, so the calendar
    // cannot drift into a fifth reading of the same field.
    const WowDate expected = unpackWowPackedTime(0x09E1C000u);
    CHECK(cal.events[0].eventTime.fullYear() == expected.fullYear());
    CHECK(cal.events[0].eventTime.month == expected.month);
    CHECK(cal.events[0].eventTime.day == expected.day);
    CHECK(cal.zoneTime.fullYear() == expected.fullYear());
}

TEST_CASE("An empty calendar is not an error", "[calendar]") {
    // What a new character gets. Every count zero, and the two times still
    // present between the lists — a parser that skipped the times when the
    // lists were empty would read the trailing counts as clock values.
    Packet p(0x436);
    p.writeUInt32(0);
    p.writeUInt32(0);
    p.writeUInt32(1300000000u);
    p.writeUInt32(0x09E1C000u);
    p.writeUInt32(0);
    p.writeUInt32(1135814400u);
    p.writeUInt32(0);
    p.writeUInt32(0);

    CalendarData cal;
    REQUIRE(parseCalendarSendCalendar(p, cal));
    CHECK(cal.invites.empty());
    CHECK(cal.events.empty());
    CHECK(cal.holidays.empty());
    CHECK(cal.serverTimeUnix == 1300000000u);
    CHECK(cal.resetRelationTime == 1135814400u);
    CHECK(p.getRemainingSize() == 0);
}

TEST_CASE("A packet cut short is refused rather than half-believed",
          "[calendar]") {
    // Truncation at every length, because the interesting ones are not at the
    // edges: a cut inside a holiday's 46 array words leaves five plausible
    // fields already read, and a cut inside a title leaves the string reader
    // running to the end of the buffer.
    const Packet whole = fullCalendar();
    const std::vector<uint8_t>& bytes = whole.getData();
    for (size_t len = 0; len < bytes.size(); ++len) {
        Packet cut(0x436, std::vector<uint8_t>(bytes.begin(), bytes.begin() + len));
        CalendarData cal;
        INFO("truncated to " << len << " of " << bytes.size());
        CHECK_FALSE(parseCalendarSendCalendar(cut, cal));
    }
    // And the whole thing still parses, so the loop above is not passing
    // because the parser refuses everything.
    Packet full = fullCalendar();
    CalendarData cal;
    CHECK(parseCalendarSendCalendar(full, cal));
}

TEST_CASE("A count larger than the packet reserves nothing", "[calendar]") {
    // The counts are server-controlled uint32s. Believed literally, the first
    // of these asks for four billion invites before a single read fails.
    for (uint32_t count : {0xFFFFFFFFu, 0x10000000u, 1000u}) {
        Packet p(0x436);
        p.writeUInt32(count);
        CalendarData cal;
        INFO("claimed invites: " << count);
        CHECK_FALSE(parseCalendarSendCalendar(p, cal));
    }
}
