#!/usr/bin/env python3
"""Blocks of code repeated across files, and what has already been judged.

    tools/duplicate_block_check.py

WHY

Duplication in this tree is rarely a whole function with the same name. It is a
run of lines that says the same thing in two places, usually because a per
expansion reader was copied and then edited. The copies drift, and the drift is
the bug: TBC dropped an item's +Mana for exactly that reason, and the only
visible sign was that its stat switch was a copy of one three files away.

So this measures rather than guesses, and it remembers the verdicts. Every pair
below has been looked at once; without that list the same settled pairs come
back to the top of every scan and get investigated again.

WHAT IT DOES

Normalises each file to its code lines, hashes every sliding window of twelve,
and reports windows that appear in more than one file. Comments, blank lines
and preprocessor lines are dropped, so a shared comment does not read as shared
code, and windows that are mostly punctuation are skipped.

It also counts windows repeated inside a single file, which is where the
duplication moved once the cross-file pairs were dealt with: a vertex layout
written out once per pipeline, a mask read once per thing built from it, a
sort planned once per container. Those are reported per file rather than
pair by pair, because a file with forty of them has one shape repeated, not
forty shapes.

WHAT IT CANNOT SEE

Duplication that has been reworded. It also cannot tell a copy from a
deliberate per-expansion difference, which is the whole reason the verdicts
are written down rather than inferred.
"""
import collections
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WINDOW = 12

# Pairs looked at and left alone, with the reason. A pair listed here is not
# reported; anything else is either new or was never judged.
SETTLED = {
    ("include/game/game_handler.hpp", "include/game/social_handler.hpp"):
        "GameHandler forwards the guild and mail calls to SocialHandler. The "
        "declarations match because it is a facade; the bodies do not, and "
        "guild_commands, social_panel and lua_social_api all reach it through "
        "GameHandler.",
    ("src/core/entity_spawner_player.cpp", "src/rendering/character_preview.cpp"):
        "The equipment-to-geoset pick, in the world and in the paper doll. The "
        "shared run of erases and inserts is genuinely the same, and the two "
        "ends of it are not: the preview substitutes within a geoset group "
        "where the spawner takes the exact index, and the preview keeps hair "
        "under a helm on purpose because it draws no head attachment, so "
        "hiding it would leave a bald head and no helmet. Merging the middle "
        "means agreeing on those two first.",
    ("include/game/game_handler.hpp", "include/game/inventory_handler.hpp"):
        "The same facade as the pair above, for bags, mail, the bank, the "
        "guild bank and the auction house. The declarations match because "
        "GameHandler forwards; the bodies do not. The one way this pair can "
        "actually go wrong is a writable accessor that skips the forwarding, "
        "which tools/forwarding_ref_check.py pins at zero.",
    ("src/game/packet_parsers_classic.cpp", "src/game/world_packets_entity.cpp"):
        "The classic and WotLK item-query readers. WotLK scores two candidate "
        "price layouts against each other to decide which the server sent, "
        "which is a different mechanism rather than a longer version of the "
        "same one.",
}


def code_lines(text):
    """The lines that carry code, with their original numbers."""
    out, numbers = [], []
    for i, raw in enumerate(text.split("\n")):
        line = raw.strip()
        if not line or line.startswith(("//", "/*", "*", "#")):
            continue
        if line in ("{", "}", "};", "})", "});"):
            continue
        out.append(re.sub(r"\s+", " ", line))
        numbers.append(i + 1)
    return out, numbers


def main():
    files = sorted(list((ROOT / "src").rglob("*.cpp")) +
                   list((ROOT / "include").rglob("*.hpp")))
    if not files:
        print("Found no sources. The zero below means the scan broke.")
        return 1

    blocks = collections.defaultdict(list)
    for path in files:
        try:
            lines, numbers = code_lines(path.read_text(errors="ignore"))
        except OSError:
            continue
        rel = str(path.relative_to(ROOT))
        for i in range(len(lines) - WINDOW):
            window = lines[i:i + WINDOW]
            # Near-identical filler (a run of assignments to the same thing)
            # and windows that are mostly braces say nothing useful.
            if len(set(window)) < WINDOW - 1:
                continue
            if sum("(" in x for x in window) < WINDOW // 2:
                continue
            digest = hashlib.md5("\n".join(window).encode()).hexdigest()
            blocks[digest].append((rel, numbers[i]))

    # Group by the set of files a repeated window spans.
    pairs = collections.defaultdict(list)
    for hits in blocks.values():
        names = tuple(sorted({f for f, _ in hits}))
        if len(names) > 1:
            pairs[names].append(hits)

    # Windows repeated inside one file, counted per file.
    within = collections.Counter()
    for hits in blocks.values():
        names = {f for f, _ in hits}
        if len(names) == 1 and len(hits) > 1:
            within[hits[0][0]] += 1

    unjudged, settled = [], 0
    for names, windows in pairs.items():
        key = names if len(names) == 2 else None
        if key in SETTLED:
            settled += 1
            continue
        unjudged.append((len(windows), names, windows[0]))
    unjudged.sort(reverse=True)

    print(f"{len(files)} files, window of {WINDOW} code lines")
    print(f"{settled} file pair(s) settled and not reported\n")
    print(f"{len(unjudged)} file pair(s) sharing code:")
    for count, names, sample in unjudged[:20]:
        where = ", ".join(f"{f}:{l}" for f, l in sorted(set(sample))[:3])
        print(f"  {count:3} block(s)  {where}")
    if not unjudged:
        print("  (none)")

    print(f"\n{sum(within.values())} block(s) repeated within one file:")
    for rel, count in within.most_common(10):
        print(f"  {count:3} block(s)  {rel}")
    if not within:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
