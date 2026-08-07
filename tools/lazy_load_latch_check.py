#!/usr/bin/env python3
"""Lazy loaders that record "loaded" before they have read anything.

The shape:

    if (loaded_) return;
    loaded_ = true;                       // <- latched here
    auto* am = services_.assetManager;
    if (!am || !am->isInitialized()) return;

A caller can reach a lazy loader before the asset manager is up. When that
happens the flag latches, the early return fires, and the file is never read
again for the rest of the session. Eighteen loaders were written this way:
skill names, spell names, faction, area and map names, taxi nodes, talents,
glyphs, repair costs, achievements, titles, spell visuals.

It is timing-dependent, so it presents as a different missing thing on each run
and never reproduces on demand. It surfaced here as a probe printing nothing at
all in a log where the client was plainly in the world — the loader had been
marked done without ever opening the file.

The fix is always the same: latch *after* the precondition, so "the assets are
not ready yet" is not recorded as "this file has been read". Latching after a
real but failed read is fine and is not reported — a missing file should not be
retried on every call.

This looks for a `*Loaded*`/`*Init*` flag set to true with a bail-out on the
asset manager below it. It deliberately does not flag a bail-out on the *result
of a read* (`if (!dbc || !dbc->isLoaded()) return;`), which is the correct
place to latch.

**Two spellings of the same mistake, and the first version only saw one.**
The lazily-loaded resolvers in application.cpp keep their flag in a shared_ptr
and write `*loaded = true`, and they guard on the pointer being non-null rather
than on the assets being up:

    if (!am || *loaded) return;
    *loaded = true;                       // am may exist but not be initialised

loadDBC answers nullptr before initialisation, so those latch on a read that
returned nothing — spell icons, item icons, spell cast times and random stat
bonuses, all empty for the session. A leading `*` and a `!am` without
`isInitialized` were enough to hide four of them from the check written to find
exactly this.
"""
import re
import sys
import pathlib

# Anchored to this file, not to the working directory. sweep_guard runs from
# the build tree under ctest, where a relative "src" does not exist — and the
# tool exited with a message instead of a count, which the guard could only
# report as "the report's shape changed".
root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 \
    else pathlib.Path(__file__).resolve().parent.parent / "src"
if not root.is_dir():
    sys.exit(f"no such directory: {root}")

LATCH = re.compile(
    r'^\s*\*?(?:owner_\.)?(\w*(?:[Ll]oaded|[Ii]nitialized)\w*)(?:Ref\(\))?\s*=\s*true;')
# A guard on the pointer alone. Having an asset manager is not having assets:
# loadDBC answers nullptr until it is initialised.
# `am` exactly, or a name with "asset" in it. It used to be \w*[Aa]m\w*, which
# matches any identifier containing those two letters — languageNamesLoaded_,
# frameCount, cameraReady — and reported a loader whose guard was three lines
# above it and perfectly correct. A pattern loose enough to match "Names" will
# find something in every file eventually.
NULL_ONLY = re.compile(r'if\s*\(\s*!\s*(am|\w*[Aa]sset\w*)\b(?![^)]*isInitialized)')
# The precondition that must come first: the assets, however it is spelled.
PRECOND = re.compile(
    r'if\s*\(\s*!\s*\w*[Aa]sset\w*'                 # !assetManager / !cachedAssetManager_
    r'|if\s*\(\s*!am\b'                             # !am || !am->isInitialized()
)

hits = []
for p in sorted(root.rglob("*.cpp")):
    lines = p.read_text(errors="ignore").splitlines()
    for i, line in enumerate(lines):
        m = LATCH.match(line)
        if not m:
            continue
        # Stopped at the end of the enclosing function. Without that, a latch
        # written as the last statement of one function was read against the
        # opening guard of the *next* one: CharacterPreview::loadCreature
        # latches on its final line and loadPreviewM2 begins four lines later
        # with `if (!assetManager_) return false;`, which reported a function
        # that does check — at its top, before doing anything — as one that
        # does not.
        window = []
        for w in lines[i + 1:i + 8]:
            if w.startswith("}"):
                break
            window.append(w)
        for j, w in enumerate(window):
            if PRECOND.search(w) and "return" in "\n".join(window[j:j + 2]):
                hits.append((p.as_posix(), i + 1, m.group(1), w.strip()[:58]))
                break
        else:
            # The other spelling: guarded above the latch, but on the pointer
            # being non-null rather than on the assets being ready.
            above = lines[max(0, i - 4):i + 1]
            for w in above:
                if NULL_ONLY.search(w) and "loadDBC" in "\n".join(lines[i:i + 12]):
                    hits.append((p.as_posix(), i + 1, m.group(1),
                                 w.strip()[:58] + "   (no isInitialized)"))
                    break

print(f"scanned {len(list(root.rglob('*.cpp')))} translation units")
# The count is printed either way, so sweep_guard has something to pin. It used
# to say "no loader latches" on the clean run, which reads well and gives a
# ratchet nothing to hold on to.
print(f"\n{len(hits)} latch before checking that the assets exist:\n")
if not hits:
    print("  (none)")
else:
    for f, ln, flag, cond in hits:
        print(f"  {f}:{ln}")
        print(f"      {flag} = true   before   {cond}")
    print("\nMove the latch below the check. A file that is genuinely unreadable")
    print("should still latch after the attempt; one that was never opened must not.")
