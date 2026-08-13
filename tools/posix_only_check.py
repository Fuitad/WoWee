#!/usr/bin/env python3
"""POSIX-only functions called outside the header that makes them portable.

    tools/posix_only_check.py

WHY

Three Windows CI failures in a row were the same shape: a function that exists
on Linux and macOS and not on Windows, called directly, in one place among
several that had all remembered the #ifdef.

    setenv / unsetenv    eight places, one forgot     core/env.hpp
    localtime_r          nine places, one forgot      core/local_time.hpp

Each cost a full Windows build to discover, and each was greppable from any
host. Once a portable wrapper exists, the rule is simply that nothing else
calls the bare function.

WHAT IT DOES

For each POSIX-only name below, finds every call outside the header that wraps
it. The wrapper itself is expected to contain the #ifdef; everything else
should be going through it.

WHAT IT CANNOT SEE

A POSIX-only function nobody has been bitten by yet. This list grows when
Windows finds the next one, which is the honest state of it: it turns a
repeat into a one-off rather than predicting the first occurrence.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TREE = [ROOT / "src", ROOT / "include", ROOT / "tests", ROOT / "tools"]

# function -> the header allowed to call it, because it is what wraps it.
WRAPPED = {
    "setenv": "include/core/env.hpp",
    "unsetenv": "include/core/env.hpp",
    "localtime_r": "include/core/local_time.hpp",
    "gmtime_r": "include/core/local_time.hpp",
    # Not a Windows problem: these exist everywhere. They hand back a pointer
    # into one shared buffer, so the next caller on any thread overwrites what
    # the last one is still reading - a combat log line stamped with a mail
    # expiry date, and no way to reproduce it on purpose. Same rule, same
    # wrapper: it returns the struct by value.
    "localtime": "include/core/local_time.hpp",
    "gmtime": "include/core/local_time.hpp",
}


def main():
    missing = [w for w in set(WRAPPED.values()) if not (ROOT / w).is_file()]
    if missing:
        print("These wrappers are gone, so the rule below is meaningless: "
              + ", ".join(missing))
        return 1

    offenders = []
    for base in TREE:
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in (".c", ".cpp", ".h", ".hpp", ".mm"):
                continue
            rel = str(path.relative_to(ROOT))
            try:
                text = path.read_text(errors="ignore")
            except OSError:
                continue
            for i, raw in enumerate(text.split("\n")):
                stripped = raw.strip()
                if stripped.startswith("//") or stripped.startswith("*"):
                    continue
                for name, wrapper in WRAPPED.items():
                    if rel == wrapper:
                        continue
                    if re.search(r"(?<![\w:])(?:std::)?%s\s*\(" % re.escape(name), raw):
                        offenders.append((rel, i + 1, name, wrapper))

    print(f"{len(WRAPPED)} POSIX-only function(s) with a portable wrapper\n")
    print(f"{len(offenders)} called directly instead:")
    for rel, line, name, wrapper in offenders:
        print(f"  {rel}:{line}  {name}  (use {wrapper})")
    if not offenders:
        print("  (none)")
    return 1 if offenders else 0


if __name__ == "__main__":
    sys.exit(main())
