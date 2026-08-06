#!/usr/bin/env python3
"""This client's own slash commands, and whether FrameXML's chat can reach them.

    tools/client_command_bridge_check.py

WHAT HANDING CHAT OVER TOOK AWAY

ChatPanel keeps a registry of slash commands — about a hundred and ninety
aliases — and it was dispatched from exactly one place: this client's own chat
input, in chat_panel.cpp, after SlashCmdList and before the emote fallthrough.

FrameXML's edit box does not know that registry exists. ChatEdit_ParseText
walks SlashCmdList and consults nothing beyond it, so the moment chat became
FrameXML's, every command the registry answered and the interface did not
stopped being typeable. Seventy-one of them: /unstuck and its two siblings,
/coords, /loc, /whereami, /zone, /transport, /threat, /instance, /difficulty,
/players, /online, /screenshot, the helm and cloak toggles, the GM helpers.

Nothing raised. Typing one produced FrameXML's "Type /help for a listing of
commands", which reads exactly like a command that never existed.

WHAT IT COUNTS

Registry aliases with no FrameXML SLASH_* equivalent **and** no bridge. The
bridge is the bootstrap chunk in addon_manager.cpp that walks
__WoweeClientCommandNames() after FrameXML has loaded and registers whatever
the interface has not already claimed. With it, this is zero; without it, it is
the full seventy-one.

So the number is the answer to a real question rather than a proxy for one, and
removing the bridge is what makes it fail.

WHY THE BRIDGE RUNS AFTER FRAMEXML AND NOT BEFORE

The taken set has to be complete or a client command shadows an interface one.
That fault has happened here: SLASH_FOLLOW1 through 7 are FrameXML's, the
client's chat tried SlashCmdList first, and /follow landed on a no-op while a
working followTarget sat unused.

WHAT IT CANNOT SEE

Whether the command *works* once reached — only that the name resolves. And it
reads aliases out of the C++ by pattern, so a command registered some other way
than an aliases() override is invisible; the count printed first is what says
whether that parse is still finding them.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files, without_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
COMMANDS = ROOT / "src/ui/chat/commands"
INTERFACE = ROOT / "Data/interface"
BRIDGE = ROOT / "src/addons/addon_manager.cpp"


def registry_aliases():
    names = set()
    for path in COMMANDS.glob("*.cpp"):
        text = path.read_text(errors="ignore")
        for m in re.finditer(r"aliases\(\)\s*const\s*override\s*\{\s*return\s*\{([^}]*)\}", text):
            for raw in m.group(1).split(","):
                alias = raw.strip().strip('"').lower()
                if alias:
                    names.add(alias)
    return names


def framexml_slash_names():
    names = set()
    for path in loaded_files(INTERFACE):
        text = without_comments(path.read_text(errors="ignore"))
        for m in re.finditer(r'SLASH_[A-Z0-9_]+\s*\d*\s*=\s*"/([A-Za-z0-9]+)"', text):
            names.add(m.group(1).lower())
    return names


def bridged():
    """Does the bootstrap actually register the registry into SlashCmdList?

    Both halves, because either alone does nothing: the names have to be asked
    for, and something has to be written into SlashCmdList with them.
    """
    text = BRIDGE.read_text(errors="ignore")
    return ("__WoweeClientCommandNames" in text
            and "__WoweeRunClientCommand" in text
            and "SlashCmdList[key]" in text)


def main():
    aliases = registry_aliases()
    slash = framexml_slash_names()
    have_bridge = bridged()

    print(f"{len(aliases)} client command aliases, {len(slash)} FrameXML slash names, "
          f"bridge {'present' if have_bridge else 'ABSENT'}")
    if not aliases:
        print("  CANARY: no aliases parsed out of src/ui/chat/commands — "
              "the count below is meaningless.")
    print()

    missing = sorted(a for a in aliases if a not in slash)
    unreachable = [] if have_bridge else missing
    print(f"{len(unreachable)} client command(s) FrameXML's chat cannot reach:\n")
    for name in unreachable:
        print(f"  /{name}")
    if not unreachable:
        print(f"  (none — {len(missing)} have no FrameXML equivalent and the "
              f"bridge carries all of them)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
