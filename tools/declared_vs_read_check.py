#!/usr/bin/env python3
"""What the interface *declares* about itself, and whether anything reads it.

Every other sweep here asks what the Lua calls. This one asks what the XML
says a frame is, and what the two sides agree a constant means. A frame can be
declared perfectly and behave wrongly when the declaration is read by nobody,
and nothing about that failure is visible: no missing name, no nil, no raise.

    tools/declared_vs_read_check.py

THREE THINGS IT COMPARES

**Attributes the emitter never names.** The chat box says historyLines="32"
and ignoreArrows="true"; the cast bar says drawLayer="BORDER". All three were
going nowhere. An attribute with no method behind it is fine to ignore —
emitting a call to a method that does not exist is worse — so this is a list to
read rather than a list to empty.

**Script handlers nothing fires.** GameTooltip declares OnTooltipCleared and
the chat box declares OnTextSet; neither had ever been called, so a tooltip
kept a money line it should have dropped and a message put into the box by
anything but typing went out on the wrong channel.

**Constants set in both places that disagree.** The bootstrap pre-sets these so
an addon has them before the interface loads — and on master the interface does
not load, so the bootstrap's values are the only ones there are.
BOOKTYPE_PET was 1 against "pet": a number compared to a string is false rather
than an error, so the question was always answered no.

WHAT IT CANNOT SEE

Whether an unread attribute matters. Most of the remainder have no method
behind them at all, and a few are XML namespace bookkeeping. Read before
acting.

THE SEVENTEEN ATTRIBUTES, READ 2026-08-05

Not boilerplate, and not attributes:

  * xmlns, xsi, schemaLocation on Ui — XML namespace declarations. They will
    never be read and are counted only because this reads every name in the
    schema.

No mechanism here to honour them:

  * horizTile, vertTile on Texture — eleven uses, all of them chat frame
    borders and tab backgrounds. Tiling needs a REPEAT sampler and the UI's is
    shared and CLAMP_TO_EDGE, so uv > 1 clamps instead of repeating. The cost
    of leaving it is a border strip stretched rather than tiled.
  * monochrome on Font, rotatesTexture on StatusBar, protected on Frame —
    a font flag, a fill-direction texture rotation, and secure-frame marking.
  * minimapArrowModel, minimapPlayerModel on Minimap — the player arrow is
    drawn in minimap_display.frag.glsl, not from a model.

No consequence:

  * debug, platform on Binding — build metadata.
  * nonBlocking on Texture — an async-load hint for a loader that is already
    async.
  * bytes and indented on FontString, countInvisibleLetters on EditBox,
    dontSavePosition on ScrollingMessageFrame — limits and layout niceties
    with no case behind them in this interface.

The one that came out of this list was motionScriptsWhileDisabled, and it is
worth noting why it did not belong with the rest: it read as inert, and it was
the opposite. Nothing here suppressed motion scripts at all, so every greyed
control answered the mouse — the permissive superset, which looks like the
attribute working. WoW fires OnEnter on a disabled button only when this asks,
which is how a greyed control explains why it is greyed.
"""
import pathlib
import re
import sys
import pathlib as _pathlib
sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
from framexml_source import without_comments

ROOT = pathlib.Path(__file__).resolve().parent.parent
FRAMEXML = ROOT / "Data/interface/framexml"
EMITTER = ROOT / "src/ui/framexml_emitter.cpp"
ADDONS = ROOT / "src/addons"
SRC = ROOT / "src"


def strip_xml_comments(text):
    return re.sub(r"<!--.*?-->", "", text, flags=re.S)


def attributes():
    """Attribute name -> the elements it appears on."""
    out = {}
    for path in FRAMEXML.rglob("*.xml"):
        text = strip_xml_comments(path.read_text(errors="ignore"))
        for m in re.finditer(r"<(\w+)([^>]*)>", text):
            for attr in re.findall(r'\b([a-zA-Z]\w*)\s*=\s*"', m.group(2)):
                out.setdefault(attr, set()).add(m.group(1))
    return out


def script_types():
    out = set()
    for path in FRAMEXML.rglob("*.xml"):
        text = strip_xml_comments(path.read_text(errors="ignore"))
        out |= set(re.findall(r"<(On[A-Z]\w*)", text))
    for path in FRAMEXML.rglob("*.lua"):
        text = without_comments(path.read_text(errors="ignore"))
        out |= set(re.findall(r'SetScript\(\s*"(On\w+)"', text))
    return out


def constants():
    """(bootstrap value, framexml value) for names set in both."""
    boot = {}
    for path in ADDONS.rglob("*.cpp"):
        for m in re.finditer(r'"([A-Z][A-Z0-9_]{2,})\s*=\s*([^"\n]{1,40}?)\\n"',
                             path.read_text(errors="ignore")):
            boot[m.group(1)] = m.group(2).strip()
    fx = {}
    for path in FRAMEXML.rglob("*.lua"):
        text = without_comments(path.read_text(errors="ignore"))
        for m in re.finditer(r"^([A-Z][A-Z0-9_]{2,})\s*=\s*([^\n;]{1,40})",
                             text, re.M):
            fx.setdefault(m.group(1), m.group(2).strip())
    # Whitespace and quote style are not disagreements, and neither is one
    # side writing a number the other writes as the constant that holds it —
    # INVSLOT_LAST_EQUIPPED against INVSLOT_TABARD, both nineteen.
    def norm(v):
        v = v.rstrip(";").strip()
        if re.fullmatch(r"[A-Z][A-Z0-9_]*", v) and v in fx:
            v = fx[v].rstrip(";").strip()
        return v.replace('"', "'").replace(" ", "")
    return {n: (boot[n], fx[n]) for n in sorted(set(boot) & set(fx))
            if norm(boot[n]) != norm(fx[n])}


def vocabulary(pattern, known_from):
    """Names FrameXML asks for by string, and which the client never answers.

    Case-folded on both sides, because the bindings fold too: uidropdownmenu
    asks for "uiscale" where everything else says "uiScale", and an exact match
    once answered "0" for it, which laid every dropdown out at SetScale(0).
    """
    want = {}
    for path in list(FRAMEXML.rglob("*.lua")) + list(FRAMEXML.rglob("*.xml")):
        text = without_comments(path.read_text(errors="ignore"))
        for m in re.finditer(pattern, text):
            want.setdefault(m.group(1).lower(), m.group(1))
    known = set()
    for path in known_from.rglob("*.cpp"):
        text = path.read_text(errors="ignore")
        # Comments stripped first. A comment explaining what a CVar does quotes
        # its name, and counting that as an answer made removing the answer
        # invisible — which is exactly the regression this is meant to catch.
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        known |= {s.lower() for s in
                  re.findall(r'"([A-Za-z][A-Za-z0-9_]{2,})"', text)}
    return want, sorted(want[n] for n in want if n not in known)


def main():
    emitter = EMITTER.read_text(errors="ignore") if EMITTER.exists() else ""
    named = set(re.findall(r'"(\w+)"', emitter))
    attrs = attributes()
    unread = sorted(a for a in attrs if a not in named)

    fired = set()
    for path in SRC.rglob("*.cpp"):
        fired |= set(re.findall(r'"(On[A-Z]\w+)"', path.read_text(errors="ignore")))
    unfired = sorted(s for s in script_types() if s not in fired)

    differing = constants()

    print(f"{len(attrs)} attributes declared, {len(unread)} the emitter never "
          f"names:\n")
    for a in unread:
        print(f"  {a:<28} on {', '.join(sorted(attrs[a])[:2])}")
    if not unread:
        print("  (none)")

    print(f"\n{len(unfired)} script type(s) declared and never fired:\n")
    for s in unfired:
        print(f"  {s}")
    if not unfired:
        print("  (none)")

    sounds, silent = vocabulary(r'PlaySound\(\s*"([A-Za-z0-9_]+)"', SRC)
    # "No hand-written mapping", not "silent". PlaySound resolves the name
    # through SoundEntries.dbc first — the interface is naming a row in that
    # table — and 102 of the 103 names it asks for are in it. The mapping below
    # is the fallback for an install missing the sound or the table, so a name
    # here is one that has no approximation to fall back *to*, not one that
    # makes no noise.
    print(f"\n{len(sounds)} sound names asked for, {len(silent)} with no "
          f"hand-written mapping behind them:\n")
    for n in silent[:8]:
        print(f"  {n}")
    if len(silent) > 8:
        print(f"  ... and {len(silent) - 8} more")
    if not silent:
        print("  (none)")

    cvars, unanswered = vocabulary(
        r'(?:GetCVar|GetCVarBool|SetCVar|RegisterCVar|GetCVarDefault)'
        r'\(\s*"([A-Za-z0-9_]+)"', SRC)
    print(f"\n{len(cvars)} CVars named, {len(unanswered)} the client never "
          f"answers:\n")
    for n in unanswered[:8]:
        print(f"  {n}")
    if len(unanswered) > 8:
        print(f"  ... and {len(unanswered) - 8} more")
    if not unanswered:
        print("  (none)")

    print(f"\n{len(differing)} constant(s) set in both places with different "
          f"values:\n")
    for name, (b, f) in differing.items():
        print(f"  {name:<28} bootstrap={b:<20} framexml={f}")
    if not differing:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
