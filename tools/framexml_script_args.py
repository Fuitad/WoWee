#!/usr/bin/env python3
"""Inline handler bodies naming an argument their signature does not carry.

    tools/framexml_script_args.py

An <OnEnter> body is emitted as a Lua function, and the emitter decides its
parameter list from the script's name - `self, motion` for OnEnter, `self,
button` for OnClick, `self, offset` for OnVerticalScroll. A body that names
something the list does not carry does not fail: Lua reads it as a global,
finds nothing, and hands back nil. The line runs, the comparison takes the
wrong branch, and nothing anywhere says a value went missing.

Both halves of that go wrong the same way and neither raises:

  * the emitter's list is short - the handler really does receive the value in
    WoW and this client does not name it, which is how every scroll frame in
    the interface scrolled to nil until OnVerticalScroll was given `offset`
  * the body is wrong - it names an argument belonging to a different handler,
    usually because the code was moved between two

WHAT IT LOOKS FOR

Every inline script body in the XML, against the emitter's own table read out
of framexml_emitter.cpp rather than copied here. Any identifier used in the
body that is a parameter name of *some* handler but not of this one, and that
the body does not declare as a local of its own, is reported.

WHAT IT CANNOT SEE

A body that calls a named function and lets that function be short - the
argument is passed correctly and lost one level down. It also cannot see
whether the emitter's list is right in the first place, only whether it and
the bodies agree; a name missing from every signature and every body is
invisible to both sides of the comparison.

False positives come from ordinary variables that happen to share a parameter
name - `name`, `value`, `text` and `parent` are common. Ones assigned in the
body are subtracted; ones read from a global table are not, so check what a
row's name refers to before treating it as a gap.

WHAT IS LEFT, AND WHY

Two, both the same line of Blizzard's own: character creation's scroll frames
call GlueScrollFrame_OnScrollRangeChanged(self, yrange) from their OnLoad,
where there is no yrange to pass. It is deliberate - the function opens with
`if ( not yrange ) then yrange = self:GetVerticalScrollRange() end`, which this
client answers - so the nil is expected on both sides and nothing is missing.
The ceiling is there for the third, which will not be.
"""
import re
import sys
from pathlib import Path

import sys as _s; _s.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files

ROOT = Path(__file__).resolve().parent.parent
XML = ROOT / "Data/interface"
EMITTER = ROOT / "src/ui/framexml_emitter.cpp"

#: Parameter names that are also functions everything can reach. A body
#: calling `min(amount, guildBankMoney)` is not reading a handler argument, and
#: the two spellings are indistinguishable by shape.
GLOBALS = {"min", "max", "next", "type", "text"}


def signatures():
    """The emitter's script-to-parameters table, read from the emitter.

    Asked of the one place that decides, for the reason framexml_provides
    exists: a copy here would be right on the day it was written and wrong the
    first time a handler gained an argument.
    """
    src = EMITTER.read_text(errors="ignore")
    body = src[src.index("scriptParameters"):src.index("scriptSignature")]
    table, pending = {}, []
    for m in re.finditer(r'script == "(\w+)"|return "([^"]*)";', body):
        if m.group(1):
            pending.append(m.group(1))
        else:
            for name in pending:
                table[name] = [p.strip() for p in m.group(2).split(",")]
            pending = []
    # The fallthrough at the end is every handler not named above, and it takes
    # only self. Recorded under a key nothing matches so the vocabulary below
    # still counts `self` as a parameter somewhere.
    table.setdefault("", ["self"])
    return table


def main():
    table = signatures()
    vocabulary = {p for params in table.values() for p in params}
    default = table[""]

    rows = []
    # Only files the loader opens; see framexml_source.loaded_files.
    for path in sorted(q for q in loaded_files(XML)
                       if q.suffix.lower() == ".xml"):
        # Blanked in place rather than deleted, so every offset stays where it
        # was. Deleting shifts every line number after the first comment, and a
        # report that sends you to the wrong line costs more than it saves.
        text = re.sub(r"<!--.*?-->",
                      lambda m: re.sub(r"[^\n]", " ", m.group(0)),
                      path.read_text(errors="ignore"), flags=re.S)
        for m in re.finditer(r"<(On\w+)\s*(?:/>|>(.*?)</\1>)", text, re.S):
            script, body = m.group(1), m.group(2)
            if not body or not body.strip():
                continue
            params = set(table.get(script, default))
            # Its own locals, and the loop variables that are locals too.
            # `local min;` declares one as surely as `local min = 0` does,
            # and stopping only at `=` or a newline missed the semicolon
            # spelling - which is how UIPanelScrollFrameTemplate, the busiest
            # scroll template in the interface, read as broken.
            local = set(re.findall(r"\blocal\s+([\w,\s]+?)\s*[=;\n]", body))
            declared = {n.strip() for group in local for n in group.split(",")}
            declared |= set(re.findall(r"\bfor\s+([\w,\s]+?)\s*(?:=|\bin\b)", body))
            declared |= set(re.findall(r"\bfunction\s*\(([^)]*)\)", body))
            declared = {n.strip() for entry in declared for n in entry.split(",")}

            # Fields and string contents first. `NORMAL_FONT_COLOR.r` is not
            # a use of `r`, and neither is "text" - and between them those two
            # were nine of every ten findings in the first run, which is the
            # difference between a report worth reading and a list.
            # Comments, strings and fields first, in that order.
            # `-- reposition the up and down buttons` is not a use of `down`,
            # "text" is not a use of `text`, and `NORMAL_FONT_COLOR.r` is not
            # a use of `r`. Between them those three were nine of every ten
            # findings in the first run, which is the difference between a
            # report worth reading and a list.
            bare = re.sub(r"--\[\[.*?\]\]|--[^\n]*", " ", body, flags=re.S)
            bare = re.sub(r"'[^'\n]*'|\"[^\"\n]*\"", " ", bare)
            bare = re.sub(r"[.:]\s*[A-Za-z_]\w*", " ", bare)
            used = set(re.findall(r"\b([A-Za-z_]\w*)\b", bare))
            missing = (used & vocabulary) - params - declared - GLOBALS
            for name in sorted(missing):
                line = text.count("\n", 0, m.start()) + 1
                rows.append((f"{path.name}:{line}", script, name))

    print(f"{len(table) - 1} handlers with a signature of their own; "
          f"{len(vocabulary)} distinct parameter names\n")
    print(f"{len(rows)} body/signature disagreement(s):\n")
    for where, script, name in rows:
        print(f"  {where}  <{script}> reads '{name}', which its signature "
              f"does not carry")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
