#!/usr/bin/env python3
"""Names defined in more than one place, where load order decides the winner.

Three times now a feature has been dead because something defined a name twice
and the copy that won was the empty one. The bug is invisible from either site:
each looks like the only definition, and nothing errors — the call just does
nothing, which reads as an unimplemented feature rather than a shadowed one.

Three mechanisms, all found in this codebase:

  bootstrap over binding   The Lua in lua_engine.cpp runs after the C tables are
                           registered, so a stub there replaces the real
                           implementation. Twelve did, including the pet bar and
                           the stance bar.

  table field over metatable   A field on a frame's own table beats the frame
                           metatable. GameTooltip carried five of these and they
                           shadowed every tooltip binding.

  binding against binding  The same name in two C tables. GetActionBarPage was
                           registered twice against different storage, so
                           changing the page moved one number and reading it
                           returned the other.

Not every hit is a fault: two definitions that agree are clutter, and a Lua one
that calls through to the real methods is fine. The point is to see them.

    python3 tools/api_shadowing_check.py
"""

import re
import sys
from pathlib import Path

ADDONS = Path(__file__).resolve().parent.parent / "src" / "addons"


def sources():
    return sorted(ADDONS.glob("*.cpp"))


def scan():
    binding_names = {}          # name -> [files]
    bootstrap_globals = {}      # name -> [files]
    counting_stubs = set()      # names the bootstrap answers zero for
    metatable_methods = set()
    zero_bindings = set()       # bindings that answer a constant
    table_methods = {}          # object -> {method: file}

    for path in sources():
        text = path.read_text(errors="replace")

        for name in re.findall(r'\{\s*"([A-Za-z_]\w*)"\s*,\s*(?:lua_|\[)', text):
            binding_names.setdefault(name, []).append(path.name)
        # A binding that is itself a plain stub answers the same as the
        # counting list, so one shadowing the other changes nothing.
        for name in re.findall(
                r'\{\s*"([A-Za-z_]\w*)"\s*,\s*lua_Return(?:Zero|Nil|Nothing|False)\b', text):
            zero_bindings.add(name)
        for name in re.findall(r'\{\s*"([A-Za-z_]\w*)"\s*,\s*lua_', text):
            metatable_methods.add(name)
        for name in re.findall(r'"function mt:([A-Za-z_]\w*)', text):
            metatable_methods.add(name)

        # Globals the bootstrap Lua defines: "function Name(" and "Name = function".
        for name in re.findall(r'"function\s+([A-Za-z_]\w*)\s*\(', text):
            bootstrap_globals.setdefault(name, []).append(path.name)
        for name in re.findall(r'"([A-Za-z_]\w*)\s*=\s*function', text):
            bootstrap_globals.setdefault(name, []).append(path.name)

        # The counting stubs, defined by looping over a list of names rather
        # than one at a time. They are bootstrap Lua like any other, so one that
        # shares a name with a C binding wins and answers zero forever —
        # GetNumMacroIcons did exactly that, leaving the icon picker empty
        # beside a working GetMacroIconInfo.
        for block in re.findall(r"local counting = \{(.*?)\}", text, re.S):
            for name in re.findall(r"'(\w+)'", block):
                counting_stubs.add(name)

        # Methods hung directly on a named global's table.
        for obj, meth in re.findall(r'"function ([A-Z]\w*):([A-Za-z_]\w*)\(', text):
            table_methods.setdefault(obj, {})[meth] = path.name

    return (binding_names, bootstrap_globals, metatable_methods,
            table_methods, counting_stubs, zero_bindings)


def main():
    bindings, boot, mt, tables, counting, zeroes = scan()
    # A counting stub over a binding matters only when the binding
    # does real work; two ways of answering zero are not a fault.
    for name in counting:
        # Not filtered by metatable_methods: that set holds every binding,
        # so testing against it silenced the very case this catches.
        if name in bindings and name not in zeroes:
            boot.setdefault(name, []).append('lua_engine.cpp (counting)')
    problems = 0

    over_binding = sorted(set(bindings) & set(boot))
    if over_binding:
        problems += len(over_binding)
        print("bootstrap Lua shadows a binding "
              "(the bootstrap runs later, so it wins):")
        for n in over_binding:
            print(f"    {n:<28} binding in {', '.join(sorted(set(bindings[n])))}")

    twice = sorted(n for n, files in bindings.items() if len(files) > 1)
    if twice:
        problems += len(twice)
        print("\nregistered as a binding more than once "
              "(the later registration wins):")
        for n in twice:
            print(f"    {n:<28} {', '.join(sorted(set(bindings[n])))}")

    field_hits = {o: sorted(m for m in ms if m in mt) for o, ms in tables.items()}
    field_hits = {o: ms for o, ms in field_hits.items() if ms}
    if field_hits:
        problems += sum(len(ms) for ms in field_hits.values())
        print("\na table field shadows the frame metatable "
              "(a field always wins):")
        for o in sorted(field_hits):
            print(f"    {o}: {' '.join(field_hits[o])}")

    # The one hit here that is never ambiguous: a name bound in C into
    # frameMethods and then redefined as Lua on the same metatable. The
    # bootstrap runs afterwards, so the Lua one always wins — and these are
    # written as no-ops, which turns a working method into silence. It has
    # happened twice: EnableMouse, so no frame took the mouse, and SetBackdrop
    # with its two colour setters, so no panel drew a background.
    eng = (ADDONS / "lua_engine.cpp").read_text(errors="ignore")
    m = re.search(r"static const struct luaL_Reg frameMethods\[\] = \{(.*?)\n    \};",
                  eng, re.S)
    if m:
        c_bound = set(re.findall(r'\{"(\w+)"', m.group(1)))
        after = eng[eng.index('"local mt = __WoweeFrameMT'):] \
            if '"local mt = __WoweeFrameMT' in eng else ""
        lua_defined = set(re.findall(r'"function mt:(\w+)', after))
        both = sorted(c_bound & lua_defined)
        if both:
            problems += len(both)
            print("\nbound in C and then redefined as Lua on the frame metatable"
                  "\n(the Lua one runs later and wins — this is always a fault):")
            for n in both:
                print(f"    {n}")

    print("\nA caveat this cannot see past: frame methods and globals are"
          "\nregistered the same way here, so HasFocus the edit-box method and"
          "\nHasFocus the focus-target query look like one name in two places."
          "\nCheck what each is before treating a hit as a fault.")

    if not problems:
        print("no name is defined in two places that could disagree")
    else:
        print(f"\n{problems} to look at — each is a name whose winner depends on "
              "load order.\nSome are harmless duplication; the dangerous ones are "
              "a stub over an implementation,\nand a setter and getter that end up "
              "on opposite sides.")
    # Never fails the build: most hits are clutter, and a check that cries wolf
    # gets muted. This is for reading.
    return 0


if __name__ == "__main__":
    sys.exit(main())
