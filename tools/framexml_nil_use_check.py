#!/usr/bin/env python3
"""Calls to functions that do not exist, used where nil raises immediately.

The previous sweep only caught `local v = Missing()` followed by a use of v.
A call used straight in the expression is the same fault with no variable in
between, and concatenation is a third raising context that neither covered:
"text"..nil is an error in Lua, not the empty string.
"""
import re, pathlib, collections, sys

root = pathlib.Path("Data/interface")
files = list(root.glob("framexml/*.lua")) + list(root.glob("addons/*/*.lua"))
texts = {f: f.read_text(errors="ignore") for f in files}

csrc = "\n".join(p.read_text(errors="ignore")
                 for p in pathlib.Path("src/addons").glob("*.cpp"))
c_bindings = set(re.findall(r'\{"(\w+)",', csrc)) | \
             set(re.findall(r'static int lua_(\w+)', csrc))
lua_defined = set()
for t in texts.values():
    lua_defined |= set(re.findall(r'\bfunction\s+([A-Za-z_]\w*)\s*\(', t))
    lua_defined |= set(re.findall(r'\b([A-Za-z_]\w*)\s*=\s*function\s*\(', t))
# Names the bootstrap defines as counting stubs answer 0, not nil.
counting = set(re.findall(r"'([A-Za-z_]\w*)'",
    re.search(r'local counting = \{(.*?)\n\s*"\}', open('src/addons/lua_engine.cpp').read(), re.S).group(1)))
known = c_bindings | lua_defined | counting

CALL = r'(?<![:.\w])([A-Z][A-Za-z0-9_]{3,})\s*\([^()]*\)'
patterns = [
    ("compared as a number", re.compile(CALL + r'\s*(?:[<>]=?)')),
    ("in arithmetic",        re.compile(CALL + r'\s*[-+*/]\s*[\w("\']')),
    # The call after a `..` needs its own pattern. CALL starts with a
    # (?<![:.\w]) meant to skip obj.Method(), and after a concatenation the
    # character before the name IS a dot — so the operand of every `.."x"..`
    # was rejected by the very pattern written to find it. That is how
    # _G["FACIAL_HAIR_"..GetFacialHairCustomization()] went unreported while
    # raising in BarberShop_OnLoad.
    ("concatenated", re.compile(r'\.\.\s*([A-Z][A-Za-z0-9_]{3,})\s*\([^()]*\)'
                                r'|' + CALL + r'\s*\.\.')),
]

# The same fault with a variable in between: `local x = Missing()` and then x
# used somewhere nil raises, a few lines down. Concatenation belongs here as
# much as arithmetic — the barber shop reads
#     local hairCustomization = GetHairCustomization();
#     ... _G["HAIR_"..hairCustomization.."_STYLE"]
# and it was the concatenation, not any arithmetic, that took the addon down.
ASSIGN = re.compile(r'\blocal\s+([A-Za-z_]\w*)\s*=\s*'
                    r'(?<![:.])([A-Z][A-Za-z0-9_]{3,})\s*\(')

hits = collections.defaultdict(list)
for f, t in texts.items():
    lines = t.splitlines()
    for i, line in enumerate(lines, 1):
        if line.lstrip().startswith("--"):
            continue
        for label, pat in patterns:
            for m in pat.finditer(line):
                fn = next(g for g in m.groups() if g)
                if fn in known:
                    continue
                hits[fn].append((label, f"{f.name}:{i}: {line.strip()[:88]}"))

        m = ASSIGN.search(line)
        if not m or m.group(2) in known:
            continue
        var, fn = m.group(1), m.group(2)
        use = re.compile(r'\b' + re.escape(var) + r'\s*(?:[<>]=?|[-+*/]|\.\.)'
                         r'|(?:[-+*/]|\.\.)\s*\b' + re.escape(var) + r'\b')
        # A guard between the two makes the use safe, and this is the whole
        # reason the shape was left out of the contract checker before: most
        # candidates are `local x = Foo(); if ( x ) then ... x .. "y" ...`,
        # where nil never reaches anything. Without noticing the guard the
        # report is 20-odd false positives and gets ignored.
        guard = re.compile(r'\bif\s*\(?\s*(?:not\s+)?' + re.escape(var) + r'\b')
        for j, later in enumerate(lines[i:i + 20], start=i + 1):
            if later.lstrip().startswith("--"):
                continue
            if guard.search(later):
                break
            if use.search(later):
                hits[fn].append(("assigned then used",
                                 f"{f.name}:{j}: {later.strip()[:88]}"))
                break

for fn in sorted(hits):
    labels = {l for l, _ in hits[fn]}
    print(f"\n### {fn}  [{', '.join(sorted(labels))}]")
    for _, h in hits[fn][:3]:
        print("   ", h)
print(f"\n{len(hits)} missing functions used where nil raises")
