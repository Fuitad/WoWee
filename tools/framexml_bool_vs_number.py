#!/usr/bin/env python3
"""Bindings answering something a numeric comparison can never match.

Two shapes, one failure:

  * **a boolean**, where `x == 0` against `true`/`false` is silently false and
    the branch simply never runs. IsActionInRange is the case that named this:
    actionbutton.lua tests `valid == 0` and `valid == 1` for the out-of-range
    indicator, and a boolean would have failed both and hidden it forever.

  * **nil**, which is worse, because it fails the comparison in the *other*
    direction. `nil ~= 0` is **true** in Lua, so a binding answering nil where
    the reader asks `if ( x ~= 0 )` takes the branch meant for "there is one"
    every single time — carrying the nil into whatever is behind it.

    GetQuestWorldMapAreaID was that. WorldMap_OpenToQuest asks
    `if ( mapID ~= 0 )` and then `if ( floorNumber ~= 0 )`, and lua_ReturnNil
    ran both, on the path the quest tracker takes whenever a tracked quest is
    clicked. SetMapByID drops a zero id and SetDungeonMapLevel is a no-op, so
    nothing came of it — it was inert by luck, not by design, and the answer
    for "none" is now the pair of zeroes the reader is testing for.

The nil arm only sees bindings registered straight to a shared nil returner.
A hand-written body that pushes nil on some path is invisible here, and that
is the shape to widen to if this ever reports zero for long.

THE TWO IT REPORTS TODAY

  * GetMapDebugObjectInfo — unreachable. The loop that calls it runs to
    GetNumMapDebugObjects(), which answers zero, so `for i = 1, 0` never
    executes. It is reported because this sweep reads comparisons, not
    reachability.
  * GetWintergraspWaitTime — guarded the careful way, `if ( nextBattleTime and
    nextBattleTime > 60 )`, so nil takes the final else and the timer reads
    "in progress" forever. Wrong on screen and safe underneath, and there is
    no better answer: a number would be a more confident lie.

Verified failable by restoring GetQuestWorldMapAreaID's lua_ReturnNil, which
takes the report from two rows to three.
"""
import re
import subprocess
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
import sys as _s; _s.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files

XML = ROOT / "Data/interface"

# Name(...) compared numerically, or a local assigned from it then compared.
numeric = {}
for path in sorted(loaded_files(XML)):
    text = path.read_text(errors="ignore")
    for m in re.finditer(r"\b([A-Z][A-Za-z0-9_]*)\s*\([^()\n]*\)\s*(==|~=|>=?|<=?)\s*(-?\d+)", text):
        numeric.setdefault(m.group(1), set()).add(f"{path.name}: {m.group(0)[:60]}")
    # local v = Name(...)  ... later  v == 0
    #
    # Every name on the left, not only the first. `local mapID, floorNumber =
    # GetQuestWorldMapAreaID(questID)` is the case: both are compared against
    # zero two lines down, and reading one name saw neither, because what
    # follows `local mapID` is a comma rather than an equals sign. This is the
    # same blind spot framexml_unbound_globals had, where it cost 555 names.
    for m in re.finditer(r"local\s+([a-zA-Z_]\w*(?:\s*,\s*[a-zA-Z_]\w*)*)\s*=\s*"
                         r"([A-Z][A-Za-z0-9_]*)\s*\(", text):
        fn = m.group(2)
        tail = text[m.end(): m.end() + 900]
        for var in (v.strip() for v in m.group(1).split(",")):
            if re.search(rf"\b{re.escape(var)}\s*(==|~=|>=?|<=?)\s*-?\d", tail):
                numeric.setdefault(fn, set()).add(
                    f"{path.name}: local {var} = {fn}(...) then compared")

# Which of those names are C bindings, and do they push a boolean?
src = subprocess.run(["grep", "-rn", "-A", "40", "static int lua_",
                      str(ROOT / "src/addons")], capture_output=True, text=True).stdout

_ADDON_SRC = "\n".join((ROOT / "src/addons" / f).read_text(errors="ignore")
                       for f in ["lua_action_api.cpp", "lua_unit_api.cpp",
                                 "lua_inventory_api.cpp", "lua_spell_api.cpp",
                                 "lua_social_api.cpp", "lua_system_api.cpp"])

bound = {}
for m in re.finditer(r'\{"([A-Za-z0-9_]+)",\s*(?:lua_)?([A-Za-z0-9_]+)\}', _ADDON_SRC):
    bound[m.group(1)] = m.group(2)

# The inline form, whose body is right there rather than in a named function
# somewhere above. Matching only {"Name", lua_Name} asked this question of
# fewer than half the bindings while reporting a number that read as all of
# them — the same blind spot four other sweeps had, and a zero from half a
# search is not a zero.
inline_bodies = {}
for m in re.finditer(r'\{"([A-Za-z0-9_]+)",\s*\[\]\(lua_State\*\s*L?\s*\)\s*->\s*int\s*\{',
                     _ADDON_SRC):
    depth, i = 1, m.end()
    while i < len(_ADDON_SRC) and depth:
        if _ADDON_SRC[i] == "{":
            depth += 1
        elif _ADDON_SRC[i] == "}":
            depth -= 1
        i += 1
    inline_bodies[m.group(1)] = _ADDON_SRC[m.end():i - 1]
    bound.setdefault(m.group(1), m.group(1))

hits = []
for name in sorted(numeric):
    if name not in bound:
        continue
    impl = bound[name]
    # Before the body lookup, not after: a binding registered straight to a
    # shared returner has no body of its own to find, so asking for one and
    # skipping when it is missing discarded exactly the rows this arm is for.
    # Verified by restoring the fault and watching this report it.
    if impl in ("ReturnNil", "luaReturnNil"):
        hits.append((name, impl + " (answers nil)", sorted(numeric[name])[:2]))
        continue
    if name in inline_bodies:
        body = inline_bodies[name]
    else:
        body = subprocess.run(["grep", "-rn", "-A", "45", f"int {impl}(lua_State",
                               str(ROOT / "src/addons")], capture_output=True, text=True).stdout
    if not body:
        continue
    if "lua_pushboolean" in body and "lua_pushnumber" not in body.split("lua_pushboolean")[0][-400:]:
        hits.append((name, impl, sorted(numeric[name])[:2]))

# A zero here is only worth anything if the sweep can still see. Both stages
# are reported, and the canary is checked: IsActionInRange is the case that
# named this sweep, and it must at least reach the numeric-comparison set. If
# it stops appearing there, the Lua side has stopped parsing and the zero
# below means nothing.
print(f"{len(numeric)} names compared numerically in FrameXML, "
      f"{len(bound)} C bindings parsed")
canary = "IsActionInRange"
if canary in numeric:
    print(f"canary: {canary} seen compared numerically — the Lua side parses")
else:
    print(f"CANARY MISSING: {canary} not found compared numerically. "
          f"The sweep is not reading FrameXML; the count below is meaningless.")
print()

print(f"{len(hits)} binding(s) answer a boolean or nil and are compared numerically:\n")
for name, impl, where in hits:
    print(f"  {name}  ->  {impl}")
    for w in where:
        print(f"      {w}")
