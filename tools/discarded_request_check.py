#!/usr/bin/env python3
"""Requests this client sends that the server reads off the wire and throws away.

    tools/discarded_request_check.py [--server PATH]

An opcode can be perfectly real, present in both opcode tables, correctly sized
and correctly built, and still do nothing — because the server registers it with
Handle_NULL. Two hundred and thirty-eight of AzerothCore's client opcodes are
registered that way: the number exists so the packet can be recognised and
discarded, usually because the opcode was replaced in a later expansion and the
name kept.

That is the quietest failure a request can have. Nothing is malformed, nothing
is logged, no size check fires and no layout check fires — the packet leaves and
the world does not change. CMSG_CHANGEPLAYER_DIFFICULTY was the case that named
this: every difficulty change this client sent, including the ones its own
/difficulty command sends, went to Handle_NULL. The opcodes the server reads are
MSG_SET_DUNGEON_DIFFICULTY and MSG_SET_RAID_DIFFICULTY, and both were already in
the table beside it.

WHAT IT LOOKS FOR

Every opcode this client *constructs a packet with* — `network::Packet p(
wireOpcode(Opcode::X))` — checked against the handler the server registers for
that name.

Construction, not mention. The first cut matched any `wireOpcode(Opcode::X)`
and so counted the incoming-handler tables and the `wireOp == wireOpcode(...)`
comparisons in the movement code, which are the opposite of a send: ten of the
sixteen it first reported were opcodes this client only ever *receives*.

WHAT IT CANNOT SEE

An opcode the server handles but ignores in some *condition* — a guard inside
the handler rather than at the table. This is about the table only.

Nor does it know what the client should send instead. Handle_NULL says the
request is dead, not what replaced it; that is a question for the handler list
and usually has an obvious neighbour.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

#: Handlers that mean "recognised, then dropped".
DEAD = {"Handle_NULL", "Handle_ServerSide", "Handle_Deprecated"}


def server_handlers(path):
    """opcode name -> the handler the server registers for it."""
    text = Path(path).read_text(errors="ignore")
    out = {}
    for m in re.finditer(
            r"DEFINE_(?:SERVER_OPCODE_)?HANDLER\(\s*(\w+)\s*,[^,]+,[^,]+,"
            r"\s*&WorldSession::(\w+)", text):
        out[m.group(1)] = m.group(2)
    # The server-side form has no handler column; those are replies, not
    # requests, and are not this check's business.
    for m in re.finditer(r"DEFINE_SERVER_OPCODE_HANDLER\(\s*(\w+)", text):
        out.setdefault(m.group(1), "server")
    return out


def client_sends():
    """opcode name -> the files that build a packet for it."""
    out = {}
    for path in (ROOT / "src").rglob("*.cpp"):
        text = path.read_text(errors="ignore")
        for m in re.finditer(
                r"Packet\s+\w+\s*\(\s*wireOpcode\(\s*Opcode::(\w+)\s*\)", text):
            name = m.group(1)
            if name.startswith("SMSG_"):
                continue
            out.setdefault(name, set()).add(path.name)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="/home/k/azerothcore-wotlk/src/server"
                                        "/game/Server/Protocol/Opcodes.cpp")
    args = ap.parse_args()
    if not Path(args.server).is_file():
        print(f"server opcode table not found at {args.server}")
        return 0

    handlers = server_handlers(args.server)
    sends = client_sends()

    rows, unknown = [], []
    for name, files in sorted(sends.items()):
        handler = handlers.get(name)
        if handler is None:
            unknown.append(name)
        elif handler in DEAD:
            rows.append((name, handler, " ".join(sorted(files))))

    print(f"{len(sends)} opcode(s) this client sends, {len(handlers)} in the "
          f"server's table, {len(unknown)} it does not list\n")
    print(f"{len(rows)} that the server reads and discards:\n")
    for name, handler, files in rows:
        print(f"  {name:44} {handler:16} {files}")
    if not rows:
        print("  (none)")
    if unknown:
        print(f"\nnot in the server's table, so not judged: "
              f"{', '.join(unknown[:14])}"
              f"{' ...' if len(unknown) > 14 else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
