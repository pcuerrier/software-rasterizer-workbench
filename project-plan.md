# Classic JRPG — Project Plan

**Project:** Dragon Quest–style classic JRPG
**Stack:** C-style C++ (no STL/exceptions/RTTI), SDL3, custom software renderer (OpenGL swap planned), custom memory management
**Cadence:** ~1 hour/day solo development
**Canonical exceptions to no-external-code:** stb_image (PNG loading), stb_vorbis (OGG streaming)

---

## 1. Architecture Overview

Three layers:

- **Platform layer** — SDL3 init, window, input polling, audio device, file IO, timing. Thin and isolated; game code never touches SDL directly.
- **Engine layer** — reusable systems: software renderer, mode stack machine, script VM, mixer, asset loading, save serialization. No knowledge of monsters or spells.
- **Game layer** — field mode, battle mode, menus, dialogue, and all JRPG rules. Consumes data files.

**Engine/content split:** code implements systems; the actual game (maps, monsters, dialogue, tables) lives in data files.

---

## 2. Core Control Flow (settled)

### Mode stack
- Modes: title, field, dialogue, menu, battle, shop, save (~6–8 total).
- Representation: `GameMode` enum + tagged union of per-mode state in a stack array; explicit switch dispatch to `field_update()`, `battle_update()`, etc. No vtables.
- Each stack entry has a `renders_below` flag (overlay vs. replace visually). Lower modes never update. Input goes to the top mode only.
- **Communication contract:** push with a params union (e.g. `battle: encounter_id`), run in isolation, pop with a result.
  - One `ModeResult` slot owned by the stack machine (type enum + union payload, e.g. `battle_won { exp, gold }`). Written on pop, consumed by the new top on its next update, cleared after one frame.
  - Invariant: at most one pop per frame — no queue.
- Transitions (fades, battle swirl) are a property of the stack machine ("push with fade"), not modes.
- Mental model: each mode is a function call stretched over many frames.

### Script/event VM (settled)
- A single tiny bytecode VM living in **shared game state** (survives anything on the stack; serializable).
- State: `script_id, pc, status (IDLE/RUNNING/WAITING_RESULT), regs[4]` + the global flag bitset.
- The **stack machine ticks the VM**: each frame, if RUNNING, execute ops until one blocks, then update the top mode. Scripts drive the stack.
- ~20 opcodes cover the genre: SHOW_TEXT, SHOW_CHOICE, JUMP / JUMP_IF_FLAG / JUMP_IF_REG, SET_FLAG / CLEAR_FLAG, GIVE_ITEM / TAKE_ITEM / GIVE_GOLD, MOVE_NPC, WARP, START_BATTLE, PLAY_SOUND, FADE, WAIT, END.
- Flags + registers ARE the quest system. No parallel scripts; NPC idle wandering is a behavior enum, not a script.
- Scripts attach to: NPCs (`script_id`), trigger tiles, map on-enter.

---

## 3. Renderer

- **Render command buffer** (settled): game pushes abstract commands (`draw_tilemap_layer`, `draw_sprite`, `draw_text`, `draw_window`, `fill_rect`, `set_fade`); a backend consumes them. Software backend now, OpenGL later — same command stream. Game code never thinks in physical pixels.
- **Fixed low internal resolution** (e.g. 320×180 or 256×224) + integer upscale as the final blit (letterbox on non-integer multiples). Internal size is a startup variable, not a `#define`.
- **32-bit RGBA**, colorkey/1-bit-alpha sprite transparency. No palette format.
- Software work is small: opaque tile blit (hot loop), transparent sprite blit, filled rect, glyph blit from font atlas, edge clipping. Optional SIMD stretch goal on the tile blit.
- **Sub-tile scrolling:** camera in pixel coords; first tile = `cam/16`, drawn at `-(cam%16)`, draw one extra row/column.
- **Keep camera position as a float in game state from day one** (renderer may ignore the fraction).

### Smooth-scrolling / resolution exit paths
- *Tier 1 (cheap):* render low-res slightly oversized; apply fractional camera on the upscale blit (best with a GPU final blit). ~1 day.
- *Tier 2:* full native-res backend — game keeps thinking in the 320×180 space; only backend blits change. Realistic timing: do it with the OpenGL swap.

### Aspect ratios (late-project)
- **Variable internal width, fixed internal height**, clamped to a max (support ~16:10 → 21:9; letterbox beyond).
- Early habits that make it free: UI positions derived from `screen_w/screen_h` (anchoring, never hardcoded x), buffer size as a variable.
- Maps: a couple of tiles of border padding; small interiors centered or camera-clamped.
- Battle screen is a composed screen: backdrop extends outward, enemy group centered, menus edge-anchored.

---

## 4. Field System

- **Tile-to-tile movement** (settled): entity spatial state is `tile_x, tile_y, facing, move_progress`. Logic on the grid, rendering interpolates. No bounding boxes or float positions.
- **Tilemap:** tile index array + properties **per tile-type in the tileset** (blocked, water, damage, encounter zone, counter). Two visual layers: ground + over (canopies, roofs).
- **Camera:** follow player, clamp to map bounds; small interiors center-with-void.
- **NPCs:** flat per-map pool: sprite, position, facing, behavior enum (static / wander / fixed-path), `script_id`. NPCs block tiles; wander logic avoids trigger tiles. Interaction = tile-in-front lookup.
- **Triggers:** step-on, interact (facing + confirm), map-enter. Warps are a built-in step-on action (fade, load map, place, fade in); scripts only for special entrances.
- **Map lifecycle:** one map loaded at a time; load = tear down entity pool, load data, run on-enter script. Persistent world changes (chests, dead NPCs) via global flags; wander positions reset.
- **Encounters:** step counter + per-tile encounter zone; on hit, push battle with the zone's table.

---

## 5. Battle System

- **State machine:** INTRO → COMMAND_INPUT → RESOLVE (actions execute one at a time as sequenced message beats) → victory/defeat check → repeat. Terminals: VICTORY, DEFEAT (wake at church, half gold), FLED. Battle is a text-pacing engine with math attached (own small sequencer, not the event VM).
- **Uniform action model:** actor + action type + target(s) + resolution fn. Player and enemy actions share code; field and battle item/spell use share resolution.
- **Data-driven spells:** `{effect_type, power, target_mode, mp_cost, element}` with ~10 effect type enums (DAMAGE, HEAL, BUFF, STATUS, FLEE_MOD…). Hardcode only the rare weird ones.
- **Turn order:** agility + randomness, recomputed per round. Stats: HP/MP/Str/Agi/Def + level; attack/defense derived from equipment. One central damage function (DQ-ish: `(atk − def/2)/2 ± variance`).
- **Enemy AI:** weighted action list per monster + a couple of condition hooks (heal below 25%). Nothing more.
- **Status effects:** application w/ resist, per-round tick or turn-skip, expiry, battle-end persistence rule (poison persists, sleep doesn't). Keep the list to 4–6 initially — main scope lever.
- **Boundaries:** entry = encounter_id (from field or script); exit = result slot; touches party/inventory/gold/exp only. Never touches map state.

---

## 6. Progression, Inventory, Menus

- Party: fixed array of ≤4 flat character structs (stats, level, exp, equipment indices, status) — also the save payload.
- Level curves and stat growth as **data tables**, not formulas.
- Equipment: slots hold item indices; `recalc_stats()` on equip/level-up; derived stats never stored.
- **Menu system = two widgets:**
  - `menu_list`: windowed cursor list (window rect, entries, cursor, scroll, cancel) + nesting via a small list stack. Items, spells, targets, shops, save slots, battle commands are all configurations of it.
  - Message/text box with paged text + blinking prompt, shared by dialogue and battle.

---

## 7. Data Pipeline

- **Tabular data** (monsters, items, spells, encounters, curves, shops): hand-authored text files, ~200-line owned parser, **hot-reload debug key**.
- **Maps: Tiled** + a weekend importer (tile layers, per-tile properties, object layers for NPCs/triggers/warps). Own editor is a known-seductive subproject — explicit scope decision if ever. Optional later: in-game debug tile painting.
- **Scripts:** readable text DSL with labels → bytecode via a tiny Python assembler (build-time error checking; string extraction → string table = localization seam). Direct load-time parsing acceptable until scripts multiply.
- **Load-raw now, bake-ready design:** game parses source formats directly, but parses *into* the same in-memory structs a future baker would emit. No asset packer until load times demand it. Images: load PNG via stb_image.
- Directory shape: `data/tables/`, `data/maps/`, `data/scripts/`, `data/images/`, `data/audio/`.

### IDs and references (settled)
- Runtime: everything is a small integer index into flat arrays. No pointers-as-references, no runtime string lookups.
- Source files: **symbolic names** (`drops herb`, not `drops 12`).
- **Two-phase load:** (1) parse all records, assign indices, build name→index tables per record type, leave references unresolved; (2) resolution pass writes indices, reports *all* missing references with file/record context. Ordering and circularity become non-issues.
- One global name registry per record type across files. **Flags declared explicitly in `flags.txt`** — a typo'd flag is a build error, never a silent always-false.
- **Saves store names (or name-hashes), not indices** — immune to content reordering. Flags saved as set-names, not a bitset.
- Debug builds keep name tables resident: reverse lookup for logs/crashes, console commands (`give herb`, `setflag BRIDGE_FIXED`).
- Total machinery ~400–500 lines, reused by every record type.

---

## 8. Save/Load

- Versioned binary snapshot of shared game state: party, inventory, gold, flags (by name), VM state, current map + tile position.
- Does NOT save: mode stack, per-map entity positions, renderer state. Saving happens in field mode only (churches/menu) → stack is always `[field]` at save time.
- Build the load path early (debug quicksave by M5-ish) — late save systems always find state that leaked outside the serializable core.

---

## 9. Audio (settled: command-buffer architecture)

- Same shape as the renderer: game emits abstract commands (`play_sound(id)`, `play_music(id, fade)`, `stop_music(fade)`, volume) into a buffer; the mixer backend consumes them. Commands carry **symbolic asset IDs, never data pointers** — asset residency is the backend's business.
- The command buffer doubles as the **thread-boundary mechanism** to the SDL3 audio callback (mutex-guarded queue is fine at this scale).
- Unlike render commands, audio commands are **events addressing persistent sounds**: fire-and-forget for SFX; **named channels** (MUSIC, AMBIENT) for persistent things at DQ scale (generation-counted handles if it ever needs to generalize).
- **Reverse event buffer** backend→game for completion notifications (victory fanfare → return to field).
- Mixer: N channels into the output buffer. SFX = WAV→PCM. Music = streamed OGG (stb_vorbis) with **loop points (intro + loop body)** and **fade-out/fade-in** built into the music channel from the start.

---

## 10. Open Decisions

| Decision | Options | Decide by |
|---|---|---|
| Internal resolution | **384×216** (16:9) — was 320×180 vs. 256×224 vs. other | ✔ Decided — DR-33 |
| Enemy targeting scope | solo vs. **enemy groups** (likely) vs. full both-sides | Start of M6b |
| Inventory model | per-character caps vs. shared pouch (less UI) | Start of M6a |
| Map tooling | **Tiled** (default) vs. own editor | M5 |
| OpenGL swap timing | opportunistic; seams ready | Whenever (≥M8 natural) |

---

## 11. Milestones

Each milestone ends **runnable and visibly better**. Durations at ~1h/day.

### M0 — Skeleton (1–2 wks)
Platform layer: SDL3 window, input, fixed timestep, framebuffer + upscale blit. Render command buffer exists from day one (just `fill_rect`).
**Exit:** colored rectangle moves with arrow keys at internal resolution.

### M1 — World on screen (2–3 wks)
Tilemap blit with sub-tile camera offset, tileset loading, tile-locked movement + collision from tile properties, camera follow/clamp. Hand-authored text-grid map (no Tiled yet).
**Exit:** walking a character around a scrolling map.
→ *Sharpened spec below (current milestone detail).*

### M2 — A place, not a map (2–3 wks)
Second map + warps with fades, ground/over layers, NPC pool with wander, facing-based interaction. Bitmap font + message window widget (hardcoded text).
**Exit:** a town and an overworld; an NPC who says something.

### M3 — The script VM (2 wks)
~20-opcode VM in shared state, stack machine ticks it, flags registry, dialogue as a real stack mode with the result slot. Load-time script parsing (assembler later).
**Exit:** branching NPC conversation sets a flag another NPC reacts to. *The architecture proves itself here.*

### M4 — First blood (2–3 wks)
Battle mode on the stack: state machine, one enemy, Fight/Run, HP, damage formula, victory/defeat/fled via result slot, backdrop screen. Menu-list widget born here. Random encounters via step counter.
**Exit:** get jumped by a slime in the wild; kill it or die. *Playable proto-DQ — the motivational summit.*

### M5 — Data pipeline consolidation (2–3 wks)
Placed after M4 so every format has a real consumer. Tabular parser + name resolution + error reporting; monsters/items/spells/encounters as text files; Tiled importer replaces text grids; script assembler if load-parsing hurts; hot-reload key.
**Exit:** adding a monster or editing a map requires zero code changes.

### M6 — The RPG layer (3–4 wks; split 6a/6b)
**6a:** party, stats/exp/level tables, inventory (decide model first), equipment + `recalc_stats`, field menu from menu-list.
**6b:** items/spells usable in and out of battle, multiple enemies (groups decision), enemy AI weights, shops.
**Exit:** a party that levels, equips, and fights varied encounters.

### M7 — Persistence and sound (2–3 wks)
Save/load (versioned snapshot, name-based refs, church/menu save points). Audio command buffer + mixer + reverse events; SFX; streamed OGG with loop points and battle fades. Deliberately late (state stabilized, events exist) but before content — M8 finds save bugs for you.
**Exit:** quit, reload, continue; the world has sound.

### M8 — Content and polish (open-ended)
Status effects, remaining spell effect types, transitions/juice, then actual game content. OpenGL swap and tier-1 smooth-scroll experiment slot in when the itch strikes.

**Total engineering before M8: ~5–7 months at cadence.**

---

## Current milestone detail (rolling-wave) — M1: World on screen

**Status:** current milestone. *Locked decisions:* internal res 384×216 (DR-33); fixed 60Hz tick, no `dt` (DR-34); player rendered as `fill_rect` (real sprite → M2); single tile layer (ground+over → M2); placeholder tileset authored as **char-art + palette**; map compiled-in (no file I/O).

**Exit criterion:** walk a character around a scrolling map — tile-locked movement, collision from tile properties, camera follow + clamp.

### Systems touched

| System | Class | M1 role |
|---|---|---|
| Rasterizer | **Deep** (M1's core) | `fill_rect` → opaque tile blit via `draw_tilemap_layer` |
| Field (move / collide / camera) | Good-enough game logic | Correct, not gold-plated |
| Platform | Minimal | Render buffer 320×180 → 384×216 |

### Rasterizer — acceptance bar
- `draw_tilemap_layer` command drives an **opaque 16×16 tile blit** into the 384×216 buffer, with **edge clipping** and **sub-tile camera offset** (first tile `cam/16`, drawn at `-(cam%16)`, one extra row + column).
- **Golden framebuffer test:** fractional camera near a map corner → exact pixels (clip + offset pinned).
- *Deferred (scheduled, not cut):* colorkey/transparent sprite blit → M2; PNG via stb_image + cross-seam asset handoff → M5/M7; ground+over second layer → M2; SIMD tile blit → later; tileset-by-handle instead of pointer → when `renderer.dll` splits out.

### Field logic (in the app)
- **Movement:** `{tileX, tileY, facing, moveProgress}`; `moveProgress` advances a fixed amount per tick (DR-34); a move commits `tileX/tileY` when it completes.
- **Collision:** refuse entry into a `blocked` tile — turn to face, don't move (DQ feel).
- **Camera:** float pixel position, follows the player's interpolated pixel position, clamps to map bounds (center-small-interior → later).
- Tested via the **app-as-pure-function** surface (drive `AppUpdate`, assert `AppMemory` + emitted commands).

### Placeholder assets (compiled-in, no file I/O — DR-31)
- **Tileset — char-art + palette:** each tile is 16 rows × 16 chars in the source; a palette maps char → ARGB; `AppInit` decodes the char-art into a `Tileset` atlas (N tiles across). Tiles carry intra-tile detail so scrolling reads visibly. (Replaced by real PNG loading at M5.)
- **Map:** a compiled-in char grid (DR-20 text-grid, pre-Tiled). Iteration is free via `app.dll` hot-reload — edit the grid, rebuild the app, the map reloads live. External map files + Tiled → M5.

### New data structures (app permanent storage, built in `AppInit`)
```
Tileset  { int w, h, tileSize; uint32_t* pixels; }        // atlas, N tiles across
TileType { bool blocked; int atlasIndex; }                // per tile-type
Tilemap  { int w, h; uint8_t* tiles; }                    // indices into TileType[]
Entity   { int tileX, tileY, facing; float moveProgress; }// the player
Camera   { float x, y; }                                  // pixel coords, float (DR-09)
```

### New `shared.h` render command
`RENDER_CMD_TILEMAP_LAYER` + `RenderCmdTilemapLayer { type; uint32_t* tilesetPixels; int tilesetW, tilesetH, tileSize; uint8_t* tiles; int mapW, mapH; float camX, camY; }` + a `PushRenderCmdTilemapLayer(...)` helper. Pointer-in-command is fine while the renderer is in-process (`renderer.cpp` is `#include`d into the platform exe); it becomes a handle at the `renderer.dll` split — deferred.

### Implementation order (user writes `src/**`; Claude writes `tests/**`)
1. **Platform:** `main.cpp` `ResizeRenderBuffer(... 320, 180 ...)` → `384, 216`. *(Verify: window runs, rect still draws.)*
2. **`shared.h`:** add the tilemap-layer command enum, struct, and push helper.
3. **Renderer:** `FlushRenderCommands` gains a `RENDER_CMD_TILEMAP_LAYER` case — the clipped, camera-offset tile blit. *(The deep work.)* → **Claude writes the golden framebuffer test.**
4. **App:** define the M1 structs; in `AppInit` decode the char-art tileset + build the compiled-in map.
5. **App:** `AppUpdate` — tile-to-tile movement + collision + camera follow/clamp; push a `draw_tilemap_layer` then a `fill_rect` for the player. → **Claude writes the app-as-pure-function tests** (move into open tile; refuse blocked tile; camera clamps at edge).
6. **Verify the exit:** walk the character around the scrolling map.

---

## 12. Decision Records

Rationale and rejected alternatives for every settled decision. Format: **Decision — options considered — why.**

### Control flow

**DR-01: Modes as enum + tagged union with switch dispatch.**
*Considered:* virtual interface/vtable per mode.
*Why:* explicit, debuggable, no hidden allocation, fits no-RTTI C-style. With only 6–8 modes ever, the "cost" of touching switch statements per new mode is a feature: the game's entire control flow is visible in one place. Vtables buy extensibility this project doesn't need.

**DR-02: Overlay/replace via a per-entry `renders_below` flag; lower modes never update; input to top only.**
*Considered:* per-mode bespoke rules; `updates_below` flag.
*Why:* classic JRPGs always freeze the world under menus/dialogue, so `updates_below` is dead weight. "Input to top only" as a hard rule eliminates a whole class of double-input bugs. Battle is visually a replace but logically a push — field state must survive untouched.

**DR-03: Mode communication via one result slot + push params (no callbacks, no message queue).**
*Considered:* callback functions on push; a general message/event queue.
*Why:* callbacks create hidden control flow and lifetime questions; a queue is machinery for a problem that can't occur — only one pop can happen per frame, enforced as an invariant. Push-params-down / result-up makes each mode "a function call stretched over many frames," which is the simplest correct mental model. Result consumed on first update after regaining top, cleared after one frame.

**DR-04: Scripted events via a tiny bytecode VM in shared state.**
*Considered:* (a) coroutines/fibers or C++20 coroutines — elegant, but fights the C-style grain and makes mid-event save state opaque; (b) hand-rolled state machines per event — fine for 3 events, misery for 100; (c) the chosen VM.
*Why:* VM state (`script_id, pc, status, regs[4]`) is trivially serializable → save-anywhere-on-field and bug reproduction are free. Dialogue mode stays dumb (renders text, reports done+choice); all *meaning* lives in script data — the engine/content split applied to events. Lives in shared state (not inside field mode) so `START_BATTLE` works: the VM must survive field being buried under battle.

**DR-05: Single VM, no parallel scripts; NPC idle wandering is a behavior enum.**
*Considered:* RPG Maker–style parallel/autorun script slots.
*Why:* parallel scripts are a feature this genre doesn't need; single-VM keeps the one-pop-per-frame invariant airtight and the save format trivial. Ambient NPC motion is not narrative content — a dumb wander enum is cheaper and never conflicts with the event system.

**DR-06: VM ticked by the stack machine, before top-mode update, only when RUNNING.**
*Considered:* field mode ticking the VM.
*Why:* scripts must be able to drive the stack (push dialogue, push battle) uniformly; if field owns the VM, scripts stall whenever field isn't on top. Stack-machine ownership makes `START_BATTLE` identical to `SHOW_TEXT`.

### Renderer

**DR-07: Render command buffer between game and backend.**
*Considered:* immediate-mode direct calls into the software renderer.
*Why:* the planned software→OpenGL swap becomes an isolated backend task instead of a rewrite; free bonuses: render order decoupled from logic order, frame capture for debugging, headless testing. The seam is the entire cost control for the swap — hence it exists from M0 with just `fill_rect`.

**DR-08: Fixed low internal resolution + integer upscale.**
*Considered:* native-resolution rendering with scaled assets.
*Why:* ~16× fewer pixels filled per frame and per-blit loops stay simple (no scaling in inner loops); art pipeline works 1:1 in one crisp integer space; enforced global pixel grid = authentic DQ look. Known cost: steppy scroll on modern displays (see DR-09). Native-res is how modern hi-res pixel art gets smooth sub-pixel scrolling, but it stops looking like DQ and triples renderer effort.

**DR-09: Accepted steppy scrolling, with pre-built exit paths.**
*Tier 1:* oversized low-res buffer + fractional camera applied at the upscale blit (~1 day, best with GPU final blit). *Tier 2:* native-res backend, realistically bundled with the OpenGL swap; game code untouched if the command seam is honest. *Enabler decided now:* camera position kept as float in game state from day one, upscale is a distinct command. Tile-to-tile walking rarely exposes the issue anyway.

**DR-10: 32-bit RGBA, colorkey sprite transparency; no palette format.**
*Considered:* indexed/palette rendering (authentic, enables palette tricks).
*Why:* palette mode complicates the GPU swap and alpha effects; palette *aesthetics* come from authored assets anyway. Colorkey keeps sprite blits copy-with-test fast and matches the art style.

**DR-11: Aspect ratios: variable internal width, fixed height, clamped range; UI edge/center-anchored.**
*Considered:* (a) pure letterbox — fallback, not offensive but avoidable; (b) widest-internal + crop on narrow displays; (c) unbounded variable width.
*Why:* fixed-height/variable-width is the modern-retro standard, and the architecture supports it almost by accident (camera-driven tilemap blit, dimension-agnostic command buffer). Clamped (~16:10–21:9) because unbounded width breaks composition and battle layout at 32:9. Load-bearing early habits: UI derived from screen dims; internal size a startup variable, not a `#define`. Maps get border padding; small interiors center or clamp; battle screen is composed (backdrop extends, enemies centered, menus anchored).

### Field

**DR-12: Tile-to-tile movement.**
*Considered:* free pixel movement with bounding boxes (Zelda-style).
*Why:* the genre's defining simplification — spatial state is just `tile_x, tile_y, facing, move_progress`, collision is one tile lookup, everything trivially serializable. Free movement ~triples collision complexity for a feel this genre doesn't want.

**DR-13: Tile properties per tile-type (in tileset), not per map cell.**
*Considered:* per-cell property layers.
*Why:* far less authoring work, how the classics did it; a per-cell override layer can be added later if a real need appears. Two visual layers (ground + over) cover the genre.

**DR-14: One map loaded at a time.**
*Considered:* streaming/adjacent-map loading.
*Why:* transitions hide loads at this scale; map load becomes a clean lifecycle moment (tear down pool, load, run on-enter script). Streaming is modern-game machinery with no payoff here.

**DR-15: Warps as a built-in trigger action, not scripts.**
*Why:* hundreds of routine doors/stairs shouldn't each be a script; scripts reserved for special entrances. Persistent world changes (chests, dead NPCs) ride the global flag system rather than per-map saved state.

### Battle

**DR-16: Battle resolution as its own small message sequencer, not the event VM.**
*Why:* battle beats ("Slime attacks! Hero takes 4.") are pacing over combat math, generated dynamically per action — wrong shape for authored bytecode. Sharing the VM would couple the two systems' save states and opcode needs for no gain.

**DR-17: Uniform action model; data-driven spells via ~10 effect-type enums.**
*Considered:* fully scripted spell effects; fully hardcoded spells.
*Why:* one action shape (actor, type, targets, resolution fn) makes player/enemy and field/battle share code paths. Effect enums + `{power, target_mode, mp_cost, element}` cover DQ-tier magic without a scripting dependency; the rare weird spell gets hardcoded honestly instead of growing the data schema.

**DR-18: Enemy AI as weighted action lists + minimal condition hooks; status list capped small (4–6) initially.**
*Why:* covers the entire genre's observable behavior; both are the battle system's main scope levers. Statuses are the sneaky-large subsystem (application, tick, expiry, battle-end persistence rules each ×N).

### Data

**DR-19: Tabular data as hand-authored text with an owned ~200-line parser + hot reload.**
*Considered:* binary formats; third-party formats (JSON lib).
*Why:* a text editor is the correct authoring tool for a few hundred records; owning the parser fits no-STL and keeps error messages good; hot-reload transforms balancing iteration speed.

**DR-20: Tiled for maps.**
*Considered:* (a) own editor — a seductive multi-month subproject; would be good, but it's an explicit scope decision, not a default; (b) hand-authored text grids — survives ~10 maps.
*Why:* importer is a weekend; Tiled's layers/properties/objects map 1:1 onto the field design. Middle path available later: in-game debug tile painting.

**DR-21: Script DSL assembled to bytecode by a small Python tool; runtime parsing acceptable until scripts multiply (M3→M5).**
*Why:* the assembler buys build-time error checking (vs. runtime-when-the-NPC-fires) and is where string extraction happens — the string table doubling as a localization seam.

**DR-22: Load source formats directly now; design structs bake-ready; no asset packer yet.**
*Considered:* binary bake step from day one.
*Why:* at DQ scale load times may never justify a packer; parsing text *into* the structs a baker would emit makes adding one later mechanical. PNG via stb_image rather than a custom texture format (stb_image and stb_vorbis are the two canonical exceptions to no-external-code).

**DR-23: Symbolic names in source, integer indices at runtime, two-phase load with a resolution pass.**
*Considered:* raw numeric IDs in source; runtime string lookups; pointers as references.
*Why:* numbers fail silently (typo'd number = wrong *valid* record) and renumber on insertion; names fail loudly and read well. Two-phase (parse-all then resolve-all) makes file ordering and circular references non-issues and batches every missing-reference error into one run with file/record context. Pointers break serialization and arena relocation; runtime string lookups are C-string friction in gameplay code. ~400–500 lines, reused by every record type.

**DR-24: Flags declared explicitly in one registry file.**
*Considered:* implicit creation on first mention in scripts.
*Why:* implicit creation turns a typo'd `jump_if_flag` into a silent always-false flag — a guaranteed multi-hour bug. Explicit declaration makes it a build error.

**DR-25: Saves store names (or name-hashes), not indices.**
*Considered:* (a) raw indices — saves corrupt when content is reordered; acceptable mid-dev only; (b) stable explicit numeric IDs — robust but reintroduces number bookkeeping.
*Why:* save-by-name is immune to reordering and free given the resolution machinery; flags saved as set-names, not a bitset. Debug builds keep name tables resident for reverse lookup and console commands.

### Save & audio

**DR-26: Saving only in field mode; snapshot excludes mode stack and per-map entity positions.**
*Why:* the stack is always `[field]` at save time, shrinking the problem to the flat shared state (which the VM decision made fully serializable). Load path built early (debug quicksave ~M5) because late save systems always find state that leaked outside the serializable core.

**DR-27: Audio behind a command buffer, mirroring the renderer.**
*Considered:* direct-call mixer API.
*Why (user-driven decision):* game stays unaware of mixing/playback; backend swappable; and the command buffer doubles as the audio-thread boundary mechanism — one design solving abstraction and thread safety simultaneously. Cost: ~2 extra evenings.
*Key asymmetry vs. rendering:* draw commands are a stateless per-frame scene; audio commands are events addressing persistent sounds. Hence: fire-and-forget for SFX; **named channels** (MUSIC, AMBIENT) for persistent sounds at DQ scale (generation-counted handles only if it must generalize); a **reverse event buffer** for completion notifications (victory fanfare), keeping the seam clean vs. polling. Commands carry symbolic asset IDs, never data pointers — residency is the backend's business. Loop points and music fades built into the music channel from the start (awkward to bolt on).

### Sequencing

**DR-28: M3 (script VM) before M4 (battle).**
*Considered:* battle first — more motivating.
*Why:* the VM validates the mode-stack/result-slot architecture while it's still cheap to change; M4 then lands on proven rails. The one deliberately debatable ordering in the plan.

**DR-29: Data pipeline consolidated at M5, after first consumers exist.**
*Why:* every format gets designed against a real consumer (M4's battle needs monsters; M2's maps need an editor path) instead of speculatively. The pipeline grows alongside systems, not as an upfront phase.

**DR-30: Level curves and stat growth as data tables, not formulas.**
*Why:* tables are directly tunable per level and how the classics were balanced; formulas get bent into tables anyway once tuning starts.

### Platform seam

**DR-31: The app↔platform seam is absolute — the app makes zero OS calls; everything crosses as data buffers.**
*Considered:* pragmatic direct calls from the app for "obviously platform-y" things — a blocking `fread` for saves, a direct audio API, querying the clock or OS entropy.
*Why:* a deliberate experiment — push the platform/app separation as hard as it will go and see whether the payoffs hold. The app becomes a **pure state machine**: its only inputs are its own arenas, the frame's `UserInput`, a delivered time delta + RNG seed, and results delivered back from the platform; its only outputs are command buffers. Several payoffs are load-bearing, not aesthetic:
1. **Hot-reload safety** — no OS handle (file, audio voice, socket) ever lives inside app memory, so reloading `app.dll` can never orphan or corrupt one; app memory stays pure serializable data across reloads.
2. **Input record/replay determinism** — logging the frame's inputs + delivered results is enough to deterministically reproduce any bug (Handmade-Hero-style), essentially free once the seam is this hard.
3. **Headless testability** — the app can be driven and its emitted command buffers asserted on, with no OS present.
4. **Backend-swap / portability** — the same reason the render-command seam (DR-07) exists, applied to every OS interaction.

*Mechanism:* the seam has exactly two directions — **forward = commands/requests** (app→platform), **reverse = events/results** (platform→app) — which unifies audio completion events (DR-27) and I/O results into one concept. I/O generalizes the render-command pattern into **request/result**, because I/O has latency and a return value (unlike fire-and-forget draw commands): the app emits e.g. `SAVE_WRITE {slot, src region}` / `SAVE_READ {request_id, slot, dest region}` and receives `IO_COMPLETE {request_id, status}` on the reverse buffer a later frame. The app owns the memory — it provides the destination region from its own arenas (saves have a bounded max size) and the platform only fills it. **Assets** (images, audio, maps, tables) stay behind **symbolic IDs**, never raw bytes to the app — residency is the platform's business; **saves** are the one case the app needs raw bytes in both directions, because it owns the serialization.

*Deferred (rolling-wave):* the concrete I/O command/result structs are designed at **M7** (save/load), not now — no consumer exists earlier and building it now would be speculative. The binding constraint until then: **no direct OS call may leak into the app, even a temporary one.** First test is M1's hand-authored map — embed it as a compiled-in array or have the platform hand it in; do not `fopen` it inside the app.

*Known purity holes to revisit:* (a) debug logging currently crosses via a synchronous `PlatformLogFn` callback — a small hole; could become a log-command buffer for full purity, tolerated for now. (b) Time and randomness must be *delivered as inputs* (frame `dt`, a platform-provided seed) rather than queried, which also strengthens replay determinism.

### Testing

**DR-32: Test the contract at three seam-defined surfaces; "include the source" linkage; no static test lib until forced.**
*Considered:* (a) test everything, including platform/SDL glue and trivial plumbing — busywork that tests the framework, not the game; (b) skip tests until bugs force them — loses the safety net exactly on the deep systems we rewrite most; (c) build a static library of game code linked into both the DLLs and the test exe from the start.
*Why:* the hard app/platform seam (DR-31) splits the code into three surfaces that each want a different technique — **pure cores** (unit tests), **the app as a pure function** (drive `AppUpdate`, assert on emitted command buffers + `AppMemory`, fully headless), and **the software rasterizer** (golden framebuffer tests — deterministic pixels). Platform/SDL glue is deliberately *not* unit-tested (verified by running). Tests assert on **outputs (the contract), not implementation**, so a deep system can be rewritten — e.g. a SIMD blit pass — and the same golden test proves byte-identical output; this is what makes the tinkering safe rather than risky. Which system gets tests is a workflow decision, not per-file guilt: **a deep system's acceptance bar for a milestone includes its tests; good-enough glue gets a test only when a real bug appears.**
*Linkage — "include the source":* test TUs `#include` the unit under test directly (header-only like `arena.h`, or a single `.cpp`), compiling it into `run_tests.exe`. Rejected the static lib for now because (i) it is build-system work before the module boundaries have stabilized, and (ii) the game compiles no-exceptions + static CRT (`-EHsc- -MTd`) while GoogleTest needs exceptions + dynamic CRT (`-EHsc -MDd`), so a shared lib invites CRT-model link errors. The one discipline it demands — **one test TU per `.cpp`**, or the linker sees duplicate definitions — is free at this scale, and header-only units are immune. Migrate to a static lib only when genuinely forced (many test files fighting the include-once rule, or a need to test the exact shipping compilation), by which point the boundaries will be obvious. Rolling-wave applied to the test build itself.
*Mechanics:* GoogleTest, wired via `build_tests.bat` (auto-globs `tests/*.cpp`; `gtest_main` supplies `main()`); `build.bat` builds and runs the suite as its last step. Debug-only `ASSERT` guards — which compile to nothing in release — are covered by `#if DEBUG` death tests (`EXPECT_DEATH`). First suite: `tests/test_arena.cpp` (8 arena invariants + 1 death test), the pattern-setter for pure-core tests.

### M1 decisions

**DR-33: Internal resolution is 384×216.**
*Considered:* 320×180 (16:9, exact ×6→1080p, already wired); 256×224 (authentic SNES 4:3, clean 16×14 tiles, but pillarboxes on widescreen and contradicts DR-11); 384×216 (chosen).
*Why:* 16:9 keeps it consistent with DR-11 (fixed-height / variable-width widescreen), and 384×216 buys a wider field of view than 320×180 — 24×13.5 tiles vs. 20×11.25 at 16px — for more on-screen world. Fixed height 216 is the DR-11 base; width varies for wider aspects. Cost: 720p is a non-integer ×3.33 (handled by the fractional-scale/letterbox path); ×5→1080p and ×10→2160p are exact. Requires changing the hardcoded `320×180` in `main.cpp` to `384×216` (internal size is a startup value per DR-08, not a `#define`).

**DR-34: Movement uses a fixed 60Hz logical tick; no `dt` crosses the app/platform seam.**
*Considered:* fixed tick, no dt (chosen); platform-owned accumulator delivering an interpolation alpha now; passing wall-clock `dt` to the app.
*Why:* matches the existing loop (it already pins each frame to 1/60s and calls `AppUpdate` once), and keeps `AppUpdate` a pure fixed tick so the app stays deterministic — the record/replay payoff of DR-31. Motion is expressed **per tick** (`move_progress += fixed amount`), never scaled by wall-clock `dt`, so decoupling sim from render later for high-refresh smoothness (DR-09) means *adding interpolation* (platform delivers an alpha, app double-buffers prev/current positions), not switching to a variable timestep. Rejected variable-`dt` (breaks determinism); rejected building the accumulator + interpolation now (machinery M1 doesn't need — deferred with DR-09). Accepted known property: with no catch-up, a frame that can't hit 60Hz slows the sim rather than dropping frames — fine at M1, revisited only if it ever matters.

---

## 13. Standing Rules

1. **The milestone exit criterion is the tiebreaker.** When tempted to design ahead (the baker, general sound handles, a map editor), ship the exit, defer the rest. (Lesson carried from Grimfell planning.)
2. Game code never thinks in physical pixels; commands carry symbolic IDs, never pointers.
3. Everything that persists lives in flat, serializable shared state.
4. One pop per frame; input to the top mode only; single script VM.
5. Names in source, indices at runtime, names in saves.
6. **The app makes zero OS calls.** All outside data enters as inputs or delivered results; all outside effects leave as command/request buffers. The platform owns every OS resource. (DR-31.)
