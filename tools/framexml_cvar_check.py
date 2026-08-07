#!/usr/bin/env python3
"""CVars whose default can never match what FrameXML compares them against.

An unknown CVar answers "0" here, and for most of them that is right: nearly
every CVar in the interface is a boolean, and "0" is off.

The exception is the ones that hold a *name*. UpdatePaperdollStats takes
GetCVar("playerStatLeftDropdown") and compares it against five category names,
filling the column from whichever matches. "0" matches none of them, so it
wrote nothing — and wrote nothing *successfully*, running to completion with no
error and no warning. The character sheet showed two empty panels under the
model and there was nothing anywhere to say why.

That is the shape this looks for: a CVar compared against string literals, none
of which is "0", and which lua_system_api.cpp has no explicit answer for.

**The comparison is often a line away from the call.** uiparent.lua writes

    local lastTalkedToGM = GetCVar("lastTalkedToGM");
    if ( lastTalkedToGM ~= "" ) then GMChatFrame_LoadUI(); GMChatFrame:Show()

and "0" is not "", so the GM chat window opened on every login for a player no
GM had ever written to. Matching only `GetCVar("x") == "y"` on one line walked
straight past it, which is the same blindness the for-limit check had. Both the
direct form and an assignment compared later in the same function are checked
now.

Note what it does **not** report, because the first version did and every one
was a false positive: a CVar compared against "1" at all. That is a number,
and "0" is its sibling — off for a boolean, and the first of a set for
showBattlefieldMinimap, which is compared against "1" and "2" and means off at
zero. Requiring "0" itself to appear was too strict, since nothing ever
compares against the off value; it tests for the on ones and falls through.
"""
import re
import pathlib

# Paths resolve against the repository, not the working directory.
REPO = pathlib.Path(__file__).resolve().parent.parent

import collections
import sys

fx = (pathlib.Path(sys.argv[1]) if len(sys.argv) > 1
      else REPO / "Data" / "interface" / "framexml")
if not fx.is_dir():
    sys.exit(f"no such directory: {fx}")

# CVars the client answers deliberately, however that answer is spelled.
sysapi = (REPO / "src/addons/lua_system_api.cpp").read_text()
# Folded, because lua_GetCVar folds: the client's CVar names are not
# case-sensitive and the interface spells "uiscale" and "uiScale" both ways.
known = {m.lower() for m in re.findall(r'n == "(\w+)"', sysapi)}

compared = collections.defaultdict(set)
files = list(fx.glob("*.lua")) + list(fx.glob("addons/*/*.lua"))
for f in files:
    t = f.read_text(errors="ignore")
    for cv, val in re.findall(r'GetCVar\(\s*"(\w+)"\s*\)\s*==\s*"([^"]*)"', t):
        compared[cv].add(val)
    for val, cv in re.findall(r'"([^"]*)"\s*==\s*GetCVar\(\s*"(\w+)"\s*\)', t):
        compared[cv].add(val)
    # `local x = GetCVar("y")` then `x ~= ""` or `x == "z"` further down.
    lines = t.splitlines()
    pending = {}
    for i, line in enumerate(lines):
        if re.match(r'\s*(?:local\s+)?function\b', line):
            pending.clear()
        for var, cv in re.findall(r'\b(\w+)\s*=\s*GetCVar\(\s*"(\w+)"\s*\)', line):
            pending[var] = cv
        for var, val in re.findall(r'\b(\w+)\s*[=~]=\s*"([^"]*)"', line):
            if var in pending:
                compared[pending[var]].add(val)

faults = {cv: vals for cv, vals in compared.items()
          if cv.lower() not in known and "0" not in vals and "1" not in vals}

print(f"scanned {len(files)} files, {len(compared)} CVars compared against a literal")
if not faults:
    print("\nnone whose default is unreachable.")
else:
    print(f"\n{len(faults)} whose default \"0\" can never match:\n")
    for cv, vals in sorted(faults.items()):
        print(f"  {cv:32} compared against {sorted(vals)}")
    print("\nEach needs an explicit default in lua_GetCVar, or the code reading")
    print("it silently takes no branch at all.")
