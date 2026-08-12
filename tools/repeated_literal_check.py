#!/usr/bin/env python3
"""Distinctive numbers written out in several files.

    tools/repeated_literal_check.py

WHY THIS FINDS WHAT duplicate_block_check.py CANNOT

The block scan compares twelve normalised lines, so it sees a copy that still
looks like its original and misses one that has been reworded - and the copy
that has drifted furthest is exactly the one least likely to match. A magic
number survives rewording. `24858` is Moonkin Form whether it sits in an array,
a switch or a comparison.

Chasing that one number found five different lists of druid shapeshift forms,
three of them the trio FrameXML's stance bar depends on: the count walked eight
forms, the description and the cast a fixed six in another order. The bar walks
1..count and asks about each index, so all three had to mean the same i and did
not (fixed 2026-08-16). The block scan saw none of it: the five lists are
formatted differently and none is twelve lines.

It also found the ADT tile size in nine files with three separate TILE_SIZE
constants and two files spelling it 533.333 rather than 533.33333, which is
0.021 yards across the map - too small to see, too small to find.

THE FILTER IS THE WHOLE TOOL

Every number gives 170 literals in four or more files and is unreadable: 0.05f
is in 48 of them and means nothing. What is left after the filter below is 66
in three or more files, and the top of that list is all signal.

  * a hex constant of four or more digits - a protocol flag, a magic
  * a float carrying four or more decimals - a derived constant, not a tuning
    value somebody typed
  * an integer of five to nine digits that is not round - an id

WHAT IT CANNOT TELL YOU

Whether the repetition means anything. `86400` is in eight files and is
seconds in a day, which nobody will get wrong and which no name would protect.
`12340` is in seven and is the WotLK build, every use a deliberate fallback. A
hex bit is worse: `0x2000` is the WMO indoor group flag, a movement flag and a
spell attribute, so its nine files are three different facts. Read the hits;
the tool only says where to look.
"""
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

HEX = re.compile(r"\b0[xX][0-9a-fA-F]{4,8}\b")
PRECISE_FLOAT = re.compile(r"\b\d+\.\d{4,}f?\b")
ODD_INT = re.compile(r"\b\d{5,9}\b")
ROUND = re.compile(r"^\d+0{3,}$")

# Values that carry no meaning to share.
IGNORED = {"0xffffffff", "0xffff", "0x0000", "0x00000000", "0xfffffffe",
           "0x7fffffff", "0xff000000", "0x000000ff"}

# The .w* format headers are a written-down file format: every value in them is
# a definition, and a definition repeated is what a format table is.
SKIP_PREFIX = "include/pipeline/wowee_"


def distinctive_literals(line):
    code = line.split("//")[0]
    for match in list(HEX.finditer(code)) + list(PRECISE_FLOAT.finditer(code)) + \
                 list(ODD_INT.finditer(code)):
        literal = match.group(0)
        if literal.lower() in IGNORED:
            continue
        if ODD_INT.fullmatch(literal) and ROUND.match(literal):
            continue
        yield literal


def main():
    sources = sorted(list((ROOT / "src").rglob("*.cpp")) +
                     list((ROOT / "include").rglob("*.hpp")))
    if not sources:
        print("Found no sources. The zero below means the scan broke.")
        return 1

    places = collections.defaultdict(set)
    for path in sources:
        rel = str(path.relative_to(ROOT))
        if rel.startswith(SKIP_PREFIX):
            continue
        for line in path.read_text(errors="ignore").split("\n"):
            for literal in distinctive_literals(line):
                places[literal].add(rel)

    spread = sorted(((lit, files) for lit, files in places.items() if len(files) >= 3),
                    key=lambda kv: (-len(kv[1]), kv[0]))

    print(f"{len(places)} distinctive literal(s)")
    print(f"{len(spread)} written out in three or more files:\n")
    for literal, files in spread[:25]:
        shown = ", ".join(sorted(files)[:3])
        more = f", +{len(files) - 3} more" if len(files) > 3 else ""
        print(f"  {literal:14} {len(files):2}  {shown}{more}")
    if not spread:
        print("  (none)")
    print("\nA hit is a question. Read what each use means before naming it: the "
          "\nsame bit is a different fact in a different subsystem.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
