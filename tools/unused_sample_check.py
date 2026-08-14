#!/usr/bin/env python3
"""A sound sample collection that is loaded at start-up and never played.

    tools/unused_sample_check.py            # report
    tools/unused_sample_check.py --canary   # prove the scan can see one

THE SHAPE

An audio manager declares a collection, fills it from disk in its loader, and
reports it in a log line - and nothing ever plays it:

    std::vector<CombatSample> bloodElfMaleAttackSounds_;   // declared
    bloodElfMaleAttackSounds_.resize(9);                   // filled
    loadSound(path, bloodElfMaleAttackSounds_[i], assets); // read from disk
    LOG_INFO("... BE Male: ", bloodElfMaleAttackSounds_[0].loaded ? ...);

Every mention is the load or a log line about the load. The files are read at
every start-up and the sound cannot be triggered by anything.

WHY IT MATTERS, AND WHY THE COUNT IS NOT THE POINT

Found this way once, the answer was 34 collections and 109 wav files. But the
same symptom had three different causes, and they wanted opposite treatment:

  - a duplicate of a path that already works, per race, from a general
    system - delete it, because wiring it in gives every jump two voices;
  - a fallback nobody could reach, where the primary path is a dbc lookup
    and only an install missing that dbc would ever need these - wire it;
  - a feature that was loaded and never finished - decide, and if it cannot
    be finished, remove the load and keep the note saying what it needed.

So a hit here is a question, not a verdict. Read who else plays that kind of
sound before deciding which of the three it is.
"""
import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
AUDIO_HEADERS = ROOT / "include" / "audio"

DECL = re.compile(r"std::vector<\w+>\s+(\w+_)\s*;")
# The mentions that are the load, or a log line about the load.
LOAD_ONLY = ("resize(", "loadSound(", "LOG_INFO", "LOG_DEBUG", "LOG_WARNING")


def collections():
    found = {}
    for header in sorted(AUDIO_HEADERS.glob("*.hpp")):
        for m in DECL.finditer(header.read_text()):
            found[m.group(1)] = header.name
    return found


def mentions(name):
    out = subprocess.run(["grep", "-rn", name, "src/", "include/"],
                         capture_output=True, text=True, cwd=ROOT).stdout
    return out.splitlines()


def scan():
    hits = []
    for name, header in sorted(collections().items()):
        lines = mentions(name)
        used = [l for l in lines
                if not l.split(":")[0].endswith(".hpp")
                and not any(tok in l for tok in LOAD_ONLY)]
        if lines and not used:
            hits.append((name, header, len(lines)))
    return hits


def canary():
    """Plant a collection that is loaded and never played, and look for it."""
    header = AUDIO_HEADERS / "ui_sound_manager.hpp"
    source = ROOT / "src" / "audio" / "ui_sound_manager.cpp"
    h_before, s_before = header.read_text(), source.read_text()
    marker = "canaryNeverPlayedSounds_"
    try:
        header.write_text(h_before.replace(
            "    std::vector<UISample> pickupBagSounds_;",
            "    std::vector<UISample> pickupBagSounds_;\n"
            f"    std::vector<UISample> {marker};", 1))
        source.write_text(s_before.replace(
            "    pickupBagSounds_.resize(1);",
            f"    {marker}.resize(1);\n"
            f'    loadSound("Sound\\\\Interface\\\\Canary.wav", {marker}[0], assets);\n'
            "    pickupBagSounds_.resize(1);", 1))
        seen = [h for h in scan() if h[0] == marker]
    finally:
        header.write_text(h_before)
        source.write_text(s_before)
    if not seen:
        print("canary FAILED: a collection loaded and never played was not reported")
        return 1
    print(f"canary ok: reported {seen[0][0]}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--canary", action="store_true",
                    help="plant a loaded-but-unplayed collection and check it is seen")
    args = ap.parse_args()
    if args.canary:
        return canary()

    hits = scan()
    print(f"{len(hits)} sample collection(s) loaded and never played:\n")
    for name, header, count in hits:
        print(f"  {name:34s} {header}  ({count} mentions, all load or log)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
