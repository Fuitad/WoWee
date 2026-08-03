#!/usr/bin/env python3
"""Widget methods FrameXML calls that answer nil — and so raise when called.

A missing *global* is harmless: the fallback makes it callable and it answers
nil. A missing *method* is not. The frame metatable answers a no-op only for
names in __WoweeWidgetMethods; anything else comes back nil, and `frame:Foo()`
on nil is "attempt to call method 'Foo' (a nil value)" — a real error that
takes down whatever handler asked.

So the known-methods set is doing the same job for methods that the fallback
does for globals, and a name missing from *it* is the crashing case.

**Read the false positives below before trusting a hit.** Four rounds of them
came out of the first run, and the first draft confidently reported GetOwner
and IsOwned as missing when both are implemented. A method counts as answered
if it is any of:

  - in __WoweeWidgetMethods (a recorded no-op)
  - registered from C, however the registration is spelled
  - defined in bootstrap Lua on *any* metatable name — mt, __WoweeFrameMT,
    animMeta, groupMeta — not only `function mt:`
  - defined by the interface itself on its own objects, either as
    `function Obj:Method()` or assigned as a field, including
    `self.Desaturate = AchievementIcon_Desaturate`, which names an existing
    function rather than an inline one

Run it after any batch of widget work. A hit that survives all four is a real
call on nil.
"""
import re, pathlib, collections

src = pathlib.Path("src/addons/lua_engine.cpp").read_text()

# The curated set that answers a no-op.
block = re.search(r'"__WoweeWidgetMethods = \{\\n"(.*?)"\}\\n"', src, re.S)
known = set(re.findall(r'(\w+)=1', block.group(1))) if block else set()

# Methods actually implemented, however they are registered.
impl = set(re.findall(r'set\("(\w+)"', src))
impl |= set(re.findall(r'\{"(\w+)",\s*lua_\w+\}', src))
impl |= set(re.findall(r"mt[:.]\s*(\w+)\s*=|function mt:(\w+)", src))
impl |= {m for pair in re.findall(r"\"function mt:(\w+)", src) for m in (pair,)}
impl |= set(re.findall(r"\"mt\.(\w+) =", src))
impl |= set(re.findall(r"\"mt\['(\w+)'\]", src))
# Methods the bootstrap Lua defines on a metatable, under whatever name that
# metatable is bound to there — mt, __WoweeFrameMT, animMeta, groupMeta.
# Looking only for `function mt:` reported GetOwner and IsOwned as missing when
# both are defined a few lines apart as `function __WoweeFrameMT:...`.
impl |= set(re.findall(r"function\s+[\w.]*[Mm][Tt]\w*\s*:\s*(\w+)", src))
impl |= set(re.findall(r"function\s+\w*[Mm]eta\w*\s*:\s*(\w+)", src))
impl |= set(re.findall(r"\w+\.(\w+)\s*=\s*function\s*\(self", src))

answered = known | impl

# Methods the interface defines on its own objects. dump.lua's context:Write
# and the achievement buttons' Collapse are ordinary Lua methods on ordinary
# Lua tables — nothing to do with the frame metatable, and not missing.
interface_defined = set()
for _f in list(pathlib.Path("Data/interface").glob("framexml/*.lua")) + \
          list(pathlib.Path("Data/interface").glob("addons/*/*.lua")):
    _t = _f.read_text(errors="ignore")
    interface_defined |= set(re.findall(r'\bfunction\s+[\w.]+[:.](\w+)\s*\(', _t))
    # `self.Desaturate = AchievementIcon_Desaturate` — assigned to a named
    # function, not an inline one. Requiring `= function` missed every method
    # installed that way, which is most of the achievement buttons'.
    interface_defined |= set(re.findall(r'[\w.\]\[]+\.(\w+)\s*=\s*[\w.]+\s*;?\s*$', _t, re.M))
    interface_defined |= set(re.findall(r'[\w.]+\.(\w+)\s*=\s*function', _t))
answered |= interface_defined

interface = pathlib.Path("Data/interface")
files = list(interface.glob("framexml/*.lua")) + list(interface.glob("addons/*/*.lua"))

# obj:Method( — a real method call. Not obj.Method, which is a field read.
CALL = re.compile(r'(?<![\w."\'])([A-Za-z_]\w*)\s*:\s*([A-Z]\w*)\s*\(')

hits = collections.defaultdict(list)
for f in files:
    for i, line in enumerate(f.read_text(errors="ignore").splitlines(), 1):
        if line.lstrip().startswith("--") or line.lstrip().startswith("function"):
            continue
        for obj, meth in CALL.findall(line):
            if meth in answered:
                continue
            if meth.startswith("On"):        # script-handler names, read as fields
                continue
            hits[meth].append(f"{f.name}:{i}: {line.strip()[:80]}")

for meth in sorted(hits, key=lambda m: -len(hits[m])):
    print(f"\n### {meth}  ({len(hits[meth])} call sites)")
    for h in hits[meth][:3]:
        print("   ", h)
print(f"\n{len(hits)} methods called that neither the metatable nor the known set answers")
