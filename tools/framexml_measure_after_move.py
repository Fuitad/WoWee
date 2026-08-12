#!/usr/bin/env python3
"""Where the interface moves something and measures it in the same breath.

A rect used to be written only by the once-a-frame layout pass, so any frame
anchored or resized inside a handler measured as though it had never been
placed -- its own size sitting at the origin. Every site below read that.

The fix resolves a rect when it is asked for, so these all answer now. The
sweep stays because it names what depended on it: if rect resolution is ever
made lazy again, this is the list that breaks, and none of it breaks loudly.
"""
import re, sys, pathlib, collections

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "Data/interface")
MOVE = re.compile(r'\b([A-Za-z_][\w.\[\]"\']*)\s*:\s*(SetPoint|SetAllPoints|SetWidth|SetHeight|SetSize|ClearAllPoints)\s*\(')
MEASURE = re.compile(r'\b([A-Za-z_][\w.\[\]"\']*)\s*:\s*(GetTop|GetBottom|GetLeft|GetRight|GetWidth|GetHeight|GetCenter|GetRect)\s*\(')
WINDOW = 12   # lines; a handler that moves and measures does it close together

def scan(path):
    hits = []
    lines = path.read_text(errors="ignore").splitlines()
    moved = {}                      # name -> line it was last moved on
    for n, line in enumerate(lines):
        if line.lstrip().startswith("--"):
            continue
        for m in MOVE.finditer(line):
            moved[m.group(1)] = n
        for m in MEASURE.finditer(line):
            name = m.group(1)
            src = moved.get(name)
            if src is not None and 0 <= n - src <= WINDOW:
                hits.append((n + 1, name, m.group(2), lines[n].strip()[:70]))
    return hits

total = collections.Counter()
rows = []
for p in sorted(list(ROOT.rglob("*.lua"))):
    for line, name, how, text in scan(p):
        total[p.name] += 1
        rows.append((p.name, line, name, how, text))

print(f"{len(rows)} places measure something they just moved, in {len(total)} files\n")
for f, c in total.most_common(12):
    print(f"  {c:>3}  {f}")
print()
for f, line, name, how, text in rows[:10]:
    print(f"  {f}:{line}  {name}:{how}()")
