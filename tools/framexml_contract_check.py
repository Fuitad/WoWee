#!/usr/bin/env python3
"""Where a binding and the interface disagree about a call's shape.

A missing function is mostly harmless here: an undefined global answers nil, and
nil reads as false in a condition. What actually breaks the interface is a
function that *exists* and is the wrong shape, because Lua raises rather than
shrugging:

  * **too few arguments** — luaL_checknumber(L, 1) *raises* when the argument is
    absent. AbandonQuest() is called with none, so abandoning a quest from the
    quest log failed outright every time.
  * **too few return values** — the extra names are nil, which is fine until one
    is used. GetTalentInfo returned eight of ten, and the frame compares the
    ninth against maxRank: an error the moment a talent point is staged.
  * **too few event arguments** — same shape from the other direction.
    CHARACTER_POINTS_CHANGED is read as `arg2 > 0`, and was fired with nothing.

All three found real crashes on frames that are owned by default, and none of
them showed up in any count of missing names. This keeps them from coming back.

    tools/framexml_contract_check.py

Reports candidates, not faults. An extra nil only matters if it is used, and
plenty are guarded by `if (not x)` on the next line — UnitPowerType and
GetPetHappiness both are. Read the consumer before changing anything.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FRAMEXML = ROOT / "Data" / "interface" / "framexml"
ADDONS = ROOT / "Data" / "interface" / "addons"
BINDINGS = ROOT / "src" / "addons"
GAME = ROOT / "src" / "game"

CHECKS_ARG1 = re.compile(r"luaL_check(?:number|string|integer)\s*\(\s*L\s*,\s*1\s*\)")
NAMED_FN = re.compile(r"static int lua_(\w+)\s*\(lua_State\* L\)\s*\{")
LAMBDA_FN = re.compile(r'\{"(\w+)",\s*\[\]\(lua_State\* L\)\s*->\s*int\s*\{')


def luaSources():
    return sorted(FRAMEXML.glob("*.lua")) + sorted(ADDONS.glob("*/*.lua"))


def stripComments(text):
    text = re.sub(r"--\[(=*)\[.*?\]\1\]", "", text, flags=re.S)
    return re.sub(r"--[^\n]*", "", text)


def bindingBodies():
    """Each binding's name and its body, however it was registered."""
    for cpp in sorted(BINDINGS.glob("*.cpp")):
        s = cpp.read_text()
        for pattern, span, stops in ((NAMED_FN, 1200, ("\nstatic int ",)),
                                     (LAMBDA_FN, 900, ("}},",))):
            for m in pattern.finditer(s):
                body = s[m.end():m.end() + span]
                for stop in stops:
                    i = body.find(stop)
                    if i != -1:
                        body = body[:i]
                yield m.group(1), body, cpp.name


def checkArity():
    """Bindings that require argument one where the interface passes none."""
    bare = set()
    for p in luaSources():
        bare |= set(re.findall(r"(?<![:.\w])([A-Z][A-Za-z0-9_]{2,})\s*\(\s*\)",
                               stripComments(p.read_text(errors="ignore"))))
    hits = []
    for name, body, where in bindingBodies():
        if name in bare and CHECKS_ARG1.search(body):
            # A guarded check is fine: lua_isnoneornil before the check means
            # the bare call takes the other path.
            if "lua_isnoneornil" in body:
                continue
            hits.append((name, where))
    return sorted(set(hits))


def checkReturns():
    """Bindings returning fewer values than the interface unpacks."""
    want = {}
    for p in luaSources():
        t = stripComments(p.read_text(errors="ignore"))
        for m in re.finditer(r"local\s+([A-Za-z_][\w\s,]*?)\s*=\s*([A-Z]\w{2,})\s*\(", t):
            names = [x for x in m.group(1).split(",") if x.strip()]
            if len(names) > 1 and len(names) > want.get(m.group(2), (0, ""))[0]:
                want[m.group(2)] = (len(names), p.name)

    give = {}
    for name, body, _ in bindingBodies():
        if re.search(r"return\s+[a-z_]\w*\s*;", body):
            give[name] = None          # counted at runtime; nothing to compare
            continue
        rets = [int(x) for x in re.findall(r"return\s+(\d+)\s*;", body)]
        if rets and give.get(name, 0) is not None:
            give[name] = max(rets + [give.get(name) or 0])

    return sorted((n, give[n], c, w) for n, (c, w) in want.items()
                  if give.get(n) is not None and n in give and c > give[n])


def checkEventArgs():
    """Events whose handler unpacks more than is ever fired."""
    fired = {}
    for cpp in sorted(GAME.glob("*.cpp")) + sorted(BINDINGS.glob("*.cpp")):
        s = cpp.read_text()
        for m in re.finditer(
                r'(?:fireAddonEvent|addonEventCallbackRef\(\))\(\s*"(\w+)"\s*,\s*\{([^{}]*)\}', s):
            args = m.group(2).strip()
            n = 0 if not args else args.count(",") + 1
            fired[m.group(1)] = max(fired.get(m.group(1), 0), n)

    hits = {}
    for p in sorted(FRAMEXML.glob("*.lua")):
        t = p.read_text(errors="ignore")
        # Stop at the next branch: a fixed window crossed into the following
        # elseif once and blamed the wrong event for its argument.
        for m in re.finditer(r'event\s*==\s*"(\w+)"\s*\)?\s*then(.*?)(?:\n\s*(?:elseif|end)\b)',
                             t, re.S):
            name, body = m.group(1), m.group(2)
            if name not in fired or name in hits:
                continue
            u = re.search(r"local\s+([A-Za-z_][\w\s,]*?)\s*=\s*\.\.\.", body)
            if not u:
                continue
            want = len([x for x in u.group(1).split(",") if x.strip()])
            if want > fired[name]:
                hits[name] = (fired[name], want, p.name)
    return sorted((k,) + v for k, v in hits.items())


def main():
    arity = checkArity()
    returns = checkReturns()
    events = checkEventArgs()

    print("Bindings that require argument one, which the interface calls bare:")
    for name, where in arity:
        print(f"    {name:<30} {where}")
    print("    (none)" if not arity else "")

    print("\nBindings returning fewer values than the interface unpacks:")
    for name, got, want, where in returns:
        print(f"    {name:<30} returns {got}, unpacks {want}   ({where})")
    print("    (none)" if not returns else "")

    print("\nEvents whose handler unpacks more than is fired:")
    for name, got, want, where in events:
        print(f"    {name:<30} fired {got}, unpacks {want}   ({where})")
    print("    (none)" if not events else "")

    print(f"\n{len(arity)} arity, {len(returns)} return-count, {len(events)} event "
          f"candidates. Only the first is always a fault; the other two matter "
          f"when\nthe missing value is used rather than merely unpacked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
