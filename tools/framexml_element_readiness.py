#!/usr/bin/env python3
"""Which UI elements could be handed to FrameXML without a call raising.

The transition works element by element: framexml_takeover.cpp names a set that
FrameXML draws and suppresses this client's own version of them, and an element
joins that set once it has been seen drawing correctly. This answers the half of
"correctly" that can be checked without looking at the screen — whether every
global the element's code calls is answered by something.

    tools/framexml_element_readiness.py

Two measures, because resolving every call was only ever half of it.

  * **calls** — globals the element's code invokes that nothing answers. These
    raise.
  * **events** — events its frames RegisterEvent for that nothing in src/ ever
    fires. These do not raise: the element simply sits there, or shows stale
    data, or never opens. Six real bugs came out of this column after the call
    column had gone quiet — a mail frame that hung after sending, bag cooldown
    swirls that never drew, an achievements panel that showed the empty state it
    was built with, a master looter menu that could not open.

Reports "no known gaps", not "works". A resolved call is one that will not
raise; it says nothing about whether the frame draws, is positioned, or is
anchored to anything. The visual half is still a run of the client.

An event in the second column is a lead, not a verdict. "Never fired" can also
mean *something else is fired in its place*: SMSG_QUESTGIVER_QUEST_LIST was
sending GOSSIP_SHOW where QUEST_GREETING belonged, which opened the gossip frame
over the quest list. Read the handler before concluding the event is absent.

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
    itself. GetMapLandmarkInfo and GetMapOverlayInfo are owned by
    src/rendering/world_map/, and answering them would draw a second map over
    the first. The same goes for the barber shop: WindowManager has a working
    one that reads BarberShopStyle.dbc and sends CMSG_ALTER_APPEARANCE, so
    GetBarberShopStyleInfo and its four neighbours are absent on purpose.

    Check src/ for an owner before treating an element's remaining names as
    work. Both of these read as the last unimplemented features for several
    rounds when neither was unimplemented at all.

  * Events only the C client ever sends, where FrameXML does the same job in
    Lua. BAG_OPEN and BAG_CLOSED are the clear case: ToggleBag, OpenBag and
    CloseBag are Lua functions in containerframe.lua that show and hide the
    frames themselves, so the events are left for a bank or a merchant opening
    bags from outside — which this client does not do. Firing them would
    duplicate what the Lua already did.

    Read the caller before treating an event as missing. If the interface can
    reach the same result without it, the client is not the one failing to
    speak.

  * Events that share a branch with one already fired. GUILDBANK_UPDATE_MONEY
    is merchant's last, and it sits in the same elseif as PLAYER_MONEY — both
    just recheck the repair buttons. PLAYER_MONEY is fired, so the branch runs;
    the guild-bank variant only adds anything to a client that funds repairs
    from a guild bank, which this one does not. Applying the rule above: grep
    the handler for the event and see what else reaches the same line.

  * Whole features this client does not have, which read as a pile of events
    rather than as one absence. playerframe's eight are voice chat twice,
    vehicles four times, the Chinese anti-addiction playtime display and LFG
    role assignment — none of which exists here, and UNIT_ENTERED_VEHICLE does
    nothing but set inSeat and swap the frame art for a vehicle that cannot
    happen.

    Worth counting before working: an element showing eight missing events can
    be four features absent by design rather than eight gaps. bags, merchant
    and playerframe all read as gapped and all three are complete.

    Calls need the stronger test, because an unanswered call raises where an
    unfired event only goes unheard. Ask whether it is *reachable*, not whether
    the feature exists. mainmenubar's four vehicle calls live in
    vehiclemenubar.lua — one hop away, which is why they are counted here — and
    every one sits behind a guard that a client with no vehicles never passes:
    UnitInVehicle gates the path and UnitVehicleSkin answers nil, so the
    indicator is zero and the function returns before the call. Unreachable, as
    GetMapLandmarkInfo is behind GetNumMapLandmarks answering zero.

    minimap's Wintergrasp pair is the same, and took two hops to see. Both
    BattlefieldMgr calls sit inside `for i=1, MAX_WORLD_PVP_QUEUES`, and the
    status that gates them comes from GetWorldPVPQueueStatus, which answers nil
    three times — so it is never "queued" or "confirm". The other route in is a
    static popup shown by BATTLEFIELD_MGR_ENTRY_INVITE, which is never fired.
    Both doors are shut, and one shut door would not have been enough.

    Where the checks stop: characterframe's last four are in
    equipmentmanager.lua, and that feature is not absent here —
    GetNumEquipmentSets answers from a real set list. A player with a saved set
    runs that code and every one of those calls raises. So the same four tests
    that cleared five elements find this one real, which is the point of
    running them rather than assuming either way.

    characterframe's three events split the same way. KNOWN_TITLES_UPDATE
    shares its branch with UNIT_NAME_UPDATE and PLAYER_DAMAGE_DONE_MODS shares
    its with UNIT_STATS; both siblings are fired, so both branches already run.
    CURSOR_UPDATE is the one that is real: s_cursorType in lua_action_api.cpp
    is this client's cursor and changes at thirteen sites, none of which say
    so. Firing it wants a setter those thirteen go through — mechanical, but a
    cursor that announces half its changes is harder to reason about than one
    that announces none, so it is all of them or nothing.

    minimap's PLAYER_DIFFICULTY_CHANGED and UPDATE_INSTANCE_INFO both drive
    MiniMapInstanceDifficulty_OnEvent, which reads GetInstanceInfo and hides
    the indicator unless the difficulty says something other than normal
    five-player. GetInstanceInfo answers "party" for real when in an instance,
    so the path is live — but it reports a fixed difficulty, and firing the
    events against that changes nothing. The chain worth following is
    GroupListData::difficultyId and raidDifficultyId, which the group list
    already parses and GetInstanceInfo does not read; wire those first and the
    two events become worth firing.
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
    "partyframes":  ["partyframe.xml", "partyframetemplates.xml"],
    "micromenu":    ["mainmenubarmicrobuttons.lua", "mainmenubarmicrobuttons.xml"],
    "bagbar":       ["mainmenubarbagbuttons.lua", "mainmenubarbagbuttons.xml"],
    "gamemenu":     ["gamemenuframe.xml"],
    "worldmap":     ["worldmapframe.lua", "worldmapframe.xml"],
    # chatframe.lua is deliberately not here — it is in SHARED, because it
    # defines the slash commands the whole interface uses and its unanswered
    # names belong to whichever command was typed rather than to the chat
    # window. Its .xml is a different matter: that is the frame itself.
    #
    # voicechat.lua is left out too, and that one is a decision rather than a
    # scoping detail. There is no voice chat here and there is not going to be,
    # so every name it wants is correctly absent; counting them would put a
    # permanent floor under this element's number and make it unreadable.
    "chat":         ["floatingchatframe.lua", "floatingchatframe.xml",
                     "chatframe.xml", "chatconfigframe.lua", "chatconfigframe.xml"],
    # These four are named in framexml_takeover.cpp and were measured by
    # nothing, which is the same hole chat sat in. An element the transition can
    # be asked to hand over and that no report covers is worse than one with a
    # known gap: it reads as ready by absence.
    "bgscore":      ["worldstateframe.lua", "worldstateframe.xml"],
    "stable":       ["petstable.lua", "petstable.xml"],
    "book":         ["itemtextframe.lua", "itemtextframe.xml"],
    "totems":       ["totemframe.lua", "totemframe.xml"],
    # The twelve this client already hands over by default, and which nothing
    # had ever measured. Being enabled is not evidence of being complete — it
    # means someone once saw them draw, which is the visual half. An unanswered
    # call in one of these is a live fault in the shipping default, not a
    # candidate for a future round.
    "playerframe":  ["playerframe.lua", "playerframe.xml"],
    "targetframe":  ["targetframe.lua", "targetframe.xml"],
    "focusframe":   ["focusframe.lua", "focusframe.xml"],
    "petframe":     ["petframe.lua", "petframe.xml"],
    "castbar":      ["castingbarframe.lua", "castingbarframe.xml"],
    "buffs":        ["buffframe.lua", "buffframe.xml"],
    "minimap":      ["minimap.lua", "minimap.xml"],
    "characterframe": ["paperdollframe.lua", "paperdollframe.xml"],
    "bags":         ["containerframe.lua", "containerframe.xml"],
    "spellbook":    ["spellbookframe.lua", "spellbookframe.xml"],
    "durability":   ["durabilityframe.lua", "durabilityframe.xml"],
    "mainmenubar":  ["mainmenubar.lua", "mainmenubar.xml"],
}

# Elements whose frames arrive with a load-on-demand addon rather than with
# FrameXML. Their whole directory is the element.
ADDON_ELEMENTS = {
    "achievements": "blizzard_achievementui",
    "auctionhouse": "blizzard_auctionui",
    "barbershop":   "blizzard_barbershopui",
    "guildbank":    "blizzard_guildbankui",
    "inspect":      "blizzard_inspectui",
    "talents":      "blizzard_talentui",
    "tradeskill":   "blizzard_tradeskillui",
    "macro":        "blizzard_macroui",
    "keybindings":  "blizzard_bindingui",
    "timemanager":  "blizzard_timemanager",
    "classtrainer": "blizzard_trainerui",
}

# A handler body in XML is Lua, and holds calls that appear nowhere in any .lua.
# GuildControlSetRankFlag and TakeInboxTextItem are both only ever called from
# one, and a scan of the Lua alone reported their frames complete.
SCRIPT_BODY = re.compile(r"<(On[A-Za-z]+)>(.*?)</\1>", re.S)
CALL = re.compile(r"(?<![\w.:])([A-Z][A-Za-z0-9_]*)\s*\(")

# A Lua pattern in a string looks exactly like a call. gsub(point, "TOP(.*)",
# "BOTTOM%1") reads as a call to TOP, and did — it was three of the loot
# frame's five remaining names. Comments do the same for anything written as
# Name() in prose.
_STRINGS = re.compile(r'"(?:[^"\\\n]|\\.)*"' r"|'(?:[^'\\\n]|\\.)*'")
_COMMENT = re.compile(r"--\[\[.*?\]\]|--[^\n]*", re.S)


def code_only(src):
    """The source with string literals and comments blanked out."""
    src = _COMMENT.sub(" ", src)
    return _STRINGS.sub('""', src)


def events_fired():
    """Every event name the client can send, taken from the C++ that sends them."""
    names = set()
    # include/ as well as src/. Plenty of this client's small state changes are
    # inline in a header — closeStableWindow fires PET_STABLE_CLOSED from
    # game_handler.hpp — and scanning only src/ reported those events as never
    # sent, which is the one thing this column is supposed to be trusted on.
    roots = [os.path.join(ROOT, "src"), os.path.join(ROOT, "include")]
    for root in roots:
      for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if not fn.endswith((".cpp", ".hpp")):
                continue
            src = open(os.path.join(dirpath, fn), encoding="utf-8", errors="ignore").read()
            names |= set(re.findall(r'"([A-Z][A-Z0-9_]{3,})"', src))
    return names


def events_registered():
    """file -> the events its frames ask for."""
    want = collections.defaultdict(set)
    for path in glob.glob(os.path.join(FX, "*.lua")) + glob.glob(os.path.join(FX, "*.xml")):
        name = os.path.basename(path)
        src = open(path, encoding="utf-8", errors="ignore").read()
        want[name] |= set(re.findall(r'RegisterEvent\(\s*"([A-Z][A-Z0-9_]+)"', src))
        want[name] |= set(re.findall(r'<Event\s+name="([A-Z][A-Z0-9_]+)"', src))
    return want


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
    """function -> defining file, and file -> names it calls.

    Skips Data/interface/gluexml. That is the login screen, a separate
    interface in its own Lua state, and it defines functions under names the
    in-game one also uses — so a one hop out of an in-game file was landing in
    glue code and reporting its account-message and credits calls as gaps in
    the quest log, the quest tracker, the social frame and the help frame.
    """
    defined_by, calls = {}, collections.defaultdict(set)
    for dirpath, _, filenames in os.walk(os.path.join(ROOT, "Data", "interface")):
        if "gluexml" in dirpath.replace("\\", "/").split("/"):
            continue
        for name in filenames:
            path = os.path.join(dirpath, name)
            if name.endswith(".lua"):
                src = code_only(open(path, encoding="utf-8", errors="ignore").read())
                for fn in re.findall(
                        r"^\s*(?:local\s+)?function\s+([A-Za-z_][\w]*)\s*\(", src, re.M):
                    defined_by.setdefault(fn, name)
                for fn in re.findall(r"^\s*([A-Za-z_][\w]*)\s*=\s*function", src, re.M):
                    defined_by.setdefault(fn, name)
                calls[name] |= set(CALL.findall(src))
            elif name.endswith(".xml"):
                src = open(path, encoding="utf-8", errors="ignore").read()
                for body in SCRIPT_BODY.finditer(src):
                    calls[name] |= set(CALL.findall(code_only(body.group(2))))
    return defined_by, calls


def main():
    have = registered()
    defined_by, calls = scan_interface()
    fired = events_fired()
    wants = events_registered()

    print("element        files  calls  events")
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

        # Only the element's own files: an event another file asks for belongs
        # to that file's element, not to this one.
        want = set()
        for root in roots:
            want |= wants.get(root, set())
        # Battle.net has no counterpart on a 3.3.5 server.
        dead = sorted(e for e in want if e not in fired and not e.startswith("BN_"))

        listed = " ".join(sorted(missing)[:12] + dead[:6])
        print(f"  {element:<13} {len(files):>3}   {len(missing):>4}   {len(dead):>4}  {listed}")
        if not missing and not dead:
            ready.append(element)

    for element, addon in sorted(ADDON_ELEMENTS.items()):
        root = os.path.join(ROOT, "Data", "interface", "addons", addon)
        if not os.path.isdir(root):
            continue
        missing, nfiles = set(), 0
        for dirpath, _, filenames in os.walk(root):
            for fn in filenames:
                if not fn.endswith((".lua", ".xml")):
                    continue
                nfiles += 1
                for name in calls.get(fn, ()):
                    if name in have or name in defined_by or name.startswith("BN"):
                        continue
                    missing.add(name)
        listed = " ".join(sorted(missing)[:6])
        print(f"  {element:<13} {nfiles:>3}   {len(missing):>3}  {listed}")
        if not missing:
            ready.append(element)

    total = len(ELEMENTS) + len(ADDON_ELEMENTS)
    print()
    print(f"{len(ready)} of {total} with no known gaps: {' '.join(sorted(ready))}")
    print()
    print("To try one, name it alongside the current defaults — the environment")
    print("replaces the list rather than adding to it:")
    print("  WOWEE_FRAMEXML_UI=playerframe,targetframe,minimap,mainmenubar,"
          "characterframe,bags,castbar,spellbook,petframe,focusframe,buffs,"
          "durability,<element>")


if __name__ == "__main__":
    main()
