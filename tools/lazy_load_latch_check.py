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
"""
import re
import sys
import pathlib

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "src")
if not root.is_dir():
    sys.exit(f"no such directory: {root}")

LATCH = re.compile(
    r'^\s*(?:owner_\.)?(\w*(?:[Ll]oaded|[Ii]nitialized)\w*)(?:Ref\(\))?\s*=\s*true;')
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
        window = lines[i + 1:i + 8]
        for j, w in enumerate(window):
            if PRECOND.search(w) and "return" in "\n".join(window[j:j + 2]):
                hits.append((p.as_posix(), i + 1, m.group(1), w.strip()[:58]))
                break

print(f"scanned {len(list(root.rglob('*.cpp')))} translation units")
if not hits:
    print("\nno loader latches before its assets are checked.")
else:
    print(f"\n{len(hits)} latch before checking that the assets exist:\n")
    for f, ln, flag, cond in hits:
        print(f"  {f}:{ln}")
        print(f"      {flag} = true   before   {cond}")
    print("\nMove the latch below the check. A file that is genuinely unreadable")
    print("should still latch after the attempt; one that was never opened must not.")
