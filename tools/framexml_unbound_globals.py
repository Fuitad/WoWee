#!/usr/bin/env python3
"""Globals FrameXML calls that nothing defines.

An unanswered call *raises*, where an unfired event only goes unheard — so
this is the sharpest of the sweeps. InspectUnit and StartDuel sat here for
months behind the unit right-click menu.

File-agnostic on purpose. Every per-element sweep so far has been crippled by
its own hand-written file list, so this one maps nothing: it reports the file
each name is called from and lets the reader decide whether that file is on
screen.
"""
import re
import sys
from pathlib import Path as _ToolPath
sys.path.insert(0, str(_ToolPath(__file__).resolve().parent))
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
XML = ROOT / "Data/interface"

# Bound on the C++ side.
# One source of truth — see framexml_provides. Working this out per tool is
# how six sweeps came to disagree about what the client answers.
from framexml_provides import globals_provided, widget_methods_provided

bound = globals_provided() | widget_methods_provided()

# Defined in FrameXML itself, as a function or assigned one.
defined = set()
for path in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    t = path.read_text(errors="ignore")
    defined |= set(re.findall(r"\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", t))
    defined |= set(re.findall(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*", t, re.M))
    defined |= set(re.findall(r"\blocal\s+([A-Za-z_][A-Za-z0-9_]*)", t))
    # Frame names become globals.
    defined |= set(re.findall(r'name="\$?parent?([A-Za-z0-9_]+)"', t))
    defined |= set(re.findall(r'name="([A-Za-z0-9_]+)"', t))

LUA = {"assert","collectgarbage","dofile","error","getfenv","getmetatable","ipairs",
       "load","loadstring","next","pairs","pcall","print","rawequal","rawget","rawset",
       "select","setfenv","setmetatable","tonumber","tostring","type","unpack","xpcall",
       "require","string","table","math","os","io","coroutine","debug","format","gsub",
       "strsub","strlen","strupper","strlower","strfind","strjoin","strsplit","strtrim",
       "strrep","strrev","strbyte","strchar","tinsert","tremove","tsort","wipe","date",
       "time","difftime","abs","ceil","floor","max","min","mod","random","sqrt","bit"}

def strip_comments(text: str) -> str:
    """Drop Lua line comments, XML comments and the insides of string literals.

    Not cosmetic: two of the six names this flagged on the candidate elements
    were commented-out calls — --FCFDock_ForceTabSort and
    --GuildBankItemButton_OnUpdate — which read exactly like missing bindings
    and are not called at all.

    Strings for the same reason, and the case is worse: a Lua pattern is a
    string full of parentheses, so `strmatch(name, "DropDownList(%d+)")` read
    as a call to a global named DropDownList. That put "every dropdown in the
    interface raises as it opens" at the top of a report, which is alarming and
    wrong. Quotes are kept so the token still ends where it did.
    """
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    text = re.sub(r"--\[\[.*?\]\]|--[^\n]*", "", text, flags=re.S)
    return re.sub(r"'[^'\n]*'|\"[^\"\n]*\"", '""', text)


# Every function this interface hangs off a script handler, by name. A call
# inside one of these runs on its own the moment its panel loads, shows or
# hears an event; a call inside a dialog's OnAccept — or an OnClick, which is
# why those four events and no others are counted here — waits for someone to
# press a button that may never appear.
#
# This is the whole difference between the rows worth reading and the rest. The
# list below stood at four hundred and thirty-eight and was ignored for it,
# with SortBGList sitting in the middle: called from PVPBattlegroundFrame_OnShow,
# so the battleground panel raised as it opened.
AUTORUN = set()
for path in list(XML.rglob("*.xml")):
    t = strip_comments(path.read_text(errors="ignore"))
    AUTORUN |= set(re.findall(r'<On(?:Load|Show|Event|Update)\s+function="([A-Za-z_]\w*)"', t))
    for body in re.findall(r"<On(?:Load|Show|Event|Update)[^>]*>(.*?)</On\w+>", t, re.S):
        AUTORUN |= set(re.findall(r"(?<![\w.:])([A-Z][A-Za-z0-9_]*)\s*\(", body))
for path in list(XML.rglob("*.lua")):
    t = strip_comments(path.read_text(errors="ignore"))
    AUTORUN |= set(re.findall(
        r'SetScript\s*\(\s*"On(?:Load|Show|Event|Update)"\s*,\s*([A-Za-z_]\w*)', t))


def enclosing(text, pos):
    """The Lua function a position sits in, or None."""
    head = text.rfind("\nfunction ", 0, pos)
    local = text.rfind("\nlocal function ", 0, pos)
    start = max(head, local)
    if start < 0:
        return None
    m = re.match(r"\n(?:local )?function\s+([\w:.]+)", text[start:])
    return m.group(1) if m else None


calls = {}
autorun_hits = {}
for path in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    t = strip_comments(path.read_text(errors="ignore"))
    for m in re.finditer(r"(?<![\w.:])([A-Z][A-Za-z0-9_]*)\s*\(", t):
        calls.setdefault(m.group(1), set()).add(path.name)
        fn = enclosing(t, m.start())
        # A handler named directly, or one whose own name says what runs it.
        if fn and (fn in AUTORUN or re.search(r"_On(Load|Show|Event|Update)$", fn)):
            autorun_hits.setdefault(m.group(1), set()).add(f"{path.name}:{fn}")

missing = {n: f for n, f in calls.items()
           if n not in bound and n not in defined and n not in LUA}

print(f"{len(calls)} distinct globals called, {len(bound)} bound, "
      f"{len(defined)} defined in FrameXML\n")
print(f"{len(missing)} called and nowhere defined.\n")

# Split rather than sorted, because the two halves want different reactions.
live = {n: autorun_hits[n] for n in missing if n in autorun_hits}
print(f"{len(live)} of them from a function that runs on its own — these raise "
      f"as their panel opens:\n")
for n in sorted(live):
    print(f"  {n:<36} {', '.join(sorted(live[n])[:2])}")
if not live:
    print("  (none)")

rest = {n: f for n, f in missing.items() if n not in autorun_hits}
print(f"\n{len(rest)} reached only from something a player has to do first "
      f"(a dialog's accept, a menu click), or not reached at all:\n")
for n in sorted(rest, key=lambda k: (-len(rest[k]), k))[:25]:
    print(f"  {n:<36} {', '.join(sorted(rest[n])[:3])}")
if len(rest) > 25:
    print(f"  ... and {len(rest) - 25} more")
