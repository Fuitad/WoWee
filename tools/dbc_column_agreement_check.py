#!/usr/bin/env python3
"""A layout column that is inside the file and pointing at the wrong field.

    tools/dbc_column_agreement_check.py            # report
    tools/dbc_column_agreement_check.py --canary   # prove the scan can see one

WHAT THE OTHER TWO CHECKS CANNOT SEE

`dbc_layout_check.py` and the range test in test_dbc_layout_columns.cpp ask
whether a declared column exists in the file. That is the easy half. A column
that exists and holds the wrong field reads as data - a plausible number, or a
zero - and nothing anywhere says so.

Spell.dbc's effect block was that for as long as the layouts existed. It starts
at 71 on WotLK, 65 on TBC and 61 on vanilla, because TBC and vanilla carry an
EffectBaseDice and an EffectDicePerLevel that WotLK dropped. All four layouts
said 71, and 71 is a perfectly valid index in a 173-field file. No spell's
effect ever equalled CREATE_ITEM, so no spell had a created item or reagents,
and every trade skill window on Classic and TBC came up empty and then stopped
opening at all. It was reported as smelting not working.

THE ORACLE, WHICH NEEDS NO TABLE OF EXPECTED COLUMNS

A row id means the same row in every expansion's copy of a DBC. Spell 133 is
Fireball in all of them, and its effect, the item it creates and the reagent it
consumes are facts about Fireball rather than about a file. So the column an
expansion's layout declares has to agree with the reference file's more than any
column near it does.

Absolute agreement is the wrong test. A private server's DBCs are edited -
Turtle's Spell.dbc is edited enough that the *right* column agrees on four
spells in five - so a threshold tuned to one file is wrong for the next. What
separates right from wrong is the margin: the right column wins its
neighbourhood by a mile, 77% against 11% for the one beside it.

TWO THINGS IT HAS TO DO, BOTH LEARNED BY CANARYING

  * Ignore rows where the reference value is zero. Most of a DBC is zeros, so a
    neighbouring column of zeros agrees on tens of thousands of rows and the
    margin disappears into it.
  * Skip string columns. Their values are offsets into each file's own string
    block, so the same text is a different number in every file and every
    string column reports as disagreeing with everything.

WHAT IT CANNOT SEE

  * A table only one expansion ships. There is nothing to compare against.
  * A column both layouts get wrong in the same way. They agree, and agreement
    is all this measures.
  * Classic, which ships no DBCs of its own here - its layout is compared
    through Turtle's, which uses the same numbers.
  * A field one layout names and the other does not.
"""
import argparse
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REFERENCE = ROOT / "Data" / "db"
OVERLAYS = {
    "tbc": ROOT / "Data/expansions/tbc/overlay/db",
    "turtle": ROOT / "Data/expansions/turtle/overlay/db",
}
REFERENCE_LAYOUT = "wotlk"

# How far either side of the declared column to look. The effect block moved by
# ten; a window this wide catches that and stays cheap.
WINDOW = 12
# Enough rows carrying a non-zero reference value for the comparison to mean
# anything. Below this a single coincidence swings the answer.
MIN_ROWS = 200
# The winner has to beat the declared column by half as much again. Two columns
# of small enums agree by accident on a good fraction of rows; this is well
# outside that.
MARGIN = 1.5
# And it has to agree on a real share of the rows, not merely on more of them
# than a column that agrees on none. Turtle's Talent.dbc and TaxiPathNode.dbc
# are rewritten rather than edited, so no column of theirs corresponds to the
# reference: the declared one agreed on nothing and its neighbour on three rows
# in nine hundred, which is not evidence of anything.
MIN_SHARE = 0.20
# Records read per file. Statistics, not completeness - and Spell.dbc has fifty
# thousand rows.
SAMPLE = 6000


class Dbc:
    """A WDBC file: its records as integers, and which columns hold strings."""

    def __init__(self, path):
        raw = path.read_bytes()
        self.ok = len(raw) >= 20 and raw[:4] == b"WDBC"
        if not self.ok:
            return
        count, self.fields, size, string_size = struct.unpack_from("<IIII", raw, 4)
        body = 20 + count * size
        self.ok = size >= self.fields * 4 and len(raw) >= body
        if not self.ok:
            return
        self.rows = []
        for r in range(min(count, SAMPLE)):
            at = 20 + r * size
            self.rows.append(struct.unpack_from(f"<{self.fields}I", raw, at))
        self.strings = self._string_columns(raw[body:body + string_size])

    def _string_columns(self, block):
        """Columns whose every non-zero value is an offset to a printable string.

        Boundaries only - an offset landing inside a longer string is not the
        start of one, and taking it as such marks numeric columns as text.
        """
        if not block:
            return set()
        starts = {0}
        for i, byte in enumerate(block[:-1]):
            if byte == 0:
                starts.add(i + 1)
        out = set()
        for column in range(self.fields):
            seen = False
            for row in self.rows:
                value = row[column]
                if value == 0:
                    continue
                seen = True
                if value not in starts or value >= len(block):
                    break
                end = block.find(b"\0", value)
                text = block[value:end if end != -1 else len(block)]
                if any(byte < 0x20 and byte not in (9, 10, 13) for byte in text):
                    break
            else:
                if seen:
                    out.add(column)
        return out

    def column(self, index):
        """Row id -> the value at `index`, skipping rows whose value is zero."""
        return {row[0]: row[index] for row in self.rows
                if index < self.fields and row[index] != 0}


def layouts():
    out = {}
    for name in [REFERENCE_LAYOUT] + list(OVERLAYS):
        path = ROOT / f"Data/expansions/{name}/dbc_layouts.json"
        if path.is_file():
            out[name] = json.loads(path.read_text())
    return out


def file_for(directory, table):
    """The table's file, whatever case it is stored in."""
    for path in directory.glob("*.dbc"):
        if path.stem.lower() == table.lower():
            return path
    return None


def scan():
    known = layouts()
    reference_layout = known.get(REFERENCE_LAYOUT, {})
    hits, compared, skipped = [], 0, 0

    for expansion, directory in OVERLAYS.items():
        if not directory.is_dir():
            continue
        theirs = known.get(expansion, {})
        for table, columns in sorted(theirs.items()):
            mine = reference_layout.get(table)
            if not mine:
                continue
            their_path = file_for(directory, table)
            my_path = file_for(REFERENCE, table)
            if not their_path or not my_path:
                continue
            them, me = Dbc(their_path), Dbc(my_path)
            if not them.ok or not me.ok:
                continue

            for field, declared in sorted(columns.items()):
                if field not in mine:
                    continue
                theirs_at, mine_at = int(declared), int(mine[field])
                if theirs_at >= them.fields or mine_at >= me.fields:
                    continue
                if theirs_at in them.strings or mine_at in me.strings:
                    skipped += 1
                    continue

                reference = me.column(mine_at)
                if len(reference) < MIN_ROWS:
                    skipped += 1
                    continue

                best, best_at, declared_score = -1, None, 0
                for candidate in range(max(0, theirs_at - WINDOW),
                                       min(them.fields, theirs_at + WINDOW + 1)):
                    if candidate in them.strings:
                        continue
                    values = them.column(candidate)
                    agree = sum(1 for row, value in reference.items()
                                if values.get(row) == value)
                    if candidate == theirs_at:
                        declared_score = agree
                    if agree > best:
                        best, best_at = agree, candidate
                compared += 1

                if (best_at != theirs_at and best > declared_score * MARGIN
                        and best >= len(reference) * MIN_SHARE):
                    hits.append((expansion, table, field, theirs_at, declared_score,
                                 best_at, best, len(reference)))
    return hits, compared, skipped


def canary():
    """Move a column that is known right and confirm the scan names it."""
    path = ROOT / "Data/expansions/turtle/dbc_layouts.json"
    original = path.read_text()
    try:
        doc = json.loads(original)
        doc["Spell"]["Effect0"] = 71      # the WotLK column, wrong for vanilla
        path.write_text(json.dumps(doc, indent=2) + "\n")
        hits, _, _ = scan()
        named = [h for h in hits if h[1] == "Spell" and h[2] == "Effect0"]
    finally:
        path.write_text(original)
    if not named:
        print("CANARY FAILED: the moved column was not reported")
        return 1
    print(f"canary ok: reported turtle Spell.Effect0, best column "
          f"{named[0][5]}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--canary", action="store_true",
                    help="move a known-good column and check the scan sees it")
    args = ap.parse_args()
    if args.canary:
        return canary()

    if not REFERENCE.is_dir():
        print("no reference DBCs under Data/db, nothing to compare against")
        return 0

    hits, compared, skipped = scan()
    print(f"{compared} column(s) compared against the reference file, "
          f"{skipped} skipped as text or too sparse\n")
    print(f"{len(hits)} column(s) whose neighbour matches the reference better:\n")
    for expansion, table, field, at, score, best_at, best, rows in hits:
        print(f"  {expansion} {table}.{field}")
        print(f"      declared {at}, agrees on {score} of {rows}; "
              f"column {best_at} agrees on {best}")
    if not hits:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
