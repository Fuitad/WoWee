#!/usr/bin/env python3
"""Every handed-over element needs both halves, and neither half is loud.

Handing a panel to FrameXML is two edits in two places:

  * a **suppression entry**, so FrameXML's frame is hidden while this client
    still draws its own — without it both are on screen the moment the
    interface loads, before anyone has chosen anything;
  * a **frameXmlOwns gate** around this client's own drawing, so it stops when
    the element is handed over — without it both are on screen again, in the
    other direction.

Neither omission raises, logs, or fails a test. Both look exactly like a panel
that works, drawn twice. Twenty-four elements were being drawn twice at one
point, each for one of these two reasons.

    tools/handover_halves_check.py

WHAT IT CHECKS

For every element named in the defaults or the candidates: that some entry in
kSuppress names it, and that some file under src/ gates on it.

WHAT IT CANNOT SEE

Whether the gate is in the right place. The world map had both halves and was
still broken, because the gate wrapped the only function that *fed* the map as
well as the only one that drew it — so handing it over produced no map rather
than two. A gate around a function that does more than draw is a different
fault from a missing gate, and only reading the function finds it.

Nor whether an element needs a counterpart at all: a few are handed over by
other means and are listed here as exceptions rather than reported.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TAKEOVER = ROOT / "src/ui/framexml_takeover.cpp"

# Elements whose drawing this client never had, so there is nothing to gate.
NOTHING_TO_GATE = set()


def main():
    if not TAKEOVER.exists():
        print(f"no {TAKEOVER}")
        return 1
    cpp = TAKEOVER.read_text(errors="ignore")

    table = re.search(r"\{UiElement::PlayerFrame.*?\n\};", cpp, re.S)
    names = dict(re.findall(r'\{UiElement::(\w+),\s*"([a-z]\w*)"\}', table.group(0)))
    # A suppression row names frames, which are UpperCamelCase; the name table
    # above names elements, which are lowercase. That is what tells them apart.
    suppress = set(re.findall(r'\{UiElement::(\w+),\s*"[A-Z][^"]*"', cpp))

    gates = set()
    for path in (ROOT / "src").rglob("*.cpp"):
        gates |= set(re.findall(
            r"frameXmlOwns\(\s*(?:ui::)?UiElement::(\w+)\s*\)",
            path.read_text(errors="ignore")))

    defaults = set(re.findall(r'"(\w+)"', re.search(
        r"return std::set<std::string>\{(.*?)\};", cpp, re.S).group(1)))
    candidates = set(re.findall(r'"(\w+)"', re.search(
        r"for \(const char\* name : \{(.*?)\}\) \{", cpp, re.S).group(1)))

    live = sorted(e for e, n in names.items()
                  if n in defaults or n in candidates)
    no_gate = [e for e in live if e not in gates and e not in NOTHING_TO_GATE]
    no_suppress = [e for e in live if e not in suppress]

    print(f"{len(live)} elements handed over, {len(defaults)} by default and "
          f"{len(candidates)} named by 'candidates'\n")

    print(f"{len(no_gate)} with no frameXmlOwns gate — this client goes on "
          f"drawing when the element is handed over:")
    for e in no_gate:
        print(f'    {e:<20} "{names[e]}"')
    if not no_gate:
        print("    (none)")

    print(f"\n{len(no_suppress)} with no suppression entry — FrameXML's frame "
          f"is drawn while this client still owns it:")
    for e in no_suppress:
        print(f'    {e:<20} "{names[e]}"')
    if not no_suppress:
        print("    (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
