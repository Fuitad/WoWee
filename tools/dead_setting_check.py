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

A control built in Lua rather than XML has no name this can resolve, so
greying it does not take it off the list. That under-credits by two today (the
two voice device dropdowns) and errs towards reporting a setting as dead, which
is the safe direction for a ratchet.

Run with --canary to check the sweep can still see: it plants a control naming
a CVar nothing reads and fails if that is not reported. A matcher that has gone
blind reads exactly like a clean tree.

What that canary proves is narrow, and it is worth being plain about: it shows
the sweep can still report a name that appears nowhere at all. It cannot show
the reader test is calibrated, because a test that counts too much still
reports a name it never sees. Widening what counts as a reader is checked by
the finding count, not by the canary.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PANELS = ROOT / "Data/interface/framexml"
LUA_ROOTS = [ROOT / "Data/interface"]
CPP_ROOTS = [ROOT / "src", ROOT / "include"]

# The files that declare controls. A mention inside one of these does not make
# a setting live, and both ways of being clever about that were tried:
#
#   * Counting every mention took this sweep from 29 findings to 3, because
#     those files carry a table keyed by CVar name for tooltip text, so nearly
#     every setting appears in them. The canary still passed - a planted name
#     that appears nowhere cannot detect a reader test that has gone slack.
#   * Counting only the by-name asks - GetCVar("x") - left two settings reading
#     as live whose only reader greys a neighbouring control.
#
# The second is the honest measure of the wrong thing. A panel consulting
# itself to grey a sibling changes the panel, not the game, and this sweep is
# looking for controls that change nothing in the game. cameraSmoothStyle was
# the case that prompted the question and it proves the point: it is asked for
# by name in interfaceoptionspanels.lua, and until it was wired up it still did
# not move the camera by one degree.
DECL_FILES = {"interfaceoptionspanels.xml", "interfaceoptionspanels.lua",
              "videooptionspanels.xml", "videooptionspanels.lua",
              "audiooptionspanels.xml", "audiooptionspanels.lua"}

CVAR_DECL = re.compile(r'self\.cvar\s*=\s*"([A-Za-z0-9_]+)"')
FRAME_DECL = re.compile(r'<Frame\s+name="([A-Za-z0-9_]+)"')
CONTROL_DECL = re.compile(r'<(?:CheckButton|Slider|Button|Frame)\s+name="(\$parent[A-Za-z0-9_]*|[A-Za-z0-9_]+)"')
GREYED = re.compile(r'\{"([A-Za-z0-9_]+)",')
#: `function SomeFrameName_OnLoad (self)` - the frame is the part before _On.
LUA_HANDLER = re.compile(r'function\s+([A-Za-z0-9_]+?)_On[A-Za-z]+\s*\(')
UVAR_DECL = re.compile(r'self\.uvar\s*=\s*"([A-Za-z0-9_]+)"')
UVAR_ENTRY = re.compile(r'\["([A-Z0-9_]+)"\]\s*=\s*\{[^}]*cvar\s*=\s*"([A-Za-z0-9_]+)"')


def read(p):
    try:
        return p.read_text(errors="ignore")
    except OSError:
        return ""


def _resolve_xml_names(path):
    """Every element's resolved global name, by walking real XML nesting.

    $parent is the *enclosing element*, which is not the same as the nearest
    preceding <Frame name=...>: a named sibling declared just above wins that
    race and gives a name no frame answers to. The four voice sliders are the
    example - $parentSpeakerVolume inside AudioOptionsVoicePanel sits after a
    named BindingOutput sibling, so a textual scan reads
    AudioOptionsVoicePanelBindingOutputSpeakerVolume and the real frame is
    AudioOptionsVoicePanelSpeakerVolume.

    Returns [(element, resolved_name)] in document order, plus the source line
    of each element where the parser gives one.
    """
    try:
        import xml.etree.ElementTree as ET
    except ImportError:
        return []
    try:
        text = read(path)
        # FrameXML declares a default namespace; strip it so tags stay simple.
        text = re.sub(r'\sxmlns(:\w+)?="[^"]*"', "", text, count=1)
        root = ET.fromstring(text)
    except ET.ParseError:
        return []

    out = []

    def walk(el, parent_name):
        name = el.get("name")
        resolved = None
        if name:
            resolved = (parent_name + name[len("$parent"):]
                        if name.startswith("$parent") and parent_name else name)
            if name.startswith("$parent") and not parent_name:
                resolved = None
        out.append((el, resolved))
        for child in el:
            walk(child, resolved if resolved else parent_name)

    walk(root, None)
    return out


def declared_controls():
    """CVar -> (file:line, control frame name or None)."""
    out = {}
    for p in sorted(PANELS.glob("*.xml")):
        text = read(p)
        # Map each element to the CVar its OnLoad assigns, then to its name.
        for el, resolved in _resolve_xml_names(p):
            body = "".join(el.itertext())
            own = "".join(c.text or "" for c in el if len(c) == 0) or body
            m = CVAR_DECL.search(body)
            if not m:
                continue
            # The nearest element that both declares the cvar and has a name.
            if resolved is None:
                continue
            line = text.count("\n", 0, text.find(m.group(0))) + 1
            key = m.group(1).lower()
            prev = out.get(key)
            # Prefer the innermost element - the control itself, not the panel.
            if prev is None or len(resolved) > len(prev[1] or ""):
                out[key] = (f"{p.name}:{line}", resolved)

    # Controls built in Lua rather than XML. Their frame name is in the handler
    # they are declared inside - AudioOptionsSoundPanelHardwareDropDown sets its
    # cvar in AudioOptionsSoundPanelHardwareDropDown_OnLoad - so the enclosing
    # function names the control the same way $parent does in the markup.
    # Without this a device dropdown could be greyed and still read as
    # unhandled, because nothing here knew what it was called.
    for p in sorted(PANELS.glob("*.lua")):
        text = read(p)
        for m in CVAR_DECL.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            fns = list(LUA_HANDLER.finditer(text[:m.start()]))
            ctrl = fns[-1].group(1) if fns else None
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
