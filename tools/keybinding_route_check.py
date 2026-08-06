#!/usr/bin/env python3
"""Keys that stop working the moment their panel is handed over.

    tools/keybinding_route_check.py

Each of this client's panels polls its own keybinding from inside its own draw.
That is fine until the panel is handed to FrameXML: the draw is then gated off,
so the poll never runs, and the key does nothing at all. Nothing raises, nothing
is logged, and the panel is still openable by clicking — so it reads as a
keybinding that was never set rather than as one that was lost.

The fix in every case is a line in the route table in application.cpp, which
runs FrameXML's own toggle for that element instead. This checks that the table
has kept up.

WHAT IT LOOKS FOR

Two arms, because a key can be lost in two ways.

A KeybindingManager action polled inside a panel's own draw — a file under
src/ui that is not game_screen.cpp — and not named in the route table in
application.cpp.

And an action handled inline in game_screen.cpp whose branch never asks who
owns the element. This arm exists because the first one skipped that file
outright, on the stated ground that it "gates each one itself, in the branch
that reads the key". Escape did not, and that cost the game menu: its branch
ends by setting the flag behind *this* client's escape menu, which is drawn
only while FrameXML does not own the element — so with the menu handed over
the key set a flag nobody read and nothing appeared. A file-wide assumption
about how careful a file is, standing in for a check.

The branch is brace-matched rather than taken as a fixed span, because the
Escape branch is seventy lines and a window wide enough to hold it reaches
into the next one — where a frameXmlOwns would answer for a key that has none.

WHAT IT CANNOT SEE

Whether the panel's draw is actually gated. An ungated panel's key still works
and does not need routing; listing it would be a false alarm, and there are
none today. Nor whether the FrameXML function named in the route exists — the
comment beside the table says to check that by hand, and the three added on
2026-08-05 were checked that way: ToggleTalentFrame in uiparent.lua,
ToggleFriendsFrame(3) for the guild tab, and ToggleLFDParentFrame, whose
binding is still called TOGGLELFGPARENT because renaming it would have reset
everyone's key.

Nor whether a branch that *mentions* ownership routes the right element. That
is a judgement, and the mention is what makes it visible enough to make.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI = ROOT / "src/ui"
APP = ROOT / "src/core/application.cpp"

# Not a real action: the enum's terminator.
IGNORED = {"ACTION_COUNT"}

#: Inline actions with nothing to route to, checked once each against the
#: UiElement enum. Listed rather than filtered by shape, because each is a
#: judgement about whether FrameXML can own that thing at all.
NO_ELEMENT = {
    # Nameplates are drawn in the world by this client, not by any frame.
    # There is no UiElement for them and no FrameXML counterpart to hand the
    # key to; the NAMEPLATES binding exists in bindings.xml and drives the same
    # client-side switch.
    "TOGGLE_NAMEPLATES",
    # Raid frames likewise: the enum has PartyFrames and RaidWarning and
    # nothing for the raid grid, which belongs to Blizzard_RaidUI — a
    # load-on-demand addon rather than a handed-over element.
    "TOGGLE_RAID_FRAMES",
}


#: What makes a branch ownership-aware: it either asks who owns the element or
#: hands the key straight to the interface to act on.
#:
#: askInterface is deliberately not here, and leaving it in hid the very bug
#: this arm was written for. Escape calls it to ask FrameXML whether it closed
#: a window — a question about state, not about ownership — so counting it made
#: the branch look careful while it still ended by setting a flag nobody reads.
AWARE = ("frameXmlOwns", "runInterfaceCommand")


def inline_branches():
    """Action -> whether its branch in game_screen.cpp asks who owns the panel."""
    text = (UI / "game_screen.cpp").read_text(errors="ignore")
    out = {}
    for m in re.finditer(r"Action::([A-Z_]+)", text):
        action = m.group(1)
        if action in IGNORED:
            continue
        # From the poll to the body it guards, then brace-matched to its end.
        open_at = text.find("{", m.end())
        if open_at < 0:
            continue
        depth, i = 1, open_at + 1
        while i < len(text) and depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        body = text[open_at:i]
        aware = any(tok in body for tok in AWARE)
        # An action polled more than once counts as covered if any branch is.
        out[action] = out.get(action, False) or aware
    return out


def main():
    routed = set(re.findall(r"K::Action::([A-Z_]+)", APP.read_text(errors="ignore")))

    polled = {}
    for path in UI.rglob("*.cpp"):
        # game_screen.cpp handles its keys inline and gates them there.
        if path.name in ("game_screen.cpp", "keybinding_manager.cpp"):
            continue
        text = path.read_text(errors="ignore")
        for m in re.finditer(r"Action::([A-Z_]+)", text):
            if m.group(1) in IGNORED:
                continue
            polled.setdefault(m.group(1), set()).add(path.name)

    missing = {a: w for a, w in polled.items() if a not in routed}

    inline = inline_branches()
    blind = sorted(a for a, aware in inline.items()
                   if not aware and a not in routed and a not in NO_ELEMENT)

    print(f"{len(polled)} action(s) polled inside a panel's own draw, "
          f"{len(routed)} routed, {len(inline)} handled inline\n")
    total = len(missing) + len(blind)
    print(f"{total} that would stop working when the panel is handed "
          f"over:\n")
    for action in sorted(missing):
        print(f"  {action:28} {', '.join(sorted(missing[action]))}")
    for action in blind:
        print(f"  {action:28} game_screen.cpp — branch never asks who owns it")
    if not total:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
