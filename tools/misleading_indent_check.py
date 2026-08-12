#!/usr/bin/env python3
"""A braceless `if` whose next-but-one line is indented as though it were guarded.

    if (owner_.addonEventCallbackRef())
        fire("SPELLS_CHANGED", {});
        if (craftingWindowOpen_) fire("TRADE_SKILL_UPDATE", {});

The second call is not guarded. It read as though it were, and it called a
std::function without the null check above it - which throws rather than doing
nothing. That one was live in spell_handler.cpp.

GCC has -Wmisleading-indentation and it is on here, but it turns *itself* off
partway through a translation unit: column tracking is dropped once a file or
its headers get large enough, and it says so in passing among thousands of
lines of build output. Both instances found on 2026-08-04 were found because
something else brought a reader to the line, not because the compiler said so.

    tools/misleading_indent_check.py

WHAT IT LOOKS FOR

A control statement with no opening brace, whose body is the following line,
followed by a line at the *same* indentation as that body. The third line is
outside the statement and dressed as though it were inside.

WHAT IT CANNOT SEE

Whether the deception matters. Sometimes the unguarded statement re-tests the
same condition and behaves identically - the one in handleAuraUpdate did - and
sometimes it is a call that must not run. Both are worth bracing; only one is a
bug. Read each before changing it.

Nor a body written across several lines, or one that is itself a block. Those
are the shapes where the reader is not misled anyway.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTROL = re.compile(r"^(\s*)(if|for|while|else if)\s*\(.*\)\s*$")
ELSE = re.compile(r"^(\s*)else\s*$")


def indent_of(line):
    return len(line) - len(line.lstrip())


def interesting(line):
    """A line that is code rather than blank, comment or preprocessor."""
    s = line.strip()
    if not s or s.startswith(("//", "/*", "*", "#")):
        return False
    return True


def scan(path):
    lines = path.read_text(errors="ignore").split("\n")
    out = []
    for i, line in enumerate(lines):
        # Trailing comment removed before matching. Without that, a line ending
        # `) { // castCount(1) + spellId(4)` matched: the pattern ran greedily
        # through to a bracket inside the comment and the brace stopped
        # mattering. That was the tool's only hit and it was its own fault.
        code = line.split("//", 1)[0].rstrip()
        m = CONTROL.match(code) or ELSE.match(code)
        if not m:
            continue
        head_indent = len(m.group(1))

        # The body: the next line that is code.
        j = i + 1
        while j < len(lines) and not interesting(lines[j]):
            j += 1
        if j >= len(lines):
            continue
        body = lines[j]
        if body.strip().startswith("{"):
            continue                      # braced, so nobody is misled
        body_indent = indent_of(body)
        if body_indent <= head_indent:
            continue                      # not a body at all
        # A body spanning more than one line is not the shape this is about.
        if not body.rstrip().endswith((";", "}")):
            continue

        # The line after it, at the same indentation, is outside and looks in.
        k = j + 1
        while k < len(lines) and not interesting(lines[k]):
            k += 1
        if k >= len(lines):
            continue
        after = lines[k]
        if indent_of(after) != body_indent:
            continue
        if after.strip().startswith(("else", "}", "break", "continue")):
            continue
        out.append((j + 1, k + 1, body.strip()[:48], after.strip()[:48]))
    return out


def main():
    roots = [ROOT / "src", ROOT / "include"]
    rows = []
    files = 0
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".cpp", ".hpp"):
                continue
            files += 1
            for body_line, after_line, body, after in scan(path):
                rows.append((path.relative_to(ROOT), body_line, after_line,
                             body, after))

    print(f"{files} files\n")
    print(f"{len(rows)} statement(s) dressed as though a braceless control "
          f"statement guarded them:\n")
    for rel, body_line, after_line, body, after in rows:
        print(f"  {rel}:{after_line}")
        print(f"      guarded:   {body}")
        print(f"      not:       {after}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
