#!/usr/bin/env python3
"""Chat types FrameXML can hand SendChatMessage that it does not map.

    tools/chat_type_coverage_check.py

WHY THIS IS WORTH A SWEEP

lua_SendChatMessage mapped eight of the thirteen types and initialised its
variable to SAY, so anything it did not recognise was said out loud to everyone
standing nearby. That is the worst available default: the message went
somewhere, no error was raised, and the only way to notice was to be told by
whoever read it.

CHANNEL was the one that mattered. Every numbered channel - General, Trade,
LookingForGroup - sends through this binding, so with FrameXML drawing the chat
every channel line was said instead. RAID_WARNING, EMOTE, AFK and DND went the
same way.

The binding refuses an unknown type now rather than defaulting, so the failure
mode is a warning in the log and nothing sent. This counts what is still
unmapped so that refusal does not quietly become the new silence.

WHERE THE SENDABLE SET COMES FROM

Two places, and both are needed:

  * ChatEdit_HandleChatType resolves a slash command to a SlashCmdList key and
    hands that key to processChatType as the chat type, so a type is sendable
    when ChatTypeInfo["X"] and SLASH_X1 both exist.
  * Some are sent without ever being a chat type on the edit box - chatframe
    calls SendChatMessage(msg, "AFK") and (msg, "DND") directly.

THE ONE IT REPORTS, AND IT IS CORRECT

REPLY - /r. processChatType rewrites it before anything is sent: it looks the
last tell target up and sets the edit box's chatType to WHISPER, so REPLY never
reaches the binding at all. Mapping it would be mapping something that cannot
arrive.

BN_CONVERSATION and BN_WHISPER do not appear here, because neither is a
ChatTypeInfo key with a matching SLASH_ prefix and neither is passed to
SendChatMessage as a literal - they are set on the edit box and sent through
BNSendWhisper. If the anchor is ever widened to catch them, they belong with
REPLY rather than in the binding: Battle.net is not here, and sending its chat
as something else is worse than refusing it.

WHAT IT CANNOT SEE

Whether the type is mapped to the *right* number. Those come from
SharedDefines.h and were checked against it by hand: EMOTE 0x0A, CHANNEL 0x11,
AFK 0x17, DND 0x18, RAID_WARNING 0x28, BATTLEGROUND 0x2C.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files, without_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
INTERFACE = ROOT / "Data/interface"
BINDING = ROOT / "src/addons/lua_social_api.cpp"


def sendable_types():
    info, slash, literal = set(), set(), set()
    for path in loaded_files(INTERFACE):
        text = without_comments(path.read_text(errors="ignore"))
        info |= set(re.findall(r'ChatTypeInfo\["([A-Z_0-9]+)"\]', text))
        slash |= {m.group(1) for m in re.finditer(r"\bSLASH_([A-Z_0-9]+?)\d+\s*=", text)}
        literal |= {m.group(1) for m in
                    re.finditer(r'SendChatMessage\([^,]+,\s*"([A-Z_]+)"', text)}
    return (info & slash) | literal


def mapped_types():
    text = BINDING.read_text(errors="ignore")
    return {m.group(1) for m in re.finditer(r'typeStr == "([A-Z_]+)"', text)}


def main():
    sendable = sendable_types()
    mapped = mapped_types()

    print(f"{len(sendable)} chat types FrameXML can send, "
          f"{len(mapped)} mapped by SendChatMessage")
    if "SAY" not in sendable:
        print("  CANARY: SAY is not in the sendable set - the interface is not "
              "parsing and the count below is meaningless.")
    if "SAY" not in mapped:
        print("  CANARY: SAY is not mapped - the binding is not parsing.")
    print()

    # Named rather than counted, so a second one cannot arrive behind it.
    #
    # REPLY is a ChatTypeInfo entry - it colours the edit box - and never a send
    # type. Both paths that use it turn it into a whisper first: SlashCmdList
    # REPLY calls SendChatMessage(msg, "WHISPER", ...) against
    # ChatEdit_GetLastTellTarget, and the edit box sets its chatType attribute
    # to WHISPER before anything leaves. Checked that /r reaches a target while
    # pinning this: ChatEdit_SetLastTellTarget is called from
    # ChatFrame_MessageEventHandler with arg2 of an incoming whisper, which
    # this client fires with the sender's name.
    EXPECTED_UNMAPPED = {"REPLY": "converted to WHISPER before it is sent"}
    missing = sorted(sendable - mapped - set(EXPECTED_UNMAPPED))
    print(f"{len(missing)} chat type(s) SendChatMessage does not map:\n")
    for name in missing:
        print(f"  {name}")
    if not missing:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
