#!/usr/bin/env python3
"""Widget methods FrameXML calls that answer nil, and so raise.

The global surface has its own sweep. This is the other half: `frame:Method()`
rather than `Method()`, resolved through the widget metatable instead of _G.

Three things can happen to `frame:Foo()`:

  * Foo is in the method table (`{"Foo", lua_...}`) or has a Lua shim
    (`__WoweeFrameMT:Foo` / `mt:Foo`) — it works.
  * Foo is in the no-op allowlist (`"Foo=1"`) — it answers a harmless no-op.
    Silent, and the subject of its own audit; see framexml_missing_api_list.
  * Foo is none of those — the fallback records it and **returns nil**, so the
    call raises.

This finds the third kind without needing a run. The runtime does record them
(as `widget:<name>`), but only for methods a session actually reached, and only
into a report that has to be read.

False positives to expect, and why they cannot be filtered by shape:
  * Methods FrameXML defines on its own tables and objects — a table is not a
    widget, and `:Method()` looks identical either way. The script subtracts
    every `function X:Method` and `X.Method = function` it can see, which
    catches most. It does not catch a method *assigned* from another function:
    blizzard_achievementui does `self.Collapse = AchievementButton_Collapse`,
    so Collapse and Expand are reported and are fine. Check how a name is
    defined before treating it as a gap.
  * Methods on Lua's own types (`("x"):format(...)`) — lowercase, so the
    uppercase-initial filter drops them.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
XML = ROOT / "Data/interface"
ENGINE = ROOT / "src/addons/lua_engine.cpp"


def strip(text, is_xml):
    if is_xml:
        text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    return re.sub(r"--\[\[.*?\]\]|--[^\n]*", "", text, flags=re.S)


# What the engine answers for — asked of the one place that decides rather
# than worked out again here. This file had its own copy of that regex, and it
# matched only `mt:`, so every method the bootstrap defines on animMeta or
# groupMeta read as unanswered: IsPlaying, SetDuration and SetOffset were all
# reported against the glyph frame when the animation system has provided them
# since it was written. One fact in two places, drifting.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_provides import widget_methods_provided  # noqa: E402

answered_by_engine = widget_methods_provided()

# Methods FrameXML defines itself, on frames or on its own tables.
defined = set()
called = {}
for p in list(XML.rglob("*.lua")) + list(XML.rglob("*.xml")):
    text = strip(p.read_text(errors="ignore"), p.suffix == ".xml")
    defined |= set(re.findall(r"\bfunction\s+[\w.]+[.:](\w+)\s*\(", text))
    defined |= set(re.findall(r"[\w.]+\.(\w+)\s*=\s*function", text))
    for m in re.finditer(r":(\w+)\s*\(", text):
        called.setdefault(m.group(1), set()).add(p.name)

# Uppercase-initial only: WoW widget methods are PascalCase, and the fallback
# itself only records those. On* are script handler names read as fields.
answered = answered_by_engine | defined
missing = {
    name: files
    for name, files in called.items()
    if name[:1].isupper() and not name.startswith("On") and name not in answered
}

print(f"{len(answered_by_engine)} answered by the engine, "
      f"{len(defined)} defined by FrameXML\n")
print(f"{len(missing)} widget methods called that answer nil:\n")
for name, files in sorted(missing.items(), key=lambda kv: (-len(kv[1]), kv[0])):
    where = " ".join(sorted(files)[:3])
    more = f" +{len(files) - 3}" if len(files) > 3 else ""
    print(f"  {name:34} {len(files):3} file(s)  {where}{more}")

sys.exit(0)
