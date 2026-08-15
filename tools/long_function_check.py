#!/usr/bin/env python3
"""Functions over a length that makes them hard to change safely.

Measured in code lines: blank lines and whole-line comments are excluded, so
dense commenting is not penalised and removing a comment cannot improve the
result.

That measure also removes the need to special-case registration tables, which
are the longest definitions here and are not difficult to read, since every
entry has the same shape and only one is consulted at a time. A table of plain
entries falls below the limit once its comments are excluded. A table whose
entries are inline lambdas stays reported, correctly: registerSystemLuaAPI is
1906 code lines, most of them function bodies.

Run with --canary to check the sweep can still see: it adds a function well
over the limit and fails if that is not reported. A matcher that
has gone blind reads exactly like a clean tree.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "src", ROOT / "include"]

#: How many code lines before a function is worth reporting. Not a style rule -
#: plenty of 200-line functions here are a switch over a wire format and read
#: fine. This is the length past which nobody holds it all at once.
LIMIT = 400

#: A definition opening at column zero, with its body starting on the same line.
DEF = re.compile(
    r'^[A-Za-z_][\w:<>,&*\s]*\b([A-Za-z_]\w*)\s*\([^;]*\)\s*(?:const\s*)?\{\s*$')

def functions_in(text, path):
    """(name, first line, body lines) for each top-level definition."""
    lines = text.split("\n")
    out = []
    depth = 0
    start = None
    name = None
    for i, line in enumerate(lines):
        if start is None:
            m = DEF.match(line)
            if m:
                name, start, depth = m.group(1), i, 0
            else:
                continue
        depth += line.count("{") - line.count("}")
        if depth <= 0 and i > start:
            out.append((name, start + 1, lines[start + 1:i], path))
            start = None
    return out


def code_lines(body):
    """Body lines that are neither blank nor a comment on their own."""
    return sum(1 for l in body
               if l.strip() and not l.strip().startswith("//"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--canary", action="store_true")
    ap.add_argument("--limit", type=int, default=LIMIT)
    args = ap.parse_args()

    found = []
    total = 0
    for root in SOURCES:
        for p in sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.hpp"))):
            for name, line, body, path in functions_in(
                    p.read_text(errors="ignore"), p):
                total += 1
                n = code_lines(body)
                if n > args.limit:
                    found.append((n, name, path, line))

    if args.canary:
        planted = "\n".join(
            f"    if (x == {i}) {{ y += {i}; step{i}(y); }} else {{ y -= {i}; }}"
            for i in range(args.limit + 10))
        fake = f"void woweeCanaryLongFunction(int x) {{\n{planted}\n}}\n"
        for name, line, body, path in functions_in(fake, Path("canary.cpp")):
            total += 1
            n = code_lines(body)
            if n > args.limit:
                found.append((n, name, path, line))

    found.sort(reverse=True)
    print(f"{len(found)} function(s) over {args.limit} code lines, "
          f"of {total} examined:")
    for n, name, path, line in found:
        rel = path if isinstance(path, str) else path.as_posix()
        try:
            rel = Path(rel).relative_to(ROOT).as_posix()
        except ValueError:
            pass
        print(f"  {n:5d}  {name:34s} {rel}:{line}")

    if args.canary:
        if not any(name == "woweeCanaryLongFunction" for _, name, _, _ in found):
            print("CANARY FAILED: the planted long function was not reported")
            return 1
        print("canary ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
