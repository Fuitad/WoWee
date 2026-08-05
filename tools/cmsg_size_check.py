#!/usr/bin/env python3
"""Client requests shorter than the server reads.

packet_size_check.py asks the incoming question: whether a handler's length
guard is longer than the packet the server sends, which disables it silently.
This is the same question pointed the other way, and it fails harder.

A WorldPacket read past its end throws inside AzerothCore's ByteBuffer. The
session catches it, logs, and drops the packet — so the request simply never
happened. There is no error to the client, no refusal, nothing on screen. From
this side it looks exactly like a server that ignored you.

That is how accepting a Wintergrasp invitation came to do nothing for as long
as the code existed. HandleBfEntryInviteResponse reads a uint32 battle id and
then a uint8 flag; this client sent the flag alone, so the server read it as
the low byte of a battle id and ran out of buffer on the next field.

    tools/cmsg_size_check.py [--server PATH]

HOW EACH SIDE IS MEASURED

Server: from the handler named in Opcodes.cpp, the local declarations are
collected (`uint32 BattleId;`) and then the first `recvData >> a >> b` chain is
summed using those types. The sum stops at the first name whose type is not a
plain integer — a string, an ObjectGuid, anything declared elsewhere — because
past that point the length is not fixed. What is summed is therefore a minimum
the server *will* read before it can branch.

Client: from `network::Packet name(wireOpcode(Opcode::CMSG_X))`, the
`name.writeUintN(...)` calls that follow are summed until the send. A
writeString or a writePackedGuid ends the sum, since neither has a width here
either — and a request that opens with one cannot be compared at all and is
skipped rather than guessed at.

VALIDATED AGAINST THE BUG IT WAS WRITTEN FOR

As acceptBfMgrInvite stood before the fix — one writeUInt8 straight after the
packet was constructed — this reports it: sends 1, server reads 5. It does not
report it now, and not because the bug is fixed: the fix routed all three
answers through one helper that takes the opcode as an argument, so there is no
Opcode:: literal beside the writes for this to find. Worth knowing before
reading a zero as coverage.

WHAT IT CANNOT SEE

  * A request built by a helper that takes its opcode as a parameter. The
    literal is at the call site and the writes are in the helper, and nothing
    here joins them.
  * A conditional write. `if (withFlag) pkt.writeUInt8(...)` ends the sum and
    marks the size inexact, and an inexact size is never reported — being
    silent is the right failure here, since a guess would report every
    branching builder as short.
  * Two thirds of what this client sends. Most AzerothCore handlers open with
    an ObjectGuid or a string, neither of which has a width that can be summed,
    so there is no minimum to compare against. The run prints its own coverage
    for that reason.

WHAT IT DELIBERATELY DOES NOT DO

Compare field by field. Two packets of the same length can still disagree
about what those bytes mean, and this cannot see that. It answers one
question — whether the server will run out of buffer — and a clean run is not
a statement that the layouts match.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

WIDTH = {"uint8": 1, "int8": 1, "uint16": 2, "int16": 2, "uint32": 4, "int32": 4,
         "uint64": 8, "int64": 8, "float": 4, "double": 8, "bool": 1}

# writeUInt32 -> 4, and the same for the rest of the client's spellings.
CLIENT_WIDTH = {"writeUInt8": 1, "writeInt8": 1, "writeUInt16": 2, "writeInt16": 2,
                "writeUInt32": 4, "writeInt32": 4, "writeUInt64": 8, "writeInt64": 8,
                "writeFloat": 4, "writeDouble": 8}


def handler_for_opcode(server_root):
    """CMSG name -> the WorldSession method that reads it."""
    path = Path(server_root) / "Server/Protocol/Opcodes.cpp"
    if not path.is_file():
        return {}
    out = {}
    for m in re.finditer(
            r"DEFINE_HANDLER\(\s*(CMSG_\w+|MSG_\w+)\s*,[^)]*?"
            r"&WorldSession::(\w+)\s*\)", path.read_text(errors="ignore")):
        if m.group(2) != "Handle_NULL":
            out[m.group(1)] = m.group(2)
    return out


def server_reads(server_root, handlers):
    """CMSG name -> bytes the server reads before it can branch."""
    wanted = {name: op for op, name in handlers.items()}
    out = {}
    for path in Path(server_root).rglob("*.cpp"):
        src = path.read_text(errors="ignore")
        for m in re.finditer(r"void WorldSession::(\w+)\s*\(\s*WorldPacket\s*&\s*(\w+)\s*\)\s*\{", src):
            method, arg = m.group(1), m.group(2)
            if method not in wanted:
                continue
            body = src[m.end():m.end() + 2000]
            body = body[:body.find("\n}")] if "\n}" in body else body
            # Declarations, in every spelling AzerothCore uses: one name, a
            # comma-separated list, and either with an initialiser. Matching
            # only `uint32 name;` measured a third of what it could — a
            # handler declaring `uint32 a, b;` came out as reading nothing and
            # was skipped rather than compared.
            types = {}
            for decl in re.finditer(
                    r"\b(" + "|".join(WIDTH) + r")\s+([^;{}()]+);", body):
                for name in decl.group(2).split(","):
                    name = name.split("=")[0].strip()
                    if re.fullmatch(r"\w+", name):
                        types[name] = decl.group(1)
            chain = re.search(re.escape(arg) + r"\s*>>\s*([^;]+);", body)
            if not chain:
                continue
            total = 0
            for name in [n.strip() for n in chain.group(1).split(">>")]:
                if name not in types:
                    break          # a string, a guid, or declared elsewhere
                total += WIDTH[types[name]]
            if total:
                out[wanted[method]] = total
    return out


def client_writes():
    """CMSG name -> (bytes written, whether the size is exact)."""
    out = {}
    for path in list((ROOT / "src/game").rglob("*.cpp")) + \
                list((ROOT / "src/addons").rglob("*.cpp")):
        src = path.read_text(errors="ignore")
        for m in re.finditer(
                r"network::Packet\s+(\w+)\s*\(\s*wireOpcode\(\s*Opcode::(CMSG_\w+|MSG_\w+)",
                src):
            var, opcode = m.group(1), m.group(2)
            body = src[m.end():m.end() + 1500]
            total, exact = 0, True
            for line in body.split("\n")[1:40]:
                s = line.strip()
                if not s or s.startswith("//"):
                    continue
                # Every write on the line, not the first. This client puts
                # three on one line where the fields are short —
                #     pkt.writeUInt8(a); pkt.writeUInt8(0); pkt.writeUInt32(b);
                # — and reading only the leading one measured
                # CMSG_BATTLEFIELD_PORT as three bytes against the server's
                # nine. Both sites write all nine; the report was wrong, and a
                # sweep that invents a fault is worse than one that misses it.
                calls = re.findall(re.escape(var) + r"\.(\w+)\s*\(", s)
                if calls:
                    for call in calls:
                        if call in CLIENT_WIDTH:
                            total += CLIENT_WIDTH[call]
                        else:
                            # writeString, writePackedGuid: no width here.
                            exact = False
                            break
                    if not exact:
                        break
                    continue
                if "send(" in s or s.startswith(("return", "}")):
                    break
                # Anything else in the middle — a loop, a branch — ends it.
                exact = False
                break
            if total or not exact:
                prev = out.get(opcode)
                if prev is None or total > prev[0]:
                    out[opcode] = (total, exact)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="/home/k/azerothcore-wotlk/src/server/game")
    args = ap.parse_args()
    if not Path(args.server).is_dir():
        print(f"server source not found at {args.server}")
        return 1

    handlers = handler_for_opcode(args.server)
    reads = server_reads(args.server, handlers)
    writes = client_writes()
    shared = sorted(set(reads) & set(writes))

    inexact = sum(1 for op in shared if not writes[op][1])
    print(f"{len(reads)} server handlers with a measurable prefix, "
          f"{len(writes)} client requests built, {len(shared)} in both")
    print(f"of those {len(shared)}, {inexact} end in a string, a guid or a branch "
          f"and cannot be sized — so\n{len(shared) - inexact} are actually "
          f"compared. A zero below is a zero over those.\n")

    rows = []
    for op in shared:
        sent, exact = writes[op]
        # Only when the client's size is exact. A request that ends in a string
        # or a guid has more to come and cannot be short on this evidence.
        if exact and sent < reads[op]:
            rows.append((op, sent, reads[op]))

    print(f"{len(rows)} request(s) shorter than the server reads — these are "
          f"dropped, silently:\n")
    for op, sent, needs in rows:
        print(f"  {op:52} sends {sent}, server reads {needs}")
    if not rows:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
