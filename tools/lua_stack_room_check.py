#!/usr/bin/env python3
"""Bindings that push a value per row without asking for the room first.

Lua guarantees a binding only a small slack above its arguments - LUA_MINSTACK,
twenty slots. A binding that pushes one value per party member, per gossip
option or per child frame can go past that, and going past it does not raise:
it writes outside the stack and corrupts the heap. `GetChildren` on UIParent
did exactly that on its first run, with 267 children, and the process died in
realloc rather than in Lua.

So: any loop whose *body* pushes, inside a binding with no `lua_checkstack`, is
reported. The bound does not have to be huge to matter - it has to be something
this client does not choose. A guild roster, a gossip list and a comma in a
chat line are all that is needed.

    tools/lua_stack_room_check.py

The loop's own body is matched by brace, not by a window of lines. A window
reports every loop that merely sits above a push - five of five hits on the
first attempt were that, and a check whose every finding is false is one nobody
reads.

Fixed-count loops (`for (int i = 0; i < 3; ++i)`) are not reported: three is a
number this client chose, and twenty slots covers it. Nor is a binding that
returns a literal count - a loop that pushes and then returns 1 is a search
that pushes once, bounded at one however long the list is. That is 64 of the
69 hits the first version reported.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINDINGS = ROOT / "src" / "addons"

LOOP = re.compile(r"\b(for|while)\s*\(")
# A loop whose trip count is fixed by a literal in the header. Anything else -
# a range-based for, or a while - is bounded by data.
FIXED_FOR = re.compile(r"for\s*\(\s*[\w:]+\s+\w+\s*=\s*\d+\s*;[^;]*<=?\s*\d+\s*;")


def bodyAt(text, openBrace):
    """The text between a `{` and its match."""
    depth = 0
    for i in range(openBrace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[openBrace + 1:i], i
    return text[openBrace + 1:], len(text)


def functionsIn(text):
    """Every binding body, as (name, start, body).

    Both registration forms, because they are the same binding to Lua and a
    check that reads one of them asks its question of half the client.
    """
    out = []
    for m in re.finditer(r"(?:int\s+(lua_\w+)\s*\(lua_State\*\s*L\s*\)|"
                         r'\{"(\w+)",\s*\[\]\(lua_State\*\s*L\)\s*->\s*int)\s*\{',
                         text):
        name = m.group(1) or m.group(2)
        brace = text.index("{", m.end() - 1)
        body, _ = bodyAt(text, brace)
        out.append((name, m.start(), body))
    return out


def main():
    findings = []
    scanned = 0
    for path in sorted(BINDINGS.glob("*.cpp")):
        text = path.read_text(errors="ignore")
        base = text[:0]
        for name, start, body in functionsIn(text):
            scanned += 1
            if "lua_checkstack" in body:
                continue
            # Only bindings whose return *count* is a variable. A loop that
            # pushes and returns a literal is a search that pushes once -
            # bounded at one however long the list is, and 64 of the first 69
            # findings were exactly that.
            if not re.search(r"return\s+(?!\d)\w+\s*;", body):
                continue
            for lm in LOOP.finditer(body):
                header = body[lm.start():body.find("{", lm.start()) + 1] \
                    if body.find("{", lm.start()) != -1 else ""
                if FIXED_FOR.search(header):
                    continue
                brace = body.find("{", lm.start())
                if brace == -1:
                    continue
                # Only the loop's own body, so a push that merely follows the
                # loop is not attributed to it.
                inner, _ = bodyAt(body, brace)
                if "lua_push" not in inner:
                    continue
                line = text.count("\n", 0, start) + body.count("\n", 0, lm.start()) + 1
                findings.append((path.name, line, name,
                                 header.strip().splitlines()[0][:70]))
                break

    print(f"scanned {scanned} binding bodies\n")
    for fname, line, name, header in findings:
        print(f"  {fname}:{line}  {name}")
        print(f"      {header}")
    print(f"\n{len(findings)} binding(s) push per row without asking for the room")
    return 0


if __name__ == "__main__":
    sys.exit(main())
