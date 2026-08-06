#!/usr/bin/env python3
"""Frames the interface declares that the emitter never creates.

    tools/framexml_frame_emitted_check.py

WHY THIS FINDS WHAT THE OTHER SWEEPS CANNOT

Every other interface sweep reads the XML, or reads the Lua, and reasons about
what should happen in between. This runs the emitter and compares its output
against the file it was given: for each concrete named frame in the XML, does
the generated Lua create anything by that name.

A frame the emitter drops is the worst shape this transition has. Nothing
raises — the frame simply is not there — so every handler that touches it dies
on a nil index at some later, unrelated moment, and what the player sees is a
panel that is present and does nothing. Several bug reports of exactly that
shape are what prompted this.

It needs the `framexml_emit` target, which is built with the tests:

    cmake --build build --target framexml_emit

WHAT IT COMPARES

Concrete (non-virtual) frame elements carrying a literal name, against the Lua
emitFrameXml produces for that same file. Zero today, across 172 in-game files
and 2369 frames.

It also compiles what was emitted, which is a second and separate claim: a
syntax error anywhere in a chunk takes down every frame in that file, and the
name check cannot see it because the name is in the text either way. Also zero
— and seen to fail before that zero was believed: putting `local x = = 1` in a
gamemenuframe OnLoad makes framexml_emit exit 2 and name the emitted line.

WHAT IT DELIBERATELY SKIPS

  * gluexml. The login screen is a separate interface the client does not load
    in-game, and every one of its files reports missing frames because of it.
  * `$parent`-substituted names, which are resolved at runtime from the
    parent's name and are not literals to search for.
  * Anything inside an XML comment. skillframe.xml keeps a commented-out
    ACCEPT button and worldstateframe.xml three commented score rows; reading
    those as findings was the first version's only output.

WHAT IT CANNOT SEE

Whether a frame that *was* created is configured correctly — the wrong size,
a dropped attribute, a script that did not attach. It answers existence only.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EMIT = ROOT / "build/bin/framexml_emit"

FRAME_EL = (r"(?:Frame|Button|CheckButton|StatusBar|Slider|EditBox|ScrollFrame"
            r"|ScrollingMessageFrame|MessageFrame|SimpleHTML|ColorSelect|Model"
            r"|PlayerModel|DressUpModel|TabardModel|Cooldown|GameTooltip"
            r"|MovieFrame|Minimap)")


def declared_frames(text):
    """Concrete named frames, comments already stripped."""
    out = []
    for m in re.finditer(r"<(" + FRAME_EL + r")\b([^>]*)>", text):
        attrs = m.group(2)
        if 'virtual="true"' in attrs:
            continue
        name = re.search(r'name="([^"$]+)"', attrs)
        if name:
            out.append(name.group(1))
    return out


def main():
    if not EMIT.exists():
        print("  framexml_emit is not built — nothing was checked.")
        print("  cmake --build build --target framexml_emit")
        return 1

    files = [p for p in sorted((ROOT / "Data/interface").rglob("*.xml"))
             if "gluexml" not in str(p)]
    rows, total = [], 0
    for p in files:
        text = re.sub(r"<!--.*?-->", "", p.read_text(errors="ignore"), flags=re.S)
        names = declared_frames(text)
        if not names:
            continue
        total += len(names)
        try:
            run = subprocess.run([str(EMIT), str(p)], capture_output=True,
                                 text=True, timeout=60)
        except subprocess.TimeoutExpired:
            rows.append((p.name, "emitter timed out", []))
            continue
        if run.returncode == 2:
            # The frames are all in the text; the chunk will not load, so none
            # of them is ever created. Reported separately because the fix is
            # in the emitter's output, not in its coverage.
            first = (run.stderr.strip().splitlines() or ["?"])[0]
            rows.append((p.name, "emitted Lua does not compile", [first[:90]]))
            continue
        if run.returncode != 0:
            rows.append((p.name, "emitter failed", []))
            continue
        absent = [n for n in names if n not in run.stdout]
        if absent:
            rows.append((p.name, f"{len(absent)} of {len(names)}", absent[:5]))

    # The canary: a name that is certainly in the file and certainly emitted.
    # Without it an emitter that produced nothing at all would report a clean
    # zero, since every comparison would be against an empty haystack... which
    # is the same silent-success failure the wire sweeps had.
    probe = ROOT / "Data/interface/framexml/gamemenuframe.xml"
    if probe.exists():
        run = subprocess.run([str(EMIT), str(probe)], capture_output=True, text=True)
        if "GameMenuFrame" not in run.stdout:
            print("  CANARY FAILED: the emitter produced no GameMenuFrame.")
            print("  Every count below is meaningless.\n")
            return 1

    print(f"{len(files)} in-game xml files, {total} named frames\n")
    print(f"{len(rows)} file(s) with frames the emitter does not create:\n")
    for name, why, examples in rows:
        print(f"  {name:40} {why}")
        if examples:
            print(f"       e.g. {examples}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
