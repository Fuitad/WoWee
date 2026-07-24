# Changelog

## [v2.0.29-preview] — 2026-07-23

### World
- **Game objects went missing after leaving an area and coming back, for the rest of the session.** Walking away dropped every instance of a mailbox or chest model, and the 60-second unused-model reaper then evicted the model itself; the reload on return did not reliably produce a drawn object. Game object models are now pinned in the renderer, so the reap/reload cycle never happens for them. They are a small bounded set — one per display ID actually encountered — and ambient doodads are still reaped normally
- Game objects are exempt from the adaptive doodad render distance. That distance collapses to its densest-scene value in any populated area, which in a city means roughly 200 units, so mailboxes and chests vanished well inside the range the server still considered them visible. They now hold a 600-unit floor; frustum and occlusion culling are unaffected
- GPU cull results are matched back to instances by ID rather than array index. The visibility buffer is read a full frame-slot cycle after it is written, and instances are appended and swap-removed in between, so a respawned object landing at the volatile tail of the array could inherit the verdict of whatever transient object held that slot two frames earlier
- Game objects no longer take the HiZ occlusion test at all. A mailbox or chest sits flush against a wall or doorframe, exactly where the coarse depth pyramid reports a false occlusion; once culled it stops being drawn, so it never regains the last-frame depth that would clear the false verdict, and it stayed invisible in place until the camera moved. These small gameplay props now opt out of occlusion culling entirely (frustum and distance culling still bound them), so they cannot vanish while in view

### Lighting
- **Hearth fires, campfires and forges cast light.** Fires express their flame as particle emitters rather than a glow card, and the path that turns emitters into a light was gated on lantern-like models — so a fireplace full of burning wood lit nothing at all. Open flame now takes that path, with a wider, warmer light than a candle wick, and forges are classified as the contained fires they are
- **Forges rendered as black windows.** `BLACKSMITHFORGE.m2` is nothing but the fire in the hearth, an effect card on a black background, but the additive override that handles exactly that shape was gated on spell effects, so it drew opaque and filled the opening with a black rectangle. Its black backing is colour-keyed too: the texture is `ARMORREFLECT`, whose name carries none of the flame or glow tokens the colour-key hint looks for
- Lamps, torches and braziers gutter. Each fixture's phase is hashed from its own placement, so no two pulse together — a synchronised row of street lamps reads as a rendering artifact rather than firelight — and the guttering drives the light each one casts, not just its glow sprite, since the pool of light on the ground is what the eye actually reads as fire
- Darkshire's town hall clock face is lit by a fire behind it. WMO emissive was a flag tuned for Stormwind's lamp glass, far too bright for a clock face, and is now a level: the new one keeps normal daylight shading, adds a warm glow that fades up as the scene darkens, and wavers on three detuned sines so the flame never visibly loops. A tight sun highlight and a Fresnel sheen sell the pane of glass over the dial
- Hearth fire light no longer turns the surrounding brickwork orange. Local lights are unshadowed, so their radius is how far the glow reaches straight through whatever surrounds the fire; at 11 units a hearth lit its entire chimney from the inside out

### UI
- **"Your auction of … has sold!" named the wrong item ("Item #0").** The owner-notification packet was parsed with a phantom `action` field, which pushed the item-entry read to an offset holding a zero padding word; the real `item_template` sits at offset 20. It now parses correctly (and always reads as "sold", since expiry and outbids arrive on their own opcodes)
- **Readable letter/note items resolve their `$`-tokens.** The item-text window drew the body raw, so a quest letter showed literal markup like "$g himself : herself;". It now runs the same placeholder replacer as quest and chat text, filling in gender ($g), player name ($n), line breaks ($b), and the rest
- **The achievements window shows each achievement's real icon** instead of a gold star. Achievement.dbc's IconID (added to the DBC layout) resolves through SpellIcon.dbc to the artwork, rendered as a bordered 32px icon with the name and point value beside it; a star placeholder still fills the slot while an icon streams in or if it's missing
- **Raid target markers never appeared on marked enemies.** Three separate faults stacked: `GameHandler` kept its own copy of the marks that nothing ever wrote, so the target frame, nameplates, minimap and party list all read zeros; the wire format was inverted, with the full list (which carries only the icons that are set, not a fixed eight) and the single-mark form (which leads with the setter's GUID on WotLK but not on classic or TBC) swapped; and the marks were drawn as text symbols the font has no code points for, so they rendered as '?' boxes. Marks now use Blizzard's icon artwork, floating above the unit as in the original client, and are covered by tests across both wire layouts
- Solo players can set target markers. The server only broadcasts them to a group, so marking while ungrouped did nothing and the feature could not be used, or tested, without a second player. Grouped marking stays server-authoritative
- The DPS meter sits under the target frame instead of near the bottom of the screen, and can be dragged; the position persists, and right-click returns it home
- **The bank window lists the cost of each unpurchased bag slot.** The "Buy Slot" button gave no price and let you click slots out of order; it now shows the gold cost per slot from `BankBagSlotPrices.dbc`, and only the next slot in sequence is buyable — later ones are locked with their price shown. Buying now goes through an "are you sure?" confirmation that names the cost, rather than spending gold on a single click
- The bank has a Sort button that arranges the main bank and every bank bag by quality, then item ID, then stack size — the same ordering as the backpack Sort, driven client-side one swap per frame
- The bank can show every slot as one continuous grid via a "Combine bags" toggle, instead of splitting each bank bag into its own labeled section
- **Spell descriptions resolve their `$`-tokens everywhere, not just in the talent panel.** Buff/aura tooltips, the spellbook, and item "Use/Equip" effect lines showed raw markup like "Stamina and Spirit increased by $s1" or "Restores $/5;s1 health per second". Token substitution now lives on a shared `GameHandler::formatSpellDescription` used by all of them, and additionally handles the `$/N;` division token (food regen) alongside the base-point, duration, cross-spell, and plural/gender forms
- **Spell tooltips now show the full effect Description instead of the terse one-liner.** The client read Spell.dbc's short `Tooltip` string, so a food item said only "Restores 3 health per second" and never mentioned the Well Fed buff it grants. Descriptions now come from the `Description` column (falling back to `Tooltip`), so food reads "Restores 17 health over 18 sec … become well fed and gain 2 Stamina and Spirit for 15 min."
- **Talent tooltips describe what the talent actually does.** The talent panel read Spell.dbc's `Tooltip` column, whose index in the layouts pointed at an empty locale slot, so hovering a talent showed only its name and rank. Tooltips now pull the spell `Description` (added to every expansion's DBC layout at its verified column) and resolve WoW's `$`-token grammar against live spell data — `$s/$o/$m/$M` base points and `$d` durations, including cross-spell `$<spellId>` references like `$14201d`, plus `$l`/`$g` plural and gender forms — so "receive a $14201s1% damage bonus for $14201d" reads "receive a 4% damage bonus for 12 sec". Tokens with no local source (`$h` proc chance, `$t` period) are stripped cleanly rather than shown raw. An unlearned talent shows its rank-1 effect under "Effect:"
- **Spell.dbc `Tooltip` (and TBC `Rank`) columns pointed at the wrong offset.** The WotLK layout's `Tooltip` and the TBC layout's `Rank`/`Tooltip` indices assumed the wrong locale-block stride, landing on empty columns; they now point at the verified enUS strings, so spell rank text and short tooltips resolve on those clients too
- **The auction "Create Auction" item picker lists everything you can sell.** It only walked the 16-slot base backpack, so items in equipped bags never appeared; it now enumerates the backpack and every equipped bag, and posts the chosen item by its server GUID so any container works
- **New mail announces itself.** Receiving mail while online — or logging in with mail already waiting — now prints "You have new mail." to chat and plays a notification cue, on top of the existing minimap indicator; it fires once per unread state rather than repeating while the mail sits unread
- The keyring is interactive and always visible when enabled
- AddOns can be enabled and disabled from a manager on character select, and unimplemented addon widget methods fall back to no-ops rather than erroring
- The target frame no longer stays stuck at a previous target's width

### Quests
- Quest giver markers refresh as soon as an objective completes, rather than only after leaving and re-entering the area. Sweeps are coalesced behind a one-second cooldown, since one request fans out to a packet per nearby giver and completion events arrive in bursts
- Key-locked chests open by using the key item on them; unlocked and quest-gated chests open instead of being refused

### Audio
- Capital City Bells volume is independent of the ambient slider

### Character
- Barber shop appearance changes apply without a restart

---

## [v2.0.7-preview] — 2026-07-12

### Camera
- **Hills no longer clip through the third-person camera.** Terrain was only ever a floor clamp at the camera's final position, so a rise *between* the character and the camera sliced straight through the view and the clamp just popped the camera upward after the fact. The camera ray now marches the terrain heightfield (coarse ~1.25-unit steps, then bisection for a tight limit) and the resulting distance feeds the same asymmetric pull-in/recover smoothing as the WMO wall raycast. Worst case is ~28 bilinear height lookups a frame — negligible. Pull-in snaps 1:1 while the mouse or turn keys are actively rotating, since the 60 ms ease was exactly the window where a fast swing into a hillside still dipped underground
- The terrain march is skipped inside interior WMOs (terrain above a tunnel is not a real occluder) and when the pivot itself sits below the heightfield (caves, WMO basements, ADT holes), so it cannot pin the camera to first-person where the heightfield is irrelevant
- X now dives while swimming instead of toggling sit, water-exit assists are suppressed while diving, and the swim-depth gate only applies on water entry — deliberate dives can go arbitrarily deep

### UI
- Crafting panel reagent lines show live have/need counts, recounted every frame so consumption is visible mid-craft; Create/Create All disable when any reagent is short

---

## [v2.0.6-preview] — 2026-07-12

### Networking
- **Stop dropping every packet that has no payload.** `handlePacket()` ignored any packet whose body was empty, but `getSize()` is the payload length and the opcode is carried separately — for the many opcodes with no payload, the opcode *is* the message. All of them were swallowed before dispatch, which is also why no "unhandled opcode" warning ever fired for them. Eight registered opcodes were affected:
  - `SMSG_LOGOUT_COMPLETE` — the server logged the character out and moved on while the client waited forever, so the logout countdown ended and nothing happened
  - `SMSG_LOGOUT_CANCEL_ACK` — a cancelled logout was never confirmed
  - `SMSG_ATTACKSWING_NOTINRANGE` / `_BADFACING` / `_DEADTARGET` / `_CANT_ATTACK` — none of the four auto-attack errors ever reached the player
  - `SMSG_PET_BROKEN`, `SMSG_INVALIDATE_PLAYER`

### Logout
- `/quit` and `/exit` now leave the game when the server confirms the logout; `/logout` and `/camp` return to character select. They were all aliases of one command that did neither
- `/logout` was not a command at all: `aliases()` is the complete name list and it only listed camp, quit and exit, so `/help` had been advertising a command that silently did nothing
- The logout pose is a sit again. The server stuns the player to root them for the countdown, and we mapped `UNIT_FLAG_STUNNED` straight to the stun animation, which played over the sit and left the character slumped

### UI
- The breath, fatigue and feign-death bars count down. `SMSG_START_MIRROR_TIMER` hands the client a remaining time and a scale and the server then only re-syncs on change, but nothing ticked the value — so the breath bar sat frozen while you drowned. The sub-millisecond remainder is carried, so the timer does not run slow at high frame rates
- Fix the tail of `SMSG_START_MIRROR_TIMER`: it is paused(1) + spellId(4), not the reverse

---

## [v2.0.5-preview] — 2026-07-12

### Build
- **The build was shipping shaders compiled on 4 April.** `compile_shaders()` wrote its SPIR-V into the runtime tree, and the POST_BUILD step then copied the whole source `assets/` directory over it — including the `.spv` files tracked in git. The stale committed shaders won every build, so three months of GLSL edits never reached the GPU. Seven shaders were affected: `character.frag`, `character.vert`, `terrain.frag`, `water.frag`, `m2_particle.frag`, and both FSR2 compute shaders. Shaders now compile in place next to the GLSL, so the tracked `.spv` and the shader the GPU runs cannot diverge

### Character Select
- Preview is larger (panel widened, render target raised to 640x800), shows the character's equipped weapons, and stands them in their racial glue scene — Stormwind for humans, Durotar for orcs, and so on
- Scene backdrops are placed from the camera and attachment point the M2 carries (M2Loader now parses cameras); their geometry sits hundreds of units from the model origin, so nothing else can position them
- Weapons and enchant visuals no longer leak between characters: weapon attachment ran past an early return for characters whose body skin could not be composited, and fixed model ids meant every character after the first was handed the first one's weapon model

### Rendering
- Backdrops are no longer erased by the character alpha heuristics. Stormwind's walls are DXT5 with an unused alpha channel (mean alpha 17/255, every texel below the cutoff), and inferring a cutout from "the texture has alpha" discarded the whole building, leaving the sky showing through it

### Item Enhancements
- Temporary weapon enchants show as the weapon's icon with its remaining time, in the right slot. SMSG_ITEM_ENCHANT_TIME_UPDATE carries the item's *enchantment* slot (TEMP_ENCHANTMENT_SLOT = 1), not the equipment slot, so every temporary enchant was labelled "Off Hand" — even on a two-hander

### Merged
- Extract `buildFactionHostilityMap()` into a shared free function (#95)

---

## [v2.0.4-preview] — 2026-07-12

### UI
- Show the build version and date bottom-left on the login screen and right-aligned in the settings window
- `core/version.hpp` is generated from `git describe --tags --abbrev=0` by `cmake/GitVersion.cmake`, so the client always reports the last tagged release. It regenerates on every build rather than only when cmake reconfigures, and rewrites the header only when the version actually changed
- The build stamp is a date, not a timestamp: a clock time would change the header every build and force a full recompile of everything including it

### Build
- Un-ignore `cmake/*.cmake`. The repo's blanket `*.cmake` rule targets CMake build output and would have silently excluded the new hand-written module from the tree, breaking a fresh clone

---

## [v2.0.3-preview] — 2026-07-12

### Item Enhancements (sharpening stones, weightstones, weapon oils)
- Send TARGET_FLAG_ITEM in CMSG_USE_ITEM. Item-enhancement consumables cast their spell onto another item, but the client only ever wrote a unit or self target, so the server dropped the cast and the item did nothing
- Using such an item now reads the on-use spell's Spell.dbc Targets mask and arms an item-targeting cursor; the next item clicked (in bags or equipped) receives the enchant. Escape or right-click cancels
- Add the Spell.dbc `Targets` column to all four expansion layouts (Classic 13, TBC 14, WotLK 16)
- Weapon enchant visuals: resolve SpellItemEnchantment → ItemVisuals → ItemVisualEffects and attach the effect M2 (e.g. the sharpening-stone glint) to the weapon model's item-visual attachment points, rendered additive and unlit
- Applying an enchant now marks equipment dirty even though the displayInfoId is unchanged, so the visual appears without re-equipping

### Bug Fixes
- Read enchant names from the correct SpellItemEnchantment.dbc column. The name moved across expansions (Vanilla 10, TBC 13, WotLK 14) but every caller used field 8, an integer column that getString() treated as a string-block offset — so names came back garbled mid-string ("Sharpened (+2 Damage)" surfaced as "ockbiter 3"). Resolved from the record width via `detectEnchantmentNameField()`

### Tests
- New `test_use_item_packet` suite: CMSG_USE_ITEM SpellCastTargets encoding for WotLK, Classic and TBC (item, unit, and self targeting)
- DBC tests for enchant name/ItemVisual column detection and the enchant → effect-model resolution chain

---

## [v1.9.7-preview] — 2026-07-09

### Bug Fixes
- Fix world login pipeline: login-critical opcodes (AUTH_CHALLENGE, AUTH_RESPONSE, CHAR_ENUM, CHAR_CREATE, CHAR_DELETE, WARDEN_DATA) now fall back to hardcoded wire values when opcode table lookup fails, preventing "Unhandled world opcode: 0x1ec" blocking character list retrieval (issue #87)
- OpcodeTable::loadFromJson() now loads into temporaries and only swaps on success — a failed reload no longer wipes the working table
- Integrity hash is now build-aware: Classic-era DLLs (fmod.dll, ijl15.dll, dbghelp.dll, unicows.dll) only required for builds <=6005 or Turtle; TBC/WotLK clients hash only the .exe

### Animation & Camera
- Rework strafing to use walk/run animations with SpineLow bone torso twist instead of dedicated strafe/run-left/right animations
- Add `setInstanceTorsoYaw()` to CharacterRenderer for per-instance upper-body rotation
- Camera smoothing snaps 1:1 while actively dragging or keyboard turning instead of always lerping, reducing perceived input lag
- Add `travelYaw_` tracker to CameraController for movement vector heading separate from camera facing
- Mount strafing uses MOUNT_RUN_LEFT/RIGHT animations when available
- Default mouse invert changed to off

### Tests
- Add "OpcodeTable failed reload preserves existing data" test case

---

## [v1.9.1-preview] — released, captures changes since v1.8.9-preview

### Architecture
- Break Application::getInstance() singleton from GameHandler via GameServices struct
- EntityController refactoring (SOLID decomposition)
- Extract 8 domain handler classes from GameHandler
- Replace 3,300-line switch with dispatch table
- Multi-platform Docker build system (Linux, macOS arm64/x86_64, Windows cross-compilation)
- Decompose ChatPanel monolith into 15+ modules under `src/ui/chat/` with IChatCommand interface, ChatCommandRegistry, MacroEvaluator, ChatMarkupParser/Renderer, ChatBubbleManager, ChatTabManager, GameStateAdapter, and 11 command modules (PR #62)
- Decompose WorldMap (1,360 LOC) into 16 modules under `src/rendering/world_map/` with WorldMapFacade (PIMPL), CompositeRenderer, DataRepository, CoordinateProjection, ViewStateMachine, 9 overlay layers (PR #61)
- Extract reusable CatmullRomSpline module to `src/math/` with O(log n) binary search and fused position+tangent evaluation (PR #60)
- Decompose TransportManager (`transport_manager.cpp` 1,200→~370 LOC): extract TransportPathRepository, TransportClockSync, TransportAnimator; consolidate 7 duplicated spline parsers into `spline_packet.cpp` (PR #60)

### World Editor (tools/editor/)
- Standalone world editor for creating custom WoW zones (~130k LOC across ~500 source files in `tools/editor/`, including procedural mesh/texture generators)
- 6 editing modes: Sculpt, Paint, Objects, Water, NPCs, Quests
- 30+ terrain tools: procedural generators (hill, mesa, crater, canyon, island, ridge, dunes), thermal erosion, noise, mirror/rotate, stamp copy/paste with file persistence
- Multi-select objects (Ctrl+Shift+Click), Select All (Ctrl+A), Select by Type (M2/WMO)
- Time-of-day lighting with dawn/dusk/night transitions and color pickers
- Texture eyedropper (Alt+Click), brush size presets + bracket keys
- Object tools: snap to ground, align to slope, flatten terrain around buildings, scatter with auto-align
- River/road path tool with click-to-set points and translucent preview ribbon
- Quest chains with circular reference detection, inline editing, load/save
- 631 creature presets across 8 categories with patrol path editing
- Full undo/redo for ALL terrain operations (generators, transforms, paint)
- Auto-save with configurable interval, unsaved changes quit confirmation
- Zone rename, recent zones menu, adjacent tile export with edge stitching
- Zone metadata panel: configurable Map ID, Display Name, Description
- Zone gameplay flags: Allow Flying, PvP, Indoor, Sanctuary (serialized to zone.json)
- Zone audio configuration: music track, day/night ambience, volume sliders, presets
- PNG/JPG/BMP/TGA heightmap image import (any resolution, 8/16-bit, undoable)
- Collision slope overlay on minimap (steep terrain visualization)
- Client-side WOC collision loading with walkability queries
- Zone map image export: colored top-down PNG with terrain, water, objects
- SQL spawn export for AzerothCore/TrinityCore (creature_template, creature,
  waypoint_data, quest_template — ready-to-import .sql files)
- Server module generator: one-click AzerothCore module with map registration,
  spawns, teleport command, zone flags, conf snippet, and admin README
- Biome vegetation auto-population: one-click procedural placement of
  trees, rocks, bushes, ferns per biome (10 biomes with density rules)
- Live open format validation (0-7 score) in File menu

### Novel Open Formats (7/7 Blizzard format replacements)
- ADT → WOT/WHM: terrain metadata + binary heightmap with alpha maps and doodad/WMO placements
- WDT → zone.json: map definition with full placement arrays
- BLP → PNG: texture override system
- DBC → JSON: data tables via DBCFile::loadJSON()
- M2 → WOM (WOM1/WOM2): static models + animated models with bones, keyframes, skeletal binding
- WMO → WOB (WOB1): buildings with material flags/shader/blendMode, doodad rotation
- Collision → WOC (WOC1): walkability mesh with slope classification, hole support, water flags
- WCP (WCP1): content pack archive with categorized file list
- Terrain stamps: portable terrain features saved as JSON
- All formats documented in FORMAT_SPEC.md v1.1
- Client auto-loads open formats from custom_zones/ and output/ directories
- Batch convert: M2→WOM and WMO→WOB from filesystem or asset manifest
- WCP Import & Load: one-click unpack + auto-open for editing
- 328 test assertions across 84 test cases (DBC binary+JSON, WOB, WHM, WOT, WOC)

### Features
- Spell visual effects system with bone-tracked ribbons and particles (PR #58)
- GM command support: 190-command data table with dot-prefix interception, tab-completion, `/gmhelp` with category filter (PR #62)
- ZMP pixel-accurate zone hover detection on world map (PR #63)
- Textured player arrow (MinimapArrow.blp) on world map (PR #63)
- Multi-segment path interpolation for entity movement (PR #59)
- Character screen keyboard navigation (Up/Down/Enter) (PR #59)

### Bug Fixes (v1.8.10+)
- Fix walk/run animation persisting after entity arrival (PR #59)
- Fix entity teleport during dead-reckoning overrun phase (PR #59)
- Fix Vulkan crash on window resize when minimized (0×0 extent) (PR #59)
- Fix quest log not populating on quest accept (PR #59)
- Fix hit-reaction animation being overridden on next frame (PR #59)
- Fix ChatType enum values to match WoW wire protocol (SAY=0x01 not 0x00) (PR #62)
- Fix BG_SYSTEM_* values from 82–84 (UB in bitmask shifts) to 0x24–0x26 (PR #62)
- Fix infinite Enter key loop after teleport (PR #62)
- Remove stale kVOffset (-0.15) from zone hover detection causing ~15% vertical offset
- Add null guard for cachedGameHandler_ in ChatPanel input callback
- Fix cosmic highlight aspect ratio with resolution-independent square rendering
- Skip transport waypoints with broken coordinate conversion instead of silent use
- Fix spline endpoint validation bypass for entities near world origin
- Fix off-by-one in chat link insertion buffer capacity check
- Zero window border in world map to eliminate content/window gap

### Tests
- Add 19 new test files (27 total, up from 8):
  - Chat: chat_markup_parser, chat_tab_completer, gm_commands, macro_evaluator
  - World map: world_map, coordinate_projection, exploration_state, map_resolver, view_state_machine, zone_metadata
  - Transport/spline: spline, transport_components, transport_path_repo
  - Animation: animation_ids, locomotion_fsm, combat_fsm, activity_fsm, anim_capability, indoor_shadows

### Bug Fixes (v1.8.2–v1.8.9)
- Fix VkTexture ownsSampler_ flag after move/destroy (prevented double-free)
- Fix unsigned underflow in Warden PE section loading (buffer overflow on malformed modules)
- Add bounds checks to Warden readLE32/readLE16 (out-of-bounds on untrusted PE data)
- Fix undefined behavior: SDL_BUTTON(0) computed 1 << -1 (negative shift)
- Fix BigNum::toHex/toDecimal null dereference on OpenSSL allocation failure
- Remove duplicate zone weather entry silently overwriting Dustwallow Marsh
- Fix LLVM apt repo codename (jammy→noble) in macOS Docker build
- Add missing mkdir in Linux Docker build script
- Clamp player percentage stats (block/dodge/parry/crit) to prevent NaN from corrupted packets
- Guard fsPath underflow in tryLoadPngOverride

### Code Quality (v1.8.2–v1.8.9)
- 30+ named constants replacing magic numbers across game, rendering, and pipeline code
- 55+ why-comments documenting WoW protocol quirks, format specifics, and design rationale
- 8 DRY extractions (findOnUseSpellId, createFallbackTextures, finalizeSampler,
  renderClassRestriction/renderRaceRestriction, and more)
- Scope macOS -undefined dynamic_lookup linker flag to wowee target only
- Replace goto patterns with structured control flow (do/while(false), lambdas)
- Zero out GameServices in Application::shutdown to prevent dangling pointers

---

## [v1.8.1-preview] — 2026-03-23

### Performance
- Eliminate ~70 unnecessary sqrt ops per frame; constexpr reciprocals and cache optimizations
- Skip bone animation for LOD3 models; frustum-cull water surfaces
- Eliminate per-frame heap allocations in M2 renderer
- Convert entity/skill/DBC/warden maps to unordered_map; fix 3x contacts scan
- Eliminate double map lookups and dynamic_cast in render loops
- Use second GPU queue for parallel texture/buffer uploads
- Time-budget tile finalization to prevent 1+ second main-loop stalls
- Add Vulkan pipeline cache persistence for faster startup

### Bug Fixes
- Fix spline parsing with expansion context; preload DBC caches at world entry
- Fix NPC/player attack animation to use weapon-appropriate anim ID
- Fix equipment visibility and follow-target run speed
- Fix inspect (packed GUID) and client-side auto-walk for follow
- Fix mail money uint64, other-player cape textures, zone toast dedup, TCP_NODELAY
- Guard spline point loop against unsigned underflow; guard hexDecode/stoi/stof
- Fix infinite recursion in toLowerInPlace and operator precedence bugs
- Fix 3D audio coords for PLAY_OBJECT_SOUND; correct melee swing sound paths
- Prevent Vulkan sampler exhaustion crash; skip pipeline cache on NVIDIA
- Skip FSR3 frame gen on non-AMD GPUs to prevent driver crash
- Fix chest GO interaction (send GAMEOBJ_USE+LOOT together)
- Restore WMO wall collision threshold; fix off-screen bag positions
- Guard texture log dedup sets with mutex for thread safety
- Fix lua_pcall return check in ACTIONBAR_PAGE_CHANGED

### Features
- Render equipment on other players (helmets, weapons, belts, wrists, shoulders)
- Target frame right-click context menu
- Crafting sounds and Create All button
- Server-synced bag sort
- Log GPU vendor/name at init

### Security
- Add path traversal rejection and packet length validation

### Code Quality
- Packet API: add readPackedGuid, writePackedGuid, writeFloat, getRemainingSize,
  hasRemaining, hasData, skipAll (replacing 1300+ verbose expressions)
- GameHandler helpers: isInWorld, isPreWotlk, guidToUnitId, lookupName,
  getUnitByGuid, fireAddonEvent, withSoundManager
- Dispatch table: registerHandler, registerSkipHandler, registerWorldHandler,
  registerErrorHandler (replacing 120+ lambda wrappers)
- Shared ui_colors.hpp with named constants replacing 200+ inline color literals
- Promote 50+ static const arrays to constexpr across audio/core/rendering/UI
- Deduplicate class name/color functions, enchantment cache, item-set DBC keys
- Extract settings tabs, GameHandler::update() phases, loadWeaponM2 into methods
- Remove 12 duplicate dispatch registrations and C-style casts
- Extract toHexString, toLowerInPlace, duration formatting, Lua return helpers
