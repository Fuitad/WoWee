#!/usr/bin/env python3
"""What the client answers for — the one place that decides.

Seven sweeps each worked this out for themselves, and they disagreed. Most read
only the C++ binding tables, which is between a third and a half of the answer:
138 names are provided from bootstrap Lua living inside C++ string literals, and
a tool that cannot see those reports them as gaps.

That is not hypothetical. `framexml_reachable_globals` reported
GetNumStationeries as unbound and reachable from the mail frame. It has been
answered by the counting table since long before, and acting on that report made
things worse — binding it explicitly removed it from the missing-API report,
which is the one thing that report exists to preserve. Meanwhile
`framexml_element_readiness` had it right the whole time, because its
`registered()` did read the bootstrap.

So: one implementation, imported. A sweep that wants to know whether a name is
answered asks here.

    from framexml_provides import globals_provided, widget_methods_provided
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADDONS = ROOT / "src/addons"
ENGINE = ADDONS / "lua_engine.cpp"


def _sources():
    return [p.read_text(encoding="utf-8", errors="ignore")
            for p in sorted(ADDONS.glob("*.cpp"))]


def globals_provided():
    """Every global name the client answers, however it answers it.

    Four routes, and missing any one of them produces false gaps:

      * a C++ table entry, `{"Name", lua_Name}`
      * an explicit `lua_setglobal(L, "Name")`
      * a function defined in bootstrap Lua, which lives in C++ string
        literals — `function Name(` and `function Name:`
      * a name in a quoted list inside the bootstrap, which is how the counting
        table provides a zero for thirty-five names so that a nil never reaches
        a `for` limit
    """
    names = set()
    for src in _sources():
        names |= set(re.findall(r'\{\s*"([A-Za-z_]\w*)"\s*,', src))
        names |= set(re.findall(r'lua_setglobal\(\s*\w+\s*,\s*"([A-Za-z_]\w*)"', src))
        names |= set(re.findall(r"function\s+([A-Za-z_]\w*)\s*[:(]", src))
        names |= set(re.findall(r"'([A-Za-z_]\w*)'", src))
    return names


def widget_methods_provided():
    """Method names a frame answers: the method table, Lua shims, the allowlist.

    Separate from globals because they resolve through the widget metatable
    rather than _G, and a name can be one without being the other.
    """
    src = ENGINE.read_text(encoding="utf-8", errors="ignore")
    table = set(re.findall(r'\{"([A-Za-z_]\w*)",\s*lua_', src))
    shims = set(re.findall(r'(?:__WoweeFrameMT|mt)\s*:\s*(\w+)\s*\(', src))
    # Several names per string literal — "SetMovable=1,SetNormalTexture=1,\n" —
    # so anchoring on the opening quote finds only the first of each and
    # under-counts the allowlist by four to one.
    allowlist = set(re.findall(r"\b([A-Za-z]\w*)=1", src))
    return table | shims | allowlist


def counting_table():
    """Names given a zero so a nil never reaches a numeric `for` limit.

    Worth having apart: these are deliberately *not* real implementations, and
    they stay in the missing-API report under a "count:" prefix. Binding one
    for real takes it out of that report, which is a loss rather than progress.
    """
    src = ENGINE.read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"local counting = \{(.*?)\}", src, re.S)
    return set(re.findall(r"'(\w+)'", m.group(1))) if m else set()


if __name__ == "__main__":
    g, w, c = globals_provided(), widget_methods_provided(), counting_table()
    print(f"{len(g)} globals provided")
    print(f"{len(w)} widget methods provided")
    print(f"{len(c)} of the globals are counting-table zeros, which belong in "
          f"the missing-API report and should not be bound away")
