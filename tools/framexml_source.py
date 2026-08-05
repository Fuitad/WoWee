#!/usr/bin/env python3
"""Reading FrameXML's text without being fooled by it.

Twelve sweeps had their own copy of "strip the comments", written slightly
differently each time, and only one of them had learned to strip string
literals as well. That is this repository's dominant bug shape applied to its
own tools: one rule in twelve places, eleven of them a version behind.

TWO RULES, AND WHICH TO ASK FOR

`without_comments` is for a sweep that reads *names out of strings* — the
event-arity family matches `event == "SOMETHING"`, the CVar label check reads
the literal a branch compares against. Those need the strings intact.

`without_comments_or_strings` is for a sweep that reads *syntax* — anything
looking for `Foo(` and calling it a call. A Lua pattern is a string full of
parentheses, and `strmatch(name, "DropDownList(%d+)")` reads as a call to a
global named DropDownList. That put "every dropdown in the interface raises as
it opens" at the top of a report, which is alarming and wrong.

OFFSETS ARE PRESERVED

Both blank in place rather than deleting, so every line number and index is
where it was. A report that sends you to the wrong line costs more than it
saves, and two of these sweeps did exactly that before they were caught.
"""
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
