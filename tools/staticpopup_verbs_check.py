#!/usr/bin/env python3
"""Globals a static popup's buttons call, that nothing answers.

    tools/staticpopup_verbs_check.py

A static popup is the last thing between a player and an irreversible action:
"this item will bind to you", "abandon your pet", "log out". Its OnAccept is a
button somebody presses on purpose. If that body calls a name nothing binds, the
click raises — and because the dialog is built, shown, and takes the click
first, the failure looks like the game ignoring a decision the player already
made, rather than like a missing feature.

The unbound-globals sweep already sees all of these, and files them under
"reached only from something a player has to do first". That grouping is right
for a menu nobody opens; it is wrong for these, because the popup is *shown by
the client's own event handler* and the player is being asked to answer it.
PetRename was in that pile until the naming dialog was found taking a name,
asking for confirmation, and raising.

WHAT IT LOOKS FOR

Every StaticPopupDialogs entry, every function-valued hook in it, and every
capitalised call inside those hooks. Method calls are skipped — `self:GetText()`
and `self.editBox:SetText()` are widget methods and belong to the other sweep.

WHAT IT CANNOT SEE

Whether the popup is reachable. Some are shown only by messages this client is
never sent: the arena team ones need an arena invite, and CONFIRM_ACCEPT_SOCKETS
needs the socketing UI. A name here is a raise *if* the dialog opens, and
whether it opens is a separate question the event-gap report answers.

Nor a call inside a helper the hook calls. This reads the hook bodies only.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
XML = ROOT / "Data/interface"
ADDONS = ROOT / "src/addons"


def bound_names():
    """Every global the client binds or FrameXML itself defines."""
    out = set()
    for path in ADDONS.rglob("*.cpp"):
        text = path.read_text(errors="ignore")
        out |= set(re.findall(r'\{\s*"(\w+)"', text))
        out |= set(re.findall(r'lua_setglobal\(L_?\s*,\s*"(\w+)"', text))
        # The bootstrap's name lists, which are Lua source inside C strings.
        out |= set(re.findall(r"'(\w+)'", text))
        out |= set(re.findall(r'^\s*"?\s*(\w+)\s*=\s*function', text, re.M))
    for path in XML.rglob("*.lua"):
        text = path.read_text(errors="ignore")
        out |= set(re.findall(r"^\s*function\s+([A-Za-z_]\w*)\s*\(", text, re.M))
        out |= set(re.findall(r"^([A-Za-z_]\w*)\s*=\s*function", text, re.M))
    return out


# A call that is not preceded by ':' or '.', which would make it a method.
CALL = re.compile(r"(?<![:.\w])([A-Z][A-Za-z0-9_]{2,})\s*\(")
HOOK = re.compile(r"(\bOn\w+|\bEditBoxOn\w+)\s*=\s*function\b")


def hooks(body):
    """Each function-valued hook in a popup's table, by name and body.

    Sliced from one hook's `function` to the next hook's name rather than by
    matching `end`, because these bodies nest conditionals and a naive match
    stops at the first one.
    """
    starts = [(m.start(), m.group(1)) for m in HOOK.finditer(body)]
    for i, (pos, name) in enumerate(starts):
        end = starts[i + 1][0] if i + 1 < len(starts) else len(body)
        yield name, body[pos:end]


def main():
    bound = bound_names()
    popups = ROOT / "Data/interface/framexml/staticpopup.lua"
    text = popups.read_text(errors="ignore")
    text = re.sub(r"--\[\[.*?\]\]", "", text, flags=re.S)
    blocks = re.findall(r'StaticPopupDialogs\["(\w+)"\]\s*=\s*\{(.*?)\n\};',
                        text, re.S)

    missing = {}
    for name, body in blocks:
        for hook_name, hook_body in hooks(body):
            for call in CALL.findall(hook_body):
                if call not in bound:
                    missing.setdefault(call, set()).add(f"{name}.{hook_name}")

    print(f"{len(blocks)} static popups parsed\n")
    print(f"{len(missing)} name(s) a popup button calls and nothing answers:\n")
    for name in sorted(missing):
        where = sorted(missing[name])
        tail = f" (+{len(where) - 3})" if len(where) > 3 else ""
        print(f"  {name:32} {', '.join(where[:3])}{tail}")
    if not missing:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
