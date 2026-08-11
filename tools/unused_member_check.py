#!/usr/bin/env python3
"""Class members stored and never read.

    tools/unused_member_check.py

WHY

A member assigned in a constructor and never read again is dead state that
still costs a pointer's worth of lifetime coupling, and it is usually the
remains of something that was removed halfway.
WorldLoader::worldLoadGeneration_ carried a comment describing re-entrant load
detection that was never written.

Clang reports these as -Wunused-private-field, but only on some versions. The
clang on the Windows CI image reported two that clang 18 on Linux does not, so
a local build is not a gate for this class and CI catches it one per run.

This asks the same question with grep, so any host answers it.

WHAT IT DOES

For each class member declared in a header under include/, counts every
mention of that name across include/, src/ and tools/. A member whose only
mentions are its declaration and a constructor initialiser is reported.

WHAT IT CANNOT SEE

Members reached through a macro, or named the same as something else and so
appearing to be read when they are not. It is deliberately conservative: a
name mentioned anywhere else at all is left alone, so it under-reports rather
than crying wolf.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "include", ROOT / "src", ROOT / "tools"]

# A trailing-underscore member declaration: `Type name_;` or `Type name_ = init;`
DECL = re.compile(
    r"^\s{2,}(?:mutable\s+|static\s+|const\s+)*"
    r"[A-Za-z_][\w:<>,\s\*&]*?[\s\*&]"
    r"(\w+_)\s*(?:=[^;]*)?;\s*(?://.*)?$", re.M)

# Settled: names whose only readers are outside what this can see.
EXPECTED = set()


def files():
    for base in SOURCES:
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.suffix in (".cpp", ".hpp", ".h", ".inc", ".mm"):
                yield path


def main():
    corpus = {}
    for path in files():
        try:
            corpus[path] = path.read_text(errors="ignore")
        except OSError:
            pass
    if not corpus:
        print("Read no sources. The zero below means the scan broke.")
        return 1

    headers = [p for p in corpus if p.suffix in (".hpp", ".h")
               and str(p).startswith(str(ROOT / "include"))]

    # Every line that mentions a member, so each can be judged a read or a
    # write. Counting mentions alone is not enough: `nextResourceId_++` and
    # `markupParser_.parse(...)` are one mention each and are real uses.
    lines_for = {}
    for path, text in corpus.items():
        for raw in text.split("\n"):
            for name in set(re.findall(r"\b(\w+_)\b", raw)):
                lines_for.setdefault(name, []).append((path, raw))

    def is_write_only(name, path, decl_line):
        """True when nothing anywhere reads this member."""
        assign = re.compile(r"(?<![=!<>])\b%s\s*=(?!=)" % re.escape(name))
        init = re.compile(r"[,:]\s*%s\s*\(" % re.escape(name))
        for where, raw in lines_for.get(name, []):
            stripped = raw.strip()
            if where == path and stripped == decl_line.strip():
                continue                       # the declaration itself
            if init.search(raw):
                continue                       # a constructor initialiser
            m = assign.search(raw)
            if m and name not in raw[m.end():]:
                continue                       # written and not also read here
            return False                       # anything else reads it
        return True

    dead = []
    for header in sorted(headers):
        text = corpus[header]
        for m in DECL.finditer(text):
            name = m.group(1)
            if name in EXPECTED or name.startswith("__"):
                continue
            decl_line = m.group(0)
            if not is_write_only(name, header, decl_line):
                continue
            line = text.count("\n", 0, m.start()) + 1
            dead.append((str(header.relative_to(ROOT)), line, name))

    print(f"{len(headers)} headers, {len(lines_for)} member names\n")
    print(f"{len(dead)} members stored and never read:")
    for path, line, name in dead:
        print(f"  {path}:{line}  {name}")
    if not dead:
        print("  (none)")
    return 1 if dead else 0


if __name__ == "__main__":
    sys.exit(main())
