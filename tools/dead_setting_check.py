#!/usr/bin/env python3
"""Options panel controls whose CVar nothing ever reads.

Every checkbox, slider and dropdown in the options panels names a CVar in
self.cvar. The control will save that CVar and read it back happily whether or
not anything acts on it, so a setting with no reader looks exactly like a
setting that works: it remembers what you chose and changes nothing.

A CVar counts as read if any of these mention it, outside the panel definition
that declares it:

  * FrameXML asking for it directly - GetCVar/GetCVarBool/SetCVar
  * a uvarInfo entry mapping it to a global, where that global is read
  * the client itself - storedCVarValue, or the name as a string literal

Names are matched case-insensitively, because the client lowercases them.

Run with --canary to check the sweep can still see: it plants a control naming
a CVar nothing reads and fails if that is not reported. A matcher that has gone
blind reads exactly like a clean tree.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PANELS = ROOT / "Data/interface/framexml"
LUA_ROOTS = [ROOT / "Data/interface"]
CPP_ROOTS = [ROOT / "src", ROOT / "include"]

# The files that only declare controls. A mention here is the declaration
# itself, not a reader.
DECL_FILES = {"interfaceoptionspanels.xml", "interfaceoptionspanels.lua",
              "videooptionspanels.xml", "videooptionspanels.lua",
              "audiooptionspanels.xml", "audiooptionspanels.lua"}

CVAR_DECL = re.compile(r'self\.cvar\s*=\s*"([A-Za-z0-9_]+)"')
FRAME_DECL = re.compile(r'<Frame\s+name="([A-Za-z0-9_]+)"')
CONTROL_DECL = re.compile(r'<(?:CheckButton|Slider|Button|Frame)\s+name="(\$parent[A-Za-z0-9_]*|[A-Za-z0-9_]+)"')
GREYED = re.compile(r'\{"([A-Za-z0-9_]+)",')
UVAR_DECL = re.compile(r'self\.uvar\s*=\s*"([A-Za-z0-9_]+)"')
UVAR_ENTRY = re.compile(r'\["([A-Z0-9_]+)"\]\s*=\s*\{[^}]*cvar\s*=\s*"([A-Za-z0-9_]+)"')


def read(p):
    try:
        return p.read_text(errors="ignore")
    except OSError:
        return ""


def declared_controls():
    """CVar -> (file:line, control frame name or None).

    The frame name is what the greying list keys on, so it is resolved here:
    the nearest enclosing <Frame name=...> supplies what $parent stands for.
    """
    out = {}
    for p in sorted(PANELS.glob("*.xml")) + sorted(PANELS.glob("*.lua")):
        text = read(p)
        for m in CVAR_DECL.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            head = text[:m.start()]
            ctrl = None
            cm = list(CONTROL_DECL.finditer(head))
            if cm:
                ctrl = cm[-1].group(1)
                if ctrl.startswith("$parent"):
                    fm = list(FRAME_DECL.finditer(head))
                    ctrl = (fm[-1].group(1) + ctrl[len("$parent"):]) if fm else None
            out.setdefault(m.group(1).lower(), (f"{p.name}:{line}", ctrl))
    return out


def greyed_controls():
    """Frame names the client greys out with a stated reason."""
    text = read(ROOT / "include/addons/addon_lua_snippets.hpp")
    start = text.find("kFixedControlsLua")
    if start == -1:
        return set()
    end = text.find(")LUA", start)
    return {n for n in GREYED.findall(text[start:end])}


def uvar_map():
    """cvar (lower) -> uvar global, from uvarInfo entries."""
    text = read(PANELS / "interfaceoptionsframe.lua")
    return {c.lower(): u for u, c in UVAR_ENTRY.findall(text)}


def gather(roots, suffixes):
    for root in roots:
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if p.is_file() and p.suffix in suffixes:
                yield p


def readers(extra_snippets):
    """Lowercased names mentioned anywhere that is not a declaration site."""
    seen = set()
    globals_read = set()
    for p in gather(LUA_ROOTS, {".lua", ".xml"}):
        if p.name in DECL_FILES:
            continue
        text = read(p).lower()
        seen.add((p, text))
        globals_read.add((p, text))
    for p in gather(CPP_ROOTS, {".cpp", ".hpp", ".h"}):
        seen.add((p, read(p).lower()))
    for name, text in extra_snippets:
        seen.add((name, text.lower()))
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--canary", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    controls = declared_controls()
    uvars = uvar_map()
    greyed = greyed_controls()

    extra = []
    if args.canary:
        controls["woweecanarysettingnoreader"] = ("canary:0", None)

    corpus = readers(extra)

    dead = []
    handled = []
    for cvar, (where, ctrl) in sorted(controls.items()):
        needle = cvar
        found = False
        for _, text in corpus:
            if needle in text:
                found = True
                break
        if not found and cvar in uvars:
            g = uvars[cvar].lower()
            for _, text in corpus:
                if g in text:
                    found = True
                    break
        if not found:
            if ctrl in greyed:
                handled.append((cvar, where))
            else:
                dead.append((cvar, where))

    total = len(controls)
    print(f"settings with no reader and no greying: {len(dead)} of {total} declared "
          f"({len(handled)} more are dead but greyed with a reason)")
    for cvar, where in dead:
        print(f"  {cvar:38s} {where}")

    if args.canary:
        if not any(c == "woweecanarysettingnoreader" for c, _ in dead):
            print("CANARY FAILED: planted dead setting was not reported")
            return 1
        print("canary ok")
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
