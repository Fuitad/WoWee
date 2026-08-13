#!/usr/bin/env python3
"""Two functions that are the same function, written twice.

    tools/function_similarity_check.py

WHY

duplicate_block_check.py reads a sliding window of twelve code lines and is at
zero. That does not mean there is no duplicated knowledge left: most functions
here are shorter than its window, and it only sees literal repetition, so a
copy that has been reworded is invisible to it.

This compares whole function bodies instead, which catches both. What it found
on its first run, with the block scan reporting nothing:

    M2Instance::updateModelMatrix / WMOInstance::updateModelMatrix
        Identical. The two had disagreed about the rotation composition order
        for a long time - and nothing on flat ground can tell X,Y,Z from
        Z,Y,X, which is why four attempts to fix it were each judged against
        evidence that could not.
    EntitySpawner::getRenderFootZForGuid / getRenderPositionForGuid
        Fourteen identical lines resolving a guid to the instance that draws
        it, in three functions, all of which have to agree.
    CorpseMarkerLayer::clearTexture / PlayerMarkerLayer::clearTexture
        A Vulkan teardown of a texture and its ImGui descriptor set, written
        four times counting the destructors, each needing both halves in the
        right order.
    MountDust::shutdown / Weather::shutdown
        The same four device resources released by the same two systems.
    SpellbookScreen::getSpellIcon / TalentScreen::getSpellIcon
        An icon cache whose two failure modes must be cached differently.

WHAT IT DOES

Extracts every out-of-line member function definition of five to forty-three
code lines from src/, strips comments and blank lines, and compares bodies of
similar length with difflib. Pairs at 0.88 or above are reported unless they
are named in SETTLED below.

Comments are stripped deliberately: two functions that differ only in how
their comment is worded are one function, and reading the comment as content
is how a first version of the pipeline sweep reported three renderers that
were fine.

WHAT IT CANNOT SEE

Free functions, lambdas, and anything under five code lines. A pair whose
bodies were reworded past 0.88 similarity - the block scan cannot see those
either, and `repeated_literal_check.py` is the third angle for exactly that.

A zero here means nothing unless canaried: copy a real function into another
file under a new class name and confirm this names the pair.
"""
import collections
import difflib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
THRESHOLD = 0.88
MIN_LINES = 5
MAX_LINES = 43

# Pairs read and judged as deliberately separate. Keyed by the two qualified
# names, sorted, so a pair stops resurfacing once someone has looked at it.
SETTLED = {
    # The .w* open formats each carry their own faction enum and their own
    # words for it, and the NUMBERS DIFFER. Alliance is 0 in the guild, char,
    # channel and auction formats and 1 in the mount, achievement, event and
    # map ones, because four of them have a "both" or "neutral" value below it
    # and four do not. The switch bodies look interchangeable and merging any
    # two of them would rewrite what is stored in a file on disk as something
    # else. Same for the map type names: Battleground is 2 in one and 3 in the
    # other. Check the enum, not the switch.
    "faction and type names across the .w* formats": [
        ("WoweeAchievement::factionName", "WoweeEvent::factionGroupName"),
        ("WoweeAchievement::factionName", "WoweeMaps::factionGroupName"),
        ("WoweeAuction::factionAccessName", "WoweeChannel::factionAccessName"),
        ("WoweeAuction::factionAccessName", "WoweeChars::raceFactionName"),
        ("WoweeChannel::factionAccessName", "WoweeGuild::factionName"),
        ("WoweeChars::raceFactionName", "WoweeGuild::factionName"),
        ("WoweeEvent::factionGroupName", "WoweeMaps::factionGroupName"),
        ("WoweeGuild::factionName", "WoweeMount::factionName"),
        ("WoweeMaps::mapTypeName", "WoweeWorldMap::worldTypeName"),
    ],
    # Both release the same four device resources, through the one helper, and
    # then clear the state each holds - which is not the same state: Weather
    # keeps a second array of positions and MountDust does not. What is shared
    # is already shared; what is left is each system's own members.
    "particle system teardown": [
        ("MountDust::shutdown", "Weather::shutdown"),
    ],
    # Two unrelated sockets closing a file descriptor. The world socket leaves
    # its receive buffer alone because its caller clears that along with the
    # crypto state and the trace window; the plain socket owns its buffer and
    # clears it here. Merging them would either strand the world socket's
    # buffer or clear it twice at a point where the caller is mid-teardown.
    "socket close": [
        ("TCPSocket::disconnect", "WorldSocket::closeSocketNoJoin"),
    ],
}

DEF = re.compile(r"^[\w:<>,&\*][\w:<>,&\* ]*?\b(\w+)::(\w+)\([^;]*?\)\s*(?:const\s*)?\{?\s*$")


def settled_pairs():
    out = set()
    for pairs in SETTLED.values():
        for a, b in pairs:
            out.add(tuple(sorted((a, b))))
    return out


def bodies(path):
    """Every out-of-line member definition in `path`, as (name, code lines)."""
    lines = path.read_text(errors="ignore").split("\n")
    out, i = [], 0
    while i < len(lines):
        m = DEF.match(lines[i])
        if not m:
            i += 1
            continue
        depth, span, j, started = 0, [], i, False
        while j < len(lines):
            depth += lines[j].count("{") - lines[j].count("}")
            if "{" in lines[j]:
                started = True
            span.append(lines[j])
            j += 1
            if started and depth <= 0:
                break
        if MIN_LINES + 2 <= len(span) <= MAX_LINES + 2:
            code = []
            for line in span[1:-1]:
                stripped = re.sub(r"//.*", "", line).strip()
                if not stripped or stripped.startswith(("/*", "*")):
                    continue
                code.append(re.sub(r"\s+", " ", stripped))
            if len(code) >= MIN_LINES:
                out.append((f"{m.group(1)}::{m.group(2)}", code))
        i = j
    return out


def main() -> int:
    src = ROOT / "src"
    if not src.is_dir():
        print("src is not here; the zero below would mean the scan broke.")
        return 1

    found = []
    for path in sorted(src.rglob("*.cpp")):
        for name, code in bodies(path):
            found.append((str(path.relative_to(ROOT)), name, code))
    if not found:
        print("Found no function bodies at all, which cannot be right.")
        return 1

    buckets = collections.defaultdict(list)
    for rec in found:
        buckets[len(rec[2]) // 3].append(rec)

    settled = settled_pairs()
    seen, hits = set(), []
    for key, group in sorted(buckets.items()):
        near = group + buckets.get(key + 1, [])
        for file_a, name_a, code_a in group:
            for file_b, name_b, code_b in near:
                if (file_a, name_a) == (file_b, name_b):
                    continue
                pair = tuple(sorted([(file_a, name_a), (file_b, name_b)]))
                if pair in seen:
                    continue
                seen.add(pair)
                if tuple(sorted((name_a, name_b))) in settled:
                    continue
                ratio = difflib.SequenceMatcher(None, code_a, code_b).ratio()
                if ratio >= THRESHOLD:
                    hits.append((ratio, len(code_a), file_a, name_a, file_b, name_b))

    hits.sort(reverse=True)
    print(f"{len(found)} function bodies of {MIN_LINES}..{MAX_LINES} code lines")
    print(f"{len(settled)} pair(s) settled and not reported\n")
    print(f"{len(hits)} pair(s) that are one function written twice:")
    if not hits:
        print("  (none)")
    for ratio, length, file_a, name_a, file_b, name_b in hits:
        print(f"  {ratio:.2f} {length:3d} lines  {name_a}  ({file_a})")
        print(f"                    {name_b}  ({file_b})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
