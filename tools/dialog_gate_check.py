#!/usr/bin/env python3
"""Dialogs this client draws without asking whether FrameXML draws them too.

    tools/dialog_gate_check.py

"Owned or suppressed" is the rule for elements, and it applies to dialogs one at
a time rather than to the dialog manager as a whole. FrameXML answers most of
the same events with a static popup of its own — DUEL_REQUESTED, GUILD_INVITE,
ShowResurrectRequest — so a client popup with no ownership check draws beside
it: two dialogs for one question, each with its own accept button and its own
idea of what was answered.

Three of the fourteen in renderDialogs were gated and the rest were not, which
read as a decision until each was checked against what FrameXML does with the
same event. Seven were doubles, three of them on the default tier.

WHAT IT LOOKS FOR

Calls to a render*Popup or render*Dialog inside DialogManager's two dispatch
functions, with no frameXmlOwns on the same line or the two above it.

WHAT IT CANNOT SEE

Whether FrameXML has a counterpart at all. The ones left are left on purpose and
each was read: the duel countdown has none; the shared quest, the battleground
invite and the battlefield-manager invite need an event this client never fires,
so FrameXML's side cannot appear. That last reason is thinner than the others
and stops holding the day one of those events is wired — which is why the
ceiling is a number to look at rather than a zero to trust.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANAGER = ROOT / "src/ui/dialog_manager.cpp"
DISPATCH = ("renderDialogs", "renderLateDialogs")
CALL = re.compile(r"\brender[A-Z]\w*(?:Popup|Dialog|Window|Countdown)\s*\(")


def body_of(text, method):
    m = re.search(r"void DialogManager::" + method + r"\s*\([^)]*\)\s*\{", text)
    if not m:
        return None, 0
    depth, i = 1, m.end()
    while i < len(text) and depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    return text[m.end():i], text.count("\n", 0, m.end()) + 1


def main():
    text = MANAGER.read_text(errors="ignore")
    rows, total = [], 0
    for method in DISPATCH:
        body, first = body_of(text, method)
        if body is None:
            continue
        lines = body.split("\n")
        for i, line in enumerate(lines):
            if not CALL.search(line):
                continue
            total += 1
            # Same line, or an `if (...)` on the line above with nothing else
            # on it — the shape a wrapped call hangs from.
            #
            # Not "the line above contains frameXmlOwns", which is what this
            # tried first: these calls sit in a run, so an ungated one directly
            # under a gated one borrows its neighbour's check and reports
            # covered. Five of the six were hidden that way, including two the
            # last commit had deliberately left ungated.
            if "frameXmlOwns" in line:
                continue
            above = lines[i - 1].strip() if i else ""
            if re.fullmatch(r"if\s*\(.*frameXmlOwns.*\)", above):
                continue
            rows.append((method, first + i, line.strip()[:66]))

    print(f"{total} dialog(s) drawn from DialogManager's dispatch\n")
    print(f"{len(rows)} with no ownership check:\n")
    for method, line_no, text_ in rows:
        print(f"  dialog_manager.cpp:{line_no}  [{method}]")
        print(f"      {text_}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
