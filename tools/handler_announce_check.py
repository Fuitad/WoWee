#!/usr/bin/env python3
"""Packet handlers that tell the player and not the interface.

    tools/handler_announce_check.py

This client keeps its own model of the game and FrameXML keeps another, and
only an event joins them. A handler that updates the first and announces
nothing produces a bug with one distinctive signature: **right after a relog
and wrong until then**, because a relog rebuilds the interface's copy from
scratch. It is a hard bug to see, because nothing is broken at the moment it
happens.

WHAT IT LOOKS FOR

Handlers that call addSystemChatMessage or raiseUiError - the client deciding
this is worth telling the *player* - and fire no addon event at all. If it is
worth a line of chat it is usually worth a redraw, and a line of chat is not
one.

That is a heuristic, not a rule, and it is chosen because it has a precedent
rather than because it is tight: SMSG_QUESTUPDATE_FAILED and
SMSG_QUESTUPDATE_FAILEDTIMER were exactly this shape, and both left a failed
quest looking fine in the log until the next login.

WHAT IT CANNOT SEE

Handlers that change state silently and should announce - the larger half of
the same problem, and undetectable from shape alone, because most handlers
that touch state correctly say nothing. It also cannot tell whether the event
a handler *does* fire is the right one.

WHAT IS LEFT, AND WHY

The first run found two: SMSG_EQUIPMENT_SET_SAVED pushed a newly saved set
into the list and fired nothing, so the equipment manager did not show it
until the next login, and SMSG_SUMMON_CANCEL cleared the pending summon
without CANCEL_SUMMON, leaving a dialog offering a summon the server had
already withdrawn.

Re-read on 2026-08-05, and the count went 20 -> 13. Three of those were this
sweep's own fault and two were real.

THE FALSE ROWS, WHICH MATTERED MORE THAN THE COUNT

addonEventCallback_ - the same call spelled from inside GameHandler, where
there is no Ref() accessor - was not in TELLS_INTERFACE, and
game_handler_packets is where most handlers live. So a whole file's worth of
handlers that announce perfectly well were reported as silent.
SMSG_BATTLEFIELD_MGR_ENTRY_INVITE fires its event two lines below the chat
message this sweep caught it by.

A false row here is worse than a missing one. The obvious response to "this
handler tells nobody" is to add a fire - and raiseUiError already reaches
UI_ERROR_MESSAGE through addUIError, which is how this file once grew a second
answer to a question already answered two headers away.

THE TWO REAL ONES

  * SMSG_CALENDAR_SEND_NUM_PENDING stored the count and fired nothing, and
    CalendarGetNumPendingInvites answered a constant zero. The calendar addon
    is refused by name, but the indicator is not its: gametime.lua puts it on
    the minimap's date button and asks only on CALENDAR_UPDATE_PENDING_INVITES
    and PLAYER_ENTERING_WORLD. Correct at login, stale for the session - both
    halves fixed.
  * handleLfgJoinResult set lfgState_ and said nothing, so joining a queue left
    the dungeon finder reading the old state until some other LFG packet
    happened along. It fires LFG_UPDATE now, which is what handleLfgUpdate
    fires two handlers down for the same reason.

WHAT IS LEFT

Thirteen, each read. What they write is bookkeeping this client keeps for
itself - a fishing attempt that failed, a purchase refused, a home location
that GetBindLocation polls rather than being told about, a logout after which
there is no interface left to tell. The message is the whole content and a
chat line is the right place for it. The ceiling is a number to look at when
it moves, not a queue.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import without_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "src/game"

#: Telling the player. raiseUiError does reach the interface, as
#: UI_ERROR_MESSAGE - but that is another line to read, not a redraw, so it
#: counts here rather than below.
TELLS_PLAYER = ("addSystemChatMessage", "raiseUiError")
#: Writing to this client's own model: a member, a handler-owned reference, or
#: a field of something pulled out of one. Deliberately loose - the question is
#: whether anything was changed at all, not what.
WRITES_STATE = re.compile(
    r"\b\w+Ref\(\)(?:\[[^\]]*\])?(?:\.\w+)?\s*=[^=]"
    r"|\b\w+_\s*(?:\[[^\]]*\])?\s*=[^=]"
    r"|\b(?:quest|entry|item|data|info|state)\.\w+\s*=[^=]")

#: Telling the interface - either directly, or through one of the announce*
#: helpers that exist so the several events one change needs are fired from a
#: single place. Matching the whole family rather than naming them keeps the
#: next one from arriving as a false positive; announceLootRollClosed did.
#: addonEventCallbackRef() without the call after it: a handler firing several
#: events takes the callback into a local first - `auto fire = ...; fire(...)`
#: - and requiring the immediate call reported the whole dungeon-finder
#: proposal path, which fires four.
#: addonEventCallback_ is the same thing spelled from inside GameHandler,
#: where there is no Ref() accessor to go through - and game_handler_packets
#: is where most handlers live, so leaving it out reported a whole file's worth
#: of handlers that announce perfectly well. SMSG_BATTLEFIELD_MGR_ENTRY_INVITE
#: was one: it fires its event two lines below the chat message this sweep
#: caught it by. A false row here is worse than a missing one, because the
#: obvious response to it is to add a second fire for an event already sent.
TELLS_INTERFACE = ("fireAddonEvent", "addonEventCallbackRef()",
                   "addonEventCallback_", "pendingEvents_.emit")
ANNOUNCES = re.compile(r"\bannounce[A-Z]\w*\(")

#: Rows checked one at a time and found to be right as they stand, with what
#: settled each.
#:
#: Pinned as a *set* rather than as a count, which is what this list is for. A
#: ceiling of thirteen says only how many there are: fix one, introduce
#: another, and the number never moves. handleGuildCommandResult was exactly
#: that - it emptied the guild name, the ranks and the whole roster on leaving
#: a guild and told nobody, and sat inside an accepted count for as long as the
#: count was all that was pinned.
#:
#: The common thread in what remains: the state written is this client's own
#: bookkeeping, which no binding reads, or it is read on demand rather than
#: drawn from an event.
EXPECTED = {
    # Auto-attack bookkeeping - the out-of-range flag and the warning cooldown.
    # No binding reads either. The message itself reaches the interface as
    # UI_ERROR_MESSAGE through raiseUiError, which is where the real client
    # puts these.
    "SMSG_ATTACKSWING_BADFACING": "auto-attack state, error already raised",
    "SMSG_ATTACKSWING_CANT_ATTACK": "auto-attack state, error already raised",
    "SMSG_ATTACKSWING_NOTINRANGE": "auto-attack state, error already raised",
    "SMSG_ATTACKSWING_NOTSTANDING": "auto-attack state, error already raised",
    # The home bind. GetBindLocation reads it when the hearthstone tooltip or
    # the confirm-binder popup draws; 3.3.5 has no bind-changed event.
    "SMSG_BINDPOINTUPDATE": "bind location, read on demand",
    "SMSG_PLAYERBOUND": "bind location, read on demand",
    # Pending-purchase bookkeeping, and it calls addUIError itself.
    "SMSG_BUY_FAILED": "purchase bookkeeping, error already raised",
    # Clears this client's bobber guid; the message is the whole of it and now
    # goes through raiseUiError to the error frame.
    "SMSG_FISH_ESCAPED": "fishing state, error already raised",
    "SMSG_FISH_NOT_HOOKED": "fishing state, error already raised",
    # The bobber's bite animation, played through this client's own callback.
    # Nothing in FrameXML draws a game object's animation.
    "SMSG_GAMEOBJECT_CUSTOM_ANIM": "bobber animation, no interface counterpart",
    # Clears the pending turn-in request, which is this client's own.
    "SMSG_QUESTGIVER_QUEST_INVALID": "turn-in bookkeeping, error already raised",
    # The guild name and info text, which GetGuildInfo and GetGuildInfoText
    # read when asked. The panel redraws on GUILD_ROSTER_UPDATE, fired by the
    # roster handler.
    "SocialHandler::handleGuildInfo": "guild name, read on demand",
    # The seconds until the daily quest reset, which GetQuestResetTime answers
    # when the quest log's tooltip asks for it. Nothing draws it from an event
    # because 3.3.5 has none - the same shape as the bind location below. The
    # chat line is /time's answer and appears only when the player typed it.
    "SocialHandler::handleQueryTimeResponse": "daily reset offset, read on demand",
    # The session is ending and the client is leaving the world.
    "SocialHandler::handleLogoutComplete": "logging out, nothing left to draw",
}

#: `table[Opcode::X] = [this](...) { … };` and the dispatchTable_ spelling.
HANDLER = re.compile(
    r"(?:table|dispatchTable_)\[Opcode::(\w+)\]\s*=\s*\[[^\]]*\]\s*\([^)]*\)\s*\{")

#: The other half. A dispatch entry is often one line - `= [this](Packet& p) {
#: handleFoo(p); }` - with the work in a named method somewhere else in the
#: file, and reading only the lambda sees a body that calls one function.
#: SMSG_QUEST_CONFIRM_ACCEPT was exactly that: it set the pending share, told
#: the player in chat and fired nothing, and this check walked straight past it
#: while a walk of ungated draw surfaces found it from the other end.
NAMED = re.compile(
    r"^[A-Za-z_][\w:<>, ]*\s(\w+::handle\w+)\([^)]*network::Packet\s*&[^)]*\)\s*\{",
    re.M)


def body_after(text, start):
    """The braced block beginning at the { that `start` points just past."""
    depth, i = 1, start
    while i < len(text) and depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    return text[start:i]


def main():
    rows = []
    total = 0
    for path in sorted(GAME.glob("*.cpp")):
        text = without_comments(path.read_text(errors="ignore"))
        found = [(m.group(1), m.end()) for m in HANDLER.finditer(text)]
        found += [(m.group(1), m.end()) for m in NAMED.finditer(text)]
        for name, at in found:
            body = body_after(text, at)
            total += 1
            if not any(t in body for t in TELLS_PLAYER):
                continue
            if any(t in body for t in TELLS_INTERFACE) or ANNOUNCES.search(body):
                continue
            # ...and changed something while it was at it. Without this the
            # report is a hundred and nine notifications - an attack that
            # missed, an auction that sold - which carry no state and have
            # nothing for an interface to redraw. What matters is a handler
            # that wrote to the model and announced nothing, which is what
            # SMSG_QUESTUPDATE_COMPLETE did: it set quest.complete and left
            # the tracker showing the quest as unfinished until the next login.
            if not WRITES_STATE.search(body):
                continue
            if name in EXPECTED:
                continue
            line = text.count("\n", 0, at) + 1
            rows.append((name, f"{path.name}:{line}"))

    print(f"{total} packet handler(s) read, inline and named\n")
    print(f"{len(rows)} that tell the player and not the interface:\n")
    for opcode, where in sorted(rows):
        print(f"  {opcode:44} {where}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
