#!/usr/bin/env python3
"""Two sweeps with their own copy of the same parser.

    tools/tool_duplication_check.py

WHY

`tools/` is a hundred and six scripts and nothing scanned them. Four pairs of
functions were duplicated across them, two character for character:

    binding_bodies      20 lines, in framexml_lua_override_check and
                        framexml_vararg_spread
    resolve_body        19 lines, in framexml_bool_vs_number and
                        framexml_key_returns
    canonicalize         6 lines, in the two opcode checks

A sweep is the worst place to keep a private copy of a parser. When one of
these stops recognising something - a registration spelling, an alias chain -
it does not fail. It reports **fewer** findings, and a sweep that quietly sees
less of its subject goes green for a reason nobody looks at. That is precisely
the failure the sweeps exist to catch, turned on the sweeps themselves.

The three above now live in lua_binding_scan.py and opcode_map_utils.py, beside
ownership_walk.py, which was already shared for the same reason.

WHAT IT LOOKS FOR

Function bodies of four or more code lines appearing in more than one script
under tools/, compared with difflib at 0.80. Comments and blank lines are
stripped, so a pair that differs only in how it is explained still counts as
one function.

WHAT IT CANNOT SEE

A parser reimplemented in a different shape - a regex here and a state machine
there - which is the form this takes once someone knows it is being checked.
"""
import difflib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
THRESHOLD = 0.80
MIN_LINES = 4

# Pairs read and judged as separate on purpose, by function name.
SETTLED = {
    # Every check that walks a directory has a `sources`-shaped helper. They
    # are four lines of glob and differ in what they glob; naming that would be
    # longer than the thing named.
    "sources",
    "main",
}


def bodies(path):
    lines = path.read_text(errors="ignore").split("\n")
    out, i = [], 0
    while i < len(lines):
        m = re.match(r"^def (\w+)\(", lines[i])
        if not m:
            i += 1
            continue
        j = i + 1
        block = []
        while j < len(lines) and (not lines[j].strip() or lines[j].startswith((" ", "\t"))):
            block.append(lines[j])
            j += 1
        code = [re.sub(r"\s+", " ", line).strip() for line in block]
        code = [line for line in code
                if line and not line.startswith("#") and not line.startswith('"')]
        if len(code) >= MIN_LINES and m.group(1) not in SETTLED:
            out.append((m.group(1), tuple(code)))
        i = j
    return out


def main() -> int:
    scripts = sorted(ROOT.glob("tools/*.py"))
    if not scripts:
        print("No tools found; the zero below would mean the scan broke.")
        return 1

    found = [(p.name, name, code) for p in scripts for name, code in bodies(p)]
    hits = []
    for a in range(len(found)):
        file_a, name_a, code_a = found[a]
        for b in range(a + 1, len(found)):
            file_b, name_b, code_b = found[b]
            if file_a == file_b:
                continue
            ratio = difflib.SequenceMatcher(None, code_a, code_b).ratio()
            if ratio >= THRESHOLD:
                hits.append((ratio, len(code_a), file_a, name_a, file_b, name_b))
    hits.sort(reverse=True)

    print(f"{len(scripts)} scripts, {len(found)} functions of {MIN_LINES}+ code lines")
    print(f"{len(SETTLED)} name(s) settled and not reported\n")
    print(f"{len(hits)} function(s) written twice across scripts:")
    if not hits:
        print("  (none)")
    for ratio, length, file_a, name_a, file_b, name_b in hits:
        print(f"  {ratio:.2f} {length:3d} lines  {name_a} ({file_a})  /  {name_b} ({file_b})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
