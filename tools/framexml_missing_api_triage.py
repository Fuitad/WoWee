#!/usr/bin/env python3
"""Sort a session's missing-API report into the ones worth acting on.

    tools/framexml_missing_api_triage.py [path-to-missing_api.txt]

The report a session writes is measured rather than inferred, which makes it
the most valuable thing a run produces — and also the most misleading, because
most of what lands in it is correctly absent. Every entry has been triaged by
hand three times now and the answer keeps coming back "not a gap", so this
asks the question the same way each time instead.

The one thing this can decide, and the whole reason it works: a name FrameXML
*defines somewhere* but that was not defined at shutdown belongs to a file that
did not load. Nearly always that is a load-on-demand addon nobody opened — the
raid frames, the combat text, the time manager. Those need nothing.

WHAT THE CATEGORIES MEAN
------------------------
* **in an addon that did not load** — defined under Data/interface/addons.
  Correct: it resolves when the panel is opened. Only suspicious if the panel
  *was* opened this session.

* **defined nowhere in this interface** — two different things wear this label,
  so it prints the callers and you read them:
    - Blizzard's own dangling references. 3.3.5 ships several. `CaptureBar_Hide`
      is named in worldstateframe.lua's ExtendedUI table and defined in no file;
      `OptionsFrame_ToggleSubCategories` is assigned from XML while the function
      that exists is `OptionsListButton_ToggleSubCategories`. Both assign nil in
      the real client too. Defining them would diverge from retail, not fix it.
    - Frames the C client creates. worldmapframe.lua says so in a comment —
      "PlayerArrowEffectFrame is created in code: CWorldMap::CreatePlayerArrowFrame()".
      This client draws its own world map, so those stay absent on purpose.

* **assigned nil at file scope** — `ZonePVPType = nil` in zonetext.lua. Lua
  cannot tell "assigned nil" from "never assigned", so the recorder cannot
  either. Permanent false positive.

* **widget field** — a field read off a frame, not a call. Usually optional and
  guarded at every read; `TextString` is only set on bars that have a text
  FontString. Worth a look when the field is one the interface always expects.

* **worth reading** — everything left. This is the output that matters.
"""

import os
import pathlib
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")
INTERFACE = os.path.join(ROOT, "Data", "interface")
DEFAULT_REPORT = os.path.expanduser("~/.wowee/missing_api.txt")


def interface_files():
    """Every interface source file, and whether it belongs to an addon."""
    for dirpath, _, filenames in os.walk(INTERFACE):
        parts = dirpath.replace("\\", "/").split("/")
        # The login screen runs in its own Lua state and shares no globals.
        if "gluexml" in parts:
            continue
        in_addon = "addons" in parts
        for name in filenames:
            if name.endswith((".lua", ".xml")):
                yield os.path.join(dirpath, name), in_addon


def index():
    """name -> (defined_in_addon, files that mention it)."""
    defines, mentions = {}, {}
    for path, in_addon in interface_files():
        src = open(path, encoding="utf-8", errors="ignore").read()
        rel = os.path.relpath(path, INTERFACE)
        for match in re.finditer(r"function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", src):
            defines.setdefault(match.group(1), []).append((rel, in_addon))
        # `X = nil` at file scope reads as absent forever.
        for match in re.finditer(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*nil\s*;?\s*$",
                                 src, re.M):
            defines.setdefault(match.group(1), []).append((rel, in_addon))
        for match in re.finditer(r"\b([A-Z][A-Za-z0-9_]*)\b", src):
            mentions.setdefault(match.group(1), set()).add(rel)
    return defines, mentions


def read_report(path):
    """The real-gaps section only — the rest of the file is already triaged."""
    names = []
    for line in open(path, encoding="utf-8", errors="ignore"):
        line = line.strip()
        if line.startswith("--"):
            break
        if line:
            names.append(line)
    return names


BOOTSTRAP = pathlib.Path(__file__).resolve().parent.parent / "src/addons"


def bootstrap_globals():
    """Globals the client builds in C before the interface loads.

    A frame created in a bootstrap chunk is defined as far as FrameXML is
    concerned and undefined as far as a scan of Data/interface can tell — the
    two look identical from there, which is what the note at the bottom of this
    report used to warn about. It can be read instead: the chunks are Lua in
    C string literals, and a created frame is spelled the same way there.
    """
    names = set()
    for path in BOOTSTRAP.rglob("*.cpp"):
        text = path.read_text(errors="ignore")
        names |= set(re.findall(r'([A-Za-z_]\w*)\s*=\s*CreateFrame\(', text))
        names |= set(re.findall(r'function\s+([A-Za-z_]\w*)\s*\(', text))
    return names


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_REPORT
    if not os.path.exists(path):
        print(f"no report at {path}")
        print("A session writes it during clean shutdown; if it is missing or")
        print("stale, the client did not get that far.")
        return 1

    names = read_report(path)
    defines, mentions = index()
    built_in_c = bootstrap_globals()

    buckets = {
        "created by the client in C": [],
        "in an addon that did not load": [],
        "assigned nil at file scope": [],
        "defined nowhere in this interface": [],
        "widget field": [],
        "worth reading": [],
    }

    for name in names:
        if name.startswith("widget:"):
            buckets["widget field"].append((name[len("widget:"):], []))
            continue
        bare = name.split(":", 1)[-1]
        if bare in built_in_c:
            buckets["created by the client in C"].append(
                (bare, sorted(mentions.get(bare, ()))[:2]))
            continue
        where = defines.get(bare)
        if where and any(in_addon for _, in_addon in where):
            buckets["in an addon that did not load"].append(
                (bare, [f for f, _ in where]))
        elif where:
            buckets["assigned nil at file scope"].append(
                (bare, [f for f, _ in where]))
        else:
            callers = sorted(mentions.get(bare, ()))[:2]
            buckets["defined nowhere in this interface"].append((bare, callers))

    print(f"{path}\n{len(names)} names in the real-gaps section\n")
    for label, rows in buckets.items():
        if not rows:
            continue
        print(f"-- {label} ({len(rows)}) --")
        for bare, where in sorted(rows):
            print(f"  {bare:<38} {' '.join(where)}")
        print()

    real = len(buckets["worth reading"])
    print(f"{real} worth reading." if real else
          "Nothing left that is a gap in this client. Read the callers of the")
    if not real:
        print("'defined nowhere' group before believing that — a frame defined")
        print("in an addon's own XML has been read as missing here more than")
        print("once, and a Blizzard leftover looks the same as a real gap.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
