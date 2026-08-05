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
# No context window at all. See gated_by() below.


def gated_by(lines, i):
    """Whether the call on line i sits in an if/else chain that tests ownership.

    Proximity does not work here and reading a window of lines is what made an
    earlier version of this report clean while a control was dead. These calls
    sit in runs — eleven micro-menu buttons one after another — so any window
    wide enough to reach a wrapped gate also reaches the *previous button's*
    gate, and an unrouted call borrows its neighbour's check.

    So walk the chain instead, upwards from the call, through exactly the shapes
    a branch is made of:

        if (frameXmlOwns(X)) <call>;          gate on the call's own line
        else                 <call>;          gate on the line above
        if (frameXmlOwns(X))                  gate two or more lines up, with
            <other>;                          the if-branch body between
        else
            <call>;

    and stop at the first line that is none of those — which for an unrouted
    button is `if (button(...)) {`, the control itself.
    """
    budget = 12
    for j in range(i - 1, -1, -1):
        line = lines[j].strip()
        # Comments cost nothing: these branches carry long ones, and a bound
        # that counts them stops short of the gate they are explaining.
        if not line or line.startswith("//") or line.startswith("/*") or line.startswith("*"):
            continue
        budget -= 1
        if budget < 0:
            return False
        # `else if (...)` is both: it opens another branch and is not the gate
        # for the ones below it, so keep walking to the chain's first `if`.
        if line.startswith("else if") or line.startswith("} else if"):
            continue
        if line in ("else", "else {", "} else {") or line.startswith("} else"):
            continue
        if line.startswith("if (") or line.startswith("if("):
            return any(word in line for word in OWNERSHIP)
        # Any statement: a branch body can be several lines, and the walk stops
        # at the first `if` regardless, which is what keeps a neighbour's gate
        # from being borrowed.
        if line.endswith(";") or line.endswith("{"):
            continue
        return False
    return False


def main():
    call = re.compile(r"\b\w+\.(" + "|".join(VERBS) + r")\s*\(")
    rows = []
    for path in sorted(UI.rglob("*.cpp")):
        lines = path.read_text(errors="ignore").split("\n")
        for i, line in enumerate(lines):
            if not call.search(line):
                continue
            if gated_by(lines, i):
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
