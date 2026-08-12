// SMSG_CALENDAR_SEND_CALENDAR, read against the server that writes it.
//
// Six lists back to back, no length prefix on any of them, and four of the six
// rows carry a packed guid or a string - so a row read one field wrong does
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

#include <ctime>
#include "core/local_time.hpp"

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
    // signed - as a uint32 it is four billion and every guard on it inverts.
    CHECK(cal.events[1].dungeonId == -1);
    CHECK(cal.events[1].title.empty());

    CHECK(cal.serverTimeUnix == 1300000000u);

    REQUIRE(cal.lockouts.size() == 1);
    CHECK(cal.lockouts[0].mapId == 533);
    CHECK(cal.lockouts[0].difficulty == 1);
    CHECK(cal.lockouts[0].secondsRemaining == 86400);
    // A full guid, not a packed one - the server writes the instance guid
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
    // present between the lists - a parser that skipped the times when the
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

TEST_CASE("A day's rows are holidays first, then events by time", "[calendar]") {
    // The interface reads a day through one index space: CalendarGetNumDayEvents
    // counts the rows and three separate getters index into the same list. So
    // the order is decided once, and a change to it moves every row the player
    // clicks on.
    CalendarData cal;

    CalendarEvent late;
    late.title = "Late";
    late.eventTime.month = 8; late.eventTime.day = 12;
    late.eventTime.yearSince2000 = 26;
    late.eventTime.hour = 20; late.eventTime.minute = 30;
    CalendarEvent early = late;
    early.title = "Early";
    early.eventTime.hour = 9; early.eventTime.minute = 0;
    CalendarEvent otherDay = late;
    otherDay.title = "Tomorrow";
    otherDay.eventTime.day = 13;
    // Pushed out of order on purpose: the wire order is the server's, not the
    // player's, and the grid has to sort it.
    cal.events = {late, early, otherDay};

    CalendarHoliday festival;
    festival.id = 141;
    festival.textureFilename = "Calendar_HallowsEnd";
    // Three days from 11 August 2026, 00:00.
    festival.dates[0] = (26u << 24) | (7u << 20) | (10u << 14);
    festival.durations[0] = 72;
    cal.holidays = {festival};

    const auto rows = calendarEntriesForDay(cal, 8, 12, 2026);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].kind == CalendarEntryKind::Holiday);
    // The 12th is the holiday's second day, so its name is drawn once on the
    // 11th and this row is skipped - FrameXML tests sequenceType ~= "ONGOING".
    CHECK(rows[0].ongoing);
    CHECK(rows[1].kind == CalendarEntryKind::Event);
    CHECK(cal.events[rows[1].index].title == "Early");
    CHECK(cal.events[rows[2].index].title == "Late");

    // The first day of the holiday is not ongoing, and has no events.
    const auto first = calendarEntriesForDay(cal, 8, 11, 2026);
    REQUIRE(first.size() == 1);
    CHECK_FALSE(first[0].ongoing);

    // The day after it ends carries only its own event.
    const auto after = calendarEntriesForDay(cal, 8, 14, 2026);
    CHECK(after.empty());
    const auto tomorrow = calendarEntriesForDay(cal, 8, 13, 2026);
    REQUIRE(tomorrow.size() == 2);   // holiday's third day, plus the event
    CHECK(cal.events[tomorrow[1].index].title == "Tomorrow");

    // A different year is a different day, even on the same date.
    CHECK(calendarEntriesForDay(cal, 8, 12, 2027).empty());
}

TEST_CASE("A holiday with no duration still happens on its day", "[calendar]") {
    // Zero hours is what the server sends for a one-off, and rounding it to
    // zero days would drop the holiday entirely rather than show it once.
    CalendarData cal;
    CalendarHoliday oneOff;
    oneOff.dates[0] = (26u << 24) | (7u << 20) | (10u << 14);
    oneOff.durations[0] = 0;
    cal.holidays = {oneOff};

    CHECK(calendarEntriesForDay(cal, 8, 11, 2026).size() == 1);
    CHECK(calendarEntriesForDay(cal, 8, 12, 2026).empty());
}

namespace {

/// One SMSG_CALENDAR_SEND_EVENT, laid out as CalendarMgr::SendCalendarEvent
/// writes it (CalendarMgr.cpp:627).
Packet oneEvent() {
    Packet p(0x043D);
    p.writeUInt8(0);                          // sendType
    p.writePackedGuid(0x070000000000ABCDull); // creator
    p.writeUInt64(4242);                      // eventId
    p.writeString("Raid Night");
    p.writeString("Bring flasks");
    p.writeUInt8(1);                          // type
    p.writeUInt8(0);                          // repeatable
    p.writeUInt32(100);                       // maxInvites
    p.writeUInt32(static_cast<uint32_t>(-1)); // dungeonId
    p.writeUInt32(0x0400);                    // flags
    p.writeUInt32(0x09E1C000u);               // eventTime
    p.writeUInt32(0x09E1C000u);               // timeZoneTime
    p.writeUInt32(77);                        // guildId

    p.writeUInt32(2);                         // invitees
    p.writePackedGuid(0x11ull);
    p.writeUInt8(80); p.writeUInt8(1); p.writeUInt8(2); p.writeUInt8(1);
    p.writeUInt64(9001);
    p.writeUInt32(0x09E1C000u);
    p.writeString("see you there");
    p.writePackedGuid(0x070000000000BEEFull);
    p.writeUInt8(72); p.writeUInt8(0); p.writeUInt8(0); p.writeUInt8(0);
    p.writeUInt64(9002);
    p.writeUInt32(0);
    p.writeString("");                        // an empty note is still a row
    return p;
}

}  // namespace

TEST_CASE("One event reads back with its invite list", "[calendar]") {
    Packet p = oneEvent();
    CalendarEventDetail ev;
    REQUIRE(parseCalendarSendEvent(p, ev));
    CHECK(p.getRemainingSize() == 0);

    CHECK(ev.creatorGuid == 0x070000000000ABCDull);
    CHECK(ev.eventId == 4242);
    CHECK(ev.title == "Raid Night");
    CHECK(ev.description == "Bring flasks");
    CHECK(ev.maxInvites == 100);
    // -1 is "no dungeon" and only reads that way signed; as a uint32 it is
    // four billion and every guard on it inverts.
    CHECK(ev.dungeonId == -1);
    CHECK(ev.flags == 0x0400u);
    CHECK(ev.guildId == 77);

    REQUIRE(ev.invitees.size() == 2);
    CHECK(ev.invitees[0].guid == 0x11ull);
    CHECK(ev.invitees[0].level == 80);
    CHECK(ev.invitees[0].status == 1);
    CHECK(ev.invitees[0].rank == 2);
    CHECK(ev.invitees[0].isGuildMember);
    CHECK(ev.invitees[0].inviteId == 9001);
    CHECK(ev.invitees[0].note == "see you there");
    // The second row is what proves the first was read to its exact end: a
    // packed guid, a packed time and a string all vary in length, so one field
    // misread slides this row rather than failing.
    CHECK(ev.invitees[1].guid == 0x070000000000BEEFull);
    CHECK(ev.invitees[1].level == 72);
    CHECK(ev.invitees[1].inviteId == 9002);
    CHECK(ev.invitees[1].note.empty());
}

TEST_CASE("An event packet cut short is refused", "[calendar]") {
    const Packet whole = oneEvent();
    const std::vector<uint8_t>& bytes = whole.getData();
    for (size_t len = 0; len < bytes.size(); ++len) {
        Packet cut(0x043D, std::vector<uint8_t>(bytes.begin(), bytes.begin() + len));
        CalendarEventDetail ev;
        INFO("truncated to " << len << " of " << bytes.size());
        CHECK_FALSE(parseCalendarSendEvent(cut, ev));
    }
    Packet full = oneEvent();
    CalendarEventDetail ev;
    CHECK(parseCalendarSendEvent(full, ev));
}

TEST_CASE("A raid lockout lands on the day it expires", "[calendar]") {
    // The packet dates a lockout only by how long it has left, so the day it
    // belongs to is the server's clock plus that remainder. Getting it wrong
    // puts the lockout on the wrong day with no sign of it - the entry is
    // there, it is simply on a day the player is not looking at.
    CalendarData cal;
    // A fixed instant, then a lockout expiring two days later. Both are
    // converted through localtime here for the same reason the code does:
    // the calendar grid is in local days, and a UTC day boundary would put
    // the lockout on the wrong side of midnight for half the world.
    const std::time_t base = 1300000000;
    cal.serverTimeUnix = static_cast<uint32_t>(base);
    CalendarRaidLockout lock;
    lock.mapId = 533;
    lock.difficulty = 1;
    lock.secondsRemaining = 2 * 24 * 60 * 60;
    cal.lockouts.push_back(lock);

    const std::time_t expiry = base + lock.secondsRemaining;
    std::tm tmv = wowee::core::localTime(expiry);

    const auto onDay = calendarEntriesForDay(cal, tmv.tm_mon + 1, tmv.tm_mday,
                                             tmv.tm_year + 1900);
    REQUIRE(onDay.size() == 1);
    CHECK(onDay[0].kind == CalendarEntryKind::RaidLockout);
    CHECK(onDay[0].index == 0);
    CHECK(onDay[0].hour == tmv.tm_hour);
    CHECK(onDay[0].minute == tmv.tm_min);

    // And on no other day.
    const auto dayBefore = calendarEntriesForDay(cal, tmv.tm_mon + 1,
                                                 tmv.tm_mday - 1,
                                                 tmv.tm_year + 1900);
    CHECK(dayBefore.empty());

    // With no server clock there is nothing to count from, so it is placed
    // nowhere rather than on 1 January 1970.
    cal.serverTimeUnix = 0;
    CHECK(calendarEntriesForDay(cal, tmv.tm_mon + 1, tmv.tm_mday,
                                tmv.tm_year + 1900).empty());
}
