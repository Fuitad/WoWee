#!/usr/bin/env python3
"""Test targets that ctest runs but the sanitiser build never sees.

    tools/test_registration_check.py

WHY

`register_test_target` does two things a test cannot do without: it adds the
target to ALL_TEST_TARGETS, which is what the ASAN and UBSan build iterates,
and on Windows it links ws2_32, which several tests need through their
networking sources.

Six targets called `add_test` and not it. They ran under ctest and passed, and
were quietly absent from every sanitised run - which is the one place a test
exists to be run. Nothing reports that: the suite is green either way, and the
absence only shows up as a bug the sanitiser would have caught and did not.

Registration is easy to forget precisely because forgetting it costs nothing
visible. The blocks were written by hand, one copied from the next, and six of
about a hundred and forty lost the line.

WHAT IT LOOKS FOR

Every `add_test(NAME x COMMAND target)` in tests/CMakeLists.txt, and whether
that target is registered - directly, or by one of the wowee_add_test helpers,
which register what they build.

WHAT IT CANNOT SEE

A target registered under a computed name, and a test added from another
CMakeLists. Neither exists here.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main() -> int:
    cml = ROOT / "tests" / "CMakeLists.txt"
    if not cml.is_file():
        print("tests/CMakeLists.txt is not here; the zero below would mean the "
              "scan broke rather than every test being registered.")
        return 1
    text = cml.read_text()

    added = re.findall(r"add_test\(NAME\s+(\w+)\s+COMMAND\s+(\w+)\)", text)
    registered = set(re.findall(r"register_test_target\((\w+)\)", text))
    # The helpers register what they build.
    registered |= set(re.findall(r"wowee_add(?:_packet)?_test\(\s*(\w+)", text))

    if not added:
        print("Found no add_test calls at all, which cannot be right.")
        return 1

    missing = sorted({target for _, target in added if target not in registered})

    # Only the hand-written calls: the helpers register what they build, and
    # their add_test is inside the function rather than in a block.
    print(f"{len(added)} test(s) added by a hand-written block")
    print(f"\n{len(missing)} that the sanitiser build never sees:")
    if not missing:
        print("  (none)")
    for target in missing:
        print(f"  {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
