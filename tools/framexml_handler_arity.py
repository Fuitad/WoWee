#!/usr/bin/env python3
"""Handlers that unpack at the top, against what the client fires.

    tools/framexml_handler_arity.py

framexml_event_arity.py reads what a handler unpacks *inside the branch that
names an event*, and deliberately skips handlers that unpack once at the top —
because one handler usually serves many events and attributing the widest
unpack to all of them invents shortfalls.

That skip has a blind spot, and it is not a small one: when a handler serves
exactly ONE event, the unpack at its top is that event's signature and nothing
else. UNIT_SPELLCAST_SUCCEEDED lived in it. The client fired the unit and the
spell id; CastRandomManager_OnEvent reads `local unit, name, rank = ...` and
calls strlower on the second and third, so the id landed where the name belongs
and a /castsequence macro raised on a nil rank.

Tracing that one found the rest of the family. Every UNIT_SPELLCAST_* event
carries unit, spell name, rank, cast id, spell id; all of them were being fired
with two values. The cast bar takes its cast id from UnitCastingInfo and
compares `select(4, ...)` against it before finishing the bar, so that argument
being absent meant the branch which flashes the bar and clears self.casting
never ran — on a default element, on every cast.

WHAT IT LOOKS FOR

A function taking (self, event, ...) whose body names exactly one event, and
which unpacks two or more values at the top. Compared against the widest fire
of that event anywhere in src/.

WHAT IT CANNOT SEE

A handler serving several events — the other sweep's territory, and still
skipped here for the same reason. Nor whether the values are in the right
ORDER, which is the fault this actually found: two values were fired where two
were read, and the second was the wrong one. Arity is a floor, not a contract.
"""
import pathlib
import re
import sys
import pathlib as _pathlib
sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
from framexml_source import without_comments

ROOT = pathlib.Path(__file__).resolve().parent.parent
XML = ROOT / "Data/interface"
SRC = ROOT / "src"

FIRE = re.compile(
    r'(?:fireAddonEvent|addonEventCallbackRef\(\))\s*\(\s*"(\w+)"\s*,\s*\{(.*?)\}',
    re.S)
HANDLER = re.compile(
    r"function\s+(\w+)\s*\(\s*self\s*,\s*event\s*,\s*\.\.\.\s*\)(.*?)\nend", re.S)
UNPACK = re.compile(r"local\s+([\w\s,]+?)\s*=\s*\.\.\.")


def count_args(body):
    body = body.strip()
    if not body:
        return 0
    depth, n = 0, 1
    for ch in body:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            n += 1
    return n


def fired():
    """Event -> the widest argument count fired anywhere."""
    out = {}
    for path in SRC.rglob("*.cpp"):
        text = path.read_text(errors="ignore")
        for m in FIRE.finditer(text):
            n = count_args(m.group(2))
            out[m.group(1)] = max(out.get(m.group(1), 0), n)
    return out


def main():
    have = fired()
    rows = []
    for path in XML.rglob("*.lua"):
        text = without_comments(path.read_text(errors="ignore"))
        for m in HANDLER.finditer(text):
            fn, body = m.group(1), m.group(2)
            u = UNPACK.search(body[:400])
            if not u:
                continue
            names = [x.strip() for x in u.group(1).split(",") if x.strip()]
            if len(names) < 2:
                continue
            events = set(re.findall(r'event\s*==\s*"(\w+)"', body))
            if len(events) != 1:
                continue          # several events: the other sweep's job
            event = events.pop()
            n = have.get(event)
            if n is not None and n < len(names):
                rows.append((event, n, len(names), fn, path.name,
                             ", ".join(names)))

    print(f"{len(have)} events fired from src/\n")
    print(f"{len(rows)} single-event handler(s) unpacking more at the top "
          f"than is fired:\n")
    for event, n, need, fn, where, names in sorted(rows):
        print(f"  {event:36} fires {n}, reads {need}   [{where}:{fn}]")
        print(f"      local {names} = ...")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
