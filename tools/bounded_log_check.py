#!/usr/bin/env python3
"""A diagnostic meant to fire a few times, whose counter can never advance.

    tools/bounded_log_check.py            # report
    tools/bounded_log_check.py --canary   # prove the scan can see one

THE SHAPE

A log line in a per-frame path is bounded by a static counter, and the counter
is advanced under a *narrower* condition than the one that lets the line
through:

    static int diagFramesRemaining = 1;
    if (diagFramesRemaining > 0 && weighted.size() <= 3) {
        LOG_INFO("Light volume ", ...);
        if (weighted.size() == 3) --diagFramesRemaining;   // may never happen
    }

Every reading of that says "once". It logged for a whole session: almost
nowhere has three light volumes in range, so the inner test never held, the
counter never reached zero, and the line was written every frame - 534 of one
log's 3384 lines, four minutes in.

WHY IT IS WORTH A SWEEP RATHER THAN A READ

It is invisible three ways over. The guard is right there and reads as
correct; the count is small so nobody expects volume; and it only costs
anything when the log level is open, so it is silent in every normal run and
loud in exactly the run someone is doing to diagnose something else. This one
was found because it drowned the session it was meant to help - the log was
written on the main thread once per frame, and only while the player moved,
because the logger folds a line identical to the one before it and walking is
what made each line different. It read as the world changing brightness with
movement.

WHAT IT LOOKS FOR

A static integer declared in a function, tested in an `if` whose body contains
a log call, where every update to that counter inside the body sits under a
further condition. A counter advanced unconditionally beside the log is the
correct shape and is not reported.

WHAT IT CANNOT SEE

  * A bound that is not a static counter - a time stamp, a member, a set of
    things already reported.
  * A counter advanced correctly whose bound is simply too large.
  * A log with no bound at all in a path that runs every frame. That is a
    different scan and a much noisier one: most such calls are in setup
    functions that only look like hot paths from their names.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ["src", "include"]
SUFFIXES = {".cpp", ".hpp", ".h", ".cc"}
LOG_CALL = re.compile(r"\bLOG_(INFO|WARNING|ERROR|DEBUG)\s*\(")
STATIC_COUNTER = re.compile(
    r"\bstatic\s+(?:const\s+)?(?:int|unsigned|uint\d+_t|size_t|long)\s+(\w+)\s*=")


def sources():
    for base in SOURCES:
        for path in sorted((ROOT / base).rglob("*")):
            if path.suffix in SUFFIXES and path.is_file():
                yield path


def block_at(lines, start):
    """The braced block opening on `start`, as (first, last) line indices."""
    depth = 0
    for i in range(start, len(lines)):
        depth += lines[i].count("{") - lines[i].count("}")
        if depth <= 0 and i > start:
            return start, i
        if depth < 0:
            return start, i
    return start, len(lines) - 1


def scan():
    hits = []
    for path in sources():
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        counters = {}
        for no, line in enumerate(lines):
            m = STATIC_COUNTER.search(line)
            if m:
                counters[m.group(1)] = no
        if not counters:
            continue

        for no, line in enumerate(lines):
            if not line.lstrip().startswith("if"):
                continue
            named = [c for c in counters if re.search(r"\b" + re.escape(c) + r"\b", line)]
            if not named:
                continue
            first, last = block_at(lines, no)
            body = lines[first:last + 1]
            if not any(LOG_CALL.search(b) for b in body):
                continue

            for counter in named:
                # A guard that advances the counter itself always advances it,
                # whatever the body then does. `if (++seen <= 10) { ... if
                # (seen == 10) LOG("no more of these"); }` is that, and is
                # right.
                if re.search(r"(\+\+|--)\s*" + re.escape(counter) + r"\b"
                             r"|\b" + re.escape(counter) + r"\s*(\+\+|--|\+=|-=)", line):
                    continue
                # Not the guard line itself. `if (++seen <= 5)` advances the
                # counter in the condition, which is the correct idiom and by
                # far the commonest one here - thirteen of them, and reporting
                # those would have been the whole output.
                updates = [(i, b) for i, b in enumerate(body)
                           if i > 0 and
                           re.search(r"(\+\+|--)\s*" + re.escape(counter) + r"\b"
                                     r"|\b" + re.escape(counter) + r"\s*(\+\+|--|\+=|-=|=)", b)]
                if not updates:
                    continue           # advanced elsewhere; not this shape
                # Unconditional means the update's own line is not a branch and
                # sits at the body's own indentation rather than inside one.
                body_indent = len(body[1]) - len(body[1].lstrip()) if len(body) > 1 else 0
                conditional = []
                for i, text in updates:
                    indent = len(text) - len(text.lstrip())
                    inline_if = re.match(r"\s*(if|else)\b", text)
                    if inline_if or indent > body_indent:
                        conditional.append((i, text.strip()))
                if conditional and len(conditional) == len(updates):
                    hits.append((path.relative_to(ROOT).as_posix(), no + 1,
                                 counter, line.strip(), conditional[0][1]))
    return hits


CANARY = """void tick(int n) {
    static int diagFramesRemaining = 1;
    if (diagFramesRemaining > 0 && n <= 3) {
        LOG_INFO("planted ", n);
        if (n == 3) --diagFramesRemaining;
    }
}
"""


def canary():
    planted = ROOT / "src" / "bounded_log_canary.cpp"
    planted.write_text(CANARY, encoding="utf-8")
    try:
        named = [h for h in scan() if h[0].endswith("bounded_log_canary.cpp")]
    finally:
        planted.unlink()
    if not named:
        print("CANARY FAILED: the planted counter was not reported")
        return 1
    print(f"canary ok: reported {named[0][2]} at line {named[0][1]}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--canary", action="store_true",
                    help="plant a counter that cannot advance and check it is seen")
    args = ap.parse_args()
    if args.canary:
        return canary()

    hits = scan()
    print(f"{len(hits)} bounded log(s) whose counter only advances conditionally:\n")
    for rel, no, counter, guard, update in hits:
        print(f"  {rel}:{no}  {counter}")
        print(f"      guard:  {guard}")
        print(f"      update: {update}")
    if not hits:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
