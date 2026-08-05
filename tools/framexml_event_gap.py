#!/usr/bin/env python3
"""Events the interface waits for that this client never sends.

The companion to framexml_element_readiness.py, and the more productive of the
two. That one asks whether an element's calls are answered — a call that is not
raises, so the gap is loud. This asks whether its events arrive, and the failure
is silent: the frame simply sits there, or shows what it was drawn with, or
never opens at all.

    tools/framexml_event_gap.py

Eleven real bugs came out of this after the call side had gone quiet — a mail
frame that hung after sending, bag cooldown swirls that never drew, an
achievements panel showing the empty state it was built with, a master looter
menu that could not open, the whole dungeon finder silently inert.

THE SHAPE TO LOOK FOR
---------------------
Nearly every one was the same: a message **received, parsed, stored, and never
announced**. The data was there and something already read it — often this
client's own ImGui window, which is why the feature worked while FrameXML's
version of it did not. Only the notification between the two was missing.

So the second table below is the one to read first: events with no sender whose
source message the client *already handles*. Those are two working halves with
nothing in between, and they are nearly always real.

TWO THINGS THIS CANNOT DECIDE
-----------------------------
* **"Never fired" can mean "something else is fired in its place."**
  SMSG_QUESTGIVER_QUEST_LIST was sending GOSSIP_SHOW where QUEST_GREETING
  belonged, so the gossip frame opened over the quest list. Read the handler.

* **Check whether the server sends the message, by finding where it builds it.**
  The source is on this machine — /home/k/azerothcore-wotlk — so grep for
  `WorldPacket data(SMSG_...)` in src/server/game.

  Do *not* use the STATUS_NEVER field in Opcodes.cpp for this. It reads like a
  statement that the server never sends the opcode and is not one:
  SMSG_ITEM_TEXT_QUERY_RESPONSE and SMSG_GMRESPONSE_RECEIVED are both marked
  STATUS_NEVER and both are built and sent, in ItemHandler.cpp and TicketMgr.cpp.
  Trusting that field produced four wrong conclusions in a row here.

  By the correct test, the three names below are all genuinely sent —
  BattlefieldHandler.cpp and LFGMgr.cpp build them — so they are real gaps
  rather than dead opcodes.

* **An event is only worth firing if the data behind it exists.** The
  battlefield eject pair carries a relocation and a reason this client does not
  parse; a popup saying a player is being moved without being able to say where
  is worse than no popup. Same for the mail refund lock and the inbound GM
  replies. Check what the handler actually holds before wiring it.
"""

import os
import re

ROOT = os.path.join(os.path.dirname(__file__), "..")
INTERFACE = os.path.join(ROOT, "Data", "interface")
SRC = os.path.join(ROOT, "src")

EVENT = r'"([A-Z][A-Z0-9_]{3,})"'


def registered():
    """Events FrameXML asks for, and which files ask."""
    where = {}
    for dirpath, _, filenames in os.walk(INTERFACE):
        # The login screen is a separate interface in its own Lua state.
        if "gluexml" in dirpath.replace("\\", "/").split("/"):
            continue
        for name in filenames:
            if not name.endswith((".lua", ".xml")):
                continue
            src = open(os.path.join(dirpath, name), encoding="utf-8",
                       errors="ignore").read()
            # The closing paren matters: alternatepowerbar.lua registers
            # RegisterEvent("UNIT_"..self.powerName), and without it the
            # literal half of that concatenation was read as an event called
            # "UNIT_" — a name nothing can ever send, reported as a gap on
            # every run. A computed name cannot be checked from here at all.
            for pattern in (r'RegisterEvent\(\s*' + EVENT + r'\s*\)',
                            r'<Event\s+name=' + EVENT):
                for match in re.finditer(pattern, src):
                    where.setdefault(match.group(1), set()).add(name)
    return where


def sent():
    """Events this client can fire, from the calls that fire them."""
    names = set()
    for dirpath, _, filenames in os.walk(SRC):
        for name in filenames:
            if not name.endswith((".cpp", ".hpp")):
                continue
            src = open(os.path.join(dirpath, name), encoding="utf-8",
                       errors="ignore").read()
            # Two call shapes: addonEventCallbackRef()( ... ) through an
            # accessor, and addonEventCallback_( ... ) on the member directly.
            # Requiring both parens missed every one of the second kind.
            names |= set(re.findall(
                r'addonEventCallback[A-Za-z_]*(?:\(\))?\(\s*' + EVENT, src))
            names |= set(re.findall(r'fireAddonEvent\(\s*' + EVENT, src))
            # An event fired through a local alias:
            #     auto fire = owner_.addonEventCallbackRef();
            #     fire("LFG_PROPOSAL_UPDATE", {});
            # Neither pattern above sees that call, so every event sent this
            # way read as never sent — LFG_PROPOSAL_UPDATE was reported as a
            # gap while sitting two lines under its own alias.
            for alias in set(re.findall(
                    r'auto\s+(\w+)\s*=\s*[\w_.>()-]*addonEventCallback\w*\(\)', src)):
                names |= set(re.findall(
                    r'(?<![\w.])' + re.escape(alias) + r'\(\s*' + EVENT, src))
    return names


def handled_opcodes():
    """SMSG names this client has a handler for, and which of those only skip.

    A skipped opcode is named in the dispatch table and read to the end without
    being parsed — the deliberate answer for a feature this client does not
    have. Counting one as handled put SMSG_UPDATE_LFG_LIST at the top of the
    "read these first" list, where it stayed: the raid browser is not
    implemented, so an event backed by it is exactly as absent as one with no
    handler at all, and pointing at it every run made the list look answered
    when it was not.
    """
    names, skipped = set(), set()
    for dirpath, _, filenames in os.walk(os.path.join(SRC, "game")):
        for name in filenames:
            if not name.endswith(".cpp"):
                continue
            src = open(os.path.join(dirpath, name), encoding="utf-8",
                       errors="ignore").read()
            names |= set(re.findall(r"Opcode::(SMSG_[A-Z0-9_]+)", src))
            # registerSkipHandler(Opcode::X), and the lambda spelling of the
            # same thing — a body that reads to the end and does nothing else,
            # whether written once or shared by a list of opcodes.
            skipped |= set(re.findall(
                r"registerSkipHandler\(\s*Opcode::(SMSG_[A-Z0-9_]+)", src))
            for block in re.finditer(
                    r"for\s*\(\s*auto\s+\w+\s*:\s*\{([^}]*)\}\s*\)\s*\{"
                    r"\s*table\[\w+\]\s*=\s*\[\]\([^)]*\)\s*\{"
                    r"\s*packet\.skipAll\(\);\s*\};", src):
                skipped |= set(re.findall(r"Opcode::(SMSG_[A-Z0-9_]+)", block.group(1)))
            skipped |= set(re.findall(
                r"table\[Opcode::(SMSG_[A-Z0-9_]+)\]\s*=\s*\[\]\([^)]*\)\s*\{"
                r"\s*packet\.skipAll\(\);\s*\};", src))
    return names - skipped, skipped


def main():
    where, fired = registered(), sent()
    opcodes, skipped = handled_opcodes()

    # Battle.net has no counterpart on a 3.3.5 server.
    gap = sorted(e for e in where if e not in fired and not e.startswith("BN_"))

    def squash(name):
        return name.replace("_", "")

    backed = []
    for event in gap:
        for opcode in opcodes:
            body = opcode[len("SMSG_"):]
            if squash(body) == squash(event) or squash(event) in squash(body):
                backed.append((event, opcode))
                break

    print(f"registered by FrameXML : {len(where)}")
    print(f"never sent             : {len(gap)}")
    print()
    print("never sent, but the source message IS handled — read these first:")
    for event, opcode in backed:
        files = " ".join(sorted(where[event])[:2])
        print(f"  {event:<38} {opcode:<44} {files}")
    print()
    print(f"{len(backed)} of {len(gap)} have a handler already. The rest are")
    print("features this client does not have, and are correctly silent.")

    # Told apart rather than hidden. An opcode that is only skipped is a
    # decision on record — the packet arrives, is read to the end and nothing
    # is done with it — and an event behind one is as absent as an event behind
    # no handler at all. Worth seeing, and worth not being told to read first.
    onlySkipped = []
    for event in gap:
        for opcode in skipped:
            body = opcode[len("SMSG_"):]
            if squash(body) == squash(event) or squash(event) in squash(body):
                onlySkipped.append((event, opcode))
                break
    if onlySkipped:
        print()
        print("Backed only by a skip handler — the packet is read and dropped, "
              "which is\na decision rather than a gap:")
        for event, opcode in onlySkipped:
            print(f"  {event:<38} {opcode}")


if __name__ == "__main__":
    main()
