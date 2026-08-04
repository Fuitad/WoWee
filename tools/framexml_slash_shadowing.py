#!/usr/bin/env python3
"""Client slash commands that FrameXML shadows with a handler that does nothing.

This client's chat tries SlashCmdList before its own registry and returns as
soon as it finds a handler — and dispatchSlashCommand returns true even when
that handler *errors*. So any command FrameXML defines wins, working or not.

/follow was found this way: SLASH_FOLLOW1..7 cover /f, /follow and /fol, all
landing on a no-op FollowUnit while the client's own /follow sat unreachable
behind it.

Reports only where both sides claim the same command AND FrameXML's handler
bottoms out in a stub or a missing global — that is the pairing that loses a
working feature.

KNOWN FALSE POSITIVE: a handler whose *first* branch is a Battle.net call but
whose else is real. /ignore reads BNet_GetPresenceID, which resolves to
GetAutoCompletePresenceID answering nil here, and falls through to
AddOrDelIgnore; /leave gates BNLeaveConversation behind
`nameNum > MAX_WOW_CHAT_CHANNELS` and otherwise calls LeaveChannelByName. Both
are bound and both work. Read the whole handler before believing a shadow —
this sweep sees the first call, not the branch that runs.
"""
import re
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
XML = ROOT / "Data/interface"

STUBS = {"lua_ReturnNil", "lua_ReturnZero", "lua_ReturnFalse", "lua_ReturnNothing",
         "lua_ContainerNoOp", "lua_ContainerFalse"}

# Bound at all — the loose pattern, because a lambda body full of braces is
# still a binding and matching only trivial ones made every real
# implementation read as missing. InspectUnit was reported dead that way.
# One source of truth for what is answered — see framexml_provides. Working
# this out per tool is how six sweeps came to disagree about it.
import sys as _sys
_sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_provides import globals_provided

bound, noop = globals_provided(), set()
for f in (ROOT / "src/addons").glob("*.cpp"):
    s = f.read_text(errors="ignore")
    # Does nothing: a named stub, or a lambda whose whole body discards L.
    for m in re.finditer(r'\{"([A-Za-z0-9_]+)",\s*(?:&)?\s*(lua_[A-Za-z0-9_]+)\}', s):
        if m.group(2) in STUBS:
            noop.add(m.group(1))
    for m in re.finditer(r'\{"([A-Za-z0-9_]+)",\s*\[\]\(lua_State\*\s*L\)\s*->\s*int\s*\{\s*\(void\)L;\s*return 0;\s*\}\}', s):
        noop.add(m.group(1))

# Defined in FrameXML itself — ChatFrame_DisplayUsageError and ShowUIPanel are
# Lua, not bindings, and are not this client's business.
defined = set()
for path in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    t = path.read_text(errors="ignore")
    defined |= set(re.findall(r"\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", t))
    defined |= set(re.findall(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*", t, re.M))

# SLASH_<NAME><n> = "/cmd"
slash = {}
for path in XML.rglob("*.lua"):
    for m in re.finditer(r'SLASH_([A-Z0-9_]+?)(\d)\s*=\s*"(/[^"]+)"', path.read_text(errors="ignore")):
        slash.setdefault(m.group(1), set()).add(m.group(3).lower())

# SlashCmdList["NAME"] = function(...) ... end  -> globals it calls
handlers = {}
for path in XML.rglob("*.lua"):
    t = path.read_text(errors="ignore")
    for m in re.finditer(r'SlashCmdList\["([A-Z0-9_]+)"\]\s*=\s*function\b(.{0,700}?)\nend', t, re.S):
        handlers.setdefault(m.group(1), "")
        handlers[m.group(1)] += m.group(2)

# The client's own registry: aliases() { return {"follow", "f"}; }
client = {}
for f in (ROOT / "src/ui/chat/commands").glob("*.cpp"):
    s = f.read_text(errors="ignore")
    for m in re.finditer(r"aliases\(\)[^{]*\{\s*return\s*\{([^}]*)\}", s):
        for a in re.findall(r'"([^"]+)"', m.group(1)):
            client["/" + a.lower()] = f.name

print(f"{len(slash)} SLASH names, {len(handlers)} handlers, "
      f"{len(client)} client commands\n")

rows = []
for name, cmds in sorted(slash.items()):
    body = handlers.get(name)
    if not body:
        continue
    overlap = sorted(c for c in cmds if c in client)
    if not overlap:
        continue
    calls = set(re.findall(r"(?<![\w.:])([A-Z][A-Za-z0-9_]*)\s*\(", body))
    dead = sorted(c for c in calls
                  if c in noop or (c not in bound and c not in defined))
    if dead:
        rows.append((name, overlap, dead, client[overlap[0]]))

print(f"{len(rows)} client command(s) shadowed by a handler that does nothing:\n")
for name, cmds, dead, where in rows:
    print(f"  {' '.join(cmds):<26} SlashCmdList[{name}] -> {', '.join(dead)}")
    print(f"      client has it in {where}")
