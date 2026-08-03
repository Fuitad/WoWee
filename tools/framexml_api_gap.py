#!/usr/bin/env python3
"""How far the Lua API is from running FrameXML.

FrameXML calls a few thousand globals, but most of them are its own — it
defines them itself as it loads. Subtracting those leaves the client API it
expects to already be there, which is the real measure of what is left to do.

Counting the raw call list instead gives a number several times too large and
makes the job look hopeless, which is why this subtracts.

Usage:  tools/framexml_api_gap.py [path to Interface/FrameXML]

Reports the size of the gap and the most-used names in it, so the work can be
ordered by what FrameXML actually leans on rather than by guesswork.
"""
import sys

import re, os, collections
#!/usr/bin/env python3
"""How far the Lua API is from running FrameXML.

FrameXML calls a few thousand globals, but most of them are its own — it
defines them itself as it loads. Subtracting those leaves the client API it
expects to already be there, which is the real measure of what is left to do.

Counting the raw call list instead gives a number several times too large and
makes the job look hopeless, which is why this subtracts.

Usage:  tools/framexml_api_gap.py [path to Interface/FrameXML]

Reports the size of the gap and the most-used names in it, so the work can be
ordered by what FrameXML actually leans on rather than by guesswork.
"""
import sys


# Lower case: the tree on disk is Data/interface/framexml, and the mixed-case
# spelling this defaulted to only ever resolved on a case-insensitive
# filesystem. Everywhere else it raised FileNotFoundError before reading a line.
fx = sys.argv[1] if len(sys.argv) > 1 else "Data/interface/framexml"
addons = os.path.join(os.path.dirname(__file__), "..", "src", "addons")

eng = ""
for f in os.listdir(addons):
    if f.endswith(".cpp"): eng += open(addons+"/"+f).read()
have  = set(re.findall(r'lua_setglobal\(L_?,\s*"(\w+)"', eng))
have |= set(re.findall(r'\{"(\w+)"\s*,', eng))
have |= set(re.findall(r'"(\w+)\s*=', eng))
have |= set(re.findall(r'"function (\w+)\(', eng))

# What FrameXML itself defines — these are not gaps.
defined, calls = set(), collections.Counter()
for fn in sorted(os.listdir(fx)):
    if not fn.endswith(".lua"): continue
    src = open(os.path.join(fx,fn), errors="ignore").read()
    defined |= set(re.findall(r'^\s*function\s+([A-Za-z_][\w]*)\s*\(', src, re.M))
    defined |= set(re.findall(r'^\s*([A-Za-z_][\w]*)\s*=\s*function', src, re.M))
    for m in re.finditer(r'(?<![\w.:])([A-Z][A-Za-z0-9_]{2,})\s*\(', src):
        calls[m.group(1)] += 1

missing = [(n,c) for n,c in calls.most_common() if n not in have and n not in defined]
print(f"called by FrameXML      : {len(calls)}")
print(f"defined by FrameXML     : {len(defined & set(calls))}")
print(f"already provided        : {len(set(calls) & have)}")
print(f"genuinely missing API   : {len(missing)}")
print(f"  calls they account for: {sum(c for _,c in missing)}")
print()
for n,c in missing[:20]: print(f"  {c:5d}  {n}")
