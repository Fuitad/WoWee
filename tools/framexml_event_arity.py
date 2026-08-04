#!/usr/bin/env python3
"""Events fired with fewer arguments than the handler unpacks.

The unfired-event column says whether an event is sent at all. This asks the
next question: whether what is sent carries what the handler reads.

A handler that unpacks four values from an event fired with two gets nil for
the last two, and nil is not an error — it is a blank name, a missing amount, a
branch that quietly takes the other path. Nothing raises and nothing is
reported, which puts this in the same family as the no-op allowlist.

The shape matched is FrameXML's own:

    if ( event == "SOME_EVENT" ) then
        local a, b, c = ...;

so the destructure is inside the branch that names the event, and the count is
unambiguous. Handlers that unpack once at the top for many events are NOT
matched — the arity there belongs to no single event, and guessing which would
manufacture findings.

Both sides can be wrong. Read the handler before adding an argument: FrameXML
sometimes unpacks more than the server ever sends, and the extra is a value the
real client had and this one has no source for.
"""
import re
import sys
from pathlib import Path

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
for path in SRC.rglob("*.cpp"):
    text = path.read_text(errors="ignore")
    for m in FIRE.finditer(text):
        name = m.group(1)
        if not name:
            continue          # event name held in a variable — arity unknowable here
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

# ---- What the handlers unpack, inside the branch that names the event ----
# One event per branch. A condition naming several — battlefieldframe.lua has
# `UPDATE_BATTLEFIELD_STATUS or ZONE_CHANGED_NEW_AREA or ZONE_CHANGED` sharing
# one body — unpacks for whichever of them carries arguments, and attributing
# that count to the others invents a shortfall. Same reasoning as skipping the
# handlers that unpack once at the top.
BRANCH = re.compile(
    r'\(\s*event\s*==\s*"(\w+)"\s*\)\s*then(.*?)(?=\belseif\b|\bend\b)', re.S)
UNPACK = re.compile(r"local\s+([\w\s,]+?)\s*=\s*\.\.\.")

wanted = {}
for path in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    text = re.sub(r"--\[\[.*?\]\]|--[^\n]*", "", path.read_text(errors="ignore"), flags=re.S)
    for m in BRANCH.finditer(text):
        name, body = m.group(1), m.group(2)
        u = UNPACK.search(body)
        if not u:
            continue
        names = [n.strip() for n in u.group(1).split(",") if n.strip()]
        if not names:
            continue
        prev = wanted.get(name)
        entry = (len(names), path.name, ", ".join(names))
        if prev is None or len(names) > prev[0]:
            wanted[name] = entry

rows = []
for name, (need, where, names) in sorted(wanted.items()):
    have = fired.get(name)
    if have is None:
        continue              # not fired at all — the other sweep's column
    if need > have:
        rows.append((need - have, name, have, need, where, names))

print(f"{len(fired)} events fired with a known argument count, "
      f"{len(wanted)} unpacked inside their own branch\n")
print(f"{len(rows)} fired with fewer arguments than a handler reads:\n")
for short, name, have, need, where, names in sorted(rows, reverse=True):
    print(f"  {name:34} fires {have}, reads {need}   [{where}]")
    print(f"      local {names} = ...")

sys.exit(0)
