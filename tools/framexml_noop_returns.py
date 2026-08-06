#!/usr/bin/env python3
"""Widget no-ops whose answer someone reads.

    tools/framexml_noop_returns.py

THE GAP THIS FILLS

`framexml_unbound_widget_methods` asks whether a method resolves at all, and
counts the no-op allowlist as answered. For "does this call raise on the spot"
that is right. It is wrong for the caller that uses what comes back: a no-op
returns nil, and nil compared against a number raises exactly as an absent
method does — one line later, in a different function, which is why it reads
as a different bug.

GetFieldSize is the case that prompted this. It sat in the allowlist, so both
of the existing sweeps called it answered. Its one caller in all of FrameXML
is GuildEventLog_Update, which reads it into `max` and compares a running byte
count against it. The comparison raised on the first guild event carrying a
message and the SetText at the end of the function never ran, so the log was
blank whenever it had anything in it and correct whenever it did not.

WHAT IT LOOKS FOR

Calls to an allowlist-only method whose result is consumed on the same line,
split by what the consumption does:

  raises   arithmetic, comparison, concatenation, indexing, or a `for` limit —
           nil in any of these is an error, so the enclosing function stops
  silent   assigned to a name, or returned, or passed as an argument — the nil
           travels, and where it lands has to be read
  fine     called for effect, or tested for truth (nil is falsy, which is what
           a no-op is for) — not reported

WHAT IT CANNOT SEE

Where a nil goes once assigned. The silent tier is a list to read, not a list
of bugs; most no-ops are no-ops because nothing wanted the answer. It also
cannot tell a widget from a plain Lua table. Names FrameXML defines on its own
tables are subtracted, which covers the common case — FramePositionDelegate's
GetUIPanel was four of the first five findings — but a method *assigned* from
another function rather than declared is not seen. Check what the receiver is
before acting on a row.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_provides import noop_widget_methods  # noqa: E402

import sys as _s; _s.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files

ROOT = Path(__file__).resolve().parent.parent
XML = ROOT / "Data/interface"

#: Consuming the answer in one of these raises when the answer is nil.
RAISES = re.compile(r"^\s*(?:[-+*/%^<>]|[<>=~]=|\.\.|\[)")
#: ...whether it comes after the call or before it. Before means stepping back
#: over the receiver first — `a .. frame:Foo()` has the concat three tokens
#: from the call, not next to it, and a check anchored on the call itself finds
#: nothing at all. Both halves of this file's first draft were anchored there.
RAISES_BEFORE = re.compile(r"(?:[-+*/%^<>]|[<>=~]=|\.\.)\s*$")
#: The answer is kept or handed on, and the nil surfaces somewhere else.
TRAVELS = re.compile(r"(?:\blocal\s+[\w,\s]+=|[^=<>~]=|\breturn\b|,)\s*$")
#: What a receiver expression is made of, walking backwards from the colon.
RECEIVER = set("_.[]\"'")


def strip(text, is_xml):
    """Blank out comments in place, keeping every offset where it was.

    Deleting them shifts every line number after the first comment, and a
    report that sends you to the wrong line costs more than it saves. Newlines
    survive so line counts hold; everything else becomes a space.
    """
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    if is_xml:
        text = re.sub(r"<!--.*?-->", blank, text, flags=re.S)
    return re.sub(r"--\[\[.*?\]\]|--[^\n]*", blank, text, flags=re.S)


def close_paren(text, open_at):
    """Index just past the ) matching the ( at open_at, or None."""
    depth, i = 0, open_at
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        elif text[i] == "\n":
            return None
        i += 1
    return None


def main():
    # Only files the loader opens — GlueXML is refused by name and its
    # calls say nothing about this client.
    files = sorted(loaded_files(XML))
    bodies = {p: strip(p.read_text(errors="ignore"), p.suffix == ".xml")
              for p in files}

    # Methods FrameXML puts on its own tables. `:Method()` looks the same
    # whether the receiver is a widget or a plain table, and FramePositionDelegate
    # — a table with a dozen methods of its own — supplied four of this sweep's
    # first five findings. The sibling sweep subtracts these for the same reason.
    defined = set()
    for text in bodies.values():
        defined |= set(re.findall(r"\bfunction\s+[\w.]+[.:](\w+)\s*\(", text))
        defined |= set(re.findall(r"[\w.]+\.(\w+)\s*=\s*function", text))

    noops = noop_widget_methods() - defined
    raises, travels = [], []

    for p in files:
        text = bodies[p]
        for m in re.finditer(r":([A-Za-z_]\w*)\s*\(", text):
            name = m.group(1)
            if name not in noops:
                continue
            end = close_paren(text, text.index("(", m.end() - 1))
            if end is None:
                continue
            line_no = text.count("\n", 0, m.start()) + 1
            line = text[text.rfind("\n", 0, m.start()) + 1:
                        text.find("\n", m.start())].strip()
            # Everything on the line before the receiver, not before the call:
            # the receiver sits between the operator and the colon.
            head = text.rfind("\n", 0, m.start()) + 1
            recv, depth = m.start(), 0
            while recv > head and (text[recv - 1].isalnum()
                                   or text[recv - 1] in RECEIVER):
                ch = text[recv - 1]
                # `.` belongs to a receiver (`a.b:Foo()`) but `..` does not,
                # and a walk that cannot tell them apart steps over a
                # concatenation into the assignment beyond it — which is how
                # `_G["INPUT_"..self:GetInputLanguage()]` read as a value merely
                # being kept rather than one being concatenated.
                #
                # Unless the `..` is inside brackets, where it builds the
                # receiver's own name and has nothing to do with the call:
                # `_G["Sparkle"..i.."Highlight"]:SetModelScale(...)` joins
                # strings to find a frame. Both spellings are everywhere, and a
                # depth-blind rule reports the second kind as the first.
                if ch == "]":
                    depth += 1
                elif ch == "[":
                    depth -= 1
                elif depth == 0 and ch == "." and text[recv - 2:recv] == "..":
                    break
                recv -= 1
            before = text[head:recv]
            after = text[end:text.find("\n", end)]
            row = (name, f"{p.name}:{line_no}", line[:78])
            # `x:Foo() and x:Foo() ~= y` is not a nil comparison — the first
            # call guards the second, and nil short-circuits before the
            # operator is reached. Blizzard writes this a lot.
            call = text[recv:end]
            guarded = f"{call} and " in text[head:recv + len(call)] or \
                      f"{call} and " in line
            if not guarded and (RAISES.match(after) or RAISES_BEFORE.search(before)):
                raises.append(row)
            elif TRAVELS.search(before):
                travels.append(row)

    print(f"{len(noops)} widget methods answered only by the no-op\n")
    print(f"{len(raises)} whose answer is used where nil raises:\n")
    for name, where, line in raises:
        print(f"  {where}  [{name}]")
        print(f"      {line}")
    if not raises:
        print("  (none)")
    print(f"\n{len(travels)} whose answer is kept or handed on:\n")
    for name, where, line in travels:
        print(f"  {where}  [{name}]")
        print(f"      {line}")
    if not travels:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
