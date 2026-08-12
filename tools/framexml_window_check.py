#!/usr/bin/env python3
"""Top-level FrameXML windows that nothing has decided about.

Every window the original interface can put on screen has to be exactly one of:

  * **owned** - FrameXML draws it and this client's own render call is gated on
    frameXmlOwns(UiElement::X), or
  * **suppressed** - this client draws it and FrameXML's copy is hidden.

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
    r'<(?:Frame|Button|MessageFrame|ScrollingMessageFrame|ScrollFrame'
    r'|GameTooltip|StatusBar|Slider|EditBox|CheckButton)\b([^>]*)>')
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
        "DungeonCompletionAlertFrame1",
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


INHERITS = re.compile(r'inherits="([^"]+)"')


def uiParentTemplates():
    """Virtual templates that put whatever inherits them on UIParent.

    A frame does not have to say parent="UIParent" itself. GroupLootFrame1
    through 4 say only inherits="GroupLootFrameTemplate", and it is the
    template that names the parent - so looking for the literal attribute
    walked straight past four top-level windows, which then drew beside this
    client's own roll dialog with nothing reporting it.
    """
    templates = {}          # name -> (onUIParent, inheritedNames)
    for path in sorted(FRAMEXML.glob("*.xml")):
        text = path.read_text(errors="ignore")
        for attrs in FRAME.findall(text):
            if 'virtual="true"' not in attrs:
                continue
            name = NAME.search(attrs)
            if not name:
                continue
            inherited = INHERITS.search(attrs)
            templates[name.group(1)] = (
                'parent="UIParent"' in attrs,
                [n.strip() for n in inherited.group(1).split(",")] if inherited else [],
            )

    # A template may inherit the parent from another template; settle it.
    def resolve(name, seen=()):
        if name not in templates or name in seen:
            return False
        onParent, parents = templates[name]
        return onParent or any(resolve(p, seen + (name,)) for p in parents)

    return {n for n in templates if resolve(n)}


def topLevelWindows():
    onUIParent = uiParentTemplates()
    found = {}
    for path in sorted(FRAMEXML.glob("*.xml")):
        text = path.read_text(errors="ignore")
        for attrs in FRAME.findall(text):
            if 'virtual="true"' in attrs:
                continue
            if 'parent="UIParent"' not in attrs:
                inherited = INHERITS.search(attrs)
                if not inherited:
                    continue
                if not any(n.strip() in onUIParent
                           for n in inherited.group(1).split(",")):
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

    print("Neither owned nor suppressed - check whether this client draws one:")
    for name, source in sorted(unknown.items()):
        print(f"    {name:<32} {source}")
    print("\nIf this client draws the same thing, name the frame in "
          "framexml_takeover.cpp\nand gate the client's render call on "
          "frameXmlOwns. If it does not, add it to\nIGNORED here with the "
          "reason, so the next run stays short.")
    return 0


def checkInherits():
    """Every inherits= naming something the XML never declares.

    A frame that inherits a template which is not there loses every part the
    template would have given it - its art, its regions, its scripts - and
    says nothing. Same for a font string naming a font object that does not
    exist: the text draws in whatever the default is.

    Nothing else here would catch it. The emitter copies the name through, the
    compile check only asks whether the Lua parses, and a frame with no parts
    looks like a frame whose parts failed to draw.

    inherits= names either a virtual frame template *or* a virtual FontString
    template *or* a <Font> object, so all three count as declarations -
    counting only <Font> reported four false positives on the first run.
    """
    FRAME_TAGS = (r'(?:Frame|Button|CheckButton|StatusBar|Slider|EditBox'
                  r'|ScrollFrame|ScrollingMessageFrame|MessageFrame|GameTooltip'
                  r'|Model|PlayerModel|DressUpModel|TabardModel|ColorSelect'
                  r'|SimpleHTML|Cooldown|QuestPOIFrame|Minimap|MovieFrame'
                  r'|FontString)')
    xmls = sorted(FRAMEXML.glob("*.xml")) + \
           sorted((ROOT / "Data" / "interface" / "addons").glob("*/*.xml"))
    declared, referenced = set(), {}
    for path in xmls:
        text = path.read_text(errors="ignore")
        declared |= set(re.findall(r'<Font\b[^>]*name="([^"]+)"', text))
        for m in re.finditer(r'<' + FRAME_TAGS + r'\b([^>]*)>', text):
            attrs = m.group(1)
            if 'virtual="true"' not in attrs:
                continue
            n = re.search(r'name="([^"]+)"', attrs)
            if n:
                declared.add(n.group(1))
    for path in xmls:
        text = re.sub(r"<!--.*?-->", "", path.read_text(errors="ignore"), flags=re.S)
        for m in re.finditer(r'<' + FRAME_TAGS + r'\b([^>]*)>', text):
            inh = re.search(r'inherits="([^"]+)"', m.group(1))
            if not inh:
                continue
            for name in inh.group(1).split(","):
                referenced.setdefault(name.strip(), []).append(path.name)
    missing = {n: f for n, f in referenced.items() if n not in declared}
    print(f"\n{len(referenced)} templates and font objects inherited; "
          f"{len(declared)} declared.")
    if not missing:
        print("Every inherits= resolves.")
        return
    print("\nInherited but never declared - the frame loses everything the "
          "template would have given it:")
    for name, files in sorted(missing.items(), key=lambda kv: -len(kv[1])):
        print(f"    {name:<38} {len(files)} uses, e.g. {files[0]}")


if __name__ == "__main__":
    code = main()
    checkInherits()
    sys.exit(code)
