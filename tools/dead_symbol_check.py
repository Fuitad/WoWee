#!/usr/bin/env python3
"""Member functions that are declared, defined, and never called.

    tools/dead_symbol_check.py

WHY

The GameHandler decomposition and the FrameXML transition both left work
behind: a function whose last caller moved elsewhere still compiles, still
links, and still passes every test. SpellHandler kept a whole item-use path
this way, three methods and a helper, reachable only from each other. A
trainer-categorisation island lasted longer than that.

Dead code is not merely clutter here. Two of it are worse: someone reads it as
the way a thing is done and copies it, or someone wires it up years later and
gets a version that stopped being maintained the day its caller left.

WHAT IT DOES

Reads member function declarations out of the headers, then counts uses of each
name across the whole tree, ignoring the declaration and the out-of-line
definition. A name with no uses left is a candidate.

WHAT IT CANNOT SEE, AND WHY IT REPORTS RATHER THAN JUDGES

  * A virtual called through a base class pointer looks unused on the override.
  * Anything reached from Lua by name, which is a string literal here.
  * Anything reached through a function pointer or a dispatch table.
  * Constructors, destructors and operators, which are skipped outright.

It also misses a call split across lines, where the name sits on a
continuation with no statement keyword in front of it.

So a hit is a question, not an answer, and the answer is the build: remove the
declaration and the definition together, compile, and a live one fails to
resolve. That is a complete oracle for anything not reached through a virtual,
a function pointer or a Lua name, which is why those three are filtered out
before removing rather than argued about afterwards.

Every fix to this file so far has been the same bug in a different disguise: a
line that is not a declaration parsed as one, so its call sites were discarded
and the function looked dead. `foo(a, b);`, `return Class::foo(a);` and
`: foo(x);` have each done it. When a name with obvious callers shows up here,
suspect the parser before the codebase.
"""
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# `void foo(...)` / `bool foo(...) const;` inside a class body. Deliberately
# narrow: a return type, a name, an open paren, ending in a semicolon.
#
# The return type has to start with a word character, which also keeps a
# ternary continuation like `: getModelMatrix(x);` from parsing as one. Letting it start with
# whitespace makes `    foo(a, b);` - an ordinary call - parse as a declaration
# of foo with an empty return type, and then every call site is discarded as
# "the declaration" and every function looks dead. That is what the first
# version of this did, and it reported 682 of 3167 functions as unused.
# A statement keyword before the name makes `return foo(x);` and `else bar();`
# parse as declarations too, which discards their call sites the same way.
STATEMENT_START = re.compile(r"^\s*(?:return|else|do|case|co_return|throw)\b")

DECL = re.compile(
    r"^\s{2,}(?:virtual\s+|static\s+|inline\s+|explicit\s+|\[\[nodiscard\]\]\s+)*"
    r"(?:const\s+)?\w[\w:<>,\s\*&]*?[\s\*&](\w+)\s*\([^;{]*\)\s*(?:const\s*)?"
    r"(?:noexcept\s*)?(?:override\s*)?(?:=\s*0\s*)?;\s*$")

# `void Class::name(` at the start of a line: the out-of-line definition.
#
# Matched per name rather than per line. A qualified *call* at the start of a
# line looks the same - `audio::AudioEngine::instance().setMasterVolume(x);`
# reads as a definition of `instance` - so discarding the whole line loses
# every other name on it, and setMasterVolume looked dead with three callers.
DEFN = re.compile(r"^[\w:<>,\s\*&]*\b\w+::(\w+)\s*\(")

# A free function at namespace scope, declared or defined inline in a header:
# `bool waterCellRendered(...);` or `inline void foo(...) {`. These are the
# other half of the surface: the member scan above cannot see them, and a
# helper whose last caller moved away looks exactly like one that is about to
# get its first.
FREE_DECL = re.compile(
    r"^(?:inline\s+|static\s+|constexpr\s+|\[\[nodiscard\]\]\s+)*"
    r"(?:const\s+)?\w[\w:<>,\s\*&]*?[\s\*&](\w+)\s*\([^;{]*\)\s*(?:const\s*)?"
    r"(?:noexcept\s*)?[;{]\s*$")

# A free function's *definition* head. Unlike a member, it carries no
# `Class::` to recognise it by, so without this the definition line counts as a
# use of itself and every free function looks alive.
FREE_DEFN = re.compile(
    r"^(?:inline\s+|static\s+|constexpr\s+)*"
    r"(?:const\s+)?\w[\w:<>,\s\*&]*?[\s\*&](\w+)\s*\([^;]*\)\s*(?:const\s*)?"
    r"(?:noexcept\s*)?\{")

# Names that mean something else, or whose callers are not C++.
SKIP_NAMES = {
    "if", "for", "while", "switch", "return", "sizeof", "operator",
    "explicit", "virtual", "static", "inline", "const", "void", "public",
    "private", "protected", "class", "struct", "enum", "namespace",
}


def declared_members():
    """name -> the headers that declare it, members and free functions alike."""
    out = collections.defaultdict(set)
    for path in sorted((ROOT / "include").rglob("*.hpp")):
        text = path.read_text(errors="ignore")
        for line in text.split("\n"):
            if STATEMENT_START.match(line):
                continue
            match = DECL.match(line) or FREE_DECL.match(line)
            if not match:
                continue
            name = match.group(1)
            if name in SKIP_NAMES or name.startswith("~"):
                continue
            # Constructors: the name matches the enclosing type, which this
            # regex cannot see, so drop anything starting upper-case as a
            # rough stand-in. Member functions here are lowerCamelCase.
            if name[0].isupper():
                continue
            out[name].add(str(path.relative_to(ROOT)))
    return out


def use_counts(names):
    """How many times each name appears somewhere that is not its own
    declaration or out-of-line definition."""
    counts = collections.Counter()
    wanted = set(names)
    pattern = re.compile(r"\b(" + "|".join(re.escape(n) for n in names) + r")\b")
    for folder in ("src", "include", "tools", "tests"):
        base = ROOT / folder
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix not in (".cpp", ".hpp", ".lua", ".py", ".xml"):
                continue
            for line in path.read_text(errors="ignore").split("\n"):
                stripped = line.strip()
                statement = bool(STATEMENT_START.match(line))

                # The declaration itself is not a use. A statement that merely
                # looks like one is.
                if not statement and (DECL.match(line) or FREE_DECL.match(line)):
                    continue

                # A definition head, not a qualified call: `return
                # Class::name(x);` matches DEFN too, and treating that as the
                # definition discards the very call being looked for.
                defined_here = None
                if not statement and not stripped.endswith(";"):
                    definition = DEFN.match(line) or FREE_DEFN.match(line)
                    if definition:
                        defined_here = definition.group(1)

                for match in pattern.finditer(line):
                    name = match.group(1)
                    # A function's own definition is not a use of it; anything
                    # else on the same line still is.
                    if name == defined_here and name in wanted:
                        continue
                    counts[name] += 1
    return counts


def main():
    declared = declared_members()
    if not declared:
        print("Recognised no declarations. The zero below means the scan broke.")
        return 1

    counts = use_counts(list(declared))
    dead = sorted(n for n in declared if counts[n] == 0)

    # The .w* format headers are a library the client only partly uses, so an
    # accessor nothing calls there is API rather than dead weight. Counted
    # separately, because only the other number is meant to stay at its floor.
    FORMAT_HEADERS = "include/pipeline/wowee_"
    outside = [n for n in dead
               if not all(h.startswith(FORMAT_HEADERS) for h in declared[n])]

    print(f"{len(declared)} member function name(s) declared in headers")
    print(f"{len(dead)} with no use anywhere outside their declaration")
    print(f"{len(outside)} of those outside the .w* format headers:\n")
    for name in outside:
        where = ", ".join(sorted(declared[name])[:2])
        print(f"  {name:44} {where}")
    if not outside:
        print("  (none)")
    print("\nEach is a question rather than an answer: virtuals called through a"
          "\nbase, Lua bindings and dispatch tables all look like this.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
