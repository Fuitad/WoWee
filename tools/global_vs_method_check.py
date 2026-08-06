#!/usr/bin/env python3
"""Globals FrameXML calls that exist here only as a widget method.

    tools/global_vs_method_check.py

WHY THIS IS NOT COVERED BY THE UNBOUND-GLOBAL SWEEP

framexml_unbound_globals decides a name is bound by finding it registered
somewhere under src/addons. Widget methods register by name exactly as globals
do, so a method is enough to make a global read as answered — and the global
stays nil, and every FrameXML call of it raises.

GetText was that, and it sat there for months. FrameXML's GetText(token, gender)
looks a global string up with its gendered variant, and ReputationFrame_Update
builds every standing label with it. Meanwhile
`set("GetText", lua_FontString_GetText)` registers a FontString method of the
same name. The static sweep counted the global bound; the reputation list raised
on open regardless. Only the runtime missing-API report could tell them apart,
and only after someone opened that panel.

WHAT IT LOOKS FOR

A name that is (a) called by FrameXML with no `:` or `.` in front of it, so it
is meant as a global, (b) registered as a widget method here, and (c) not
registered as a global anywhere and not defined by FrameXML's own Lua.

All four conditions matter. Dropping (c) reports every method whose name is also
a real global — HasFocus, GetName, SetText — which is most of them. Dropping the
Lua-definition check reports names FrameXML answers itself.

THE RELATED SHAPE THIS DOES *NOT* REPORT

The reverse collision, where a global and a method share a name and both exist.
That is api_shadowing_check's list, and it is noise there for the same reason:
this codebase registers both through the same mechanism, so HasFocus the
edit-box method and HasFocus the focus query look like one name written twice.
Two live examples of the confusion that causes: GetCursorPosition is both the
EditBox caret index (one value) and the global cursor position (two), which made
framexml_short_returns report the global as answering short; GetText is both a
FontString reader and the global above.

WHAT IT CANNOT SEE

A global answered by bootstrap Lua rather than by a C binding — those are picked
up by the FrameXML-definition check only if they use `function Name(`. And it
reads calls, not reachability: a name called only from a file nothing loads is
still counted, which is why loaded_files does the filtering.

Verified failable: deleting the GetText global from lua_system_api.cpp takes
this from zero rows to one, naming GetText.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files, without_comments_or_strings  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
ADDONS = ROOT / "src/addons"
INTERFACE = ROOT / "Data/interface"


def widget_methods():
    """Names registered on a widget's method table."""
    text = (ADDONS / "lua_engine.cpp").read_text(errors="ignore")
    names = set(re.findall(r'\bset\("([A-Za-z_]\w*)"', text))
    names |= set(re.findall(r'\{"([A-Za-z_]\w*)",\s*lua_\w+\}', text))
    names |= set(re.findall(r"function mt:([A-Za-z_]\w*)", text))
    names |= set(re.findall(r"mt\['([A-Za-z_]\w*)'\]", text))
    return names


def global_bindings():
    """Names registered as globals, through a table or lua_setglobal."""
    names = set()
    for path in ADDONS.glob("lua_*_api.cpp"):
        names |= set(re.findall(r'\{"([A-Za-z_]\w*)",', path.read_text(errors="ignore")))
    for path in ADDONS.glob("*.cpp"):
        names |= set(re.findall(r'lua_setglobal\(\s*L_?\s*,\s*"([A-Za-z_]\w*)"',
                                path.read_text(errors="ignore")))
    return names


def interface():
    """(names called bare, names FrameXML defines itself)"""
    called, defined = {}, set()
    for path in loaded_files(INTERFACE):
        text = without_comments_or_strings(path.read_text(errors="ignore"))
        # No ':' or '.' before it, so this is a global call and not a method.
        for m in re.finditer(r"(?<![\w.:])([A-Z][A-Za-z0-9_]{2,})\s*\(", text):
            called.setdefault(m.group(1), set()).add(path.name)
        defined |= set(re.findall(r"function\s+([A-Za-z_]\w*)\s*\(", text))
        defined |= set(re.findall(r"([A-Za-z_]\w*)\s*=\s*function", text))
    return called, defined


def main():
    methods = widget_methods()
    globs = global_bindings()
    called, defined = interface()

    # A zero means nothing without evidence that all three sides parsed. Each
    # canary is a fact that must hold however the code is rearranged.
    print(f"{len(methods)} widget methods, {len(globs)} global bindings, "
          f"{len(called)} names called bare in FrameXML")
    problems = []
    if "SetText" not in methods:
        problems.append("no SetText among the widget methods — the method side is not parsing")
    if "UnitName" not in globs:
        problems.append("no UnitName among the globals — the global side is not parsing")
    if "UnitName" not in called:
        problems.append("FrameXML never seen calling UnitName — the interface is not parsing")
    for p in problems:
        print(f"  CANARY: {p}")
    if problems:
        print("  The count below is meaningless while a canary is missing.")
    print()

    hits = sorted(n for n in called
                  if n in methods and n not in globs and n not in defined)
    print(f"{len(hits)} global(s) called by FrameXML and bound only as a widget method:\n")
    for name in hits:
        where = " ".join(sorted(called[name])[:3])
        print(f"  {name:<28} {where}")
    if not hits:
        print("  (none)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
