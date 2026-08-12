#!/usr/bin/env python3
"""Event arguments in the wrong positions, where the count is right.

    tools/framexml_event_order.py

Both arity sweeps ask how MANY values an event carries. Neither can see which
value went where, and that is the fault they missed: UNIT_SPELLCAST_SUCCEEDED
fired two values where two were read, and the second was the spell id sitting in
the spell name's place. Arity is a floor, not a contract.

WHAT IT COMPARES

FrameXML names what it unpacks - `local unit, name, rank = ...` - and the client
writes an expression per argument. Those pair off positionally, and the names on
one side say what the expressions on the other are supposed to be. A position
FrameXML calls `name`, `text`, `title`, `message`, `link` or `rank` that is
handed `std::to_string(somethingId)` is an id in a name's slot. A position it
calls `...ID`, `...index` or `...slot` handed a string literal or a name-shaped
variable is the reverse.

WHAT IT CANNOT SEE

Two positions of the same kind swapped - a bag id and a slot id the wrong way
round read alike from here. It is also blind when FrameXML unpacks into names
that say nothing, which is the more common way this hides.

ITEM_PUSH was both at once, and was a live example rather than a hypothetical.
mainmenubarbagbuttons.lua opens `local arg1, arg2 = ...` - no kind in either
name - and then treats arg1 as a bag identifier and arg2 as an icon:

    local id = self:GetParent():GetID();
    if ( id == arg1 ) then self:ReplaceIconTexture(arg2);

This client fired it with (itemId, count): two numbers where a bag id and a
texture were meant, so nothing here could tell, and the comparison never
matched. Fixed 2026-08-05.

The translation looked as though it needed a run, and did not - it is readable
from both ends. Item::GetBagSlot answers the container's own inventory slot,
and INVENTORY_SLOT_BAG_START is 19, so the four worn bags are 19 to 22 and the
backpack is INVENTORY_SLOT_BAG_0, 255. On the interface's side the bag buttons
take their ids from GetInventorySlotInfo("Bag0Slot") and friends - 20 to 23 -
while MainMenuBarBackpackButton declares id="0" in the XML. So a worn bag is
the server's slot plus one and the backpack is a special case.

The lesson is the one worth keeping: "this needs a look in-world" was wrong,
and what settled it was reading the *other* side rather than the same side
again. GetInventorySlotInfo was three greps away the whole time. Nor an argument built by a helper: an event fired
through one call, as the spellcast events are now, is one expression and pairs
with nothing. That is not a hole so much as the reason the helper is better -
the order lives in one place instead of at nine call sites.

Nor whether the *value* is right, only whether it is the right kind of thing.

VERIFIED BOTH WAYS. Putting the spellcast fault back makes this report it and
taking it out again makes the report empty; a matcher that silently matches
nothing reads exactly like a clean tree, and the first draft of this did.
"""
import pathlib
import re
import sys
import pathlib as _pathlib
sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
from framexml_source import without_comments, loaded_files

ROOT = pathlib.Path(__file__).resolve().parent.parent
XML = ROOT / "Data/interface"
SRC = ROOT / "src"

FIRE = re.compile(
    r'(?:fireAddonEvent|addonEventCallbackRef\(\))\s*\(\s*"(\w+)"\s*,\s*\{(.*?)\}',
    re.S)
UNPACK = re.compile(r"local\s+([\w\s,]+?)\s*=\s*\.\.\.")

# What a position is called, and what it is therefore meant to hold.
NAMEISH = re.compile(r"(?i)name$|^name$|text$|title$|message$|link$|rank$")
IDISH = re.compile(r"(?i)id$|index$|slot$")


def looks_id(expr):
    return bool(re.search(
        r"(?i)to_string\(\s*[\w.:>()\[\]-]*(id|index|slot|guid)\w*\s*[\)]", expr))


def looks_name(expr):
    return expr.startswith('"') or bool(
        re.search(r"(?i)\b\w*(name|text|title|message|link)\w*\b", expr))


def split_args(body):
    """The comma-separated arguments, at brace depth zero."""
    body = body.strip()
    if not body:
        return []
    out, depth, cur = [], 0, ""
    for ch in body:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def fired():
    """Event -> the widest argument list fired for it."""
    out = {}
    for path in SRC.rglob("*.cpp"):
        for m in FIRE.finditer(path.read_text(errors="ignore")):
            args = split_args(m.group(2))
            prev = out.get(m.group(1))
            if prev is None or len(args) > len(prev):
                out[m.group(1)] = args
    return out


def unpacked():
    """Event -> [(the names it unpacks, the file)]."""
    out = {}
    for path in sorted(q for q in loaded_files(XML) if q.suffix.lower() == ".lua"):
        text = without_comments(path.read_text(errors="ignore"))
        # A handler serving exactly one event: the unpack at its top is that
        # event's signature.
        for m in re.finditer(
                r"function\s+\w+\s*\(\s*self\s*,\s*event\s*,\s*\.\.\.\s*\)(.*?)\nend",
                text, re.S):
            body = m.group(1)
            events = set(re.findall(r'event\s*==\s*"(\w+)"', body))
            u = UNPACK.search(body[:400])
            if u and len(events) == 1:
                names = [x.strip() for x in u.group(1).split(",") if x.strip()]
                out.setdefault(events.pop(), []).append((names, path.name))
        # And an unpack inside the branch that names an event.
        for m in re.finditer(
                r'\(\s*event\s*==\s*"(\w+)"\s*\)\s*then(.*?)(?=\belseif\b|\bend\b)',
                text, re.S):
            u = UNPACK.search(m.group(2))
            if u:
                names = [x.strip() for x in u.group(1).split(",") if x.strip()]
                out.setdefault(m.group(1), []).append((names, path.name))
    return out


def main():
    have, want = fired(), unpacked()
    rows, seen = [], set()
    for event, lists in want.items():
        args = have.get(event)
        if not args:
            continue
        for names, where in lists:
            for i, name in enumerate(names):
                if i >= len(args):
                    break
                expr = args[i]
                if NAMEISH.search(name) and looks_id(expr) and not looks_name(expr):
                    why = "an id in a name's place"
                elif IDISH.search(name) and looks_name(expr) and not looks_id(expr):
                    why = "a name in an id's place"
                else:
                    continue
                if (event, i) in seen:
                    continue
                seen.add((event, i))
                rows.append((event, i + 1, name, expr, where, why))

    print(f"{len(have)} events fired, {len(want)} with named unpacks\n")
    print(f"{len(rows)} argument(s) in the wrong position:\n")
    for event, pos, name, expr, where, why in sorted(rows):
        print(f"  {event:34} arg{pos} '{name}' <- {expr[:44]}")
        print(f"      {why}   [{where}]")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
