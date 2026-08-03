#!/usr/bin/env python3
"""A bootstrap chunk that uses a local it never declared.

The Lua the engine installs at startup goes in as a series of separate chunks.
They share globals and nothing else, so each chunk that wants the frame
metatable opens by binding it to a local of its own. Writing a method into a
chunk that never declared that local indexes a nil global instead: the chunk
fails to load, everything defined in it is lost, and the only sign is one line
in the log.

That cost a session. Two tooltip methods were added to the wrong chunk and
disappeared; FrameXML calls one of them from CursorOnUpdate, which runs every
frame, so the error repeated until the render thread fell behind and the
device was lost. The build was green throughout, and so were the tests — the
failure is in generated Lua that neither of them runs.

    tools/bootstrap_chunk_check.py

Exits non-zero when a chunk is broken, because unlike the shadowing report
this one has no false positives to weigh: a chunk in this state never loads.
"""

import re
import sys
from pathlib import Path

ENGINE = Path(__file__).resolve().parent.parent / "src" / "addons" / "lua_engine.cpp"

# Locals a chunk binds for itself. Anything here used without a matching
# declaration in the same chunk is a nil global.
CHUNK_LOCALS = ("mt", "methods")

QUOTE = chr(34)
STRING_LITERAL = re.compile(QUOTE + r"((?:[^" + QUOTE + r"\\]|\\.)*)" + QUOTE)


def chunk_sources(text):
    """The Lua source of each bootstrap(...) call, in order."""
    out = []
    for piece in text.split("bootstrap(")[1:]:
        body = piece.split(");", 1)[0]
        lua = "".join(STRING_LITERAL.findall(body))
        # The literals carry escaped newlines; real ones make the excerpts read.
        out.append(lua.replace("\\n", "\n"))
    return out


def main():
    if not ENGINE.is_file():
        print(f"no engine source at {ENGINE}")
        return 0

    text = ENGINE.read_text(errors="ignore")
    broken = 0

    for index, lua in enumerate(chunk_sources(text)):
        for local in CHUNK_LOCALS:
            used = re.search(r"(?<![\w.])" + local + r"[:.\[]", lua)
            if not used:
                continue
            if re.search(r"local\s+" + local + r"\s*=", lua):
                continue
            broken += 1
            near = " ".join(lua[max(0, used.start() - 50):used.start() + 50].split())
            print(f"bootstrap chunk {index} uses '{local}' and never declares it."
                  f"\n  The chunk will fail to load and everything in it is lost."
                  f"\n  near: ...{near}...\n")

    if broken:
        print(f"{broken} chunk(s) that will not load. Bind the local at the top of "
              f"the chunk, or name the global outright.")
        return 1

    print(f"{len(chunk_sources(text))} bootstrap chunks; every local they use is "
          f"declared in the chunk that uses it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
