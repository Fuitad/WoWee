#include "game/calendar_data.hpp"

#include "network/packet.hpp"

namespace wowee {
namespace game {

namespace {

/// The smallest a row of each list can be, used to reject a count before any
/// of it is reserved. A packed guid is one byte at its shortest and a string
/// is one byte when empty, so these are lower bounds rather than sizes.
constexpr size_t kMinInviteBytes  = 8 + 8 + 1 + 1 + 1 + 1;
constexpr size_t kMinEventBytes   = 8 + 1 + 4 + 4 + 4 + 4 + 1;
constexpr size_t kLockoutBytes    = 4 + 4 + 4 + 8;
constexpr size_t kResetBytes      = 4 + 4 + 4;
constexpr size_t kMinHolidayBytes =
    5 * 4 + (CalendarHoliday::kNumDates + CalendarHoliday::kNumDurations +
             CalendarHoliday::kNumFlags) * 4 + 1;

/// A count is only believed if the packet is long enough to hold that many
/// rows at their smallest. Without this a corrupt uint32 reserves billions of
/// rows before the first read fails.
bool countFits(const network::Packet& packet, uint32_t count, size_t minRow) {
    return count == 0 || (minRow != 0 && count <= packet.getRemainingSize() / minRow);
}

/// A string, but only if its terminator is actually in the buffer.
///
/// Packet::readString stops at the end of the data as well as at a NUL, so a
/// name whose last bytes were cut off comes back looking like a complete one
/// and the parse reports success on a packet that is missing its tail. Every
/// truncation inside the final holiday's texture name read that way — 19 of
/// them, all reported clean.
bool readTerminatedString(network::Packet& packet, std::string& out) {
    const std::vector<uint8_t>& bytes = packet.getData();
    size_t at = packet.getReadPos();
    while (at < bytes.size() && bytes[at] != 0) ++at;
    if (at >= bytes.size()) return false;
    out = packet.readString();
    return true;
}

}  // namespace

bool parseCalendarSendCalendar(network::Packet& packet, CalendarData& out) {
    out = CalendarData{};

    if (!packet.hasRemaining(4)) return false;
    const uint32_t numInvites = packet.readUInt32();
    if (!countFits(packet, numInvites, kMinInviteBytes)) return false;
    out.invites.reserve(numInvites);
    for (uint32_t i = 0; i < numInvites; ++i) {
        if (!packet.hasRemaining(8 + 8 + 1 + 1 + 1)) return false;
        CalendarInvite inv;
        inv.eventId = packet.readUInt64();
        inv.inviteId = packet.readUInt64();
        inv.status = packet.readUInt8();
        inv.rank = packet.readUInt8();
        inv.isGuildEvent = packet.readUInt8() != 0;
        // The creator when the event still exists, the sender when it does
        // not — the server writes one field for both cases and there is
        // nothing in the packet that says which it chose.
        if (!packet.hasFullPackedGuid()) return false;
        inv.creatorGuid = packet.readPackedGuid();
        out.invites.push_back(inv);
    }

    if (!packet.hasRemaining(4)) return false;
    const uint32_t numEvents = packet.readUInt32();
    if (!countFits(packet, numEvents, kMinEventBytes)) return false;
    out.events.reserve(numEvents);
    for (uint32_t i = 0; i < numEvents; ++i) {
        if (!packet.hasRemaining(8 + 1)) return false;
        CalendarEvent ev;
        ev.eventId = packet.readUInt64();
        if (!readTerminatedString(packet, ev.title)) return false;
        if (!packet.hasRemaining(4 + 4 + 4 + 4)) return false;
        ev.type = packet.readUInt32();
        ev.eventTimePacked = packet.readUInt32();
        ev.eventTime = unpackWowPackedTime(ev.eventTimePacked);
        ev.flags = packet.readUInt32();
        ev.dungeonId = static_cast<int32_t>(packet.readUInt32());
        if (!packet.hasFullPackedGuid()) return false;
        ev.creatorGuid = packet.readPackedGuid();
        out.events.push_back(ev);
    }

    if (!packet.hasRemaining(4 + 4)) return false;
    out.serverTimeUnix = packet.readUInt32();
    out.zoneTime = unpackWowPackedTime(packet.readUInt32());

    if (!packet.hasRemaining(4)) return false;
    const uint32_t numLockouts = packet.readUInt32();
    if (!countFits(packet, numLockouts, kLockoutBytes)) return false;
    out.lockouts.reserve(numLockouts);
    for (uint32_t i = 0; i < numLockouts; ++i) {
        if (!packet.hasRemaining(kLockoutBytes)) return false;
        CalendarRaidLockout lock;
        lock.mapId = packet.readUInt32();
        lock.difficulty = packet.readUInt32();
        lock.secondsRemaining = packet.readUInt32();
        // A full guid rather than a packed one. The server writes this as an
        // ObjectGuid straight into the buffer, not WriteAsPacked like the two
        // creator fields above it.
        lock.instanceGuid = packet.readUInt64();
        out.lockouts.push_back(lock);
    }

    if (!packet.hasRemaining(4 + 4)) return false;
    out.resetRelationTime = packet.readUInt32();
    const uint32_t numResets = packet.readUInt32();
    if (!countFits(packet, numResets, kResetBytes)) return false;
    out.resets.reserve(numResets);
    for (uint32_t i = 0; i < numResets; ++i) {
        if (!packet.hasRemaining(kResetBytes)) return false;
        CalendarRaidReset reset;
        reset.mapId = static_cast<int32_t>(packet.readUInt32());
        reset.periodSeconds = static_cast<int32_t>(packet.readUInt32());
        reset.offsetSeconds = static_cast<int32_t>(packet.readUInt32());
        out.resets.push_back(reset);
    }

    if (!packet.hasRemaining(4)) return false;
    const uint32_t numHolidays = packet.readUInt32();
    if (!countFits(packet, numHolidays, kMinHolidayBytes)) return false;
    out.holidays.reserve(numHolidays);
    for (uint32_t i = 0; i < numHolidays; ++i) {
        if (!packet.hasRemaining(kMinHolidayBytes - 1)) return false;
        CalendarHoliday holiday;
        holiday.id = packet.readUInt32();
        holiday.region = packet.readUInt32();
        holiday.looping = packet.readUInt32();
        holiday.priority = packet.readUInt32();
        holiday.calendarFilterType = packet.readUInt32();
        for (uint32_t& date : holiday.dates) date = packet.readUInt32();
        for (uint32_t& dur : holiday.durations) dur = packet.readUInt32();
        for (uint32_t& flag : holiday.calendarFlags) flag = packet.readUInt32();
        if (!readTerminatedString(packet, holiday.textureFilename)) return false;
        out.holidays.push_back(std::move(holiday));
    }

    return true;
}

}  // namespace game
}  // namespace wowee
