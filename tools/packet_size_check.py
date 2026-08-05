#!/usr/bin/env python3
"""Client length checks that are longer than the packet the server sends.

A handler that opens with `if (!packet.hasRemaining(N)) return;` and gets a
packet shorter than N never runs. Not "runs and misreads" — never runs, no log
line, no error. From every other angle the feature is simply absent, which is
how SMSG_LFG_QUEUE_STATUS came to require thirty-three bytes of a thirty-one
byte packet and the dungeon finder's queue never updated once.

    tools/packet_size_check.py [--server PATH]

Reads AzerothCore's writers and the client's readers and compares the two.

HOW THE SERVER SIDE IS MEASURED

Only the fixed prefix, and only where it can be measured honestly. From
`WorldPacket data(SMSG_X, ...)` the following `data << uintN(...)` lines are
summed until the first thing whose width is not obvious — a loop, a string, an
ObjectGuid written packed, a conditional. Everything after that is unknown and
the sum becomes a LOWER BOUND, which is the safe direction: a client check
below the bound is fine, and only one above it is reported.

WHAT IT DELIBERATELY DOES NOT DO

Guess. A packet whose very first field is conditional yields no bound and is
skipped rather than estimated. This reports what it can prove.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

WIDTH = {"uint8": 1, "int8": 1, "uint16": 2, "int16": 2, "uint32": 4, "int32": 4,
         "uint64": 8, "int64": 8, "float": 4, "double": 8}


def server_minimums(server_root):
    """opcode -> (lower bound in bytes, whether the bound is exact)."""
    out = {}
    for path in Path(server_root).rglob("*.cpp"):
        try:
            src = path.read_text(errors="ignore")
        except OSError:
            continue
        for m in re.finditer(r"WorldPacket\s+\w+\s*\(\s*(SMSG_\w+)", src):
            opcode = m.group(1)
            total, exact = 0, True
            for line in src[m.end():].split("\n")[1:60]:
                s = line.strip()
                if not s or s.startswith("//"):
                    continue
                # The whole type name. An optional uint|int prefix here eats
                # half of it — "uint32(" captured as "32" — and every writer
                # measured as zero.
                w = re.match(r"\w+\s*<<\s*(\w+)\s*\(", s)
                if w and w.group(1) in WIDTH:
                    total += WIDTH[w.group(1)]
                    continue
                # Anything else ends the measurable prefix: a bare `data << x`
                # of unknown type, a string, a guid, a loop, a branch.
                if s.startswith(("SendPacket", "}", "return")):
                    break
                exact = False
                break
            if total and (opcode not in out or total > out[opcode][0]):
                out[opcode] = (total, exact)
    return out


def client_checks():
    """opcode -> the largest hasRemaining literal guarding its handler."""
    out = {}
    for path in (ROOT / "src/game").rglob("*.cpp"):
        src = path.read_text(errors="ignore")
        # A handler body runs from its opcode to the NEXT opcode of any kind.
        # Splitting on SMSG_ alone lets a region run past the end of its own
        # handler and pick up a guard belonging to something else, which
        # reported SMSG_TIME_SYNC_REQ — correctly guarding four — as guarding
        # eight.
        marks = [(m.start(), m.group(1)) for m in
                 re.finditer(r"Opcode::([A-Z]MSG_\w+)", src)]
        for i, (at, opcode) in enumerate(marks):
            if not opcode.startswith("SMSG_"):
                continue
            end = marks[i + 1][0] if i + 1 < len(marks) else len(src)
            body = src[at:end]
            # The FIRST guard that returns, which is the one about the packet.
            # The largest is not it: a later check inside a loop is about one
            # element of an array.
            g = re.search(r"hasRemaining\(\s*(\d+)\s*\)\s*\)\s*(?:\{\s*)?"
                          r"(?:packet\.skipAll\(\);\s*)?return", body)
            if g:
                out.setdefault(opcode, int(g.group(1)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="/home/k/azerothcore-wotlk/src/server/game")
    args = ap.parse_args()

    if not Path(args.server).is_dir():
        print(f"server source not found at {args.server}")
        return 1

    server = server_minimums(args.server)
    client = client_checks()
    shared = sorted(set(server) & set(client))

    print(f"{len(server)} server writers measured, {len(client)} client guards, "
          f"{len(shared)} opcodes in both\n")

    rows = [(op, client[op], server[op][0], server[op][1])
            for op in shared if client[op] > server[op][0]]
    print(f"{len(rows)} guard(s) longer than the packet — these handlers never run:\n")
    for op, guard, bound, exact in rows:
        qualifier = "exactly" if exact else "at least"
        print(f"  {op:44} guards {guard}, packet is {qualifier} {bound}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
