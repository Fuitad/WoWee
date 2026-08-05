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

A KeybindingManager action polled in a file under src/ui that is not
game_screen.cpp — that is, inside a panel's own draw — and not named in the
route table. Actions handled in game_screen.cpp are excluded: that file gates
each one itself, in the branch that reads the key.

WHAT IT CANNOT SEE

Whether the panel's draw is actually gated. An ungated panel's key still works
and does not need routing; listing it would be a false alarm, and there are
none today. Nor whether the FrameXML function named in the route exists — the
comment beside the table says to check that by hand, and the three added on
2026-08-05 were checked that way: ToggleTalentFrame in uiparent.lua,
ToggleFriendsFrame(3) for the guild tab, and ToggleLFDParentFrame, whose
binding is still called TOGGLELFGPARENT because renaming it would have reset
everyone's key.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI = ROOT / "src/ui"
APP = ROOT / "src/core/application.cpp"

# Not a real action: the enum's terminator.
IGNORED = {"ACTION_COUNT"}


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

    print(f"{len(polled)} action(s) polled inside a panel's own draw, "
          f"{len(routed)} routed\n")
    print(f"{len(missing)} that would stop working when the panel is handed "
          f"over:\n")
    for action in sorted(missing):
        print(f"  {action:28} {', '.join(sorted(missing[action]))}")
    if not missing:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
