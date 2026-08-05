#!/usr/bin/env python3
"""Controls that open one of this client's windows without asking who owns it.

    tools/window_route_check.py

A window whose draw is gated on frameXmlOwns has to be opened through FrameXML
once that element is handed over. Every control that opens it needs the same
branch, and they are scattered: a keybinding, a micro-menu button, a bag-bar
icon, a context-menu item, the click half of a drag handler, and the code that
puts the bags up when a vendor opens. Nothing ties those together, so each is
found only by looking.

Three lists were found by hand before this existed and each was a separate
sweep of the same seam — the keybinding routes, the micro menu, the bag bar.
This one found two more the moment it was written: the plain-click path on a
bag slot, which lives in the drag handler rather than beside the button, and
the auto-open when a vendor or the guild bank appears.

WHAT IT LOOKS FOR

A call to one of this client's window verbs — toggle, toggleCharacter,
toggleBackpack, toggleBag, openAllBags — with no ownership check in the lines
around it. The check may be the literal frameXmlOwns or a named helper that
wraps it, so both count.

WHAT IT CANNOT SEE

Whether the window that verb opens is gated at all. A panel this client still
draws in every configuration needs no branch, and listing it would be noise —
there are none today, which is why the ceiling is zero.

Nor a window opened by writing its flag directly rather than through a verb.
That shape exists and is not covered: `showFoo_ = !showFoo_` reads the same
whether it is a control or a piece of internal bookkeeping, and separating the
two by regex produced more false alarms than findings.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI = ROOT / "src/ui"

VERBS = ("toggleCharacter", "toggleBackpack", "toggleBag", "openAllBags",
         "toggleSpellbook", "toggle")
# The literal call, and any helper named for the same question.
OWNERSHIP = ("frameXmlOwns", "AreFrameXml", "IsFrameXml", "frameXmlChat")
# How far either side to look for the branch this call sits in.
# Wide enough to clear the comment blocks these branches carry. The character
# sheet's key has fifteen lines of explanation between its frameXmlOwns and the
# call in its else, and a window that stops short of that reports a site that is
# correctly gated.
BEFORE, AFTER = 24, 3


def main():
    call = re.compile(r"\b\w+\.(" + "|".join(VERBS) + r")\s*\(")
    rows = []
    for path in sorted(UI.rglob("*.cpp")):
        lines = path.read_text(errors="ignore").split("\n")
        for i, line in enumerate(lines):
            if not call.search(line):
                continue
            context = "\n".join(lines[max(0, i - BEFORE):i + AFTER])
            if any(word in context for word in OWNERSHIP):
                continue
            rows.append((path.name, i + 1, line.strip()[:72]))

    print(f"{len(rows)} window-opening call(s) with no ownership check:\n")
    for name, line_no, text in rows:
        print(f"  {name}:{line_no}")
        print(f"      {text}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
