#!/usr/bin/env python3
"""Client capabilities FrameXML cannot reach.

A verb on GameHandler that only this client's own windows ever call is a
capability that disappears the moment its element is handed over. That is how
"cannot apply a sharpening stone" happened: completeItemUseOnItem had exactly
one caller, inventory_screen.cpp, and bags are in the defaults.

Reports verbs called from src/ui/ but never from src/addons/ — the Lua
bindings being the only way FrameXML can reach anything.
"""
import re
import subprocess
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")

# Exclude what is definitely not a verb, rather than listing what is.
#
# This started as a prefix whitelist of forty-odd words and saw 173 of 1495
# names — about a fifth of the surface. InspectUnit and StartDuel were both
# missing bindings that FrameXML calls straight out of the unit menu, and both
# were invisible to it because "inspect" and "duel" were not on the list.
# Any hand-written list of what to look at is a summary, and summaries here
# hide things.
def is_verb(name: str) -> bool:
    if name.endswith("Ref") or name.endswith("_"):
        return False                      # internal accessors
    if name.startswith(("get", "is", "has", "can", "find", "lookup",
                        "format", "num", "should")):
        return False                      # getters and predicates
    # Struct fields declared in the same header read as bare words: armor,
    # angle, active, bar. A verb here is camelCase with an object.
    return any(c.isupper() for c in name)

hdr = (ROOT / "include/game/game_handler.hpp").read_text()
methods = set()
for m in re.finditer(r"\b([a-z][A-Za-z0-9_]*)\s*\(", hdr):
    name = m.group(1)
    if is_verb(name):
        methods.add(name)


def count(name: str, where: str) -> int:
    """Callers of name under a directory, excluding its own declaration."""
    try:
        out = subprocess.run(
            ["grep", "-rn", f"{name}(", str(ROOT / where)],
            capture_output=True, text=True).stdout
    except Exception:
        return 0
    n = 0
    for line in out.splitlines():
        # A definition is not a call.
        if re.search(rf"::\s*{re.escape(name)}\s*\(", line):
            continue
        n += 1
    return n


rows = []
for name in sorted(methods):
    ui = count(name, "src/ui")
    addons = count(name, "src/addons")
    if ui > 0 and addons == 0:
        rows.append((name, ui))

print(f"{len(rows)} verbs this client's own windows can reach and FrameXML cannot:\n")
for name, ui in sorted(rows, key=lambda r: -r[1]):
    out = subprocess.run(
        ["grep", "-rln", f"{name}(", str(ROOT / "src/ui")],
        capture_output=True, text=True).stdout.split()
    files = ", ".join(Path(f).name for f in out[:3])
    print(f"  {name:<38} {ui:>3} call(s)  {files}")
