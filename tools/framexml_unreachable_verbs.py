#!/usr/bin/env python3
"""Client capabilities FrameXML cannot reach.

A verb on GameHandler that only this client's own windows ever call is a
capability that disappears the moment its element is handed over. That is how
"cannot apply a sharpening stone" happened: completeItemUseOnItem had exactly
one caller, inventory_screen.cpp, and bags are in the defaults.

Reports verbs called from src/ui/ but never from src/addons/ — the Lua
bindings being the only way FrameXML can reach anything.

WHAT IT FOUND, AND THE SIXTY-SIX LEFT (2026-08-05)

The find was the slash-command registry. Twenty-one rows pointed at
src/ui/chat/commands, which is not a window at all — it is this client's
command set, dispatched from its own chat input and from nowhere else. Handing
chat over made seventy-one commands untypeable and nothing said so, because
FrameXML answers an unknown command with "Type /help for a listing of
commands". They are bridged into SlashCmdList now, and those rows are counted
separately above rather than reported.

The sixty-six left divide cleanly, and none is a gap:

  * **Not verbs at all (17)** — set*Callback, consume*, clear*Pending. Wiring
    between this client's own halves. The is_verb filter cannot tell a callback
    setter from an action because both are camelCase with an object.
  * **The other direction (1)** — runInterfaceCommand, and it is the top row
    with twenty-seven callers. This is the client calling *into* FrameXML, so
    finding it absent from src/addons is exactly right.
  * **The glue screen** — requestCharacterList, selectCharacter. Character
    select runs before FrameXML exists.
  * **The 3D world** — interactWithGameObject, interactWithNpc, lootTarget,
    tabTarget, setMouseoverGuid. Driven by clicks in the world and by
    keybindings, not by anything the interface calls.
  * **Has a bound equivalent** — inspectTarget is InspectUnit, dismissPet is
    PetDismiss, startCraftQueue is DoTradeSkill, sendAlterAppearance is
    ApplyBarberShopStyle, stablePet is ClickStablePet. All five are bound; the
    client method simply is not the route FrameXML takes.
  * **Window plumbing** — openMailCompose, setSelectedMailIndex,
    setVendorCanRepair, showDeathDialog and friends. State belonging to this
    client's own window, which FrameXML replaces whole rather than driving.

THE QUESTION WORTH RE-ASKING

Not "what did this client's window draw" — the handover tables answer that —
but **what was it the only route to**. A verb reachable from one panel and
nowhere else disappears with that panel, and nothing in the takeover machinery
is watching for it.
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


# src/ui/chat/commands is reachable again, and not through a binding.
#
# Those files are the slash-command registry, and since 2026-08-05 a bootstrap
# chunk in addon_manager registers every one of its aliases into SlashCmdList
# after FrameXML loads. So a verb whose only callers are command classes can be
# typed from FrameXML's edit box, and reporting it as unreachable is wrong —
# it was right for exactly as long as the registry had no bridge, which is how
# the bridge came to be written: twenty-one of these rows were the clue.
#
# Counted rather than filtered silently, because a zero nobody can see the
# shape of is worth less than a number with a reason beside it.
rows = []
bridged = 0
for name in sorted(methods):
    ui = count(name, "src/ui")
    addons = count(name, "src/addons")
    if ui == 0 or addons > 0:
        continue
    only_commands = count(name, "src/ui/chat/commands") == ui
    if only_commands:
        bridged += 1
        continue
    rows.append((name, ui))

print(f"{bridged} reachable only as slash commands, which the SlashCmdList "
      f"bridge carries\n")
print(f"{len(rows)} verbs this client's own windows can reach and FrameXML cannot:\n")
for name, ui in sorted(rows, key=lambda r: -r[1]):
    out = subprocess.run(
        ["grep", "-rln", f"{name}(", str(ROOT / "src/ui")],
        capture_output=True, text=True).stdout.split()
    files = ", ".join(Path(f).name for f in out[:3])
    print(f"  {name:<38} {ui:>3} call(s)  {files}")
