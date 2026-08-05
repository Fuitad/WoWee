#!/usr/bin/env python3
"""Unit bindings that never look at the unit they were asked about.

    tools/unit_argument_check.py

A binding whose name starts with Unit takes a unit token as its first argument —
"player", "target", "pet", "party1" — and every one of them is asked about more
than one unit by something in FrameXML. A binding that ignores that argument and
answers from the player does not fail: it returns a real number belonging to the
wrong character, which is the hardest kind of wrong to see. Nothing is empty,
nothing is zero, nothing raises.

This turned up three times in one day, in three files:

  * SetInventoryItem, which showed the player's own equipment on the inspect
    paperdoll — recorded in its own comment as a bug fixed once already.
  * UnitStat and UnitResistance, which listed a hunter's Strength and armour as
    the pet's on the paperdoll's pet tab.
  * UnitArmor, the same, through PaperDollFrame_SetArmor — which the pet tab
    calls with "Pet", capitalised, so a comparison against "pet" has to lower
    the token first.

WHAT IT LOOKS FOR

A binding with Unit in its name that reads no first argument, calls no unit
resolver, and answers out of player-only state. All three conditions, because
plenty of Unit bindings legitimately answer for the player alone — UnitXP and
UnitCharacterPoints are asked about nothing else by anything here.

WHAT IT CANNOT SEE

A binding that resolves its unit and then uses the answer wrongly, and one that
takes a unit somewhere other than the first argument. It also cannot tell a
binding that *should* answer for one unit only from one that has simply not been
asked yet; that judgement belongs in the commit that leaves it.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADDONS = ROOT / "src/addons"

#: Reading the first argument, or resolving it, counts as looking at the unit.
LOOKS = re.compile(r"resolveUnit|resolveUnitGuid"
                   r"|luaL_(?:opt|check)string\(\s*L\s*,\s*1")
#: State that belongs to the player and nobody else.
PLAYER_ONLY = re.compile(r"getPlayer\w*\(|playerGuid|getArmorRating|getResistance")


def bindings(text):
    """(name, body) for both the named and the inline binding shapes."""
    for m in re.finditer(r"static int (lua_\w+)\(lua_State\* L\) \{(.*?)\n\}",
                         text, re.S):
        yield m.group(1), m.group(2)
    for m in re.finditer(r'\{"(\w+)",\s*\[\]\(lua_State\* L\) -> int \{(.*?)\n        \}\}',
                         text, re.S):
        yield m.group(1), m.group(2)


def main():
    rows = []
    for path in sorted(ADDONS.glob("*.cpp")):
        text = path.read_text(errors="ignore")
        for name, body in bindings(text):
            if "Unit" not in name:
                continue
            if LOOKS.search(body):
                continue
            if not PLAYER_ONLY.search(body):
                continue
            rows.append((name, path.name))

    rows = sorted(set(rows))
    print(f"{len(rows)} unit binding(s) that never look at their unit and "
          f"answer from the player:\n")
    for name, where in rows:
        print(f"  {name:36} {where}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
