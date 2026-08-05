#!/usr/bin/env python3
"""DBC field indices that name a column the file does not have.

    tools/dbc_layout_check.py

Every DBC read in this client goes through a named index out of
Data/expansions/<x>/dbc_layouts.json — `(*layout)["IconID"]` and the like. The
name makes it look checked. Nothing checks it: the loader is handed a number,
and a number past the end of a record, or pointed at the wrong column, comes
back as zero or as whatever bytes are there. What that looks like on screen is
an icon that does not load, a stat that reads zero, a beard that is not drawn —
never an error.

WHAT IT LOOKS FOR

An index at or past the file's own field count. That is the unambiguous half of
the problem and needs no judgement: the column does not exist, so the read
cannot be right. It found CharacterFacialHairStyles.Geoset200 = 8 on a file with
eight fields, and the two columns beside it held 0xCCCCCCCC — the facial hair
geosets were being read from padding, so no beard, moustache, sideburns or
draenei tendrils were drawn at all.

WHAT IT CANNOT SEE

An index that is in range and wrong, which is the larger half. Field 6 of that
same file exists and reads zero forever. Telling that apart from a genuine zero
needs to know what the column means — which is what the sibling detectors do by
probing values, and what a person does by checking a row they recognise.

It also only reads the DBCs present under Data/db. A layout for a file this
install does not carry is skipped and counted, not assumed correct.

ONE EXPANSION AT A TIME

There is one set of DBCs here and four sets of layouts, and a layout only means
anything against the files it was written for — Faction.Name is field 19 in the
Classic file and something else entirely in the WotLK one. Checking all four
against one set of files reported three expansions' worth of noise on the first
run, and the noise looked exactly like findings.

So the layouts are scored against the files and the best-fitting one is the only
one checked. The score is content, not range: a field called Name, Title or
Description has to hold an offset into the file's own string block, and the
layout written for this data is the one whose string fields land there. On the
WotLK files here that separates the four cleanly — the Classic layout puts
Faction.Name at field 19, where the WotLK file keeps something that is not an
offset at all.

Range alone does not separate them. All four layouts have exactly one
out-of-range field, so scoring on that picked whichever sorted first.

The fit does not separate everything either, and the run prints every score so
that is visible rather than implied: TBC and WotLK agree on the string fields
checked here and both reach 100%, while Classic and Turtle fall behind. Where
two tie, the violations reported are the same ones — the layouts differ in the
columns this cannot score, not in the ones it can.
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DB = ROOT / "Data/db"
EXPANSIONS = ROOT / "Data/expansions"


def field_count(path):
    """The number of columns the file itself declares, or None if not a DBC."""
    with path.open("rb") as fh:
        head = fh.read(20)
    if len(head) < 20 or head[:4] != b"WDBC":
        return None
    _records, fields, _size, _strings = struct.unpack_from("<IIII", head, 4)
    return fields


#: Fields whose value must be an offset into the string block.
STRINGY = re.compile(r"(Name|Title|Description|SubName|Text)$")


def records(path, count):
    """Every record as a tuple of uint32, plus the string block's size."""
    data = path.read_bytes()
    rc, fc, rs, ss = struct.unpack_from("<IIII", data, 4)
    rows = [struct.unpack_from(f"<{fc}I", data, 20 + i * rs) for i in range(rc)]
    return rows, ss


def dbc_for(name):
    """Data/db is lower case on this install and the layouts are CamelCase."""
    for candidate in (name, name.lower(),
                      re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()):
        path = DB / f"{candidate}.dbc"
        if path.exists():
            return path
    return None


def main():
    if not DB.is_dir():
        print(f"no DBCs at {DB}")
        return 0

    def string_fit(layout_file):
        """How often this layout's string-named fields hold string offsets."""
        good = total = 0
        for dbc_name, fields in json.loads(layout_file.read_text()).items():
            path = dbc_for(dbc_name)
            if not path:
                continue
            count = field_count(path)
            if count is None:
                continue
            named = [i for f, i in fields.items()
                     if isinstance(i, int) and i < count and STRINGY.search(f)]
            if not named:
                continue
            rows, strings = records(path, count)
            for index in named:
                values = [r[index] for r in rows if r[index] != 0]
                if not values:
                    continue
                total += len(values)
                good += sum(1 for v in values if 0 < v < strings)
        return (good / total) if total else 0.0

    def violations(layout_file):
        """Out-of-range fields for one expansion's layouts, and how many fit."""
        found, checked, absent = [], 0, 0
        for dbc_name, fields in sorted(json.loads(layout_file.read_text()).items()):
            path = dbc_for(dbc_name)
            if not path:
                absent += 1
                continue
            count = field_count(path)
            if count is None:
                continue
            checked += 1
            for field, index in sorted(fields.items()):
                if isinstance(index, int) and index >= count:
                    found.append((dbc_name, field, index, count))
        return found, checked, absent

    scored = []
    for layout_file in sorted(EXPANSIONS.glob("*/dbc_layouts.json")):
        found, checked, absent = violations(layout_file)
        scored.append((string_fit(layout_file), layout_file.parent.name,
                       found, checked, absent))
    if not scored:
        print("no layouts found")
        return 0

    scored.sort(reverse=True)
    fit, expansion, rows, checked, absent = scored[0]
    others = ", ".join(f"{name} {f:.0%}" for f, name, *_ in scored[1:])
    print(f"data under Data/db best matches the {expansion} layouts — "
          f"{fit:.0%} of its string fields land in the string block "
          f"({checked} checked, {absent} with no file here)")
    print(f"the rest fit worse and are not this data's: {others}\n")
    print(f"{len(rows)} field(s) naming a column the file does not have:\n")
    for dbc, field, index, count in rows:
        print(f"  {dbc}.{field} = {index}, "
              f"but the file has {count} fields (0-{count - 1})")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
