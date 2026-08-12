// The month arithmetic the calendar grid is laid out from.
//
// FrameXML draws the whole month from four numbers, and every one of them goes
// straight into arithmetic - `mod((firstWeekday - CALENDAR_FIRST_WEEKDAY - 1)
// + 7, 7)` decides which column the first lands in. So a wrong answer here is
// never an error: it is a grid drawn neatly on the wrong days, which looks
// like a working calendar until someone reads it.

#include <catch_amalgamated.hpp>

#include "game/calendar_month.hpp"

using namespace wowee::game;

TEST_CASE("Leap years follow the Gregorian rule, not the divisible-by-four one",
          "[calendar]") {
    CHECK(isLeapYear(2024));
    CHECK_FALSE(isLeapYear(2023));
    // The two the shorthand rule gets wrong. 2100 is the one a client written
    // today can still be running for.
    CHECK_FALSE(isLeapYear(1900));
    CHECK_FALSE(isLeapYear(2100));
    CHECK(isLeapYear(2000));
    CHECK(isLeapYear(2400));

    CHECK(daysInMonth(2, 2024) == 29);
    CHECK(daysInMonth(2, 2023) == 28);
    CHECK(daysInMonth(2, 2100) == 28);
    CHECK(daysInMonth(2, 2000) == 29);
}

TEST_CASE("Every month has the length it should", "[calendar]") {
    static const int kExpected[12] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int m = 1; m <= 12; ++m) {
        INFO("month " << m);
        CHECK(daysInMonth(m, 2023) == kExpected[m - 1]);
    }
    // Out of range answers zero rather than a plausible 31, so a bad offset
    // draws an empty month instead of a full one on the wrong dates.
    CHECK(daysInMonth(0, 2023) == 0);
    CHECK(daysInMonth(13, 2023) == 0);
}

TEST_CASE("Weekdays are counted from Sunday, like CalendarGetDate", "[calendar]") {
    // The base is fixed by the interface: CALENDAR_WEEKDAY_NAMES[1] is Sunday
    // and CalendarGetDate already answers tm_wday + 1. Off by one here shifts
    // every month by a column.
    CHECK(weekdayOf(1, 1, 2000) == 7);   // Saturday
    CHECK(weekdayOf(7, 4, 1776) == 5);   // Thursday
    CHECK(weekdayOf(8, 7, 2026) == 6);   // Friday
    CHECK(weekdayOf(2, 29, 2024) == 5);  // Thursday - a leap day
    CHECK(weekdayOf(3, 1, 2024) == 6);   // Friday, the day after it
    // 1 March in a non-leap year is one weekday earlier than in a leap year,
    // which is the whole of what the leap correction has to get right.
    CHECK(weekdayOf(3, 1, 2023) == 4);   // Wednesday
}

TEST_CASE("A month offset walks months, and December wraps the year",
          "[calendar]") {
    const CalendarMonthInfo now = calendarMonthAt(8, 2026, 0);
    CHECK(now.month == 8);
    CHECK(now.year == 2026);
    CHECK(now.numDays == 31);
    CHECK(now.firstWeekday == weekdayOf(8, 1, 2026));

    // Forward over the year boundary.
    CHECK(calendarMonthAt(12, 2026, 1).month == 1);
    CHECK(calendarMonthAt(12, 2026, 1).year == 2027);
    // And back over it.
    CHECK(calendarMonthAt(1, 2026, -1).month == 12);
    CHECK(calendarMonthAt(1, 2026, -1).year == 2025);

    // The three the interface asks for on every update: -1, 0 and +1. The
    // previous month's day count is what fills the leading cells of the grid.
    CHECK(calendarMonthAt(3, 2024, -1).numDays == 29);  // February, leap
    CHECK(calendarMonthAt(3, 2023, -1).numDays == 28);

    // Whole years, in both directions.
    CHECK(calendarMonthAt(8, 2026, 12).month == 8);
    CHECK(calendarMonthAt(8, 2026, 12).year == 2027);
    CHECK(calendarMonthAt(8, 2026, -12).year == 2025);
}

TEST_CASE("Walking month by month never skips or repeats one", "[calendar]") {
    // Stepping the offset one at a time has to match stepping the calendar one
    // at a time, across every year boundary in the range the calendar offers.
    int month = 1, year = 2020;
    for (int offset = 0; offset < 240; ++offset) {
        const CalendarMonthInfo info = calendarMonthAt(1, 2020, offset);
        INFO("offset " << offset);
        REQUIRE(info.month == month);
        REQUIRE(info.year == year);
        if (++month > 12) { month = 1; ++year; }
    }
}

TEST_CASE("The first weekday follows from the month before it", "[calendar]") {
    // Independent of how firstWeekday is computed: the first of a month falls
    // `numDays` weekdays after the first of the month before it. A method that
    // is wrong for some months would have to be wrong consistently to pass.
    for (int offset = 0; offset < 120; ++offset) {
        const CalendarMonthInfo prev = calendarMonthAt(1, 2020, offset);
        const CalendarMonthInfo next = calendarMonthAt(1, 2020, offset + 1);
        const int expected = ((prev.firstWeekday - 1 + prev.numDays) % 7) + 1;
        INFO("month " << prev.month << "/" << prev.year);
        CHECK(next.firstWeekday == expected);
    }
}
