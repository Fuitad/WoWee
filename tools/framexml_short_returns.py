#!/usr/bin/env python3
"""Bindings that return fewer values than FrameXML unpacks.

`local a, b, c = GetThing()` with a binding that pushes two leaves c nil, and
nil is only an error when something touches it — so this shows up as a missing
column, a nil concat much later, or nothing at all until the one path that
reads the last value.

Approximate by construction: it takes the largest `return N` in the function
body against the largest left-hand side in FrameXML. Both are upper bounds, so
a hit means "worth reading", not "wrong". Prints its own parse counts so a zero
can be told from a silent failure.

Two false-positive shapes, both seen and both worth knowing before acting:

  * **Two calls on one right-hand side.** `local a, b = GetX(), GetY()` counts
    two names on the left and credits both to GetX. blizzard_talentui does this
    with GetActiveTalentGroup and GetNumTalentGroups on one line, and mainmenubar
    does it with two GetCVarBool calls.

  * **A widget method sharing a global's name.** The binding tables hold both
    globals and widget methods, and this cannot tell them apart.
    `GetCursorPosition` is a global returning x and y *and* an EditBox method
    returning one character offset; resolving the name to the method makes the
    global look short by one.
"""
import re
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
ADDONS = ROOT / "src/addons"
XML = ROOT / "Data/interface"

# name -> implementation symbol
bound = {}
for f in ADDONS.glob("*.cpp"):
    s = f.read_text(errors="ignore")
    for m in re.finditer(r'\{"([A-Za-z0-9_]+)",\s*(?:&)?\s*(lua_[A-Za-z0-9_]+)\}', s):
        bound[m.group(1)] = (m.group(2), f)

# implementation -> max returned count
pushes = {}
for f in ADDONS.glob("*.cpp"):
    s = f.read_text(errors="ignore")
    for m in re.finditer(r"\bint\s+(lua_[A-Za-z0-9_]+)\s*\(lua_State\*\s*L\s*\)\s*\{", s):
        name = m.group(1)
        # crude body slice: to the next top-level function definition
        nxt = s.find("\nint ", m.end())
        nxt2 = s.find("\nstatic int ", m.end())
        end = min(x for x in [nxt, nxt2, len(s)] if x != -1)
        body = s[m.end():end]
        # Plain `return N;` and the conditional form `return c ? N : M;`.
        # Only the first was matched, and UnitCastingInfo ends
        # `return wantChannel ? 8 : 9;` — so a binding returning nine values
        # read as returning none and never appeared here, while the cast bar
        # was raising on a shifted endTime.
        rets = [int(x) for x in re.findall(r"\breturn\s+(\d+)\s*;", body)]
        for a, b in re.findall(r"\breturn\s+[^;]*\?\s*(\d+)\s*:\s*(\d+)\s*;", body):
            rets += [int(a), int(b)]
        if rets:
            pushes[name] = max(rets)

def strip_comments(text: str) -> str:
    """Drop Lua and XML comments.

    The unbound sweep learned this and this one had not: mainmenubar.lua carries
    a commented-out `--exhaustionCurrXP, exhaustionMaxXP = GetXPExhaustion()`
    above the live single-value call, so GetXPExhaustion read as short by one
    against a line nothing runs.
    """
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    return re.sub(r"--\[\[.*?\]\]|--[^\n]*", "", text, flags=re.S)


# FrameXML: max destructured count per call
unpack = {}
for path in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    t = strip_comments(path.read_text(errors="ignore"))
    for m in re.finditer(r"(?:local\s+)?([A-Za-z_][\w., \t]*?)\s*=\s*([A-Z][A-Za-z0-9_]*)\s*\(", t):
        lhs, fn = m.group(1), m.group(2)
        if "." in lhs or "[" in lhs:
            continue
        n = len([p for p in lhs.split(",") if p.strip()])
        if n <= 1:
            continue
        prev = unpack.get(fn)
        if not prev or n > prev[0]:
            unpack[fn] = (n, f"{path.name}: {m.group(0).strip()[:70]}")

print(f"parsed {len(bound)} bindings, {len(pushes)} bodies, "
      f"{len(unpack)} destructured call sites\n")

hits = []
for fn, (n, where) in unpack.items():
    if fn not in bound:
        continue
    impl, _ = bound[fn]
    if impl not in pushes:
        continue
    if n > pushes[impl]:
        hits.append((fn, pushes[impl], n, where))

hits.sort(key=lambda h: h[2] - h[1], reverse=True)
print(f"{len(hits)} binding(s) may return short:\n")
for fn, got, want, where in hits:
    print(f"  {fn:<32} pushes {got}, unpacked {want}")
    print(f"      {where}")
