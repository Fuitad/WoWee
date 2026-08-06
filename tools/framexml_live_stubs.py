#!/usr/bin/env python3
"""Stubbed bindings that a handed-over element actually calls.

The readiness report counts a name as answered once it is bound, and a stub is
bound — so `HasPetSpells -> lua_ReturnNil` read as clean while it was hiding
the entire pet spell book. This asks the narrower question: which of the stubs
are on a code path that is on screen right now.

Being listed is not being wrong. Plenty of stubs are correct: the feature is
genuinely absent (vehicles, voice chat) or FrameXML tolerates the empty answer.
It is a reading list, ordered by how central the file is.

THE FORTY IT REPORTS TODAY, ALL CHECKED

None is a defect, but three are worth knowing about because they are visible
rather than merely absent. Read once so the count can be watched rather than
re-triaged.

VISIBLE, AND DELIBERATE

  * GetBattlefieldInstanceRunTime — the scoreboard prints "Time Elapsed: 0
    seconds" rather than nothing, because worldstateframe.lua formats whatever
    it is given. The real answer is milliseconds since the *instance* started,
    which this client cannot know: it can measure since the player joined, and
    labelling that "Time Elapsed" would be confidently wrong for anyone who
    arrived late. A zero that reads as broken is better than a number that
    reads as true.
  * ShowContainerSellCursor, ShowBuybackSellCursor — the cursor does not change
    to the sell icon over a bag item at a vendor. This client's cursor has no
    such icon to change to.

ONE THAT IS ABSENT BY CHOICE RATHER THAN BY NECESSITY

The GM survey. Its questions come from four DBCs this install carries, not from
any packet — GMSurveyCurrentSurvey maps language to survey, GMSurveySurveys
lists the question ids, GMSurveyQuestions and GMSurveyAnswers hold the text —
and the trigger is the getSurvey byte in SMSG_GMRESPONSE_STATUS_UPDATE.
Submitting is what makes it work rather than merely appear, and that means
accumulating ten answers with per-question comments for CMSG_GMSURVEY_SUBMIT.
The panel is only reached after a game master closes a ticket.

Recorded because this file previously said the questions were unparseable,
which was read off the event's name rather than off the files sitting beside
it.

TWO THAT WERE CHECKED AND ARE GENUINELY ABSENT

Written down because three neighbouring claims of the same kind turned out to
be wrong — the refund window, the GM survey and vehicle state were all called
absent and all three were reachable. These two are not.

  * Voice chat. AzerothCore's handlers read the request and throw it away:
    HandleVoiceSessionEnableOpcode is two read_skips, HandleSetActiveVoiceChannel
    another two, HandleChannelVoiceOnOpcode an empty body with a comment. No
    SMSG_VOICE_* is ever sent, so there is no session to report on and nothing
    a binding could answer from.
  * Movie recording. The renderer can capture one frame — Renderer::captureScreenshot
    writes a PNG — and there is no encoder behind it. MovieRecording_* is video.

ABSENT FEATURES, WHICH IS WHAT THE STUB SAYS (28)

The world map's debug objects, zone map, battlefield flag and vehicle
positions, dungeon map floors and Wintergrasp timer; Battle.net and its friend
list; voice chat and mutes; movie recording; mail stationery; achievement
comparison; arena opponents; the multi-cast bar's offset; addon memory usage.
Each is a feature this client does not have, and the stub is the honest shape
of that.

CORRECT ANSWERS THAT LOOK LIKE STUBS (9)

IsMacClient is false because it is not one. GetAdjustedSkillPoints is zero
because WotLK has no skill points — skillframe.lua gates every purchase verb
on it, which is why BuySkillTier and AddSkillUp are unreachable rather than
unimplemented. GetCurrentMapDungeonLevel is zero because a map with no floors
is on floor zero.
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
    # The candidates tier used to add a list on top of the defaults and adds
    # nothing now — every element is in the defaults, so the loop it was read
    # out of is gone. Read as empty rather than crashing, which is what this
    # did between the loop being removed and 2026-08-05: nothing runs this
    # sweep from the build, so nothing noticed.
    _cand_loop = _re.search(r"for \(const char\* name : \{(.*?)\}\)", tk, _re.S)
    cand = set(_re.findall(r'"([a-z]+)"', _cand_loop.group(1))) if _cand_loop else set()
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
