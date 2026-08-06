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
     r"sound names asked for, (\d+) with no hand-written mapping", 24,
     "UI sounds with no hand-written mapping (the dbc answers most of them)"),
    ("declared_vs_read_check.py",
     r"CVars named, (\d+) the client never answers", 51,
     "CVars the client never answers, so they read as off"),
    ("declared_vs_read_check.py",
     r"^(\d+) constant\(s\) set in both places", 1,
     # The one left is DEFAULT_CHAT_FRAME, and it is not a disagreement: the
     # bootstrap's table is a placeholder for before the interface loads, and
     # FrameXML replaces it with ChatFrame1 on purpose. What that cost — every
     # message FrameXML writes going to a hidden frame when this client owns
     # the chat — is handled by the redirect in AddonManager::loadFrameXml
     # rather than by making the two agree. It will not go to zero.
     "constants the bootstrap and the interface disagree about"),
    # The one remaining is correct and will not go to zero: readycheck.lua reads
    # a `preempted` flag off READY_CHECK_FINISHED, and AzerothCore's
    # HandleRaidReadyCheckFinishedOpcode broadcasts that message with an empty
    # body. There is no flag on the wire to send, and nil reads as false, which
    # is what "not preempted" wants. Lower this only if that stops being true.
    # Two, both decisions rather than gaps. READY_CHECK_FINISHED's `preempted`
    # says the check was cut short rather than answered, and this client is
    # never told which. PLAYER_FOCUS_CHANGED has no argument in this version at
    # all — focusframe unpacks `local arg1 = ...` once at the top of a chain of
    # six events and only one of them carries anything, which is the ambiguity
    # this sweep documents rather than resolves.
    ("framexml_event_arity.py",
     r"^(\d+) fired with fewer arguments than a handler reads", 2,
     "events fired with fewer arguments than a handler reads"),
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
    # Twenty-five, and most are correctly absent: their dialog is shown by a
    # message this client is never sent — the bind-confirmation family needs
    # LOOT_BIND_CONFIRM and its three siblings, none of which any opcode here
    # produces, and the arena and socketing ones need features that are not
    # here at all. The ceiling is for a regression, and for the day one of
    # those events starts being fired: a popup that can open with an unbound
    # accept is a player pressing a button and getting an error.
    #
    # That day came for CONFIRM_LOOT_ROLL on 2026-08-05, and in the useful
    # direction: it is raised by the *client*, not the server, so nothing here
    # raised it and Need on a bind-on-pickup item bound it with no warning.
    # RollOnLoot raises it now and ConfirmLootRoll answers it. Checking whether
    # a popup is reachable means asking which event shows it and whether this
    # client fires that event — not whether the verb is bound.
    ("staticpopup_verbs_check.py",
     r"^(\d+) name\(s\) a popup button calls and nothing answers", 25,
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
    # CVAR_UPDATE carries the CVar's label, not its name, and the two are
    # different spellings — so a mapping that cannot produce a label the
    # interface tests for is a branch that can never be taken. Silently: a
    # string that is not equal to another string is not an error.
    ("framexml_cvar_label_check.py",
     r"^(\d+) that no CVar name here would produce", 1,
     "CVAR_UPDATE labels the interface tests for that nothing can produce"),
    # The same question the event-arity sweeps ask, one layer down: an inline
    # <OnX> body is a function whose parameter list the emitter decides from
    # the script's name, and a body naming something that list does not carry
    # reads a global, finds nothing and carries on with nil. Two remain and
    # both are Blizzard's own deliberate nil, guarded on the far side.
    ("framexml_script_args.py",
     r"^(\d+) body/signature disagreement", 2,
     "handler bodies reading an argument their signature does not carry"),
    # A dozen things are driven by finding a FrameXML frame by name and
    # handing it something — the minimap and world map are told where to be,
    # every portrait and model frame is handed an image rendered for it. A name
    # matching nothing answers null: no error, no warning, no picture, and a
    # typo looks exactly like a frame that was never built. Zero, because there
    # is no reason to look up a name the interface does not have.
    ("framexml_lookup_names_check.py",
     r"^(\d+) looked up that the interface does not declare", 0,
     "frames this client looks up by a name the interface does not declare"),
    # Unit bindings that never look at the unit they were asked about and
    # answer from the player. A number belonging to the wrong character is the
    # hardest kind of wrong to see — nothing empty, nothing zero, nothing
    # raised. UnitStat, UnitResistance, UnitArmor, UnitAttackPower, UnitDamage
    # and UnitFactionGroup all sat here, listing a hunter's own figures as the
    # pet's and putting an Alliance badge over a Horde target;
    # SetInventoryItem did the same on the inspect paperdoll.
    #
    # Ten, each read once. Five are the player's sheet alone, the pet tab
    # having no ranged or defence line — UnitDefense, UnitRangedAttack,
    # UnitRangedAttackPower, UnitRangedDamage, UnitAttackBothHands. Two are
    # asked only with "player": UnitControllingVehicle and UnitIsSameServer.
    # GetUnitHealthModifier answers one for every unit, which is the true
    # answer in 3.3.5. The last two are the check's own blind spots, named in
    # its docstring: IsUnitOnQuest takes the unit second, and
    # UnitPlayerOrPetInRaid delegates to a binding that does resolve.
    ("unit_argument_check.py",
     r"^(\d+) unit binding\(s\) that never look at their unit", 10,
     "unit bindings answering from the player whatever they were asked"),
    # Requests the server reads off the wire and throws away — an opcode
    # registered Handle_NULL. Two, and both are accounted for:
    # CMSG_SUSPEND_COMMS_ACK is an acknowledgement the server has no use for,
    # and CMSG_PET_UNLEARN_TALENTS has no live opcode to replace it — a pet
    # talent wipe is a spell, the way the player's spec switch turned out to
    # be. A third row means a request that leaves and changes nothing, which
    # is the quietest failure a request has: nothing malformed, nothing
    # logged, no size or layout check disturbed. The difficulty change and the
    # ready-check answer both sat here.
    ("discarded_request_check.py",
     r"^(\d+) that the server reads and discards", 2,
     "requests the server reads and discards"),
    # Update-field indices against the server's own UpdateFields.h. Zero, and it
    # has to stay zero: a wrong index reads whatever sits at that slot and the
    # value is simply wrong forever, with no error anywhere. Five were wrong
    # when this was written — UNIT_FIELD_BYTES_1 and UNIT_DYNAMIC_FLAGS at 137
    # and 147, which are inside the unit block and so look right, against the
    # server's 74 and 79; and the chosen title and both PvP currencies past
    # PLAYER_END, where nothing can arrive. WotLK only: it is the only server
    # here, and the other expansions' files are unverifiable from it.
    ("update_field_check.py",
     r"^(\d+) disagree with the server's own header", 0,
     "update-field indices disagreeing with the server's header"),
    # DBC field indices naming a column the file does not have. One:
    # CharacterFacialHairStyles.Geoset200 = 8. The layouts describe the stock
    # nine-column file, which is a real shape and the right thing for them to
    # describe; the eight-column file both installs here carry is handled by
    # detectFacialHairFields, which picks 3-5 on the field count. Lower this
    # only by making that decision somewhere the JSON can express, and read a
    # new row as a column being read from padding — that one was drawing no
    # facial hair at all and saying nothing.
    #
    # One rather than four because the check now scores the four layouts
    # against the files and reads only the best-fitting one. There is a single
    # set of DBCs here; checking a Classic layout against WotLK data reported
    # three expansions' worth of noise that looked exactly like findings.
    ("dbc_layout_check.py",
     r"^(\d+) field\(s\) naming a column the file does not have", 1,
     "DBC field indices naming a column the file does not have"),
    # Packet handlers that change this client's own model, tell the player in
    # chat, and tell the interface nothing — the shape that produces a bug
    # correct after a relog and wrong until then. Twenty, each read once: what
    # they write is bookkeeping nothing draws.
    #
    # The count rose from fourteen when the sweep learned to read named handler
    # methods as well as inline lambdas — a dispatch entry is often one line
    # calling handleFoo, and reading only the lambda sees a body that calls one
    # function. Four real ones have been fixed: the equipment manager's new
    # set, the withdrawn summon dialog, a dead pet's frame and ability bar, and
    # the flight map left open for the whole flight.
    ("handler_announce_check.py",
     r"^(\d+) that tell the player and not the interface", 20,
     "handlers that change state and announce nothing"),
    # Top-level FrameXML frames named nowhere in framexml_takeover.cpp —
    # neither handed over nor suppressed.
    #
    # This comment used to say all forty-four had been checked against this
    # client's own UI and none had a counterpart. That was wrong, and wrong in
    # the way a claim written once and cited afterwards usually is: the check
    # it describes was a search for look-alike windows, and what makes a
    # duplicate is a shared *trigger*. Three of the forty-four were charter
    # windows raised by three events this client fires, beside two popups
    # social_panel.cpp draws from the same packets. They are
    # UiElement::Petition now.
    #
    # So the ceiling guards a list that has been read the right way once, on
    # 2026-08-05, with the result written into the tool's docstring: fifteen
    # dormant, eighteen opened by a control in FrameXML's own interface, four
    # waiting on an event nothing fires. The thirty-eighth is the one to look
    # at. The tool's own blind spot is frames built by CreateFrame, which its
    # docstring measures and lists.
    # Bindings answering a boolean or nil where FrameXML compares a number.
    # Never in this list until 2026-08-05, which is the whole reason its nil
    # arm could be added and be hollow at the same time: nothing ran it.
    # Two standing rows, both read and both written into its docstring —
    # an unreachable debug reader and a Wintergrasp timer whose nil is
    # `and`-guarded.
    ("framexml_bool_vs_number.py",
     r"^(\d+) binding\(s\) answer a boolean or nil", 2,
     "bindings answering a boolean or nil where a number is compared"),
    ("framexml_unaccounted_frames.py",
     r"^\d+ top-level frames, (\d+) unaccounted", 37,
     "FrameXML frames neither handed over nor suppressed"),
    # The blind spot both widget-method sweeps had: they count the no-op
    # allowlist as answered, which is right for "does the call raise here" and
    # wrong for a caller that reads what comes back. A no-op returns nil, and
    # nil in a comparison raises one line later, inside a function that looks
    # unrelated. GetFieldSize sat in the allowlist while its one caller
    # compared a byte count against it, so the guild event log came out blank
    # whenever it had events to show. Zero, because there is no such thing as
    # a deliberate one: if a caller reads the answer, the method is not a no-op.
    ("framexml_noop_returns.py",
     r"^(\d+) whose answer is used where nil raises", 0,
     "no-op widget methods whose nil answer reaches a comparison"),
    # dispatchSlashCommand stops at the first handler and reports success even
    # when that handler errors, so any command FrameXML defines wins whether it
    # works or not. Zero is the honest ceiling for a handler that can do nothing
    # at all; the tool's second list — a dead call beside a live one — is two
    # Battle.net branches that cannot run and is not guarded.
    ("framexml_slash_shadowing.py",
     r"^(\d+) client command\(s\) whose handler has no live call", 0,
     "slash commands FrameXML takes over with a handler that cannot act"),
    # The blind spot the other arity sweep has by design: it skips handlers
    # that unpack at the top, because one handler usually serves many events.
    # Where a handler serves exactly one, that unpack IS the signature.
    ("framexml_handler_arity.py",
     r"^(\d+) single-event handler\(s\) unpacking more at the top", 0,
     "single-event handlers unpacking more than the client fires"),
    # What the two arity sweeps structurally cannot see: the count being right
    # while the values are in the wrong places. That is what the spellcast
    # events did — two fired where two were read, the second one wrong.
    ("framexml_event_order.py",
     r"^(\d+) argument\(s\) in the wrong position", 0,
     "event arguments of the wrong kind for the position they are in"),
    # The same question as framexml_event_order, asked of bindings instead of
    # events: the count is right and the values are in the wrong slots.
    ("framexml_return_order.py",
     r"^(\d+) return value\(s\) in the wrong position", 0,
     "binding return values of the wrong kind for their position"),
    # A panel that polls its own keybinding from inside its own draw stops
    # answering that key the moment the draw is gated off — which is what
    # handing the element over does. Three were live on 2026-08-05: the talent
    # frame, the guild roster and the dungeon finder.
    ("keybinding_route_check.py",
     r"^(\d+) that would stop working when the panel is handed over", 0,
     "keys that stop working when their panel is handed over"),
    # A window gated on frameXmlOwns has to be opened through FrameXML once its
    # element is handed over, and the controls that open it are scattered — a
    # key, a micro-menu button, a bag icon, a context menu, the click half of a
    # drag handler, the code that puts the bags up for a vendor. Five separate
    # lists were found this way, three by hand before this existed.
    ("window_route_check.py",
     r"^(\d+) window-opening call\(s\) with no ownership check", 0,
     "controls opening a window without asking which interface owns it"),
    # The other half of the same seam: a window opened by writing its flag
    # rather than by calling a verb. Only the flags whose render is gated
    # matter — nine of thirty-two — which is what makes the list readable.
    ("window_flag_check.py",
     r"^(\d+) write\(s\) to one of those flags with no ownership check", 0,
     "window flags written without asking which interface owns the window"),
    # "Owned or suppressed" applied to dialogs one at a time. Seven were drawn
    # twice on 2026-08-05, three of them under the plain defaults. The shared
    # quest joined them later the same day, once QUEST_ACCEPT_CONFIRM started
    # being fired — which is exactly the day its reason stopped holding.
    #
    # Three left, each read: the duel countdown has no FrameXML counterpart at
    # all, the pet unlearn confirmation's CONFIRM_PET_UNLEARN exists here only
    # as a globalstring with no popup using it, and the battleground invite
    # needs CONFIRM_BATTLEFIELD_ENTRY, which nothing here fires. That last is a
    # thinner reason than the other two and stops holding the day it is wired.
    ("dialog_gate_check.py",
     r"^(\d+) with no ownership check", 3,
     "dialogs drawn without asking whether FrameXML draws them too"),
    ("api_shadowing_check.py",
     r"^\s*(\d+) to look at", 9,
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
