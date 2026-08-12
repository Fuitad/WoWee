#!/usr/bin/env python3
"""Frames this client looks up by name that the interface does not declare.

    tools/framexml_lookup_names_check.py

A dozen things are now driven by finding a FrameXML frame by name and handing
it something: the minimap is told where to be, the world map likewise, and
every portrait and model frame is handed an image rendered for it. All of that
goes through WidgetTree::findByName, and a name that matches nothing simply
answers null - no error, no warning, no picture. A typo and a frame that was
never built look exactly the same from here, and both look like "the feature
does not work".

WHAT IT LOOKS FOR

Every name this client hands to findByName, whether written at the call or
carried in one of the tables that drive several frames from one loop, against
the names FrameXML declares - `name="X"` in the XML, and the names built at
runtime with CreateFrame("Type", "X", ...).

WHAT IT CANNOT SEE

A name built by concatenation on either side. This client writes them out and
FrameXML mostly does too, but $parent expansion produces plenty of real names
that appear in no file as a literal; those are not looked up from here, and if
one ever is, this will report it and the report will be wrong.

It also says nothing about whether the frame is *shown*, only that it exists.
A correctly-named frame belonging to an element nobody handed over is found,
handed a picture, and never drawn.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import sys as _s; _s.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files

XML = ROOT / "Data/interface"
SRC = ROOT / "src"


def looked_up():
    """Names handed to findByName, at the call or through a driving table."""
    names = {}
    for path in sorted(SRC.rglob("*.cpp")):
        text = path.read_text(errors="ignore")
        for m in re.finditer(r'findByName\(\s*"([A-Za-z0-9_]+)"', text):
            names.setdefault(m.group(1), set()).add(path.name)
        # The tables: {"FrameName", &someModel_, &someWidgetId_} - a literal
        # beside the view that draws it and the id it is remembered by. Written
        # this way so several frames share one loop, which also takes the name
        # out of the findByName call.
        #
        # The widget-id field is what makes this a frame table rather than the
        # one beside it, which is {"target", &targetPortrait_, guid} and holds
        # unit ids. Matching on the view alone reported "target" and "party1"
        # as frames the interface does not declare, which is true and not the
        # question.
        for m in re.finditer(
                r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*&\w+_\s*,\s*&\w*[Ww]idgetId_',
                text):
            names.setdefault(m.group(1), set()).add(path.name)
    return names


def declared():
    """Names FrameXML gives a frame, in the XML or through CreateFrame."""
    out = set()
    for path in sorted(loaded_files(XML)):
        text = path.read_text(errors="ignore")
        out |= set(re.findall(r'name="([A-Za-z0-9_]+)"', text))
        out |= set(re.findall(
            r'CreateFrame\(\s*"[A-Za-z]+"\s*,\s*"([A-Za-z0-9_]+)"', text))
    return out


def main():
    wanted = looked_up()
    have = declared()
    missing = {n: f for n, f in wanted.items() if n not in have}

    print(f"{len(wanted)} frame name(s) looked up by this client, "
          f"{len(have)} declared by the interface\n")
    print(f"{len(missing)} looked up that the interface does not declare:\n")
    for name, files in sorted(missing.items()):
        print(f"  {name:34} {' '.join(sorted(files))}")
    if not missing:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
