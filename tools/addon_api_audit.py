#!/usr/bin/env python3
"""What an addon calls that nothing defines.

The point of this is to answer "will this addon work" before loading it, because
the missing-API fallback makes the honest answer hard to see at runtime: an
undefined global is callable and returns nil, so an addon calling one does not
error — it quietly does nothing, which reads as a feature that is present but
broken rather than one that was never wired up.

    tools/addon_api_audit.py                 # every addon, worst last
    tools/addon_api_audit.py blizzard_talentui   # one addon, names listed

Counting honestly took two tries, and both mistakes are easy to repeat:

  * `CreateFrame` is registered with lua_setglobal, not through one of the
    luaL_Reg tables. Miss that pattern and the single most-called function in
    the interface reads as undefined, and every count is nonsense.
  * `self:Click()` is a method call, not a global one. Counting those turned a
    real figure of about thirty into four hundred and twenty-two.
  * A `local function Foo()` is still a definition. Blizzard's auction house
    declares two of its helpers that way, and reading only top-level `function`
    reported both as missing from a file that defines them three lines up.

Never fails the build. It measures; it does not judge.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADDONS = ROOT / "Data" / "interface" / "addons"
FRAMEXML = ROOT / "Data" / "interface" / "framexml"
SRC = ROOT / "src" / "addons"

# A call on something, rather than a call to a global: the character before the
# name is a colon or a dot. Lua's own `string.format` is caught by this too,
# which is correct — it is not a global this client has to provide.
CALL = re.compile(r"(?<![:.\w])([A-Z][A-Za-z0-9_]{2,})\s*\(")
DEF_FUNC = re.compile(r"^\s*(?:local\s+)?function\s+([A-Za-z_]\w*)", re.M)
# `= function` specifically, and not a bare assignment: matching any `x = ...`
# swallowed every variable in the interface and took the known-set from four
# and a half thousand names to eighteen thousand, hiding real gaps rather than
# fixing false ones.
DEF_ASSIGN = re.compile(r"^\s*(?:local\s+)?([A-Za-z_]\w*)\s*=\s*function", re.M)
# A local alias of something already defined: `local Foo_orig = Foo`.
DEF_ALIAS = re.compile(r"^\s*local\s+([A-Za-z_]\w*)\s*=\s*[A-Za-z_]\w*\s*;?\s*$", re.M)
XML_NAME = re.compile(r'name="([^"$]+)"')


# Lua comments, which are not code. The guild bank's XML carries a call to
# GuildBankItemButton_OnUpdate with two dashes in front of it, and reading that
# as a call made a function nobody invokes look like a missing one.
BLOCK_COMMENT = re.compile(r"--\[(=*)\[.*?\]\1\]", re.S)
LINE_COMMENT = re.compile(r"--[^\n]*")


def read(path):
    try:
        return path.read_text(errors="ignore")
    except OSError:
        return ""


def without_comments(text):
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def known_names():
    """Everything a Lua chunk could reasonably find already defined."""
    names = set()
    for cpp in SRC.glob("*.cpp"):
        s = read(cpp)
        names |= set(re.findall(r'\{"(\w+)"\s*,', s))          # luaL_Reg tables
        names |= set(re.findall(r'set\("(\w+)"', s))           # region methods
        names |= set(re.findall(r'lua_setglobal\(\s*\w+\s*,\s*"(\w+)"', s))
        names |= set(re.findall(r'"function (\w+)\(', s))      # bootstrap Lua
        names |= set(re.findall(r'"(\w+)\s*=\s*function', s))
        # The counting stubs, which are defined by looping over a Lua list of
        # names rather than one at a time. Missing them made GetNumBankSlots,
        # GetInventoryItemCount and thirty others read as undefined when they
        # answer zero perfectly well.
        for block in re.findall(r"local counting = \{(.*?)\}", s, re.S):
            names |= set(re.findall(r"'(\w+)'", block))
    for lua in FRAMEXML.glob("*.lua"):
        s = read(lua)
        names |= set(DEF_FUNC.findall(s)) | set(DEF_ASSIGN.findall(s))
    # Addons define globals for each other: the talent frame calls
    # GlyphFrame_Update and loads Blizzard_GlyphUI to get it.
    for lua in ADDONS.glob("*/*.lua"):
        s = read(lua)
        names |= set(DEF_FUNC.findall(s)) | set(DEF_ASSIGN.findall(s))
    return names


def audit(addon_dir, known):
    body, defined = "", set()
    for f in sorted(addon_dir.glob("*.lua")) + sorted(addon_dir.glob("*.xml")):
        s = read(f)
        body += without_comments(s)
        defined |= set(DEF_FUNC.findall(s)) | set(DEF_ASSIGN.findall(s))
        defined |= set(DEF_ALIAS.findall(s))
        defined |= set(XML_NAME.findall(s))   # a frame's name is a global too
    return sorted(c for c in set(CALL.findall(body))
                  if c not in known and c not in defined)


def main():
    if not ADDONS.is_dir():
        print(f"no addons at {ADDONS}")
        return 0
    known = known_names()
    wanted = sys.argv[1] if len(sys.argv) > 1 else None

    rows = []
    for d in sorted(p for p in ADDONS.iterdir() if p.is_dir()):
        if wanted and d.name != wanted:
            continue
        rows.append((len(missing := audit(d, known)), d.name, missing))
    if not rows:
        print(f"no addon named {wanted}")
        return 0

    rows.sort()
    for count, name, missing in rows:
        if wanted or count == 0:
            print(f"{count:4}  {name}")
            for m in missing:
                print(f"        {m}")
        else:
            print(f"{count:4}  {name}")
    total = len({m for _, _, ms in rows for m in ms})
    print(f"\n{len(known)} names known; {total} distinct still missing "
          f"across {len(rows)} addon(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
