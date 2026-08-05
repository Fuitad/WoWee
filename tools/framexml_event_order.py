#!/usr/bin/env python3
"""Event arguments in the wrong positions, where the count is right.

    tools/framexml_event_order.py

Both arity sweeps ask how MANY values an event carries. Neither can see which
value went where, and that is the fault they missed: UNIT_SPELLCAST_SUCCEEDED
fired two values where two were read, and the second was the spell id sitting in
the spell name's place. Arity is a floor, not a contract.

WHAT IT COMPARES

FrameXML names what it unpacks — `local unit, name, rank = ...` — and the client
writes an expression per argument. Those pair off positionally, and the names on
one side say what the expressions on the other are supposed to be. A position
FrameXML calls `name`, `text`, `title`, `message`, `link` or `rank` that is
handed `std::to_string(somethingId)` is an id in a name's slot. A position it
calls `...ID`, `...index` or `...slot` handed a string literal or a name-shaped
variable is the reverse.

WHAT IT CANNOT SEE

Two positions of the same kind swapped — a bag id and a slot id the wrong way
round read alike from here. Nor an argument built by a helper: an event fired
through one call, as the spellcast events are now, is one expression and pairs
with nothing. That is not a hole so much as the reason the helper is better —
the order lives in one place instead of at nine call sites.

Nor whether the *value* is right, only whether it is the right kind of thing.

VERIFIED BOTH WAYS. Putting the spellcast fault back makes this report it and
taking it out again makes the report empty; a matcher that silently matches
nothing reads exactly like a clean tree, and the first draft of this did.
"""
import pathlib
import re
import sys

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
    for path in XML.rglob("*.lua"):
        text = re.sub(r"--\[\[.*?\]\]|--[^\n]*", "",
                      path.read_text(errors="ignore"), flags=re.S)
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
