#!/usr/bin/env python3
r"""Static version of the unaccounted-frame sweep.

Known blind spot, measured rather than assumed: this reads parent="UIParent"
out of the XML, so frames built at runtime by CreateFrame are invisible to it.
Forty-two named frames are created that way and six are parented to UIParent —
ChatFrame, RaidPullout, the two combat-log frames, WorldStateCaptureBar and
this client's own widget demo. None duplicates a handed-over element today,
and the capture bar cannot appear at all because GetNumWorldStateUI answers
zero. Re-measure with:

    grep -rhoP 'CreateFrame\(\s*"[A-Za-z]+"\s*,\s*"\K[A-Za-z0-9_]+' Data/interface/


Top-level FrameXML frames (parent="UIParent") whose names appear nowhere in
framexml_takeover.cpp. Each is a part of the interface nobody has decided
about: if this client draws the same thing, both are on screen.
"""
import re
from pathlib import Path

ROOT = Path("/home/k/Desktop/wowee")
XML = ROOT / "Data/interface/framexml"

src = (ROOT / "src/ui/framexml_takeover.cpp").read_text()
accounted = set()
for lit in re.findall(r'"([^"]*)"', src):
    for word in lit.split():
        if re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", word):
            accounted.add(word)

# <Frame name="X" ... parent="UIParent"> in either attribute order.
tops = {}
for path in sorted(XML.glob("*.xml")):
    text = path.read_text(errors="ignore")
    for tag in re.finditer(r"<(Frame|Button|StatusBar|ScrollFrame|MessageFrame|"
                           r"SimpleHTML|Slider|ColorSelect|Model|PlayerModel)\b[^>]*>", text):
        blob = tag.group(0)
        if 'parent="UIParent"' not in blob:
            continue
        if 'virtual="true"' in blob:
            continue
        m = re.search(r'name="([A-Za-z][A-Za-z0-9_]*)"', blob)
        if m:
            tops.setdefault(m.group(1), path.name)

missing = {n: f for n, f in tops.items() if n not in accounted}
print(f"{len(tops)} top-level frames, {len(missing)} unaccounted:\n")
for name, f in sorted(missing.items(), key=lambda kv: kv[1]):
    print(f"  {name:<34} {f}")
