#!/usr/bin/env python3
"""Sweeps that no longer run at all.

    tools/tools_run_check.py

WHY

sweep_guard runs about thirty of these and fails when one of them gets worse.
Nothing runs the rest, and a sweep nobody runs does not merely go stale - it
breaks, and the breakage is indistinguishable from the sweep being fine.

Two broke on 2026-08-05 from one edit. The candidates tier in
framexml_takeover.cpp became empty, its `for (const char* name : {...})` loop
was removed with it, and two tools parsed that loop with a regex and called
.group(1) on the None they got back. handover_halves_check is wired into
sweep_guard, so ctest caught it inside a minute. framexml_live_stubs is not,
and it sat crashing until someone happened to run it - which is the failure
this exists to make impossible.

WHAT IT DOES

Runs every tool that sweep_guard does not, and reports any that exits non-zero
or raises. It does not read their output: a sweep reporting a bad number is
sweep_guard's business, and a sweep that cannot report at all is this one's.

WHAT IS SKIPPED, AND WHY EACH

  * framexml_source, ownership_walk, opcode_map_utils - libraries, not sweeps.
    Running them does nothing and proves nothing.
  * asset_pipeline_gui, m2_viewer, upscale_textures - tools that open a window
    or process assets. Not checks, and expensive.
  * gen_opcode_registry - a generator; running it writes files.
  * anything already in sweep_guard, which runs it and reads its number.

WHAT IT CANNOT SEE

A sweep that runs, exits zero, and reports a number that means nothing -
framexml_bool_vs_number's nil arm was exactly that on the day it was written.
Only reintroducing the fault finds those.
"""
import re
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


#: Sweeps that look at named implementations for a reason, not by oversight.
#: Widget methods are all named functions - there is no inline form of one to
#: miss - so widget_field_check is complete as it stands.
PARSES_NAMED_ONLY_ON_PURPOSE = {"widget_field_check.py"}


# The game data a sweep reads is not all in the repository: Data/expansions and
# Data/opcodes are tracked, the extracted interface and the DBC files are the
# player's own. A sweep whose input is absent is not a sweep that cannot run.
DATA_INPUTS = {
    "Data/interface": TOOLS.parent / "Data/interface",
    "Data/db": TOOLS.parent / "Data/db",
}


def missing_input(path):
    """The data directory this sweep reads and this checkout does not have."""
    try:
        source = path.read_text(errors="ignore")
    except OSError:
        return None
    for name, directory in DATA_INPUTS.items():
        if name in source and not directory.is_dir():
            return name
    return None


def main():
    guard = (TOOLS / "sweep_guard.py").read_text(errors="ignore")
    checked, broken, skipped = [], [], []
    for path in sorted(TOOLS.glob("*.py")):
        if path.name in SKIP or f'"{path.name}"' in guard:
            continue
        absent = missing_input(path)
        if absent:
            # Not run, and counted separately. On a checkout without the
            # extracted interface these raise FileNotFoundError, which reads
            # exactly like a broken sweep and is not one.
            skipped.append((path.name, absent))
            continue
        checked.append(path.name)
        try:
            # From the repository root, because several sweeps resolve their
            # inputs against the working directory rather than against
            # __file__. Under ctest the working directory is the build tree,
            # and eight of them could not find Data/ from there - which is a
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
    if skipped:
        print(f"{len(skipped)} skipped, their data not in this checkout:\n")
        for name, why in skipped:
            print(f"  {name:38} needs {why}")
        print()
    print(f"{len(broken)} that cannot run:\n")
    for name, why in broken:
        print(f"  {name}")
        print(f"      {why}")
    if not broken:
        print("  (none)")

    # And the half-blind ones. A binding is registered in this codebase two
    # ways - as a named function, {"Name", lua_Name}, or as a lambda written
    # out in the table - and they are the same binding to Lua. Four sweeps
    # matched only the named form and so asked their question of less than half
    # the bindings while reporting a number that read as all of them. Fixing
    # them on 2026-08-06 turned up a raising auction sell tab, a Create All
    # that made one item, a pet sent at the wrong target, and a hundred and
    # fifty-six uncounted stubs. This is here so the fifth sweep to parse a
    # binding cannot be written the same way.
    half = []
    for path in sorted(TOOLS.glob("*.py")):
        if path.name in SKIP or path.name in PARSES_NAMED_ONLY_ON_PURPOSE:
            continue
        src = path.read_text(errors="ignore")
        # The escaped paren, not the words: a sweep that merely mentions
        # lua_State in its prose is not parsing bindings, and element_readiness
        # says "a helper with no lua_State to fire" in a comment.
        if not re.search(r"\\\(lua_State", src):
            continue
        if not re.search(r"->\\?s\*?\s*int|->\s*int", src):
            half.append(path.name)

    print()
    print(f"{len(half)} that read only one of the two binding forms:\n")
    for name in half:
        print(f"  {name}")
        print("      matches {\"Name\", lua_Name} but not the inline lambda, "
              "so it sees under half the bindings")
    if not half:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
