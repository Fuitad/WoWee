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

It reads calls, not reachability: a name called only from a file nothing loads
would still be counted, which is why loaded_files does the filtering.

Both sides come from framexml_provides, which is the one implementation of
"does the client answer this name". This tool rolled its own for a day and
missed 407 globals — everything registered through the bootstrap or the
counting table rather than a {"Name", lua_Name} row — and every one of those
would have been reported here as a global existing only as a method, which is
the exact false gap that file was written to stop.

Verified failable: deleting the GetText global from lua_system_api.cpp takes
this from zero rows to one, naming GetText.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files, without_comments_or_strings  # noqa: E402
from framexml_provides import (  # noqa: E402
    globals_provided, widget_methods_provided)

ROOT = Path(__file__).resolve().parent.parent
INTERFACE = ROOT / "Data/interface"


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
    # Both sides through framexml_provides, which is the one implementation of
    # "does the client answer this name". Rolling them here missed 407 globals
    # — everything registered through the bootstrap or the counting table
    # rather than a {"Name", lua_Name} row — and every one of those would have
    # been reported as a global that exists only as a method, which is the
    # exact false gap that file was written to stop.
    methods = widget_methods_provided()
    globs = globals_provided()
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
