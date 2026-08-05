#!/usr/bin/env python3
"""Window flags written from somewhere that never asks who owns the window.

    tools/window_flag_check.py

The companion to window_route_check.py, and the half it says it cannot see. That
one follows *verbs* — toggle, toggleBackpack, openAllBags. This one follows
*flags*: `showFoo_ = true` opens a window just as surely, and reads the same
whether it is a control or a piece of internal bookkeeping.

What makes it separable is not the write. It is the render. This client has
thirty-two show-flags and seventy places that write them, and most of those
windows are drawn in every configuration and need no branch at all. The ones
that matter are the flags whose render is gated on frameXmlOwns — because once
that element is handed over, the render is switched off and every write to its
flag is a control that does nothing.

So the filter is: find the gated renders, find the flag each one returns early
on, and only then look at who writes it. Nine flags out of thirty-two, which is
the difference between a list worth reading and one nobody will.

Found this way on 2026-08-05: the who list, after inspect, the GM ticket and the
battleground scoreboard had already been found by reading the same filter by
hand.

WHAT IT CANNOT SEE

A render whose flag is not a plain `if (!showFoo_) return;` at the top — it is
matched by shape, and a window guarded some other way is invisible here.

Whether a write is a control at all. `showFoo_ = false` inside the render, to
close the window, is a write like any other; those sit inside the render's own
file and are excluded on that basis, which is a heuristic and not a proof.
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ownership_walk import gated

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI = ROOT / "src/ui"


def sources():
    return {p: p.read_text(errors="ignore") for p in sorted(UI.rglob("*.cpp"))}


def gated_renders(src):
    """render method -> the elements it is gated behind."""
    out = {}
    for text in src.values():
        for m in re.finditer(
                r"!frameXmlOwns\(UiElement::(\w+)\)[^;{]*\{?([^{}]{0,300})", text):
            for name in re.findall(r"\.(render\w+)\s*\(", m.group(2)):
                out.setdefault(name, set()).add(m.group(1))
    return out


def guard_flags(src):
    """render method -> its flag, and the line span of its own body.

    The span, rather than the file. A control can live in the same file as the
    window it opens — the escape menu's Help button sits a few thousand lines
    above the render it belongs to — and excluding the whole file to skip the
    render's own `showFoo_ = false` hid it completely. Only the render's body is
    its own bookkeeping.
    """
    out = {}
    for path, text in src.items():
        for m in re.finditer(
                r"void\s+\w+::(render\w+)\s*\([^)]*\)\s*\{", text):
            head = text[m.end():m.end() + 400]
            g = re.search(r"if\s*\(\s*!\s*(\w*[Ss]how\w*_)\s*\)\s*(?:\{\s*)?return", head)
            if not g:
                continue
            depth, i = 1, m.end()
            while i < len(text) and depth:
                if text[i] == "{":
                    depth += 1
                elif text[i] == "}":
                    depth -= 1
                i += 1
            first = text.count("\n", 0, m.start()) + 1
            last = text.count("\n", 0, i) + 1
            out[m.group(1)] = (g.group(1), path.name, first, last)
    return out


def main():
    src = sources()
    gated_by_element = gated_renders(src)
    guards = guard_flags(src)

    watched = {}
    for render, elements in gated_by_element.items():
        if render in guards:
            flag, owner_file, first, last = guards[render]
            watched[flag] = (sorted(elements), owner_file, first, last)

    rows = []
    for path, text in src.items():
        lines = text.split("\n")
        for i, line in enumerate(lines):
            for flag, (elements, owner_file, first, last) in watched.items():
                # A write inside the render's own body is it closing itself.
                if path.name == owner_file and first <= i + 1 <= last:
                    continue
                if not re.search(r"\b" + re.escape(flag) + r"\s*=(?!=)", line):
                    continue
                if gated(lines, i):
                    continue
                rows.append((path.name, i + 1, flag, ", ".join(elements),
                             line.strip()[:64]))

    print(f"{len(gated_by_element)} gated render(s), {len(watched)} of them guarded by a "
          f"show-flag\n")
    print(f"{len(rows)} write(s) to one of those flags with no ownership "
          f"check:\n")
    for name, line_no, flag, elements, text in rows:
        print(f"  {name}:{line_no}  [{elements}]")
        print(f"      {text}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
