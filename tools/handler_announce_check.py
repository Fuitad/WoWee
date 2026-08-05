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

Handlers that call addSystemChatMessage or raiseUiError — the client deciding
this is worth telling the *player* — and fire no addon event at all. If it is
worth a line of chat it is usually worth a redraw, and a line of chat is not
one.

That is a heuristic, not a rule, and it is chosen because it has a precedent
rather than because it is tight: SMSG_QUESTUPDATE_FAILED and
SMSG_QUESTUPDATE_FAILEDTIMER were exactly this shape, and both left a failed
quest looking fine in the log until the next login.

WHAT IT CANNOT SEE

Handlers that change state silently and should announce — the larger half of
the same problem, and undetectable from shape alone, because most handlers
that touch state correctly say nothing. It also cannot tell whether the event
a handler *does* fire is the right one.

WHAT IS LEFT, AND WHY

The first run found two: SMSG_EQUIPMENT_SET_SAVED pushed a newly saved set
into the list and fired nothing, so the equipment manager did not show it
until the next login, and SMSG_SUMMON_CANCEL cleared the pending summon
without CANCEL_SUMMON, leaving a dialog offering a summon the server had
already withdrawn.

The rest were read once each. What they write is bookkeeping this client keeps
for itself — a fishing attempt that failed, a battleground invite it will act
on when answered, a home location nothing draws — so there is no second model
to bring level and the message is the whole content. The ceiling is a number
to look at when it moves, not a queue.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import without_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "src/game"

#: Telling the player. raiseUiError does reach the interface, as
#: UI_ERROR_MESSAGE — but that is another line to read, not a redraw, so it
#: counts here rather than below.
TELLS_PLAYER = ("addSystemChatMessage", "raiseUiError")
#: Writing to this client's own model: a member, a handler-owned reference, or
#: a field of something pulled out of one. Deliberately loose — the question is
#: whether anything was changed at all, not what.
WRITES_STATE = re.compile(
    r"\b\w+Ref\(\)(?:\[[^\]]*\])?(?:\.\w+)?\s*=[^=]"
    r"|\b\w+_\s*(?:\[[^\]]*\])?\s*=[^=]"
    r"|\b(?:quest|entry|item|data|info|state)\.\w+\s*=[^=]")

#: Telling the interface — either directly, or through one of the announce*
#: helpers that exist so the several events one change needs are fired from a
#: single place. Matching the whole family rather than naming them keeps the
#: next one from arriving as a false positive; announceLootRollClosed did.
#: addonEventCallbackRef() without the call after it: a handler firing several
#: events takes the callback into a local first — `auto fire = ...; fire(...)`
#: — and requiring the immediate call reported the whole dungeon-finder
#: proposal path, which fires four.
TELLS_INTERFACE = ("fireAddonEvent", "addonEventCallbackRef()",
                   "pendingEvents_.emit")
ANNOUNCES = re.compile(r"\bannounce[A-Z]\w*\(")

#: `table[Opcode::X] = [this](...) { … };` and the dispatchTable_ spelling.
HANDLER = re.compile(
    r"(?:table|dispatchTable_)\[Opcode::(\w+)\]\s*=\s*\[[^\]]*\]\s*\([^)]*\)\s*\{")

#: The other half. A dispatch entry is often one line — `= [this](Packet& p) {
#: handleFoo(p); }` — with the work in a named method somewhere else in the
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
            # report is a hundred and nine notifications — an attack that
            # missed, an auction that sold — which carry no state and have
            # nothing for an interface to redraw. What matters is a handler
            # that wrote to the model and announced nothing, which is what
            # SMSG_QUESTUPDATE_COMPLETE did: it set quest.complete and left
            # the tracker showing the quest as unfinished until the next login.
            if not WRITES_STATE.search(body):
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
