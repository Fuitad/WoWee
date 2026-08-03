#!/usr/bin/env python3
"""Which UI elements could be handed to FrameXML without a call raising.

The transition works element by element: framexml_takeover.cpp names a set that
FrameXML draws and suppresses this client's own version of them, and an element
joins that set once it has been seen drawing correctly. This answers the half of
"correctly" that can be checked without looking at the screen — whether every
global the element's code calls is answered by something.

    tools/framexml_element_readiness.py

Reports "no known gaps", not "works". A resolved call is one that will not
raise; it says nothing about whether the frame draws, is positioned, or is
anchored to anything. The visual half is still a run of the client.

WHAT COUNTS AS THE ELEMENT'S CODE
---------------------------------
This is the whole difficulty, and three scopes were tried before one was right.

  * The element's own files alone **under-reports**. QuestFrame and
    QuestLogFrame both draw their reward block through QuestInfo.lua, which is
    neither of their files and which had nine unanswered calls — so both were
    reported ready while any quest offering a spell, title or reputation reward
    would have raised.

  * The transitive call graph **over-reports**, and uselessly: nearly everything
    calls StaticPopup_Show, staticpopup.lua names the handler of every popup in
    the interface, and so every element comes out depending on all 309 of them.
    A popup's handlers are reached only when that popup is shown, which is a
    data-driven dispatch rather than a call.

  * One hop, minus shared infrastructure, is the useful middle. It catches
    QuestInfo — QuestFrame calls QuestInfo_Display directly — without dragging
    in every popup in the game. That is what found the money frame, whose six
    unanswered calls were blocking five elements at once and appeared in none of
    their own files.

KNOWN FALSE POSITIVES, which this cannot tell from a real gap:
  * Functions defined in a load-on-demand addon (AchievementFrame_*,
    BackpackTokenFrame_Update) are absent only until that addon loads.
  * Names this client answers deliberately as absent because it draws the thing
    itself — GetMapLandmarkInfo and GetMapOverlayInfo are owned by
    src/rendering/world_map/, and answering them would draw a second map over
    the first.
"""

import collections
import glob
import os
import re

ROOT = os.path.join(os.path.dirname(__file__), "..")
FX = os.path.join(ROOT, "Data", "interface", "framexml")
ADDONS = os.path.join(ROOT, "src", "addons")

# Their unanswered names belong to a particular popup, menu or chat command and
# are reached only when that one is used — so they are not the element's.
SHARED = {
    "staticpopup.lua", "uiparent.lua", "unitpopup.lua",
    "chatframe.lua", "globalstrings.lua",
}

# Element -> the files that are unambiguously its own.
ELEMENTS = {
    "questgiver":   ["questframe.lua", "questframe.xml"],
    "gossip":       ["gossipframe.lua", "gossipframe.xml"],
    "questlog":     ["questlogframe.lua", "questlogframe.xml"],
    "mail":         ["mailframe.lua", "mailframe.xml"],
    "taxi":         ["taxiframe.lua", "taxiframe.xml"],
    "loot":         ["lootframe.lua", "lootframe.xml"],
    "merchant":     ["merchantframe.lua", "merchantframe.xml"],
    "bank":         ["bankframe.lua", "bankframe.xml"],
    "questtracker": ["watchframe.lua", "watchframe.xml"],
    "help":         ["helpframe.lua", "helpframe.xml"],
    "social":       ["friendsframe.lua", "friendsframe.xml"],
}

# A handler body in XML is Lua, and holds calls that appear nowhere in any .lua.
# GuildControlSetRankFlag and TakeInboxTextItem are both only ever called from
# one, and a scan of the Lua alone reported their frames complete.
SCRIPT_BODY = re.compile(r"<(On[A-Za-z]+)>(.*?)</\1>", re.S)
CALL = re.compile(r"(?<![\w.:])([A-Z][A-Za-z0-9_]*)\s*\(")


def registered():
    """Every global name the C++ bindings answer."""
    names = set()
    for path in glob.glob(os.path.join(ADDONS, "*.cpp")):
        src = open(path, encoding="utf-8", errors="ignore").read()
        names |= set(re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,', src))
        names |= set(re.findall(
            r'lua_setglobal\(\s*\w+\s*,\s*"([A-Za-z_][A-Za-z0-9_]*)"', src))
        # Bootstrap Lua lives in C++ string literals: names defined there, and
        # the quoted lists of stub names, both count as answered.
        names |= set(re.findall(r"function\s+([A-Za-z_][A-Za-z0-9_]*)\s*[:(]", src))
        names |= set(re.findall(r"'([A-Za-z_][A-Za-z0-9_]*)'", src))
    return names


def scan_interface():
    """function -> defining file, and file -> names it calls."""
    defined_by, calls = {}, collections.defaultdict(set)
    for dirpath, _, filenames in os.walk(os.path.join(ROOT, "Data", "interface")):
        for name in filenames:
            path = os.path.join(dirpath, name)
            if name.endswith(".lua"):
                src = open(path, encoding="utf-8", errors="ignore").read()
                for fn in re.findall(
                        r"^\s*(?:local\s+)?function\s+([A-Za-z_][\w]*)\s*\(", src, re.M):
                    defined_by.setdefault(fn, name)
                for fn in re.findall(r"^\s*([A-Za-z_][\w]*)\s*=\s*function", src, re.M):
                    defined_by.setdefault(fn, name)
                calls[name] |= set(CALL.findall(src))
            elif name.endswith(".xml"):
                src = open(path, encoding="utf-8", errors="ignore").read()
                for body in SCRIPT_BODY.finditer(src):
                    calls[name] |= set(CALL.findall(body.group(2)))
    return defined_by, calls


def main():
    have = registered()
    defined_by, calls = scan_interface()

    print("element        files  unresolved")
    ready = []
    for element, roots in sorted(ELEMENTS.items()):
        files = set(roots)
        for root in roots:
            for name in calls.get(root, ()):
                owner = defined_by.get(name)
                if owner and owner not in SHARED:
                    files.add(owner)

        missing = set()
        for f in files:
            for name in calls.get(f, ()):
                if name in have or name in defined_by:
                    continue
                # Battle.net has no counterpart on a 3.3.5 server.
                if name.startswith("BN"):
                    continue
                missing.add(name)

        listed = " ".join(sorted(missing)[:6])
        print(f"  {element:<13} {len(files):>3}   {len(missing):>3}  {listed}")
        if not missing:
            ready.append(element)

    print()
    print(f"{len(ready)} of {len(ELEMENTS)} with no known gaps: {' '.join(ready)}")
    print()
    print("To try one, name it alongside the current defaults — the environment")
    print("replaces the list rather than adding to it:")
    print("  WOWEE_FRAMEXML_UI=playerframe,targetframe,minimap,mainmenubar,"
          "characterframe,bags,castbar,spellbook,petframe,focusframe,buffs,"
          "durability,<element>")


if __name__ == "__main__":
    main()
