#!/usr/bin/env python3
"""Bindings whose returns are spread into a vararg call, answering nothing.

    tools/framexml_vararg_spread.py

WHY THIS FINDS WHAT THE OTHER SWEEPS CANNOT

framexml_short_returns compares a binding's push count against what FrameXML
*unpacks by name* — `local a, b, c = X()`. That reads nothing here, because at
these call sites nothing is unpacked at all. The binding sits as the last
argument of a call, and Lua spreads every value it returns into that call's
varargs. The count is the payload.

So a binding that returns nothing is not a wrong answer, it is an empty one:
the call still happens, the loop inside it runs zero times, and the subsystem
behind it does nothing at all. Nothing raises. Nothing is missing. Every sweep
that asks "is this name bound" says yes.

Found 2026-08-06, and it was the whole of chat:

    ChatFrame_RegisterForMessages(self, GetChatWindowMessages(self:GetID()))

ChatFrame_OnLoad registers a chat frame for no CHAT_MSG_ event whatsoever —
every one of them comes from that line, which walks the names it was handed,
looks each up in ChatTypeGroup and calls RegisterEvent for the events in it.
GetChatWindowMessages answered zero values. Zero groups, zero registrations,
and a chat window that showed nothing from login to logout while the client
parsed, coloured and routed every message correctly.

WHAT IT COMPARES

Lua functions declared with a trailing `...`, called with a binding call in
final argument position, against C bindings that can return zero values. A
binding is counted as "can answer nothing" when a `return 0` is reachable in
its body without having pushed anything — which is both the stub spelling
`(void)L; return 0;` and the real one that returns early on a bad index.

An early `return 0` on a *guard* is usually right: a bad window index should
answer nothing. What matters is whether the ordinary path can also answer
nothing, so a binding whose only statement is a bare `return 0` is reported
loudly and one with pushes elsewhere is reported as worth a look.

WHAT IT CANNOT SEE

A vararg callee reached through a local alias, or one whose declaration this
sweep cannot find because it is built at runtime. And it says nothing about
whether the *values* are right — only whether there are any.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files, without_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
INTERFACE = ROOT / "Data/interface"
ADDONS = ROOT / "src/addons"

#: Answering nothing is right for these, checked once each.
EXPECTED = {
    # Genuinely has no list to give on a 3.3.5 server: there are no Battle.net
    # conversations, so every window carries none of them.
    "BNGetConversationInfo",
}


def vararg_callees():
    """Lua function names declared to take a trailing `...`."""
    names = set()
    decl = re.compile(r"function\s+([A-Za-z_][\w.:]*)\s*\(([^)]*)\)")
    for path in loaded_files(INTERFACE):
        for m in decl.finditer(without_comments(path.read_text(errors="ignore"))):
            if m.group(2).strip().endswith("..."):
                names.add(m.group(1).split(".")[-1].split(":")[-1])
    return names


def spread_sites(callees):
    """binding -> the callee it is spread into, and where."""
    # The binding's own arguments may contain one level of parens — the site
    # that mattered is GetChatWindowMessages(self:GetID()), and a pattern that
    # refuses nesting misses exactly the ones worth reading.
    inner = r"(?:[^()]|\([^()]*\))*"
    pat = re.compile(r"\b([A-Za-z_]\w*)\s*\(" + inner +
                     r",\s*([A-Z]\w+)\s*\(" + inner + r"\)\s*\)")
    out = {}
    for path in loaded_files(INTERFACE):
        for m in pat.finditer(without_comments(path.read_text(errors="ignore"))):
            callee, binding = m.group(1), m.group(2)
            if callee not in callees:
                continue
            out.setdefault(binding, (callee, path.name))
    return out


def binding_bodies():
    """name -> body, for both registration spellings."""
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


def main():
    callees = vararg_callees()
    sites = spread_sites(callees)
    bodies = binding_bodies()

    loud, quiet = [], []
    for name, (callee, where) in sorted(sites.items()):
        if name in EXPECTED:
            continue
        body = bodies.get(name)
        if body is None:
            continue                       # unbound is the other sweep's column
        if "return 0" not in body:
            continue
        pushes = len(re.findall(r"lua_push", body))
        (quiet if pushes else loud).append((name, callee, where, pushes))

    print(f"{len(callees)} vararg callees, {len(sites)} bindings spread into one\n")
    if not bodies or "GetChatWindowMessages" not in bodies:
        print("  CANARY: bindings did not parse — the report below means nothing.\n")

    print(f"{len(loud)} answer nothing at all, so the call they feed does nothing:\n")
    for name, callee, where, _ in loud:
        print(f"  {name:34} -> {callee}  [{where}]")
    if not loud:
        print("  (none)")

    print(f"\n{len(quiet)} can answer nothing on some path — read the guard:\n")
    for name, callee, where, pushes in quiet:
        print(f"  {name:34} -> {callee}  [{where}]  {pushes} push(es)")
    if not quiet:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
