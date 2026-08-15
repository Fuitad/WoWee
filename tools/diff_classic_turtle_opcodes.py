#!/usr/bin/env python3
"""
Report the semantic opcode diff between the Classic and Turtle expansion maps.

The report normalizes:
- hex formatting differences (0x67 vs 0x067)
- alias names that collapse to the same canonical opcode

It highlights:
- true wire differences for the same canonical opcode
- canonical opcodes present only in Classic or only in Turtle
- name-only differences where the wire matches after aliasing
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

from opcode_map_utils import canonicalize, load_opcode_map


RE_OPCODE_NAME = re.compile(r"^(?:CMSG|SMSG|MSG)_[A-Z0-9_]+$")


def read_aliases(path: Path) -> Dict[str, str]:
    data = json.loads(path.read_text())
    aliases = data.get("aliases", {})
    out: Dict[str, str] = {}
    for key, value in aliases.items():
        if isinstance(key, str) and isinstance(value, str):
            out[key] = value
    return out




def load_map(path: Path) -> Dict[str, int]:
    data = load_opcode_map(path)
    out: Dict[str, int] = {}
    for key, value in data.items():
        if not isinstance(key, str) or not RE_OPCODE_NAME.match(key):
            continue
        if not isinstance(value, str) or not value.lower().startswith("0x"):
            continue
        out[key] = int(value, 16)
    return out


@dataclass(frozen=True)
class CanonicalEntry:
    canonical_name: str
    raw_value: int
    raw_names: Tuple[str, ...]


def build_canonical_entries(
    raw_map: Dict[str, int], aliases: Dict[str, str]
) -> Dict[str, CanonicalEntry]:
    grouped: Dict[str, List[Tuple[str, int]]] = {}
    for raw_name, raw_value in raw_map.items():
        canonical_name = canonicalize(raw_name, aliases)
        grouped.setdefault(canonical_name, []).append((raw_name, raw_value))

    out: Dict[str, CanonicalEntry] = {}
    for canonical_name, entries in grouped.items():
        raw_values = {raw_value for _, raw_value in entries}
        if len(raw_values) != 1:
            formatted = ", ".join(
                f"{name}=0x{raw_value:03X}" for name, raw_value in sorted(entries)
            )
            raise ValueError(
                f"Expansion map contains multiple wires for canonical opcode "
                f"{canonical_name}: {formatted}"
            )
        raw_value = next(iter(raw_values))
        raw_names = tuple(sorted(name for name, _ in entries))
        out[canonical_name] = CanonicalEntry(canonical_name, raw_value, raw_names)
    return out


def format_hex(raw_value: int) -> str:
    return f"0x{raw_value:03X}"


def emit_section(title: str, rows: Iterable[str], limit: int | None) -> None:
    rows = list(rows)
    print(f"{title}: {len(rows)}")
    if not rows:
        return
    shown = rows if limit is None else rows[:limit]
    for row in shown:
        print(f"  {row}")
    if limit is not None and len(rows) > limit:
        print(f"  ... {len(rows) - limit} more")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument(
        "--limit",
        type=int,
        default=80,
        help="Maximum rows to print per section; use -1 for no limit.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    aliases = read_aliases(root / "Data/opcodes/aliases.json")
    classic_raw = load_map(root / "Data/expansions/classic/opcodes.json")
    turtle_raw = load_map(root / "Data/expansions/turtle/opcodes.json")

    classic = build_canonical_entries(classic_raw, aliases)
    turtle = build_canonical_entries(turtle_raw, aliases)

    classic_names = set(classic)
    turtle_names = set(turtle)
    shared_names = classic_names & turtle_names

    different_wire = []
    same_wire_name_only = []
    for canonical_name in sorted(shared_names):
        c = classic[canonical_name]
        t = turtle[canonical_name]
        if c.raw_value != t.raw_value:
            different_wire.append(
                f"{canonical_name}: classic={format_hex(c.raw_value)} "
                f"turtle={format_hex(t.raw_value)}"
            )
        elif c.raw_names != t.raw_names:
            same_wire_name_only.append(
                f"{canonical_name}: wire={format_hex(c.raw_value)} "
                f"classic_names={list(c.raw_names)} turtle_names={list(t.raw_names)}"
            )

    classic_only = [
        f"{name}: {format_hex(classic[name].raw_value)} names={list(classic[name].raw_names)}"
        for name in sorted(classic_names - turtle_names)
    ]
    turtle_only = [
        f"{name}: {format_hex(turtle[name].raw_value)} names={list(turtle[name].raw_names)}"
        for name in sorted(turtle_names - classic_names)
    ]

    limit = None if args.limit < 0 else args.limit

    print(f"classic canonical entries: {len(classic)}")
    print(f"turtle canonical entries: {len(turtle)}")
    print(f"shared canonical entries: {len(shared_names)}")
    print()
    emit_section("Different wire", different_wire, limit)
    print()
    emit_section("Classic only", classic_only, limit)
    print()
    emit_section("Turtle only", turtle_only, limit)
    print()
    emit_section("Same wire, name-only differences", same_wire_name_only, limit)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
