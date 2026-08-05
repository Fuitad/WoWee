#!/usr/bin/env python3
"""Files the interface asks for that are not there.

A manifest entry or a <Script file="..."> that resolves to nothing does not
raise. The loader logs a line among thousands and carries on, and what is lost
is every function that file defined — so the failure surfaces later and
somewhere else, as a global that is nil for no visible reason.

That is not hypothetical here. ToggleCharacter lives in characterframe.lua,
which the manifest does not list at all: it is pulled in by a <Script> element
inside characterframe.xml, and the reference is spelled CharacterFrame.lua
against a file that is lowercase on disk. Every step of that resolves today,
and this is what says so tomorrow.

    tools/framexml_load_check.py

THREE THINGS IT CHECKS

  * every .lua and .xml the manifest lists exists
  * every <Script file> and <Include file> reference exists
  * the same for each bundled addon's own manifest

Case-insensitively, because the loader resolves that way and the interface is
inconsistent about it — the manifests are written in Blizzard's capitalisation
and the files on disk are not.

WHAT IT CANNOT SEE

Whether a file that exists also loads. A Lua error inside one takes it and
everything referencing it down, and only a run says so.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
INTERFACE = ROOT / "Data/interface"

REFERENCE = re.compile(r'<(Script|Include)\s+file="([^"]+)"')


def files_in(directory):
    """Lowercased name -> the name as spelled on disk."""
    return {p.name.lower(): p.name for p in directory.iterdir() if p.is_file()}


def manifest_entries(toc):
    out = []
    for line in toc.read_text(errors="ignore").splitlines():
        entry = line.strip()
        if not entry or entry.startswith("#"):
            continue
        if entry.lower().endswith((".xml", ".lua")):
            out.append(entry)
    return out


def check(directory, fallback=None):
    """(files listed, references seen, unresolved rows) for one directory.

    An addon may reference a shared template out of FrameXML — by a bare name,
    or by a path back out of its own folder written with Windows separators —
    so the fallback is searched too, on the basename, exactly as the loader
    does.
    """
    if not directory.is_dir():
        return 0, 0, []
    have = files_in(directory)
    spare = files_in(fallback) if fallback and fallback.is_dir() else {}
    listed, referenced, missing = 0, 0, []

    for toc in directory.glob("*.toc"):
        for entry in manifest_entries(toc):
            listed += 1
            if entry.lower() not in have:
                missing.append((toc.name, "manifest", entry))

    for xml in directory.glob("*.xml"):
        text = xml.read_text(errors="ignore")
        for m in REFERENCE.finditer(text):
            referenced += 1
            ref = m.group(2).replace("\\", "/")
            base = ref.rsplit("/", 1)[-1].lower()
            if base not in have and base not in spare:
                missing.append((xml.name, m.group(1).lower(), m.group(2)))

    return listed, referenced, missing


def main():
    directories = [INTERFACE / "framexml"]
    addons = INTERFACE / "addons"
    if addons.is_dir():
        directories += sorted(p for p in addons.iterdir() if p.is_dir())

    total_listed = total_refs = 0
    rows = []
    framexml = INTERFACE / "framexml"
    for directory in directories:
        listed, referenced, missing = check(
            directory, fallback=None if directory == framexml else framexml)
        total_listed += listed
        total_refs += referenced
        rows += [(directory.name, *m) for m in missing]

    print(f"{len(directories)} interface directories, {total_listed} files "
          f"listed in a manifest, {total_refs} script and include references\n")
    print(f"{len(rows)} that resolve to nothing:\n")
    for where, source, kind, name in rows:
        print(f"  {where}/{source}: {kind} '{name}'")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
