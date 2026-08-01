# The widget system

The addon API used to answer without doing anything. `CreateFrame` returned a
table, events dispatched to it, and `CreateTexture` handed back an object whose
every method was a no-op — so an addon could be written, loaded and run without
putting a pixel on the screen.

There is now a real retained widget tree behind it. The same tree is what
FrameXML targets, because FrameXML is only Lua and XML over a widget system, so
building it once serves both goals: addons that draw, and a route to running the
original interface rather than imitating it.

## Shape

| Piece | Where | Notes |
|---|---|---|
| Widget tree, anchors, draw order, hit testing | `src/ui/widget_tree.cpp` | No Vulkan or ImGui, so the layout rules are testable without a device |
| Drawing, texture cache, backdrops, status bars | `src/ui/widget_renderer.cpp` | Reads `Interface\` art through the existing asset path |
| XML reader | `src/ui/xml_parser.cpp` | Enough for FrameXML: CDATA, comments, both quote styles |
| XML to Lua | `src/ui/framexml_emitter.cpp` | Emits the calls a script would make |
| Lua bindings | `src/addons/lua_engine.cpp` | Frames and regions are Lua tables carrying a `__wid` handle |

Coordinates follow WoW throughout — origin bottom-left, y upward — and flip once
at the point of drawing, so every anchor rule reads the way Blizzard documents
it rather than mirrored.

Anchors are constraints, not positions. An anchor says "this fraction of my rect
sits at that point", so one anchor plus a size places a frame and two opposing
anchors give the size as well. That is what `SetAllPoints` relies on, and how
most of FrameXML sizes its backgrounds without ever stating a size.

## Why XML becomes Lua

The alternative was to build widgets from C++ while walking the XML, which would
have meant a second implementation of everything `CreateFrame` already does —
parenting, naming, templates, script binding — kept in step with the first by
hand. Emitting Lua means XML frames and hand-written frames travel one path, a
template declared in XML is usable from a script without translation, and the
emitter's output is a string a test can read without a Lua state.

## Environment switches

Both are off by default. Both exist because the work they enable is not finished.

### `WOWEE_LUA_API_FALLBACK=1`

Unknown globals answer with a no-op instead of erroring, and every name asked
for is logged once and listed at shutdown.

This is how a large body of Lua gets brought up: rather than guessing which of
the missing functions matter, run it and collect the ones it actually reaches.

It has a real cost. Code that checks whether a function exists before using it —
which addons do constantly — sees everything as present and takes branches meant
for a different client. Names in `SCREAMING_SNAKE_CASE` are treated as constants
and still come back nil, because handing a function to something expecting a
number turns a missing value into a confusing type error further away.

### `WOWEE_LOAD_FRAMEXML=1`

Loads the original interface from `Interface/FrameXML/FrameXML.toc`, in the
order that manifest states, before any addon. It turns the fallback above on by
itself, because FrameXML cannot get through its own load without one.

Every file that fails is listed together at the end of the load, with the reason
carried up from whichever include or referenced script actually broke. Expect
failures for now; what remains is measured rather than guessed:

    tools/framexml_api_gap.py <path to Interface/FrameXML>

At the time of writing that reports 1,191 missing functions across 2,442 call
sites — out of 4,217 globals FrameXML calls, of which it defines 2,724 itself as
it loads. The tail is very flat: the most-used missing name has 47 uses and the
rest drop to about two each, so this is a long list of functions that mostly
need to exist and return something sane, not a wall of hard work.

That ranking counts every call site, but only calls at file scope can stop a
file loading — a missing name inside a handler costs nothing until the handler
runs. The per-file reasons above are what identify those, and are the list worth
working from.

Neither the XML reader nor the emitter is the constraint. All 140 XML files
parse and every one's generated Lua compiles (`tools/framexml_compile_check.cpp`),
so what fails, fails at run time.

## Known gaps

- Fonts are sized but not loaded. `FRIZQT__.TTF` and its siblings are in the
  game data, but using them needs a font atlas rebuild, which cannot happen
  while a frame is being built.
- `EditBox`, `Slider`, `ScrollFrame` and `Cooldown` are created as plain frames.
  They exist and lay out; they do not yet behave.
- Only the left mouse button reaches frames.
- The texture cache never evicts. `Interface\` art is small and reused, but a
  long session with many addons would grow it without bound.
