#!/usr/bin/env python3
"""GameTooltip setters that answer with a no-op, so the tooltip comes up blank.

    tools/tooltip_setter_check.py

WHY THESE AND NOT ALL METHODS

A tooltip setter is the whole content of a tooltip. Every other widget method
that no-ops leaves something on screen that is merely wrong; a Set* on
GameTooltip that no-ops leaves an empty box, or nothing at all, and there is no
error and no partial result to notice. Fifty-one of them are called across the
interface and each belongs to a different panel, so one missing setter is one
panel's hover that says nothing while every other hover works.

WHAT IT SEPARATES

Three answers, and the third is the fault:

  * implemented — a C binding, or a method on __WoweeFrameMT written in the
    bootstrap Lua. Both count; the bootstrap ones are how most of these are
    written, since they are made of other bindings.
  * in the no-op allowlist — answered, silently, with nothing.
  * neither — these RAISE, and take the OnEnter down with them. Zero today,
    and that is the number to watch: it is the only one here that is loud.

WHAT IT FOUND

Eight blank on 2026-08-05, four of them fixed:

  * SetTradeTargetItem — SetTradePlayerItem was written and its twin was not,
    so hovering your own offer named the item and hovering theirs said
    nothing. An asymmetry rather than a decision.
  * SetShapeshift — the stance bar, which every druid, warrior, rogue, priest
    and death knight has on screen the whole time.
  * SetMerchantCostItem — what a vendor wants besides coin. The money frame
    draws one of these per cost item and the tooltip is the only place the
    badge or token is named.
  * SetLFGDungeonReward — the reward icons on the dungeon-ready popup.

THE FOUR LEFT, AND WHY EACH STAYS

  * SetQuestLogRewardSpell — GetQuestLogRewardSpell answers nil always, so
    there is nothing for the tooltip to say. Implementing it would move the
    blank one layer down.
  * SetLFGCompletionReward — GetLFGCompletionRewardItem's contract is a
    texture and a quantity. No name is available to print, which is why its
    dungeon-reward twin above could be written and this one could not.
  * SetEquipmentSet — the equipment manager, which a stock account has off:
    the equipmentManager CVar reads "0" and that is correct.
  * SetGlyph — Blizzard_GlyphUI, which arrives with the talent addon.

WHAT IT CANNOT SEE

Whether an implemented setter fills the tooltip *correctly*. It reads which of
three answers a name gets, not what comes out.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framexml_source import loaded_files, without_comments  # noqa: E402
from framexml_provides import (  # noqa: E402
    noop_widget_methods, widget_methods_provided)

ROOT = Path(__file__).resolve().parent.parent
INTERFACE = ROOT / "Data/interface"

#: Names a tooltip is reached by. `tooltip` catches the local FrameXML takes
#: when a function is handed one, which is how most of the templates call it.
TOOLTIP = r"(?:GameTooltip|ItemRefTooltip|ShoppingTooltip\d|\btooltip)"


def called():
    out = {}
    for path in loaded_files(INTERFACE):
        text = without_comments(path.read_text(errors="ignore"))
        for m in re.finditer(TOOLTIP + r"\s*:\s*(Set[A-Za-z0-9_]+)\s*\(", text):
            out.setdefault(m.group(1), set()).add(path.name)
    return out


def main():
    calls = called()
    names = set(calls)
    # Through framexml_provides rather than by matching names here.
    #
    # That file is the one implementation of "does the client answer this", and
    # it exists because the ad-hoc versions produce false gaps — a name written
    # as a bootstrap Lua method, or listed in the counting table, is answered
    # and looks unbound to a search for {"Name",. This sweep had its own copy
    # for exactly one day.
    provided = widget_methods_provided()
    allowlist = noop_widget_methods()

    print(f"{len(calls)} tooltip Set* methods called by the interface, "
          f"{len(names & provided) - len(names & allowlist)} of them implemented")
    if "SetHyperlink" not in calls:
        print("  CANARY: SetHyperlink not seen called — the interface is not parsing.")
    print()

    # Answered, but with nothing. widget_methods_provided counts an allowlist
    # entry as provided, which is the right answer to "does this raise" and the
    # wrong one to "does this fill the tooltip" — so the two sets are asked
    # separately rather than one being derived from the other.
    blank = sorted(names & allowlist)
    raises = sorted(names - provided)

    print(f"{len(blank)} answered by the no-op fallback, so the tooltip is blank:\n")
    for name in blank:
        print(f"  {name:<28} {' '.join(sorted(calls[name])[:2])}")
    if not blank:
        print("  (none)")

    print(f"\n{len(raises)} neither implemented nor allowlisted, so they raise:\n")
    for name in raises:
        print(f"  {name:<28} {' '.join(sorted(calls[name])[:2])}")
    if not raises:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
