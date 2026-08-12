#!/usr/bin/env python3
"""Reading FrameXML's text without being fooled by it.

Twelve sweeps had their own copy of "strip the comments", written slightly
differently each time, and only one of them had learned to strip string
literals as well. That is this repository's dominant bug shape applied to its
own tools: one rule in twelve places, eleven of them a version behind.

TWO RULES, AND WHICH TO ASK FOR

`without_comments` is for a sweep that reads *names out of strings* - the
event-arity family matches `event == "SOMETHING"`, the CVar label check reads
the literal a branch compares against. Those need the strings intact.

`without_comments_or_strings` is for a sweep that reads *syntax* - anything
looking for `Foo(` and calling it a call. A Lua pattern is a string full of
parentheses, and `strmatch(name, "DropDownList(%d+)")` reads as a call to a
global named DropDownList. That put "every dropdown in the interface raises as
it opens" at the top of a report, which is alarming and wrong.

OFFSETS ARE PRESERVED

Both blank in place rather than deleting, so every line number and index is
where it was. A report that sends you to the wrong line costs more than it
saves, and two of these sweeps did exactly that before they were caught.
"""
import pathlib
import re

_XML_COMMENT = re.compile(r"<!--.*?-->", re.S)
_LUA_COMMENT = re.compile(r"--\[\[.*?\]\]|--[^\n]*", re.S)
_STRING = re.compile(r"'[^'\n]*'|\"[^\"\n]*\"")


def _blank(match):
    """Same length, same newlines, nothing else."""
    return re.sub(r"[^\n]", " ", match.group(0))


def without_comments(text):
    """XML and Lua comments blanked; string literals left alone."""
    return _LUA_COMMENT.sub(_blank, _XML_COMMENT.sub(_blank, text))


def without_comments_or_strings(text):
    """...and the inside of every string literal blanked too.

    The quotes stay, so a token still ends where it did and an unterminated
    string cannot swallow the rest of the file.
    """
    text = without_comments(text)
    return _STRING.sub(lambda m: m.group(0)[0] + " " * (len(m.group(0)) - 2)
                       + m.group(0)[-1], text)


# ── Which files the loader actually reaches ──────────────────────────────────

_REFERENCE = re.compile(r'<(?:Script|Include)\s+file="([^"]+)"', re.I)

#: An addon LoadAddOn answers "DISABLED" for. There is a second place a file
#: can fail to load from, and it is not a manifest: lua_system_api's LoadAddOn
#: refuses Blizzard_Calendar outright, because the calendar's own globals are
#: unbound and letting the addon load turns the minimap's date button into a
#: raise. Its files sit in an addon folder with a manifest of their own, so
#: nothing about the folder says they never run.
_REFUSED = re.compile(r'strcmp\(name,\s*"(\w+)"\)\s*==\s*0(?:(?!strcmp).){0,400}?"DISABLED"',
                      re.S)


def _refused_addons(root):
    """Lowercased names of addons the client declines to load."""
    source = pathlib.Path(root) / "src/addons/lua_system_api.cpp"
    if not source.is_file():
        return set()
    return {m.lower() for m in _REFUSED.findall(source.read_text(errors="ignore"))}


def loaded_files(interface):
    """Every .lua and .xml the loader reaches, as a set of Paths.

    A file sitting in the folder is not a file that loads. Blizzard's glue
    screens ship in the same directory as FrameXML and are listed in no
    manifest here, because this client has its own login and character select
    and never loads them: accountlogin, characterselect, charactercreate,
    realmlist, realmwizard, glueparent, movieframe. Sixty of the seventy-five
    names framexml_unbound_globals called "these raise as their panel opens"
    came out of those files, which cannot raise because they never run.

    Derived from the manifests and the <Script>/<Include> graph rather than
    from a list written here - a hand-written file list is what crippled every
    per-element sweep before this, and the loader's own answer does not go
    stale when a file is added.

    Case-insensitively, because the loader resolves that way and the interface
    is not consistent: manifests carry Blizzard's capitalisation and the files
    on disk are lowercase.
    """
    interface = pathlib.Path(interface)
    out = set()
    if not interface.is_dir():
        return out

    refused = _refused_addons(interface.parent.parent)

    # Every folder with a manifest, at any depth - the bundled addons sit one
    # level further down, under addons/.
    #
    # Except gluexml, which AddonManager refuses by name: the login and
    # character-select screens are this client's own, and FrameXML's have
    # exactly one way in that this is not. Counting them put sixty names in
    # the unbound-globals report under "these raise as their panel opens",
    # and not one of them can raise, because none of those files ever run.
    directories = sorted({t.parent for t in interface.rglob("*.toc")
                          if t.parent.name.lower() != "gluexml"
                          and t.parent.name.lower() not in refused})
    # An addon may reference a shared template out of FrameXML, so that folder
    # is searched as a fallback exactly as the loader searches it.
    shared = next((d for d in directories if d.name.lower() == "framexml"), None)
    spare = ({p.name.lower(): p for p in shared.iterdir() if p.is_file()}
             if shared else {})

    for directory in directories:
        have = {p.name.lower(): p for p in directory.iterdir() if p.is_file()}

        pending = []
        for toc in directory.glob("*.toc"):
            for line in toc.read_text(errors="ignore").splitlines():
                entry = line.strip()
                if entry and not entry.startswith("#") and \
                        entry.lower().endswith((".xml", ".lua")):
                    pending.append(entry)

        seen = set()
        while pending:
            ref = pending.pop().replace("\\", "/").rsplit("/", 1)[-1].lower()
            if ref in seen:
                continue
            seen.add(ref)
            path = have.get(ref) or spare.get(ref)
            if not path:
                continue
            out.add(path)
            if path.suffix.lower() == ".xml":
                text = without_comments(path.read_text(errors="ignore"))
                pending.extend(_REFERENCE.findall(text))

    return out
