#!/usr/bin/env python3
"""Top-level FrameXML windows that nothing has decided about.

Every window the original interface can put on screen has to be exactly one of:

  * **owned** — FrameXML draws it and this client's own render call is gated on
    frameXmlOwns(UiElement::X), or
  * **suppressed** — this client draws it and FrameXML's copy is hidden.

Neither means both appear, one on top of the other. That is not hypothetical:
UIErrorsFrame showed every server refusal twice because this client fires
UI_ERROR_MESSAGE and FrameXML's error frame listens for it.

The reason to run this after *any* batch of API work, rather than when something
looks wrong: a window whose functions all return nil stays empty and unnoticed,
and appears the moment they start answering. Finishing GetFactionInfo,
GetTotemInfo and GetTalentPrereqs opened three windows that had been quietly
present all along.

    tools/framexml_window_check.py           # windows nothing has decided about
    tools/framexml_window_check.py --all     # including the ones ignored below

Never fails the build: whether an unaccounted window matters depends on whether
this client draws the same thing, which is a judgement about two interfaces
rather than something a regex can settle.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FRAMEXML = ROOT / "Data" / "interface" / "framexml"
TAKEOVER = ROOT / "src" / "ui" / "framexml_takeover.cpp"

# A named, non-virtual frame hung directly off UIParent.
FRAME = re.compile(
    r'<(?:Frame|Button|MessageFrame|ScrollFrame|GameTooltip|StatusBar)\b([^>]*)>')
NAME = re.compile(r'name="([^"$]+)"')

# Windows that need no decision, and why. Kept explicit rather than pattern
# matched: "Frame ends in Tooltip" would also swallow something that mattered.
IGNORED = {
    "tooltips this client does not draw": (
        "GameTooltip", "ShoppingTooltip1", "ShoppingTooltip2", "ShoppingTooltip3",
        "ItemRefTooltip", "ItemRefShoppingTooltip1", "ItemRefShoppingTooltip2",
        "ItemRefShoppingTooltip3", "SmallTextTooltip",
    ),
    "dialogs that open only when something asks for them": (
        "StaticPopup1", "StaticPopup2", "StaticPopup3", "StaticPopup4",
        "StackSplitFrame", "CoinPickupFrame", "OpacityFrame",
        "OpacityFrameCloseButton", "PetRenamePopup", "GuildControlPopupFrame",
        "StationeryPopupFrame", "ReadyCheckFrame", "FolderPicker",
        "ChatMenu", "RatingMenuFrame", "DressUpFrame", "AutoCompleteBox",
    ),
    "systems this client has none of": (
        "LFDDungeonReadyPopup", "LFDParentFrame", "LFDRoleCheckPopup",
        "LFRParentFrame", "PVPParentFrame", "PVPBannerFrame", "ArenaFrame",
        "ArenaRegistrarFrame", "BattlefieldFrame", "VehicleMenuBar",
        "VehicleSeatIndicator", "VoiceChatTalkers", "BNToastFrame",
        "BNetReportFrame", "BNConversationInviteDialog", "FriendsFriendsFrame",
        "MinigameFrame", "MovieProgressFrame", "MacOptionsFrame",
        "MacOptionsCancelFrame", "MacOptionsCompressFrame", "RuneFrame",
        "PetitionFrame", "TabardFrame", "GuildRegistrarFrame", "ChannelPullout",
        "ChannelPulloutTab", "AddFriendFrame", "TradeFrame", "StatsFrame",
    ),
    "drawn only on an event this client never fires": (
        "RaidBossEmoteFrame", "RaidWarningFrame", "AutoFollowStatus",
        "QuestTimerFrame", "TutorialFrame", "TutorialFrameAlertButton",
        "AlertFrame", "WorldStateAlwaysUpFrame", "SubZoneTextFrame",
        "ZoneTextFrame", "ChatConfigFrame", "PartyMemberBackground",
        "ConsolidatedBuffs", "TemporaryEnchantFrame",
    ),
}
IGNORED_NAMES = {n for group in IGNORED.values() for n in group}


def topLevelWindows():
    found = {}
    for path in sorted(FRAMEXML.glob("*.xml")):
        text = path.read_text(errors="ignore")
        for attrs in FRAME.findall(text):
            if 'parent="UIParent"' not in attrs:
                continue
            if 'virtual="true"' in attrs:
                continue
            name = NAME.search(attrs)
            if name:
                found.setdefault(name.group(1), path.name)
    return found


def declaredNames():
    """Every frame or region name the XML declares, and the $parent suffixes."""
    plain, suffixes = set(), set()
    for path in list(FRAMEXML.glob("*.xml")) + \
                list((ROOT / "Data" / "interface" / "addons").glob("*/*.xml")):
        text = path.read_text(errors="ignore")
        for n in re.findall(r'name="([^"]+)"', text):
            if n.startswith("$parent"):
                suffixes.add(n[len("$parent"):])
            else:
                plain.add(n)
    return plain, suffixes


def checkSuppressionNames():
    """Names listed for suppression that no XML declares.

    A name invented here suppresses nothing and reports NOT BUILT forever, which
    looks identical to a frame that simply never opens. Names built from
    $parent are composed at run time, so a listed name counts as real when it is
    some declared frame followed by a declared $parent suffix.
    """
    plain, suffixes = declaredNames()

    # Frames the Lua builds by name at run time: BuffButton1 comes from
    # CreateFrame("Button", "BuffButton"..i, ...) and no XML mentions it.
    builtPrefixes = set()
    for path in FRAMEXML.glob("*.lua"):
        text = path.read_text(errors="ignore")
        builtPrefixes |= set(re.findall(
            r'CreateFrame\(\s*"[^"]+"\s*,\s*"([A-Za-z]\w*)"\s*\.\.', text))
        # BuffButton1 is reached as _G["BuffButton"..i]; the frame itself was
        # created from a variable, so the prefix only appears at the lookup.
        builtPrefixes |= set(re.findall(r'_G\[\s*"([A-Za-z]\w*)"\s*\.\.', text))

    def composed(name):
        """A $parent name, possibly nested: TargetFrame + TextureFrame + Name."""
        seen = set()
        while name and name not in seen:
            if name in plain:
                return True
            seen.add(name)
            longest = ""
            for suffix in suffixes:
                if suffix and name.endswith(suffix) and len(suffix) > len(longest):
                    longest = suffix
            if not longest:
                return False
            name = name[: -len(longest)]
        return False

    listed = []
    for m in re.finditer(r'\{UiElement::(\w+),\s*((?:"[^"]*"\s*)+)', TAKEOVER.read_text()):
        blob = " ".join(re.findall(r'"([^"]*)"', m.group(2)))
        listed += [(n, m.group(1)) for n in blob.split() if n and n[0].isupper()]

    unknown = []
    for name, element in listed:
        if name in plain:
            continue
        if composed(name):
            continue                      # composed from $parent at run time
        if any(name.startswith(pre) for pre in builtPrefixes):
            continue                      # named by the Lua as it creates it
        unknown.append((name, element))
    return listed, unknown


def main():
    showAll = "--all" in sys.argv
    if not TAKEOVER.is_file():
        print(f"no takeover file at {TAKEOVER}")
        return 0

    decided = TAKEOVER.read_text()
    windows = topLevelWindows()

    # A ContainerFrame is a bag, and the bags element covers all thirteen of
    # them however many the client happens to open.
    def accounted(name):
        if name in decided:
            return True
        return name.startswith("ContainerFrame") and "ContainerFrame" in decided

    unknown = {n: f for n, f in windows.items() if not accounted(n)}
    if not showAll:
        unknown = {n: f for n, f in unknown.items() if n not in IGNORED_NAMES}

    print(f"{len(windows)} top-level windows; "
          f"{len(windows) - len(unknown)} accounted for or ignored\n")
    if not unknown:
        listed, bad = checkSuppressionNames()
        if bad:
            print(f"{len(listed)} names listed for suppression; "
                  f"{len(bad)} that no XML declares:")
            for name, element in bad:
                print(f"    {name:<38} {element}")
            print("    A name nothing declares suppresses nothing.")
        else:
            print("Nothing undecided, and every suppressed name is a real frame.")
        return 0

    listed, badNames = checkSuppressionNames()
    if badNames:
        print(f"{len(listed)} frame names listed for suppression; "
              f"{len(badNames)} that no XML declares:")
        for name, element in badNames:
            print(f"    {name:<38} {element}")
        print("    A name nothing declares suppresses nothing.\n")

    print("Neither owned nor suppressed — check whether this client draws one:")
    for name, source in sorted(unknown.items()):
        print(f"    {name:<32} {source}")
    print("\nIf this client draws the same thing, name the frame in "
          "framexml_takeover.cpp\nand gate the client's render call on "
          "frameXmlOwns. If it does not, add it to\nIGNORED here with the "
          "reason, so the next run stays short.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
