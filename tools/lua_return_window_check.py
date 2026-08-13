#!/usr/bin/env python3
"""Bindings that pop after building the values they return.

    tools/lua_return_window_check.py

WHY

`return 8` from a C function hands back the top eight of the stack, whatever
they are. That is fine while the eight it wants are the last eight pushed, and
it stops being fine the moment something pops in between: the window slides
down and picks up whatever was underneath.

GetAddOnInfo did exactly that (fixed 2026-08-14). It pushed name, title, notes
and five literals, then a `lua_pop(L, 1)` meant to drop the addon entry
underneath removed the last value pushed instead. FrameXML received the
registry's entry table where the name belonged, the name where the title
belonged, and `loadable` as nil - falsy, so every addon in the list read as
unloadable.

WHAT IT DOES

For each binding whose last statement is `return N` with N above one, finds the
Nth-from-last push and asks whether anything pops between there and the return.

WHAT IT DELIBERATELY DOES NOT DO

The first version of this counted stack depth at the return and reported
anything deeper than N. That premise is wrong and produced 24 findings without
a true one among them: values left *below* the returned window are harmless,
because the window is taken from the top. Frame_GetScript leaves the
`__scripts` table under its single return value and is correct. The fault is
never the residue; it is a pop that moves the window off the values built for
it.

WHAT IT CANNOT SEE

A pop inside a branch the success path does not take, which would read as a
fault and is not one; none exist today. Values pushed by a helper on the
binding's behalf, which no regex can follow. And a window of the right size
holding the right kinds in the wrong order, which is framexml_return_order.py's
question.

VERIFIED BOTH WAYS: run against src/addons/lua_system_api.cpp as it stood at
03863d7c~1 and this reports lua_GetAddOnInfo; run against the fix and it reports
nothing.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Both forms this codebase registers a binding in: a named function, and a
# lambda written out in the table. They are the same binding to Lua, and a
# sweep that reads only the first sees under half of them - which is what
# tools_run_check.py exists to notice, and did notice about this one.
BINDING = re.compile(
    r"^static int (lua_\w+)\(lua_State\* L\)\s*\{"
    r"|\{\s*\"(\w+)\"\s*,\s*\[\]\s*\(lua_State\* L\)\s*->\s*int\s*\{", re.M)
PUSH = re.compile(r"\blua_(?:push\w+|getfield|rawgeti|newtable|createtable)\s*\(")
POP = re.compile(r"\blua_pop\s*\(")
FINAL_RETURN = re.compile(r"\breturn\s+(\d+)\s*;")


def body_of(text, start):
    """The text between the brace at `start` and its match."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
    return ""


def findings_in(text, source):
    out = []
    checked = 0
    for match in BINDING.finditer(text):
        body = body_of(text, match.end() - 1)
        returns = list(FINAL_RETURN.finditer(body))
        if not returns:
            continue
        answered = int(returns[-1].group(1))
        if answered < 2:
            continue

        head = body[:returns[-1].start()]
        pushes = list(PUSH.finditer(head))
        if len(pushes) < answered:
            # Fewer pushes than values answered means they come from somewhere
            # this cannot follow; framexml_short_returns.py asks about counts.
            continue

        checked += 1
        # Everything from the first of the N values it means to return.
        if POP.search(head[pushes[-answered].start():]):
            out.append((source, match.group(1) or match.group(2), answered))
    return checked, out


def main():
    sources = sorted((ROOT / "src" / "addons").glob("*.cpp"))
    if not sources:
        print("Found no bindings. The zero below means the scan broke.")
        return 1

    checked, findings = 0, []
    for path in sources:
        n, hits = findings_in(path.read_text(errors="ignore"),
                              str(path.relative_to(ROOT)))
        checked += n
        findings += hits

    print(f"{checked} binding(s) returning more than one value")
    print(f"{len(findings)} that pop after building them:\n")
    for source, name, answered in findings:
        print(f"  {name:36} returns {answered}   {source}")
    if not findings:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
