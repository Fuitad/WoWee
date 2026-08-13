#!/usr/bin/env python3
"""A line copied down a run of axes with one axis left behind.

    tools/copy_paste_axis_check.py            # report
    tools/copy_paste_axis_check.py --canary   # prove the scan can see one

WHAT IT LOOKS FOR

Three lines written by copying the first one twice and editing the copies:

    out.x = in.x * s;
    out.y = in.y * s;
    out.z = in.y * s;      <- the second edit did not finish

Nothing raises, no test fails unless one was written for the third axis
specifically, and reading the run confirms nothing: every line is well formed
and each one on its own is exactly what it should be. What gives it away is
that the axes stop agreeing *down the column* - two of the three columns run
x,y,z and the third runs x,y,y.

So the scan does not read a line. It reads a run of lines that are the same
line but for their axis tokens, lines them up as columns, and asks whether
every column that varies varies the same way. One that does not is the report.

THE FAMILIES

Anything written out once per member: the vector axes, the colour channels,
the rect edges, the size pair, min/max, the numbered members of a small tuple.
A run is only compared within one family, because `.x` and `.r` mean different
things and a skeleton that erased both would pair lines that are unrelated.

WHY THE SKELETON HAS TO BE EXACT

Two lines belong to one run only when they are the same text with the axis
tokens blanked. That is deliberately strict: relaxing it to "similar" pairs the
lines of adjacent statements, and every such pair reports, because two
unrelated statements have no reason to agree about anything. The strictness is
what makes a hit worth reading - the lines were literally copied.

WHAT IT CANNOT SEE

A run whose lines were reflowed differently, a run of two where the second
line's mistake is to repeat the first axis in *every* position (nothing varies,
so nothing disagrees), and any axis mistake made inside a loop over the axes
rather than written out. It also cannot tell a deliberate asymmetry from a
mistake: projecting onto a plane really does write `.y` twice. Those are named
in SETTLED below with the reason.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Members written out one line per axis. Grouped, because a run is only a run
# within one family - a skeleton that blanked `.x` and `.r` alike would line up
# lines that have nothing to do with each other.
FAMILIES = [
    ("xyzw", ["x", "y", "z", "w"]),
    ("rgba", ["r", "g", "b", "a"]),
    ("edges", ["left", "right", "top", "bottom"]),
    ("size", ["width", "height", "Width", "Height"]),
    ("bound", ["min", "max", "Min", "Max"]),
    ("pair", ["first", "second"]),
]

SOURCES = ["src", "include"]
SUFFIXES = {".cpp", ".hpp", ".h", ".cc"}

# Judged runs that are asymmetric on purpose, keyed by file and the skeleton.
SETTLED = {}


def sources():
    for base in SOURCES:
        for path in sorted((ROOT / base).rglob("*")):
            if path.suffix in SUFFIXES and path.is_file():
                yield path


def skeletonise(line, members):
    """Blank every axis token, returning the skeleton and the tokens in order.

    A token only counts where it stands alone as an identifier, so `maxHp`
    does not read as `max` and `xOffset` does not read as `x`.
    """
    tokens = []

    def take(m):
        tokens.append(m.group(0))
        return "@"

    # `std::min` is not the member `min`. Left in, the two read as one family
    # and an intersection - `lo = std::max(a.min, b.min); hi = std::min(a.max,
    # b.max)` - comes out as three columns agreeing and one disagreeing, which
    # is exactly the report shape and exactly right code.
    pattern = r"(?<!:)\b(?:" + "|".join(re.escape(m) for m in members) + r")\b"
    return re.sub(pattern, take, line), tokens


def runs_in(lines, members):
    """Consecutive lines sharing a skeleton and an axis-token count."""
    run = []
    for no, raw in lines:
        line = raw.strip()
        if not line or line.startswith("//"):
            if run:
                yield run
                run = []
            continue
        skel, tokens = skeletonise(line, members)
        if not tokens:
            if run:
                yield run
                run = []
            continue
        if run and run[0][1] == skel and len(run[0][2]) == len(tokens):
            run.append((no, skel, tokens, line))
        else:
            if run:
                yield run
            run = [(no, skel, tokens, line)]
    if run:
        yield run


def disagreeing_column(run):
    """The column whose sequence disagrees with how the others vary.

    Columns that hold the same token all the way down are constants - a scale
    factor named `max` on every line - and say nothing. Among the columns that
    do vary, every one has to vary the same way; the odd one out is the report.
    """
    width = len(run[0][2])
    columns = [[entry[2][j] for entry in run] for j in range(width)]
    varying = [j for j, col in enumerate(columns) if len(set(col)) > 1]
    if not varying:
        return None

    # The line was copied down and one column never edited, so it still holds
    # the axis the first line gave it while every other column walks on. A
    # column holding some *other* constant is a scale or a bound that belongs
    # to no axis, and says nothing.
    walked = {tuple(columns[j]) for j in varying}
    if len(walked) == 1:
        first = columns[varying[0]][0]
        for j, col in enumerate(columns):
            if j not in varying and col[0] == first:
                return j, col, columns[varying[0]]

    if len(run) < 3 or len(varying) < 2:
        # Two lines give every varying column a two-element sequence, and one
        # column reading y,x against x,y is a cross product as often as a
        # mistake. A majority needs three.
        return None
    tally = {}
    for j in varying:
        tally.setdefault(tuple(columns[j]), []).append(j)
    if len(tally) < 2:
        return None
    ranked = sorted(tally.items(), key=lambda kv: -len(kv[1]))
    if len(ranked[0][1]) > len(ranked[1][1]):
        return ranked[1][1][0], columns[ranked[1][1][0]], ranked[0][0]
    if len(tally) == 2 and len(ranked[0][1]) == len(ranked[1][1]):
        # Two columns varying two ways, so neither is the majority. The run
        # walks the axes once each, so the column that repeats an axis is the
        # one that stopped following - `x,y,y` against `x,y,z`. Where both
        # repeat or neither does it is an ordinary pairing (`a.x = b.y;
        # a.y = b.x`) and there is nothing to say.
        (seq_a, cols_a), (seq_b, cols_b) = ranked
        repeats_a = len(set(seq_a)) < len(seq_a)
        repeats_b = len(set(seq_b)) < len(seq_b)
        if repeats_a and not repeats_b:
            return cols_a[0], list(seq_a), seq_b
        if repeats_b and not repeats_a:
            return cols_b[0], list(seq_b), seq_a
    return None


def scan():
    hits = []
    for path in sources():
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        lines = list(enumerate(text.splitlines(), 1))
        rel = path.relative_to(ROOT).as_posix()
        for name, members in FAMILIES:
            for run in runs_in(lines, members):
                if len(run) < 2:
                    continue
                found = disagreeing_column(run)
                if not found:
                    continue
                if SETTLED.get(rel) == run[0][1]:
                    continue
                column, got, expected = found
                hits.append((rel, name, run, column, got, expected))
    return hits


CANARIES = {
    "a column that stops following the rest": (
        "void f(const Vec3& in, Vec3& out, float s) {\n"
        "    out.x = in.x * s;\n"
        "    out.y = in.y * s;\n"
        "    out.z = in.y * s;\n"
        "}\n"),
    "a column left behind on the first axis": (
        "void g(const Vec2& in, Vec2& out) {\n"
        "    out.x = in.x;\n"
        "    out.y = in.x;\n"
        "}\n"),
    "a size pair left behind": (
        "void h(const Rect& r, Rect& o) {\n"
        "    o.width = r.width;\n"
        "    o.height = r.width;\n"
        "}\n"),
}


def canary():
    """Plant each shape the scan claims to see and confirm it names them.

    A zero from this scan is only worth anything if the shapes it reports are
    reachable, and the two rules - a majority to disagree with, and a column
    still holding the first line's axis - are reached by different runs.
    """
    planted = ROOT / "src" / "copy_paste_axis_canary.cpp"
    failed = []
    for what, body in CANARIES.items():
        planted.write_text(body, encoding="utf-8")
        try:
            named = [h for h in scan()
                     if h[0].endswith("copy_paste_axis_canary.cpp")]
        finally:
            planted.unlink()
        print(f"{'ok  ' if named else 'FAIL'}  {what}")
        if not named:
            failed.append(what)
    if failed:
        print(f"\nCANARY FAILED: {len(failed)} planted shape(s) not reported")
        return 1
    print("\ncanary ok: every planted shape was reported")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--canary", action="store_true",
                    help="plant a known bad run and check the scan sees it")
    args = ap.parse_args()
    if args.canary:
        return canary()

    hits = scan()
    print(f"{len(hits)} run(s) where one axis disagrees with the rest\n")
    for rel, family, run, column, got, expected in hits:
        print(f"{rel}:{run[0][0]}  [{family}] column {column} "
              f"reads {'/'.join(got)} where the rest read {'/'.join(expected)}")
        for no, _, _, line in run:
            print(f"    {no}: {line}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
