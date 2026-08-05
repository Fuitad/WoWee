#!/usr/bin/env python3
"""Packets the client reads in a different shape from the one the server wrote.

The two size sweeps beside this one ask whether a packet is long enough. This
asks the harder question: whether the fields line up. A handler can guard the
right number of bytes, run every time, and still be wrong from the first field
on — SMSG_BATTLEFIELD_MGR_EJECTED read a guid where the server wrote a battle
id, and it was the *length* that gave it away, not the layout. Two packets of
the same size disagreeing about what those bytes mean is invisible to both of
the other sweeps and to the client itself: nothing raises, nothing logs, the
numbers are simply wrong.

    tools/packet_layout_check.py [--server PATH]

WHAT IT COMPARES

A sequence of widths, not a sequence of names. From the server, the run of
`data << uintN(...)` after `WorldPacket data(SMSG_X, ...)`. From this client,
the run of `packet.readUIntN()` after the handler's opcode. Each stops at the
first thing whose width is not fixed — a string, a packed guid, a loop, a
branch — because past that point the sequences cannot be lined up by position.

A mismatch in that prefix is reported with both readings, so the disagreement
can be read rather than taken on trust.

WHY WIDTHS AND NOT TYPES

Signedness does not change where a field ends, and this is about position. A
client reading int32 where the server wrote uint32 is a separate question and
not one a byte count can answer.

WHAT IT FOUND, AND WHAT SURVIVED

Six on the first run, all real: SMSG_CHAR_RENAME read a four-byte result where
the server writes one byte, SMSG_BATTLEFIELD_MGR_ENTERED read a guid the server
does not send, SMSG_GMRESPONSE_STATUS_UPDATE read a ticket id that is not
there, and the three GM ticket answers each read one byte of a uint32 — which
little-endian kept working for the delete and broke for the other two.

Two are reported and are not faults. Both have been read and neither should be
silenced, because the shapes really do differ and a future edit could make one
of them matter:

  * SMSG_AUCTION_OWNER_NOTIFICATION — the client reads the server's uint64 as
    two uint32s. Every field after it therefore lands on the same offset it
    would have anyway, and the item entry, which is what this handler is for,
    is read correctly at offset twenty.
  * SMSG_PARTY_MEMBER_STATS_FULL — an artifact of how the client side is
    measured. Its handler is a one-line registration that delegates, so the
    reads found between this opcode and the next belong to a neighbour. The
    real parser reads the leading uint8 and then a packed guid, which matches.

WHAT IT CANNOT SEE

  * Anything after the first variable-width field. Most packets have one early,
    which is why the compared prefix is often short. The run prints how many
    opcodes had a prefix worth comparing at all.
  * A handler that reads through a helper or a parser class rather than
    directly. Those are the larger packets, and they are the ones where a
    misparse is hardest to spot by eye — a real gap in this, not a small one.
  * Two fields of the same width swapped. Identical widths line up perfectly;
    only the client's own reading of what the fields mean can catch that.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SERVER_WIDTH = {"uint8": 1, "int8": 1, "uint16": 2, "int16": 2, "uint32": 4,
                "int32": 4, "uint64": 8, "int64": 8, "float": 4, "double": 8}
CLIENT_WIDTH = {"readUInt8": 1, "readInt8": 1, "readUInt16": 2, "readInt16": 2,
                "readUInt32": 4, "readInt32": 4, "readUInt64": 8, "readInt64": 8,
                "readFloat": 4, "readDouble": 8}


def server_layouts(server_root):
    """SMSG name -> the widths it writes, up to the first variable field."""
    out = {}
    for path in Path(server_root).rglob("*.cpp"):
        src = path.read_text(errors="ignore")
        for m in re.finditer(r"WorldPacket\s+(\w+)\s*\(\s*(SMSG_\w+)", src):
            var, opcode = m.group(1), m.group(2)
            widths = []
            for line in src[m.end():].split("\n")[1:60]:
                s = line.strip()
                if not s or s.startswith("//"):
                    continue
                w = re.match(r"[*]?" + re.escape(var) + r"\s*<<\s*(\w+)\s*\(", s)
                if w and w.group(1) in SERVER_WIDTH:
                    widths.append(SERVER_WIDTH[w.group(1)])
                    continue
                break
            # The longest reading wins: several call sites build the same
            # opcode and only the fullest one describes the whole prefix.
            if widths and len(widths) > len(out.get(opcode, [])):
                out[opcode] = widths
    return out


def client_layouts():
    """SMSG name -> the widths this client reads, up to the first variable one."""
    out = {}
    for path in (ROOT / "src/game").rglob("*.cpp"):
        src = path.read_text(errors="ignore")
        marks = [(m.start(), m.group(1)) for m in
                 re.finditer(r"Opcode::([A-Z]MSG_\w+)", src)]
        for i, (at, opcode) in enumerate(marks):
            if not opcode.startswith("SMSG_"):
                continue
            end = marks[i + 1][0] if i + 1 < len(marks) else len(src)
            body = src[at:end]
            widths = []
            # Reads in the order they appear, stopping at the first one whose
            # width is not fixed. A guard or a log line between two reads is
            # not a field and does not end the run; anything that consumes
            # bytes and is not a plain integer does.
            for m in re.finditer(r"\bpacket\.(\w+)\s*\(", body):
                call = m.group(1)
                if call in CLIENT_WIDTH:
                    widths.append(CLIENT_WIDTH[call])
                elif call.startswith("read") or call in ("skipAll",):
                    break
            if widths and len(widths) > len(out.get(opcode, [])):
                out[opcode] = widths
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="/home/k/azerothcore-wotlk/src/server/game")
    args = ap.parse_args()
    if not Path(args.server).is_dir():
        print(f"server source not found at {args.server}")
        return 1

    server = server_layouts(args.server)
    client = client_layouts()
    shared = sorted(set(server) & set(client))

    print(f"{len(server)} server writers with a fixed prefix, "
          f"{len(client)} client readers, {len(shared)} in both\n")

    rows = []
    for op in shared:
        s, c = server[op], client[op]
        n = min(len(s), len(c))
        if s[:n] != c[:n]:
            rows.append((op, s[:n], c[:n]))

    print(f"{len(rows)} packet(s) read in a different shape from the one written:\n")
    for op, s, c in rows:
        print(f"  {op}")
        print(f"      server writes {s}")
        print(f"      client reads  {c}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
