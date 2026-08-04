#!/usr/bin/env python3
"""Unbound globals reachable from a live file, following FrameXML's own calls.

The plain unbound sweep reports the file a name is *written* in, and judging
reachability from that is how two live raises were missed: GetQuestGreenRange
is written in uiparent.lua and reached from targetframe.lua through
GetQuestDifficultyColor; GetBindingByKey the same way through
GetBindingFromClick, reached from staticpopup.lua.

So walk it. Every FrameXML function that calls an unbound global is a carrier;
anything calling a carrier is a carrier. Report the ones a live file reaches,
with the chain.
"""
import re
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
XML = ROOT / "Data/interface"

# Built from the readiness tool's own element map plus the shared files
# every panel goes through, rather than written out by hand — a hand-made
# list of what is live is the mistake that hid the character sheet's other
# four tabs from the stub sweep and the whole verb surface from another.
# Derived, not hand-written: the elements handed over by default plus the
# candidates tier, mapped to files through the readiness tool's own table,
# and the shared files every panel goes through. A hand-made list of what is
# live is the mistake that hid the character sheet's other four tabs from one
# sweep and the whole verb surface from another — and listing *every* element
# is the opposite error, reporting friendsframe when social is deliberately
# not handed over.
LIVE = {
    "actionbutton.lua",
    "bankframe.lua",
    "bankframe.xml",
    "blizzard_achievementui.lua",
    "blizzard_achievementui.xml",
    "blizzard_auctiondressup.lua",
    "blizzard_auctiondressup.xml",
    "blizzard_auctionui.lua",
    "blizzard_auctionui.xml",
    "blizzard_auctionuitemplates.xml",
    "blizzard_bindingui.lua",
    "blizzard_bindingui.xml",
    "blizzard_guildbankui.lua",
    "blizzard_guildbankui.xml",
    "blizzard_inspectui.lua",
    "blizzard_inspectui.xml",
    "blizzard_talentui.lua",
    "blizzard_talentui.xml",
    "blizzard_tradeskillui.lua",
    "blizzard_tradeskillui.xml",
    "blizzard_trainerui.lua",
    "blizzard_trainerui.xml",
    "bonusactionbarframe.lua",
    "buffframe.lua",
    "buffframe.xml",
    "castingbarframe.lua",
    "castingbarframe.xml",
    "characterframe.lua",
    "chatconfigframe.lua",
    "chatconfigframe.xml",
    "chatframe.lua",
    "chatframe.xml",
    "containerframe.lua",
    "containerframe.xml",
    "durabilityframe.lua",
    "durabilityframe.xml",
    "floatingchatframe.lua",
    "floatingchatframe.xml",
    "focusframe.lua",
    "focusframe.xml",
    "gamemenuframe.xml",
    "gametooltip.lua",
    "gossipframe.lua",
    "gossipframe.xml",
    "helpframe.lua",
    "helpframe.xml",
    "inspecthonorframe.lua",
    "inspecthonorframe.xml",
    "inspectpaperdollframe.lua",
    "inspectpaperdollframe.xml",
    "inspectpvpframe.lua",
    "inspectpvpframe.xml",
    "inspecttalentframe.lua",
    "inspecttalentframe.xml",
    "itembuttontemplate.lua",
    "itemtextframe.lua",
    "itemtextframe.xml",
    "localization.lua",
    "lootframe.lua",
    "lootframe.xml",
    "mainmenubar.lua",
    "mainmenubar.xml",
    "mainmenubarbagbuttons.lua",
    "mainmenubarbagbuttons.xml",
    "mainmenubarmicrobuttons.lua",
    "mainmenubarmicrobuttons.xml",
    "merchantframe.lua",
    "merchantframe.xml",
    "minimap.lua",
    "minimap.xml",
    "mirrortimer.lua",
    "paperdollframe.lua",
    "paperdollframe.xml",
    "partyframe.xml",
    "partyframetemplates.xml",
    "petactionbarframe.lua",
    "petframe.lua",
    "petframe.xml",
    "petpaperdollframe.lua",
    "petstable.lua",
    "petstable.xml",
    "playerframe.lua",
    "playerframe.xml",
    "questframe.lua",
    "questframe.xml",
    "reputationframe.lua",
    "skillframe.lua",
    "spellbookframe.lua",
    "spellbookframe.xml",
    "staticpopup.lua",
    "targetframe.lua",
    "targetframe.xml",
    "taxiframe.lua",
    "taxiframe.xml",
    "tokenframe.lua",
    "totemframe.lua",
    "totemframe.xml",
    "uidropdownmenu.lua",
    "uiparent.lua",
    "unitframe.lua",
    "unitpopup.lua",
    "worldstateframe.lua",
    "worldstateframe.xml",
    "zonetext.lua",
}


def strip(t):
    t = re.sub(r"<!--.*?-->", "", t, flags=re.S)
    return re.sub(r"--\[\[.*?\]\]|--[^\n]*", "", t, flags=re.S)


bound = set()
for f in (ROOT / "src/addons").glob("*.cpp"):
    s = f.read_text(errors="ignore")
    bound |= set(re.findall(r'\{"([A-Za-z0-9_]+)"\s*,', s))
    bound |= set(re.findall(r'lua_setglobal\(\s*L_?\s*,\s*"([A-Za-z0-9_]+)"', s))
    for blob in re.findall(r'"([A-Za-z0-9_=,\\n ]{40,})"', s):
        bound |= set(re.findall(r"([A-Za-z][A-Za-z0-9_]*)=1", blob))

# Every FrameXML function body, and the file it lives in.
bodies, defined = {}, set()
for p in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    t = strip(p.read_text(errors="ignore"))
    defined |= set(re.findall(r"\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", t))
    defined |= set(re.findall(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*", t, re.M))
    for m in re.finditer(r"\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\nend", t, re.S):
        bodies[m.group(1)] = (p.name, m.group(2))

LUA = {"assert","ipairs","pairs","next","print","select","setmetatable","tonumber",
       "tostring","type","unpack","pcall","format","gsub","string","table","math",
       "date","time","abs","ceil","floor","max","min","mod","random","sqrt","bit",
       "wipe","tinsert","tremove","strsub","strlen","strupper","strlower","strfind",
       "strtrim","strjoin","strsplit","strrep","getmetatable","rawget","rawset",
       "error","loadstring","xpcall","strmatch","gmatch","tostringall"}

# Which functions call an unbound global directly.
carriers = {}
for fn, (fname, body) in bodies.items():
    for m in re.finditer(r"(?<![\w.:])([A-Z][A-Za-z0-9_]*)\s*\(", body):
        n = m.group(1)
        if n not in bound and n not in defined and n not in LUA:
            carriers.setdefault(fn, set()).add(n)

# Who calls whom.
callers = {}
for fn, (fname, body) in bodies.items():
    for m in re.finditer(r"(?<![\w.:])([A-Za-z_][A-Za-z0-9_]*)\s*\(", body):
        callers.setdefault(m.group(1), set()).add(fn)

print(f"{len(bodies)} FrameXML functions, {len(carriers)} call something unbound\n")

reported = []
for fn, names in carriers.items():
    # Walk outward from the carrier to any live file, up to four hops.
    seen, frontier, chain = {fn}, [fn], {fn: [fn]}
    hit = None
    for _ in range(4):
        nxt = []
        for cur in frontier:
            if bodies.get(cur, ("", ""))[0] in LIVE:
                hit = chain[cur]
                break
            for up in callers.get(cur, ()):
                if up in seen:
                    continue
                seen.add(up)
                chain[up] = chain[cur] + [up]
                nxt.append(up)
        if hit:
            break
        frontier = nxt
    if hit:
        reported.append((sorted(names), " <- ".join(hit), bodies[hit[-1]][0]))

print(f"{len(reported)} carriers a live file reaches:\n")
for names, path, where in sorted(reported)[:30]:
    print(f"  {', '.join(names)}")
    print(f"      {path}   [{where}]")
