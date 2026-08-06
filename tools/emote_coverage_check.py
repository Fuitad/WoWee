#!/usr/bin/env python3
"""Emote tokens FrameXML can send that DoEmote cannot answer.

    tools/emote_coverage_check.py

WHAT THIS IS FOR

ChatEdit_ParseText resolves an emote by walking EMOTEn_CMDm for a match and
then calling DoEmote with that emote's **EMOTEn_TOKEN**. So the token list is
exactly the set of names the binding has to answer, and anything it does not
answer is an emote that does nothing at all — no packet, no animation, no text,
and no error either.

DoEmote used to answer thirty-one names from a map written by hand, twenty-seven
of which are real tokens. The other two hundred and twenty-one did nothing. It
went unnoticed for as long as it did because this client's own chat never called
DoEmote: chat_panel has an emote fallthrough that reads EmoteRegistry directly,
so every emote worked right up until chat was handed over and DoEmote became the
only route.

WHAT IT CHECKS

Every EMOTEn_TOKEN against the command names in EmotesText.dbc, which is what
EmoteRegistry keys its table on — lower-cased, split on non-alphanumerics, the
same reading parseEmoteCommands makes.

It also checks that DoEmote actually goes through that registry. A binding that
went back to a literal map would keep this number low, and the point is to
notice that rather than to trust the comment above it.

ONE IS LEFT AND IT IS CORRECT

"unused", which is a placeholder token in FrameXML's own list.

WHAT IT CANNOT SEE

Whether the emote *plays*. The client sends the id and the server echoes
SMSG_EMOTE, which is where the animation comes from — an emote whose Emotes.dbc
row has AnimID 0 is text-only and correct to look silent.
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DBC = ROOT / "Data/db/emotestext.dbc"
INTERFACE = ROOT / "Data/interface"
BINDING = ROOT / "src/addons/lua_social_api.cpp"


def dbc_commands():
    """Command names out of EmotesText.dbc, read as parseEmoteCommands does."""
    if not DBC.exists():
        return None
    data = DBC.read_bytes()
    if len(data) < 20 or data[:4] != b"WDBC":
        return None
    _, records, fields, rec_size, _ = struct.unpack("<4sIIII", data[:20])
    strings = data[20 + records * rec_size:]

    def string_at(offset):
        end = strings.index(b"\0", offset)
        return strings[offset:end].decode("latin-1")

    names = set()
    for i in range(records):
        row = struct.unpack(f"<{fields}I", data[20 + i * rec_size: 20 + (i + 1) * rec_size])
        current = ""
        for ch in string_at(row[1]):
            if ch.isalnum() or ch == "_":
                current += ch.lower()
            elif current:
                names.add(current)
                current = ""
        if current:
            names.add(current)
    return names


def framexml_tokens():
    tokens = set()
    for path in loaded_files(INTERFACE):
        text = path.read_text(errors="ignore")
        for m in re.finditer(r'EMOTE\d+_TOKEN\s*=\s*"([^"]+)"', text):
            tokens.add(m.group(1).lower())
    return tokens


def main():
    tokens = framexml_tokens()
    commands = dbc_commands()
    uses_registry = "getEmoteDbcId" in BINDING.read_text(errors="ignore")

    if commands is None:
        print("emotestext.dbc not readable here — nothing to compare")
        return 0

    print(f"{len(tokens)} EMOTEn_TOKEN values, {len(commands)} command names in "
          f"emotestext.dbc, DoEmote "
          f"{'reads the registry' if uses_registry else 'DOES NOT read the registry'}")
    if not tokens:
        print("  CANARY: no tokens parsed out of the interface — "
              "the count below is meaningless.")
    print()

    # A placeholder in the DBC rather than an emote: EMOTE*_TOKEN carries the
    # literal string "unused" for the gaps in the numbering, and there is no
    # command to answer it with because there is nothing to perform. Named so
    # that a real token going unanswered still shows.
    EXPECTED_UNANSWERED = {"unused": "a DBC placeholder, not an emote"}
    unanswered = sorted((tokens - commands) if uses_registry else tokens)
    unanswered = [t for t in unanswered if t not in EXPECTED_UNANSWERED]
    print(f"{len(unanswered)} emote token(s) DoEmote cannot answer:\n")
    for name in unanswered[:40]:
        print(f"  {name}")
    if len(unanswered) > 40:
        print(f"  ... and {len(unanswered) - 40} more")
    if not unanswered:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
