#!/usr/bin/env python3
"""Channels the sound switches silence and nothing turns back up.

The interface has three enable switches - music, ambience, sound effects - and
this client applies them by zeroing a channel's volume. Zeroing has no inverse,
so every channel a switch silences has to be written again by
SettingsPanel::applyAudioVolumes, which is the one place that pushes the
client's own sliders at the audio system.

A channel in the first list and not the second goes quiet the first time its
switch is turned off and stays quiet for good, however the sliders are set. It
is silent in both senses: nothing raises, nothing is logged, and the only sign
is a sound that never plays again.

That is not hypothetical. The sound-effects switch zeroed nine managers and
applyAudioVolumes wrote eight - it set the player voice's `enabled` flag and
never its volume - so a player who turned sound effects off and on again lost
their character's grunts and speech until the next reinstall.

Compares the two lists by the accessor each uses, which is the name both sides
have in common.
"""
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def accessors_zeroed(src: str) -> set:
    """Managers a switch silences: ac->getX() inside an `if (!...On)` block."""
    found = set()
    for block in re.finditer(
            r"if\s*\(\s*!\s*\w*(?:On|Enabled)\w*\s*\)\s*\{(.*?)\n    \}",
            src, re.S):
        body = block.group(1)
        # Only the ones actually silenced here, not every manager mentioned.
        if not re.search(r"setVolume\w*\(\s*0", body):
            continue
        found |= set(re.findall(r"->\s*(get\w*(?:Manager|Coordinator))\(\)", body))
    return found


def accessors_restored(src: str) -> set:
    """Managers applyAudioVolumes writes a volume for."""
    body = re.search(r"void SettingsPanel::applyAudioVolumes\([^)]*\)\s*\{(.*?)\n\}",
                     src, re.S)
    if not body:
        return set()
    text = body.group(1)
    found = set()
    # The accessor has to be paired with an actual volume write, not merely
    # named - setEnabled alone is what let the player voice through.
    for chunk in re.finditer(r"ac->\s*(get\w*Manager)\(\)\)?\s*\{?(.*?)(?=ac->\s*get|\Z)",
                             text, re.S):
        if re.search(r"setVolume\w*\(", chunk.group(2)):
            found.add(chunk.group(1))
    return found


def main() -> int:
    lua = REPO / "src" / "addons" / "lua_system_api.cpp"
    panel = REPO / "src" / "ui" / "settings_panel.cpp"
    if not lua.exists() or not panel.exists():
        print("Cannot find both files; the zero below would mean the scan broke.")
        return 1

    zeroed = accessors_zeroed(lua.read_text(errors="ignore"))
    restored = accessors_restored(panel.read_text(errors="ignore"))
    if not zeroed:
        print("Found no channel that any switch silences, which cannot be "
              "right - the scan broke rather than the switches going away.")
        return 1

    missing = sorted(zeroed - restored)
    print(f"{len(zeroed)} channel(s) a sound switch silences, "
          f"{len(restored)} written back by applyAudioVolumes")
    print(f"\n{len(missing)} silenced and never turned back up:")
    if not missing:
        print("  (none)")
    for name in missing:
        print(f"  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
