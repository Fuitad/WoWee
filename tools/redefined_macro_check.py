#!/usr/bin/env python3
"""Macros the build already defines, redefined unguarded in a source file.

    tools/redefined_macro_check.py

WHY

CMakeLists passes a handful of macros to every translation unit through
add_compile_definitions: WIN32_LEAN_AND_MEAN and NOMINMAX among them. A file
that defines one of those again is a redefinition, which is a warning, which
under -Werror is a failed build.

It only fails where the macro is actually passed. WIN32_LEAN_AND_MEAN is added
under a Windows branch, so five files guarded their define with #ifndef, one did
not, and nothing said so until a Windows CI job stopped on it. That is one
platform's worth of round trip to learn something greppable.

WHAT IT DOES

Reads the macro names out of the CMakeLists add_compile_definitions calls, then
looks for a #define of any of them in the tree that is not immediately preceded
by an #ifndef for the same name.

WHAT IT CANNOT SEE

A macro defined through some other route (a target_compile_definitions on one
target, a generated header), and a guard written more than one line above its
define. Both under-report rather than crying wolf.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CMAKE = ROOT / "CMakeLists.txt"
TREE = [ROOT / "src", ROOT / "include", ROOT / "tools"]


def build_macros(text):
    """The bare names passed to add_compile_definitions."""
    names = set()
    for call in re.findall(r"add_compile_definitions\(([^)]*)\)", text, re.S):
        for token in call.split():
            token = token.strip()
            # Only bare names: a FOO=1 is a value and redefining it identically
            # is not what this is about.
            if re.fullmatch(r"[A-Z][A-Z0-9_]+", token):
                names.add(token)
    return names


def main():
    if not CMAKE.exists():
        print("No CMakeLists.txt. Nothing was checked - do not believe a zero.")
        return 1
    macros = build_macros(CMAKE.read_text())
    if not macros:
        print("Read no macros out of add_compile_definitions. The zero below "
              "means the parse broke, not that nothing redefines them.")
        return 1

    unguarded = []
    for base in TREE:
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in (".c", ".cpp", ".h", ".hpp", ".mm", ".inc"):
                continue
            lines = path.read_text(errors="ignore").split("\n")
            for i, raw in enumerate(lines):
                m = re.match(r"\s*#\s*define\s+([A-Z][A-Z0-9_]+)\s*$", raw)
                if not m or m.group(1) not in macros:
                    continue
                name = m.group(1)
                previous = lines[i - 1] if i else ""
                if re.match(r"\s*#\s*ifndef\s+%s\s*$" % name, previous):
                    continue
                unguarded.append((str(path.relative_to(ROOT)), i + 1, name))

    print(f"{len(macros)} macro(s) the build defines for every file: "
          f"{', '.join(sorted(macros))}\n")
    print(f"{len(unguarded)} redefined without an #ifndef:")
    for path, line, name in unguarded:
        print(f"  {path}:{line}  {name}")
    if not unguarded:
        print("  (none)")
    return 1 if unguarded else 0


if __name__ == "__main__":
    sys.exit(main())
