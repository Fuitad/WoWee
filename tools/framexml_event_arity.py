#!/usr/bin/env python3
"""Events fired with fewer arguments than the handler unpacks.

The unfired-event column says whether an event is sent at all. This asks the
next question: whether what is sent carries what the handler reads.

A handler that unpacks four values from an event fired with two gets nil for
the last two, and nil is not an error - it is a blank name, a missing amount, a
branch that quietly takes the other path. Nothing raises and nothing is
reported, which puts this in the same family as the no-op allowlist.

The shape matched is FrameXML's own:

    if ( event == "SOME_EVENT" ) then
        local a, b, c = ...;

so the destructure is inside the branch that names the event, and the count is
unambiguous. Handlers that unpack once at the top for many events are NOT
matched - the arity there belongs to no single event, and guessing which would
manufacture findings.

Both sides can be wrong. Read the handler before adding an argument: FrameXML
sometimes unpacks more than the server ever sends, and the extra is a value the
real client had and this one has no source for.
"""
import re
import sys
from pathlib import Path

import sys as _sys
_sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import without_comments, loaded_files

ROOT = Path(__file__).resolve().parent.parent
XML = ROOT / "Data/interface"
SRC = ROOT / "src"

# ---- What the client fires, and with how many arguments ----
#
# fireAddonEvent("NAME", {a, b}) and the two spellings that reach the same
# place: the raw callback and the queue used from packet handlers.
FIRE = re.compile(
    r"""(?:fireAddonEvent|addonEventCallbackRef\(\)|pendingEvents_\.emit)\s*\(\s*"""
    r"""(?:"(\w+)"|(\w+))\s*,\s*\{(.*?)\}""",
    re.S)

fired = {}
sites = {}
for path in SRC.rglob("*.cpp"):
    text = path.read_text(errors="ignore")
    for m in FIRE.finditer(text):
        name = m.group(1)
        if not name:
            continue          # event name held in a variable - arity unknowable here
        body = m.group(3).strip()
        if not body:
            count = 0
        else:
            # Commas at brace depth zero. Argument expressions contain calls
            # with their own commas, and counting those inflates every count.
            depth, count = 0, 1
            for ch in body:
                if ch in "([{":
                    depth += 1
                elif ch in ")]}":
                    depth -= 1
                elif ch == "," and depth == 0:
                    count += 1
        prev = fired.get(name)
        # A name fired from several places takes the largest, since the handler
        # only needs one path to carry what it reads.
        fired[name] = count if prev is None else max(prev, count)
        sites.setdefault(name, []).append(
            (count, f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, m.start()) + 1}"))

# ---- What the handlers unpack, inside the branch that names the event ----
# One event per branch. A condition naming several - battlefieldframe.lua has
# `UPDATE_BATTLEFIELD_STATUS or ZONE_CHANGED_NEW_AREA or ZONE_CHANGED` sharing
# one body - unpacks for whichever of them carries arguments, and attributing
# that count to the others invents a shortfall. Same reasoning as skipping the
# handlers that unpack once at the top.
BRANCH = re.compile(
    r'\(\s*event\s*==\s*"(\w+)"\s*\)\s*then(.*?)(?=\belseif\b|\bend\b)', re.S)
UNPACK = re.compile(r"local\s+([\w\s,]+?)\s*=\s*\.\.\.")
# The other spelling, and the commoner one: a body that names arg1 and arg2
# directly instead of unpacking them. Reading only `= ...` saw sixty-nine
# branches and missed several hundred, ActionButton_OnEvent among them -
# ACTIONBAR_SLOT_CHANGED was fired with no argument at all from two of its
# three sites, and the button compares `arg1 == 0 or arg1 == tonumber(self.action)`,
# so nil matched neither and not one button on the bar redrew.
ARGN = re.compile(r"\barg([1-9])\b")

wanted = {}
# Only files the loader opens. focusframe.lua is the one this was reading and
# should not have been: it is in no manifest and nothing includes it, because
# FocusFrame is declared in targetframe.xml inheriting TargetFrameTemplate and
# handled by targetframe.lua. A handler in a file that never runs cannot read
# an argument that is never fired.
for path in sorted(loaded_files(XML)):
    text = without_comments(path.read_text(errors="ignore"))
    for m in BRANCH.finditer(text):
        name, body = m.group(1), m.group(2)
        u = UNPACK.search(body)
        names = []
        if u:
            names = [n.strip() for n in u.group(1).split(",") if n.strip()]
        # However the body gets at them, the count it needs is the highest
        # position it reads - unless the function unpacked at the top, where
        # the names belong to whichever of its several events carries them and
        # attributing them to this one invents a shortfall. That is the same
        # exclusion the docstring makes for `local a, b = ...` at the top, and
        # reading argN put it back: focusframe opens with `local arg1 = ...`
        # ahead of a chain of six events, only one of which has an argument.
        highest = max((int(d) for d in ARGN.findall(body)), default=0)
        need = max(len(names), highest)
        if need == 0:
            continue
        shown = ", ".join(names) if len(names) >= need else f"arg1..arg{need}"
        prev = wanted.get(name)
        entry = (need, path.name, shown)
        if prev is None or need > prev[0]:
            wanted[name] = entry

#: Events whose handler reads an argument that is *meant* to be absent, so
#: firing fewer is the answer rather than the fault. Listed with the reason,
#: because each is a judgement about that event rather than a shape.
EXPECTED = {
    # pvpframe reads arg1 as "the roster you are holding is stale, ask for it
    # again", and redraws from what arrived only when arg1 is missing. This is
    # fired when a roster arrives, so passing anything would answer a roster by
    # requesting another one without end - and a zero would do it as surely as
    # a one, zero being true in Lua.
    "ARENA_TEAM_ROSTER_UPDATE",
    # readycheck reads arg1 as "a new ready check started before this one
    # finished", and there is nothing on the wire to answer it with:
    # AzerothCore's HandleRaidReadyCheckFinishedOpcode broadcasts
    # MSG_RAID_READY_CHECK_FINISHED with an empty body. nil reads as false,
    # which is what "not preempted" wants.
    #
    # That branch prints "you were away", and it is not gated on the argument
    # but on self.initiator, which ReadyCheckFrame_OnHide clears. So the line
    # appears only when the frame is still up, which is to say only when the
    # player never answered. Passing a false here would change nothing; passing
    # a true would suppress a message that is meant to appear.
    "READY_CHECK_FINISHED",
}

rows = []
for name, (need, where, names) in sorted(wanted.items()):
    have = fired.get(name)
    if have is None:
        continue              # not fired at all - the other sweep's column
    if name in EXPECTED:
        continue
    if need > have:
        rows.append((need - have, name, have, need, where, names))

print(f"{len(fired)} events fired with a known argument count, "
      f"{len(wanted)} unpacked inside their own branch\n")
print(f"{len(rows)} fired with fewer arguments than a handler reads:\n")
for short, name, have, need, where, names in sorted(rows, reverse=True):
    print(f"  {name:34} fires {have}, reads {need}   [{where}]")
    print(f"      reads {names}")
if not rows:
    print("  (none)")

# Taking the largest hides the case where one path carries the argument and
# another does not. That is not a shortfall in the count above and it is still a
# fault, because the argument usually means something different per path rather
# than being optional: GUILD_ROSTER_UPDATE's says "you may ask for a roster",
# false where the roster just arrived and true where a member changed and no new
# roster is coming. Fixing one site and not the other leaves the count clean and
# half the behaviour missing, which is what this section is for.
#: Events fired with different counts on purpose, each checked against the
#: branch that reads them. A set rather than a count: a ceiling of four says
#: only how many, so fixing one and introducing another leaves it unmoved.
EXPECTED_UNEVEN = {
    # The short firing is the "no ticket" answer. helpframe unpacks seven and
    # opens with `if ( category )`, under Blizzard's own comment "If there are
    # args then the player has a ticket".
    "UPDATE_TICKET": "no arguments means no ticket",
    # Two arguments when the rune is ready, one when it is not. runeframe reads
    # `local rune, usable` and branches on `not usable`, so the missing second
    # argument is what says the rune is on cooldown.
    "RUNE_POWER_UPDATE": "the missing argument is the cooldown",
    # Four from the criteria packet, one when the player ticks a box.
    # watchframe's branch does nothing with a nil elapsed or duration and then
    # calls WatchFrame_Update regardless, which is the whole point of the short
    # firing - and the achievement UI calls it itself besides.
    "TRACKED_ACHIEVEMENT_UPDATE": "short firing still redraws the tracker",
    # Two for a bag slot, one for an equipment slot - and the absent second
    # argument is what says which. containerframe guards `if ( bag and slot
    # ... )` so the short firing falls through it, and the paperdoll's is
    # `if ( not arg2 and arg1 == self:GetID() )`, which a second argument makes
    # fail outright: the square would never grey.
    "ITEM_LOCK_CHANGED": "the missing argument means an equipment slot",
}

uneven = {n: v for n, v in sites.items()
          if len({c for c, _ in v}) > 1 and n in wanted
          and n not in EXPECTED_UNEVEN}
print(f"\n{len(uneven)} fired from several places with differing counts:\n")
for name in sorted(uneven):
    counts = ", ".join(f"{c} at {w}" for c, w in sorted(uneven[name]))
    print(f"  {name:34} reads {wanted[name][0]}   [{counts}]")
if not uneven:
    print("  (none)")

sys.exit(0)
