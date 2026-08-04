#!/usr/bin/env python3
"""Stubbed bindings that a handed-over element actually calls.

The readiness report counts a name as answered once it is bound, and a stub is
bound — so `HasPetSpells -> lua_ReturnNil` read as clean while it was hiding
the entire pet spell book. This asks the narrower question: which of the stubs
are on a code path that is on screen right now.

Being listed is not being wrong. Plenty of stubs are correct: the feature is
genuinely absent (vehicles, voice chat) or FrameXML tolerates the empty answer.
It is a reading list, ordered by how central the file is.
"""
import re
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
XML = ROOT / "Data/interface"

STUBS = {"lua_ReturnNil", "lua_ReturnZero", "lua_ReturnFalse", "lua_ReturnNothing",
         "lua_ReturnTrue", "lua_ReturnEmptyString", "lua_ContainerNoOp",
         "lua_ContainerFalse", "lua_NoOp", "lua_Noop"}

# The FrameXML files that belong to elements handed over by default.
OWNED = {
    "unitframe.lua": "playerframe/targetframe", "targetframe.lua": "targetframe",
    "targetframe.xml": "targetframe", "playerframe.xml": "playerframe",
    "minimap.lua": "minimap", "minimap.xml": "minimap",
    "mainmenubar.lua": "mainmenubar", "mainmenubar.xml": "mainmenubar",
    "actionbutton.lua": "mainmenubar", "actionbutton.xml": "mainmenubar",
    "mainmenubarbagbuttons.lua": "bagbar", "bonusactionbarframe.lua": "mainmenubar",
    "characterframe.lua": "characterframe", "characterframe.xml": "characterframe",
    "paperdollframe.lua": "characterframe", "paperdollframe.xml": "characterframe",
    "containerframe.lua": "bags", "containerframe.xml": "bags",
    "castingbarframe.lua": "castbar", "castingbarframe.xml": "castbar",
    "spellbookframe.lua": "spellbook", "spellbookframe.xml": "spellbook",
    "petframe.lua": "petframe", "petframe.xml": "petframe",
    "petactionbarframe.lua": "petframe",
    "focusframe.lua": "focusframe", "buffframe.lua": "buffs",
    "buffframe.xml": "buffs", "durabilityframe.lua": "durability",
    "zonetext.lua": "zonetext", "staticpopup.lua": "dialogs",
    "uiparent.lua": "dialogs",
}

bound = {}
for f in (ROOT / "src/addons").glob("*.cpp"):
    s = f.read_text(errors="ignore")
    for m in re.finditer(r'\{"([A-Za-z0-9_]+)",\s*(?:&)?\s*(lua_[A-Za-z0-9_]+)\}', s):
        bound[m.group(1)] = m.group(2)

stubbed = {n for n, impl in bound.items() if impl in STUBS}

hits = {}
for fname, element in OWNED.items():
    matches = list(XML.rglob(fname))
    if not matches:
        continue
    text = matches[0].read_text(errors="ignore")
    for name in stubbed:
        if re.search(rf"\b{re.escape(name)}\s*\(", text):
            hits.setdefault(name, []).append(f"{element}:{fname}")

print(f"{len(bound)} bindings, {len(stubbed)} of them stubs, "
      f"{len(hits)} reached from a handed-over element\n")
for name in sorted(hits, key=lambda n: (-len(hits[n]), n)):
    print(f"  {name:<34} {bound[name]:<20} {', '.join(sorted(set(hits[name])))}")
