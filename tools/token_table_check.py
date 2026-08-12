#!/usr/bin/env python3
"""Token strings this client answers that no FrameXML table has a key for.

    tools/token_table_check.py

WHY THIS FINDS WHAT THE OTHER SWEEPS CANNOT

binding_arg_coverage_check asks whether an answer used everything it was told.
This is its mirror on the way back out: whether the answer is *spelled* the way
the caller will look it up. A token is not a value, it is a key - FrameXML takes
what UnitClass and UnitPowerType return and indexes a fixed table with it, so a
different spelling is not a wrong colour, it is a nil, and nil takes the other
branch without a word. Nothing else here can see it: the name is bound, the
argument is read, the return is a string of the right type in the right slot.

Three came out of it on 2026-08-06:

  * UnitPowerType answered "" for power type 5 where RUNES was meant, so a rune
    bar's label prefix came back nil from _G[""]. The colour survived only
    because PowerBarColor carries numeric aliases as a fallback - which is the
    reason to check the spelling rather than trust the bar looking right.
  * UnitRace returned the display name twice over, where the second return is
    the *file name* and the two differ for four races. DressUpTexturePath
    splices it straight into "DressUpBackground-"..fileName, so a Night Elf
    asked for an asset with a space in its name and the dressing room drew no
    background at all.
  * GetMirrorTimerInfo answered "FATIGUE" where MirrorTimerColors is keyed
    EXHAUSTION. MirrorTimer_Show reads color.r straight off the lookup, so that
    one raised rather than losing a colour - on a per-frame polling path, while
    drowning. The same three names existed in four places across three files and
    only this copy was wrong, so they are one table now; the sweep can see it
    because that table has a findable home.

WHAT IT COMPARES

Each C token table against the keys of the FrameXML table that is indexed by it.
Only in that direction. The reverse - a FrameXML key this client never answers -
is normal and is not reported: PowerBarColor carries AMMOSLOT and FUEL for
vehicles, and every class table carries all eleven classes whether or not the
connected realm has Death Knights.

WHAT IT CANNOT SEE

A token that is spelled correctly and *chosen* wrongly - UnitClassification
picking "elite" for a rare is four correct spellings and one wrong branch, and
reads clean here. It also only knows about pairings written into PAIRS below, so
a new token table is invisible until someone adds it; the count is printed for
that reason.

UnitRace's file names have no FrameXML table to check against - nothing keys on
them, they are spliced into a path - so they are checked for the property that
made them wrong instead: a file name never contains a space.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INTERFACE = ROOT / "Data/interface"
# Token tables live wherever the code that answers with them does; both files
# are searched so a table can sit beside its subject rather than in one bucket.
SOURCES = [ROOT / "include/addons/lua_api_helpers.hpp",
           ROOT / "include/game/game_handler.hpp"]

# C token table -> the FrameXML table(s) whose keys it must be spelled for.
PAIRS = [
    ("kLuaClassTokens", ["CLASS_ICON_TCOORDS", "RAID_CLASS_COLORS"]),
    ("kLuaPowerNames", ["PowerBarColor"]),
    ("kMirrorTimerNames", ["MirrorTimerColors"]),
    ("kLootMethodTokens", ["UnitLootMethod"]),
]


def c_tokens(name):
    """The non-empty strings in a `const char* name[...] = { ... }` table."""
    for path in SOURCES:
        src = path.read_text(errors="ignore")
        m = re.search(r"\b" + re.escape(name) + r"\[\d*\]\s*=\s*\{(.*?)\}", src, re.S)
        if m:
            return [t for t in re.findall(r'"([^"]*)"', m.group(1)) if t]
    return None


def lua_keys(table):
    """Keys of a Lua table, written either inside its literal or assigned to it.

    Both forms appear: CLASS_ICON_TCOORDS is one braced literal, PowerBarColor is
    an empty table followed by a run of PowerBarColor["KEY"] = ... assignments.
    """
    keys = set()
    for path in INTERFACE.rglob("*.lua"):
        text = path.read_text(errors="ignore")
        keys.update(re.findall(re.escape(table) + r'\s*\[\s*"([^"]+)"\s*\]', text))
        # Numeric keys as well, for the report below: a table written
        # [0]=60 [1]=70 [2]=80 says at a glance what an answer of 3 will find.
        keys.update(re.findall(re.escape(table) + r"\s*\[\s*(\d+)\s*\]\s*=", text))
        m = re.search(re.escape(table) + r"\s*=\s*\{", text)
        if m:
            depth, i = 1, m.end()
            while i < len(text) and depth:
                if text[i] == "{":
                    depth += 1
                elif text[i] == "}":
                    depth -= 1
                i += 1
            keys.update(re.findall(r'\[\s*"([^"]+)"\s*\]', text[m.end():i]))
    return keys


def table_keyed_by_binding():
    """Sites where the interface indexes a table with a binding's answer.

    `MAX_PLAYER_LEVEL_TABLE[GetAccountExpansionLevel()]` is the whole fault in
    one line: a lookup with no fallback, where a wrong answer is a nil rather
    than a wrong value, and the nil is then compared with `<`. Finding these by
    shape rather than by listing them means a new one cannot slip past - the
    pairs above have to be written down, and something nobody wrote down is
    exactly what this is for.

    It reports rather than judges: knowing the key set of the table and the
    answers of the binding are two different problems, and the second needs the
    C read. The count is pinned so a new site is read once.
    """
    sites = {}
    for path in INTERFACE.rglob("*.lua"):
        text = path.read_text(errors="ignore")
        for m in re.finditer(r"\b([A-Za-z_]\w*)\[\s*([A-Z]\w+)\s*\(", text):
            table, fn = m.group(1), m.group(2)
            if table in ("_G", "select", "string"):
                continue
            sites.setdefault((table, fn), set()).add(path.name)
    return sites


def main():
    rows = []
    checked = 0
    for c_name, tables in PAIRS:
        tokens = c_tokens(c_name)
        if tokens is None:
            rows.append(f"  {c_name} - table not found in any SOURCES file (renamed? moved?)")
            continue
        for table in tables:
            keys = lua_keys(table)
            if not keys:
                rows.append(f"  {table} - no keys parsed from the interface (renamed? not loaded?)")
                continue
            checked += 1
            for tok in tokens:
                if tok not in keys:
                    rows.append(f'  {c_name} answers "{tok}", which is not a key of {table}')

    # The race file names key nothing; they are spliced into an asset path, and
    # the fault they had was a space in one.
    files = c_tokens("kLuaRaceFileNames")
    if files is None:
        rows.append("  kLuaRaceFileNames - table not found in any SOURCES file (renamed? moved?)")
    else:
        checked += 1
        for tok in files:
            if " " in tok:
                rows.append(f'  kLuaRaceFileNames has "{tok}" - a file name cannot contain a space')

    print(f"{len(PAIRS) + 1} token table(s) paired, {checked} comparison(s) made")
    print()
    print(f"{len(rows)} token(s) spelled so the interface cannot look them up:\n")
    for r in rows:
        print(r)
    if not rows:
        print("  (none)")

    sites = table_keyed_by_binding()
    print()
    print(f"{len(sites)} table lookup(s) keyed by a binding's answer:\n")
    for (table, fn), files in sorted(sites.items()):
        keys = lua_keys(table)
        ordered = sorted(keys, key=lambda k: (not k.isdigit(), int(k) if k.isdigit() else k))
        shown = ", ".join(ordered[:8]) if keys else "(no literal keys parsed)"
        print(f"  {table}[{fn}()]   in {', '.join(sorted(files))}")
        print(f"      keys: {shown}")
    if not sites:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
