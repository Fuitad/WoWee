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
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
XML = ROOT / "Data/interface"

# Bound on the C++ side.
bound = set()
for f in (ROOT / "src/addons").glob("*.cpp"):
    s = f.read_text(errors="ignore")
    bound |= set(re.findall(r'\{"([A-Za-z0-9_]+)"\s*,', s))
    # Second registration style: lua_pushcfunction + lua_setglobal. Thirty-two
    # names go in this way, CreateFrame and GetScreenWidth among them, and
    # missing it made the whole report untrustworthy rather than merely short.
    bound |= set(re.findall(r'lua_setglobal\(\s*L_?\s*,\s*"([A-Za-z0-9_]+)"', s))
    # Names listed in the bootstrap's no-op / known-method tables count too.
    for blob in re.findall(r'"([A-Za-z0-9_=,\\n ]{40,})"', s):
        bound |= set(re.findall(r"([A-Za-z][A-Za-z0-9_]*)=1", blob))

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
    """Drop Lua line comments and XML comments.

    Not cosmetic: two of the six names this flagged on the candidate elements
    were commented-out calls — --FCFDock_ForceTabSort and
    --GuildBankItemButton_OnUpdate — which read exactly like missing bindings
    and are not called at all.
    """
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    return re.sub(r"--\[\[.*?\]\]|--[^\n]*", "", text, flags=re.S)


calls = {}
for path in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    t = strip_comments(path.read_text(errors="ignore"))
    for m in re.finditer(r"(?<![\w.:])([A-Z][A-Za-z0-9_]*)\s*\(", t):
        calls.setdefault(m.group(1), set()).add(path.name)

missing = {n: f for n, f in calls.items()
           if n not in bound and n not in defined and n not in LUA}

print(f"{len(calls)} distinct globals called, {len(bound)} bound, "
      f"{len(defined)} defined in FrameXML\n")
print(f"{len(missing)} called and nowhere defined:\n")
for n in sorted(missing, key=lambda k: (-len(missing[k]), k)):
    print(f"  {n:<36} {', '.join(sorted(missing[n])[:3])}")
