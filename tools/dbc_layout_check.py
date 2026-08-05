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

    rows, checked, absent = [], 0, 0
    for layout_file in sorted(EXPANSIONS.glob("*/dbc_layouts.json")):
        expansion = layout_file.parent.name
        layouts = json.loads(layout_file.read_text())
        for dbc_name, fields in sorted(layouts.items()):
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
                    rows.append((expansion, dbc_name, field, index, count))

    print(f"{checked} layout(s) checked against a file, {absent} with no file here\n")
    print(f"{len(rows)} field(s) naming a column the file does not have:\n")
    for expansion, dbc, field, index, count in rows:
        print(f"  {expansion}/{dbc}.{field} = {index}, "
              f"but the file has {count} fields (0-{count - 1})")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
