#!/usr/bin/env python3
"""Sweeps that no longer run at all.

    tools/tools_run_check.py

WHY

sweep_guard runs about thirty of these and fails when one of them gets worse.
Nothing runs the rest, and a sweep nobody runs does not merely go stale — it
breaks, and the breakage is indistinguishable from the sweep being fine.

Two broke on 2026-08-05 from one edit. The candidates tier in
framexml_takeover.cpp became empty, its `for (const char* name : {...})` loop
was removed with it, and two tools parsed that loop with a regex and called
.group(1) on the None they got back. handover_halves_check is wired into
sweep_guard, so ctest caught it inside a minute. framexml_live_stubs is not,
and it sat crashing until someone happened to run it — which is the failure
this exists to make impossible.

WHAT IT DOES

Runs every tool that sweep_guard does not, and reports any that exits non-zero
or raises. It does not read their output: a sweep reporting a bad number is
sweep_guard's business, and a sweep that cannot report at all is this one's.

WHAT IS SKIPPED, AND WHY EACH

  * framexml_source, ownership_walk, opcode_map_utils — libraries, not sweeps.
    Running them does nothing and proves nothing.
  * asset_pipeline_gui, m2_viewer, upscale_textures — tools that open a window
    or process assets. Not checks, and expensive.
  * gen_opcode_registry — a generator; running it writes files.
  * anything already in sweep_guard, which runs it and reads its number.

WHAT IT CANNOT SEE

A sweep that runs, exits zero, and reports a number that means nothing —
framexml_bool_vs_number's nil arm was exactly that on the day it was written.
Only reintroducing the fault finds those.
"""
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent

#: Not sweeps: libraries, generators, and things that open a window.
SKIP = {
    "framexml_source.py", "ownership_walk.py", "opcode_map_utils.py",
    "asset_pipeline_gui.py", "m2_viewer.py", "upscale_textures.py",
    "gen_opcode_registry.py", "tools_run_check.py", "sweep_guard.py",
}


def main():
    guard = (TOOLS / "sweep_guard.py").read_text(errors="ignore")
    checked, broken = [], []
    for path in sorted(TOOLS.glob("*.py")):
        if path.name in SKIP or f'"{path.name}"' in guard:
            continue
        checked.append(path.name)
        try:
            # From the repository root, because several sweeps resolve their
            # inputs against the working directory rather than against
            # __file__. Under ctest the working directory is the build tree,
            # and eight of them could not find Data/ from there — which is a
            # fact about how they are invoked, not about whether they work.
            done = subprocess.run([sys.executable, str(path)],
                                  cwd=str(TOOLS.parent),
                                  capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            broken.append((path.name, "timed out"))
            continue
        if done.returncode != 0:
            last = [ln for ln in done.stderr.strip().splitlines() if ln.strip()]
            broken.append((path.name, last[-1] if last else
                           f"exit {done.returncode} with nothing on stderr"))

    print(f"{len(checked)} sweep(s) run that sweep_guard does not\n")
    print(f"{len(broken)} that cannot run:\n")
    for name, why in broken:
        print(f"  {name}")
        print(f"      {why}")
    if not broken:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
