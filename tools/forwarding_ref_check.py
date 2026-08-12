#!/usr/bin/env python3
"""Writable accessors that reach round the forwarding their readers go through.

    tools/forwarding_ref_check.py

WHY

GameHandler was split into per-subject handlers, and what is left of it forwards:
every getter asks the sub-handler when there is one and only falls back to its
own member when there is not. In practice the sub-handler is always there, so
the member behind the fallback is never written and never read.

Next to those getters sit accessors that hand out a writable reference, for the
few places that edit a list in place rather than replacing it. Those were left
returning the member directly. A caller then writes to a list that nothing
displays, and the write is not lost in a way anybody can see: no error, no log
line, the panel simply never changes.

Two were live when this was written. Sorting an auction house column reordered
GameHandler's empty copy while the rows on screen came from the inventory
handler's, so clicking a column header did nothing. The backfill that fills in
a mail's sender once the name packet arrives wrote to the same empty copy, so
mail from a player whose name had not yet resolved kept a blank sender.

WHAT IT DOES

Pairs the members handed out by a writable Ref() accessor against the members
that appear only as the fallback branch of a forwarding accessor. A member in
both sets has a reader and a writer looking at different objects.

WHAT IT CANNOT SEE

Forwarding written in some other shape than the two below, and the same split
in a class other than GameHandler. It also cannot tell a deliberate local
cache from a mistake, which is why it reports rather than judges.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "include/game/game_handler.hpp"
SOURCES = sorted((ROOT / "src/game").glob("game_handler*.cpp"))

# `auto& xRef() { return x_; }` - a writable reference to this class's member.
WRITABLE_REF = re.compile(r"(\w+Ref)\(\)\s*(?:const\s*)?\{\s*return (\w+_?);")

# The two shapes the forwarding is written in.
FORWARD_TERNARY = re.compile(r"(\w+Handler_) \? \1->\w+\([^\)]*\) : (\w+_?)\b")
FORWARD_EARLY_RETURN = re.compile(
    r"if \((\w+Handler_)\) return \1->\w+\([^\)]*\);\s*\n\s*return (\w+_?);")


def main():
    if not HEADER.exists() or not SOURCES:
        print("Found no game handler. The zero below means the scan broke.")
        return 1

    header = HEADER.read_text(errors="ignore")
    refs = {}
    for match in WRITABLE_REF.finditer(header):
        refs.setdefault(match.group(2), []).append(match.group(1))

    source = "".join(p.read_text(errors="ignore") for p in SOURCES)
    fallback = set()
    for pattern in (FORWARD_TERNARY, FORWARD_EARLY_RETURN):
        for match in pattern.finditer(source):
            fallback.add(match.group(2))

    if not refs or not fallback:
        print("Recognised no accessors at all. The zero below means the scan broke.")
        return 1

    diverging = sorted(set(refs) & fallback)
    print(f"{len(refs)} member(s) handed out by a writable Ref()")
    print(f"{len(fallback)} member(s) reached only as a forwarding fallback\n")
    print(f"{len(diverging)} member(s) written locally and read through a sub-handler:")
    for name in diverging:
        print(f"  {name}  via {', '.join(refs[name])}")
    if not diverging:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
