#!/usr/bin/env python3
"""Bindings that answer a boolean where FrameXML compares a number.

`x == 0` against `true`/`false` is silently false, never an error, so the
branch simply never runs. IsActionInRange is the shape: actionbutton.lua tests
`valid == 0` and `valid == 1` for the out-of-range indicator, and a boolean
return would have failed both and hidden the indicator forever.
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
    for m in re.finditer(r"local\s+([a-zA-Z_]\w*)\s*=\s*([A-Z][A-Za-z0-9_]*)\s*\(", text):
        var, fn = m.group(1), m.group(2)
        tail = text[m.end(): m.end() + 900]
        if re.search(rf"\b{re.escape(var)}\s*(==|~=|>=?|<=?)\s*-?\d", tail):
            numeric.setdefault(fn, set()).add(f"{path.name}: local {var} = {fn}(...) then compared")

# Which of those names are C bindings, and do they push a boolean?
src = subprocess.run(["grep", "-rn", "-A", "40", "static int lua_",
                      str(ROOT / "src/addons")], capture_output=True, text=True).stdout

bound = {}
for m in re.finditer(r'\{"([A-Za-z0-9_]+)",\s*(?:lua_)?([A-Za-z0-9_]+)\}',
                     "\n".join((ROOT / "src/addons" / f).read_text(errors="ignore")
                               for f in ["lua_action_api.cpp", "lua_unit_api.cpp",
                                         "lua_inventory_api.cpp", "lua_spell_api.cpp",
                                         "lua_social_api.cpp", "lua_system_api.cpp"])):
    bound[m.group(1)] = m.group(2)

hits = []
for name in sorted(numeric):
    if name not in bound:
        continue
    impl = bound[name]
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

print(f"{len(hits)} binding(s) push a boolean and are compared numerically:\n")
for name, impl, where in hits:
    print(f"  {name}  ->  {impl}")
    for w in where:
        print(f"      {w}")
