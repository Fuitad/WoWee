#!/usr/bin/env python3
"""Run the fast sweeps and fail if any of them gets worse.

A sweep only helps on the day someone runs it. Every fault these catch was
found by hand at least once, and each is the kind that raises nothing, logs
nothing and fails no test — a panel drawn twice, a binding answering short, a
name in a manifest that resolves to nothing, a chunk of bootstrap Lua replacing
the binding underneath it. Left to a person remembering, they come back.

    tools/sweep_guard.py          # report and exit non-zero on a regression
    tools/sweep_guard.py --list   # show the ceilings without running anything

Each entry pins a ceiling rather than an exact figure, so a fix that lowers a
count passes and is meant to be followed by lowering the ceiling here — a
ratchet, not a snapshot. Where the honest answer is none, the ceiling is zero
and lowering it is not possible.

The six here run in under three seconds together, which is why they are the
ones wired into the build. The slower reports — element readiness, the unbound
global scan, the ungated-draw walk — stay manual.
"""
import argparse
import pathlib
import re
import subprocess
import sys

TOOLS = pathlib.Path(__file__).resolve().parent

# tool, pattern capturing the count, ceiling, what the count means
CHECKS = [
    ("handover_halves_check.py",
     r"^(\d+) with no frameXmlOwns gate", 0,
     "elements this client keeps drawing after handing them over"),
    ("handover_halves_check.py",
     r"^(\d+) with no suppression entry", 0,
     "elements FrameXML draws while this client still owns them"),
    ("handover_check.py",
     r"^(\d+) call\(s\) naming nothing that exists", 0,
     "interface commands naming a function that does not exist"),
    ("handover_check.py",
     r"^(\d+) action\(s\) acted on in more than one file", 0,
     "keys driving the interface from two places, which cancel out"),
    ("framexml_load_check.py",
     r"^(\d+) that resolve to nothing", 0,
     "manifest entries and script references pointing at no file"),
    ("framexml_short_returns.py",
     r"^(\d+) binding\(s\) may return short", 15,
     "bindings answering fewer values than the interface unpacks"),
    ("misleading_indent_check.py",
     r"^(\d+) statement\(s\) dressed as though", 0,
     "statements dressed as though a braceless if guarded them"),
    ("declared_vs_read_check.py",
     r"attributes declared, (\d+) the emitter never names", 18,
     "XML attributes the emitter never reads"),
    ("declared_vs_read_check.py",
     r"^(\d+) script type\(s\) declared and never fired", 3,
     "script handlers FrameXML declares that nothing fires"),
    ("declared_vs_read_check.py",
     r"sound names asked for, (\d+) with nothing behind them", 24,
     "UI sounds FrameXML asks for with nothing behind them"),
    ("declared_vs_read_check.py",
     r"CVars named, (\d+) the client never answers", 52,
     "CVars the client never answers, so they read as off"),
    ("declared_vs_read_check.py",
     r"^(\d+) constant\(s\) set in both places", 1,
     "constants the bootstrap and the interface disagree about"),
    # The one remaining is correct and will not go to zero: readycheck.lua reads
    # a `preempted` flag off READY_CHECK_FINISHED, and AzerothCore's
    # HandleRaidReadyCheckFinishedOpcode broadcasts that message with an empty
    # body. There is no flag on the wire to send, and nil reads as false, which
    # is what "not preempted" wants. Lower this only if that stops being true.
    ("framexml_event_arity.py",
     r"^(\d+) fired with fewer arguments than a handler reads", 1,
     "events fired with fewer arguments than a handler unpacks"),
    # Four, and all four deliberate: ITEM_LOCK_CHANGED and RUNE_POWER_UPDATE
    # both use the absence of the second argument as the signal ("not arg2" is
    # how the paperdoll knows an equipment slot from a bag slot, "not usable" is
    # how a rune knows it is spent), UPDATE_TICKET fires empty to say there is
    # no ticket, and TRACKED_ACHIEVEMENT_UPDATE carries a timer from the packet
    # and only an id from the tracking toggle, which the tracker guards for.
    # The ceiling is here for the fifth, which will be an accident.
    ("framexml_event_arity.py",
     r"^(\d+) fired from several places with differing counts", 4,
     "events whose argument count depends on which path fired them"),
    # Thirty, and most are correctly absent: their dialog is shown by a message
    # this client is never sent — the bind-confirmation family needs
    # LOOT_BIND_CONFIRM and its three siblings, none of which any opcode here
    # produces, and the arena and socketing ones need features that are not
    # here at all. The ceiling is for a regression, and for the day one of
    # those events starts being fired: a popup that can open with an unbound
    # accept is a player pressing a button and getting an error.
    ("staticpopup_verbs_check.py",
     r"^(\d+) name\(s\) a popup button calls and nothing answers", 30,
     "names a static popup's buttons call that nothing answers"),
    # The three wire-shape checks. Each is a fault that no test catches: a
    # request the server drops on the floor, a reply read at the wrong offsets,
    # a guard that stops a handler running at all.
    ("cmsg_size_check.py",
     r"^(\d+) request\(s\) shorter than the server reads", 0,
     "requests shorter than the server reads, which it drops"),
    ("cmsg_size_check.py",
     r"^(\d+) request\(s\) written in a different shape", 0,
     "requests written in a different field order from the server's"),
    ("packet_layout_check.py",
     r"^(\d+) packet\(s\) read in a different shape", 0,
     "replies read at different offsets from the ones written"),
    ("packet_size_check.py",
     r"^(\d+) guard\(s\) longer than the packet", 0,
     "guards longer than the packet, so the handler never runs"),
    # Six, and five of them are right: scripts and registered events live on
    # the Lua table and always will, and GetName reads a name set once at
    # creation. The ceiling is for the seventh. SetParent sat in this list
    # writing __parent while layout went on using the widget's, and GetCenter
    # read a field that only a dead SetPoint had ever written.
    ("widget_field_check.py",
     r"^(\d+) method\(s\) touch only a Lua field", 6,
     "frame methods writing a Lua field where the widget is what is read"),
    # Nineteen, none of them reachable: the login splash, the tic-tac-toe
    # minigame, and one talent-frame background behind a branch that cannot run
    # — SELECTEDSPEC_DISPLAYTYPE is "GOLD_INSIDE" and the texture is only asked
    # for by the two "PUSHED_OUT" spellings. The ceiling is for the twentieth. A
    # missing texture raises nothing: the frame is built, laid out and drawn,
    # and the part that should have art is simply absent.
    ("framexml_art_check.py",
     r"^(\d+) not in this install", 19,
     "art the interface asks for that this install does not have"),
    ("api_shadowing_check.py",
     r"^\s*(\d+) to look at", 10,
     "names whose winner depends on load order"),
]

# Prose rather than a count: the chunk checker says one of two sentences.
SENTENCES = [
    ("bootstrap_chunk_check.py",
     "every local they use is declared in the chunk that uses it",
     "a bootstrap chunk using a local another chunk declared"),
]


def run(tool):
    out = subprocess.run([sys.executable, str(TOOLS / tool)],
                         capture_output=True, text=True)
    return out.stdout + out.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true",
                    help="print the ceilings and exit")
    args = ap.parse_args()

    if args.list:
        for tool, _, ceiling, what in CHECKS:
            print(f"  {ceiling:>3}  {what}   [{tool}]")
        for tool, _, what in SENTENCES:
            print(f"    -  {what}   [{tool}]")
        return 0

    # One run per tool, shared by its checks.
    outputs = {}
    for tool, *_ in CHECKS:
        outputs.setdefault(tool, None)
    for tool, *_ in SENTENCES:
        outputs.setdefault(tool, None)
    for tool in outputs:
        outputs[tool] = run(tool)

    failures = []
    for tool, pattern, ceiling, what in CHECKS:
        m = re.search(pattern, outputs[tool], re.M)
        if not m:
            failures.append(f"{tool}: could not read its own count for "
                            f"'{what}' — the report's shape changed, which "
                            f"makes this guard silently useless")
            continue
        found = int(m.group(1))
        status = "ok " if found <= ceiling else "OVER"
        print(f"  {status}  {found:>3} / {ceiling:<3}  {what}")
        if found > ceiling:
            failures.append(f"{tool}: {found} {what}, ceiling is {ceiling}")

    for tool, sentence, what in SENTENCES:
        clean = sentence in outputs[tool]
        print(f"  {'ok ' if clean else 'OVER'}    -       {what}")
        if not clean:
            failures.append(f"{tool}: {what}")

    if failures:
        print(f"\n{len(failures)} sweep(s) worse than the pinned ceiling:\n")
        for f in failures:
            print(f"  {f}")
        print("\nEach of these is a fault that raises nothing and fails no "
              "other test.\nFix it, or move the ceiling deliberately and say "
              "why in the commit.")
        return 1

    print("\nEvery sweep at or under its ceiling.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
