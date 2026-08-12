#!/usr/bin/env python3
"""Test targets that reach glm without linking it.

    tools/test_glm_link_check.py

WHY

glm is not vendored. It arrives through find_package(glm), and its include
directory comes with the imported target rather than through any directory
tests/CMakeLists.txt lists - so a target that compiles a translation unit
reaching glm must call wowee_test_link_glm() or it has no way to find the
headers.

Nothing on Linux notices. There is a system copy under /usr/include and the
compiler finds it whether the target asked or not, so every one of these builds
green here and fails on macOS. CI stops at the first failure, which is why a
macOS log named test_shared_rules while eight targets were short of it.

Worse, the need is invisible at the target. test_shared_rules names
m2_loader.cpp, which says nothing about glm; m2_loader.hpp includes it two
lines in. Reading the CMake file cannot tell you, and reading the sources named
in it cannot either.

WHAT IT DOES

For every add_executable in tests/CMakeLists.txt, follows the local #include
graph out of each source it compiles and asks whether anything in that graph
mentions glm. If so, the target must appear in a wowee_test_link_glm() call.

Run on any platform, and it answers the macOS question.

WHAT IT CANNOT SEE

Includes reached through a macro or a generated header, and whether the link
helper itself is correct - only that a target which needs it says so.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CMAKE = ROOT / "tests/CMakeLists.txt"
# Where a quoted #include is resolved from, in the order the build resolves it.
SEARCH = [ROOT / "include", ROOT / "src", ROOT / "tests", ROOT / "tools/editor"]

_cache = {}


def _resolve(include):
    for base in SEARCH:
        candidate = base / include
        if candidate.exists():
            return candidate
    return None


def pulls_glm(path, seen=None):
    """Whether this file, or anything it includes, reaches glm."""
    if path is None or not path.exists() or path.is_dir():
        return False
    key = str(path)
    if key in _cache:
        return _cache[key]
    seen = seen or set()
    if key in seen:
        return False
    seen.add(key)
    _cache[key] = False        # a cycle answers no rather than recursing forever
    try:
        text = path.read_text(errors="ignore")
    except OSError:
        return False
    if "glm/" in text:
        _cache[key] = True
        return True
    for include in re.findall(r'#\s*include\s+"([^"]+)"', text):
        if pulls_glm(_resolve(include), seen):
            _cache[key] = True
            return True
    return _cache[key]


def source_path(token):
    path = ROOT / token.strip().replace("${CMAKE_SOURCE_DIR}/", "")
    return path if path.exists() else ROOT / "tests" / token.strip()


def main():
    if not CMAKE.exists():
        print("No tests/CMakeLists.txt. Nothing was checked - do not believe a zero.")
        return 1
    text = CMAKE.read_text()

    linked = set(re.findall(r"wowee_test_link_glm\((\w+)\)", text))
    targets = {m.group(1): m.group(2)
               for m in re.finditer(r"add_executable\((\w+)([^)]*)\)", text, re.S)}
    if not targets:
        print("Read no add_executable out of the test CMake. The zero below means "
              "the parse broke, not that every target is linked.")
        return 1

    short = []
    for name, body in sorted(targets.items()):
        reaching = sorted({source_path(t).name
                           for t in re.split(r"\s+", body)
                           if t.endswith(".cpp") and pulls_glm(source_path(t))})
        if reaching and name not in linked:
            short.append((name, reaching))

    print(f"{len(targets)} test targets, {len(linked)} call wowee_test_link_glm\n")
    print(f"{len(short)} reach glm without it - these build on Linux and fail on macOS:")
    for name, reaching in short:
        print(f"  {name:34} via {', '.join(reaching[:3])}")
    if not short:
        print("  (none)")
    return 1 if short else 0


if __name__ == "__main__":
    sys.exit(main())
