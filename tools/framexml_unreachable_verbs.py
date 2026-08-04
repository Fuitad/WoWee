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

# Verb prefixes: things that *do* something, as opposed to getters the
# replacement interface has its own answers for.
VERBS = ("send", "complete", "begin", "use", "accept", "confirm", "request",
         "cancel", "toggle", "buy", "sell", "split", "equip", "unequip",
         "loot", "interact", "cast", "start", "stop", "apply", "choose",
         "select", "decline", "abandon", "submit", "invite", "kick", "leave",
         "join", "learn", "train", "repair", "enchant", "craft", "open",
         "close", "pick", "put", "swap", "destroy", "delete", "report")

hdr = (ROOT / "include/game/game_handler.hpp").read_text()
methods = set()
for m in re.finditer(r"\b([a-z][A-Za-z0-9_]*)\s*\(", hdr):
    name = m.group(1)
    if name.startswith(VERBS):
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
