#!/usr/bin/env python3
"""C bindings for a name FrameXML defines itself, which therefore never run.

    tools/framexml_lua_override_check.py

WHY THIS FINDS WHAT THE OTHER SWEEPS CANNOT

Every readiness and stub check asks whether a name is *bound*. This asks the
opposite: the name is bound, in C, with a real body - and it is never reached,
because FrameXML declares a Lua function of the same name and the order settles
it. Bindings are registered in LuaEngine::initialize; FrameXML is read after.
The later definition wins, so the C one is dead from the moment the interface
loads.

Nothing reports that. The binding compiles, the name resolves, every sweep that
counts bound names counts it, and the work inside it never happens.

Usually this is the arrangement working as intended: FrameXML's version is a
wrapper that calls the C binding under a different name - UIParentLoadAddOn
wraps LoadAddOn, GetUnitName wraps UnitName. The check is for the case where it
is not, and the C body is simply lost.

InspectUnit is the cautionary one. FrameXML's version loads the inspect addon
and shows the frame; it never sends the request, which is what the C binding of
that name does. The chain works only because the addon's own InspectFrame_Show
calls NotifyInspect - and NotifyInspect was itself a no-op until 2026-08-06, so
for as long as both were true the unit menu's Inspect did nothing at all, from
two directions at once.

WHAT IT COMPARES

Top-level `function Name(` in any loaded interface file, against C bindings
registered under that name whose body is more than a token. A short body is a
stub and losing it costs nothing.

WHAT IT CANNOT SEE

A name FrameXML assigns rather than declares - `Foo = function() ... end` - and
whether the Lua version reaches the C work by some other name. That last one is
the judgement each row needs, which is why the settled ones are listed with it.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pathlib
from framexml_source import loaded_files, without_comments  # noqa: E402
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lua_binding_scan import binding_bodies
c_bindings = binding_bodies

ROOT = Path(__file__).resolve().parent.parent
INTERFACE = ROOT / "Data/interface"
ADDONS = ROOT / "src/addons"

#: A body this short is a stub, and FrameXML taking the name costs nothing.
STUB_CHARS = 200

#: Overrides checked one at a time and found to be the arrangement working,
#: with what settled each. A set rather than a count.
EXPECTED = {
    # FrameXML's wrapper calls LoadAddOn, which is this same C body under its
    # other name - and the C body's reason-token mapping exists *because* the
    # wrapper does _G["ADDON_"..reason], which would raise on a token
    # globalstrings does not define. The two are written for each other.
    "UIParentLoadAddOn": "wrapper calls LoadAddOn, the same body",
    # GetUnitName(unit, showServerName) is FrameXML's own helper over UnitName,
    # which is a real binding and is reached.
    "GetUnitName": "wrapper calls UnitName",
    # FrameXML's loads the inspect addon and shows the frame. It does not send
    # the request - the addon's InspectFrame_Show calls NotifyInspect, which
    # does. Correct only because NotifyInspect is real; see the note above.
    "InspectUnit": "addon's InspectFrame_Show calls NotifyInspect",
}


def lua_definitions():
    """Global function name -> the file that declares it."""
    out = {}
    decl = re.compile(r"^function\s+([A-Z][A-Za-z0-9_]*)\s*\(", re.M)
    for path in loaded_files(INTERFACE):
        for m in decl.finditer(without_comments(path.read_text(errors="ignore"))):
            out.setdefault(m.group(1), path.name)
    return out




def main():
    lua = lua_definitions()
    c = c_bindings()

    shadowed = sorted(set(lua) & set(c))
    rows = [n for n in shadowed
            if n not in EXPECTED and len(c[n]) > STUB_CHARS]

    print(f"{len(lua)} interface functions, {len(c)} C bindings, "
          f"{len(shadowed)} names in both\n")
    if "UIParentLoadAddOn" not in shadowed:
        print("  CANARY: a known overlap is missing - the report means nothing.\n")

    print(f"{len(rows)} C binding(s) doing real work that FrameXML overrides:\n")
    for name in rows:
        print(f"  {name:34} declared in {lua[name]}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
