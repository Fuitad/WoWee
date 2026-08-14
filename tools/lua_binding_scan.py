"""The C++ bodies behind the Lua names the interface calls.

Shared by the checks that need to read what a binding actually does -
framexml_lua_override_check and framexml_vararg_spread - because both had
their own copy of this, character for character, and a parser that stops
recognising a registration shape does not fail. It reports fewer bindings, and
a sweep that quietly sees less of its subject is worse than no sweep: it goes
green for a reason nobody looks at.

TWO REGISTRATION SPELLINGS, AND BOTH ARE IN USE

    {"UnitName", lua_UnitName}                     a named function
    {"UnitName", [](lua_State* L) -> int { ... }}   a lambda in the table

A parser that knows only the first misses several hundred bindings, and the
ones it misses are not a random sample: the lambda form is what the newer
bindings use, so the sweep would be blindest to the most recently written code.

WHAT IT CANNOT SEE

A binding registered through a macro, or one whose name is computed. Neither
exists here; if one appears, every caller of this goes blind at once, which is
the trade for having a single parser rather than several.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
ADDONS = ROOT / "src" / "addons"


def binding_bodies():
    """Bound Lua name -> the C++ body behind it, for both spellings."""
    src = "".join(p.read_text(errors="ignore") for p in sorted(ADDONS.glob("*.cpp")))

    def body_at(start):
        depth, i = 1, start
        while i < len(src) and depth:
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
            i += 1
        return src[start:i - 1]

    bodies = {}
    for m in re.finditer(r"static int (lua_\w+)\(lua_State\* L\)\s*\{", src):
        bodies[m.group(1)] = body_at(m.end())

    out = {}
    for name, impl in re.findall(r'\{"([A-Za-z_]\w*)",\s*(?:&)?\s*(lua_\w+)\}', src):
        if impl in bodies:
            out.setdefault(name, bodies[impl])
    for m in re.finditer(r'\{"([A-Za-z_]\w*)",\s*\[\]\(lua_State\* L\) -> int \{', src):
        out.setdefault(m.group(1), body_at(m.end()))
    return out
