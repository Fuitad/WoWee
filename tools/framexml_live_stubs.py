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
# Derived from the takeover file rather than written out: the elements handed
# over by default plus the candidates tier, mapped to files through the
# readiness tool's table, plus the shared files every panel goes through.
#
# It was a hand-made list and twice that was the bug — it named
# paperdollframe.lua as "the character sheet" and missed the other four
# subframes, and it covered only the defaults while the candidates tier was
# what was actually on screen.
def _live_files():
    import re as _re
    tk = (ROOT / "src/ui/framexml_takeover.cpp").read_text()
    defaults = set(_re.findall(r'"([a-z]+)"',
        _re.search(r"return std::set<std::string>\{(.*?)\};", tk, _re.S).group(1)))
    cand = set(_re.findall(r'"([a-z]+)"',
        _re.search(r"for \(const char\* name : \{(.*?)\}\)", tk, _re.S).group(1)))
    rd = (ROOT / "tools/framexml_element_readiness.py").read_text()
    ns = {}
    exec(_re.search(r"^ELEMENTS = \{.*?^\}", rd, _re.S | _re.M).group(0), ns)
    exec(_re.search(r"^ADDON_ELEMENTS = \{.*?^\}", rd, _re.S | _re.M).group(0), ns)
    out = {}
    for el in defaults | cand:
        for f in ns["ELEMENTS"].get(el, []):
            out[f] = el
        addon = ns["ADDON_ELEMENTS"].get(el)
        if addon:
            d = XML / "addons" / addon
            if d.exists():
                for p in d.rglob("*"):
                    if p.suffix in (".lua", ".xml"):
                        out[p.name] = el
    for f, el in {
            "uiparent.lua": "dialogs", "staticpopup.lua": "dialogs",
            "unitpopup.lua": "unitframes", "unitframe.lua": "unitframes",
            "targetframe.lua": "targetframe", "playerframe.lua": "playerframe",
            "actionbutton.lua": "mainmenubar", "bonusactionbarframe.lua": "mainmenubar",
            "mainmenubar.lua": "mainmenubar", "mainmenubarbagbuttons.lua": "bagbar",
            "containerframe.lua": "bags", "paperdollframe.lua": "characterframe",
            "skillframe.lua": "characterframe/skills",
            "reputationframe.lua": "characterframe/rep",
            "tokenframe.lua": "characterframe/currency",
            "petpaperdollframe.lua": "characterframe/pet",
            "characterframe.lua": "characterframe", "spellbookframe.lua": "spellbook",
            "petframe.lua": "petframe", "petactionbarframe.lua": "petframe",
            "buffframe.lua": "buffs", "castingbarframe.lua": "castbar",
            "durabilityframe.lua": "durability", "zonetext.lua": "zonetext",
            "minimap.lua": "minimap", "mirrortimer.lua": "playerframe",
            "focusframe.lua": "focusframe", "gametooltip.lua": "tooltips",
            "itembuttontemplate.lua": "shared"}.items():
        out.setdefault(f, el)
    return out


OWNED = _live_files()

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
