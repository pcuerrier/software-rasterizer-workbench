# Left Stranded — Project Plan

**Project:** *Left Stranded* (working title) — a real-time-with-pause tactical roguelike. A crew of
3–4 survivors, stranded on an alien world, fighting their way out one small map at a time.
Think **Door Kickers 2 crossed with *Aliens***: plan, execute, re-plan — against a swarm.
**Stack:** C-style C++ (no STL/exceptions/RTTI), SDL3, custom **2D** software rasterizer, custom
memory management
**Cadence:** ~1 hour/day solo development
**Canonical exceptions to no-external-code:** stb_image (PNG loading), stb_vorbis (OGG streaming)

> **Scope history.** This project began as a classic 2D JRPG (`old-jrpg-project-plan.md`, preserved
> verbatim), pivoted to a 3D-rendered turn-based tactical roguelike (**DR-35**), and pivoted again
> to its current form (**DR-41**). Decision Record numbering is continuous across all three so
> references never rot; retired DRs are listed as retired rather than deleted.

> **This plan is deliberately incomplete.** Only M1 is designed to build depth. M2 is sketched only
> far enough that M1 cannot box it in. §7 is an explicit register of what is *not* designed yet —
> it is a to-discuss list, not an oversight. Design happens just-in-time, as we implement.

---

## 1. Current State

What actually exists in `src/`, as distinct from what is planned:

- **Platform layer** (`main.cpp`, `platform/`) — SDL3 init, resizable window, input polling, DLL
  hot-reload, 384×216 render buffer with integer upscale blit, frame nailed to 60 Hz by
  sleep + spinlock. Owns every OS resource.
- **App layer** (`app.cpp`) — a 50×50 tile map of colour-gradient tiles, a 2D camera
  (`{x, y, zoom}`) with pan, zoom and view-bounds clamping. Pushes clear + set-camera + one rect
  per visible tile. Makes zero OS calls.
- **Rasterizer** (`rasterizer/rasterizer.cpp`) — clipped solid-colour rect fill, and the
  world→screen camera transform. `#include`d into the platform exe; the `renderer.dll` split is
  still planned, not done.
- **`shared.h`** — `AppMemory`, `UserInput`, three render commands (`CLEAR`, `RECT`,
  `SET_CAMERA`), the `LIST_RENDER_COMMANDS` alignment guard, the logging function pointer.
- **`arena.h`** — bump allocator, with a full unit-test suite (`tests/test_arena.cpp`).

Notable: the renderer *already* owns the camera transform — the app pushes world coordinates and
never computes a screen coordinate. That is DR-42 working before it was written down.

Known stale details to fix in passing, not worth their own milestone: the window title still reads
`"JRPG - Workbench"`, and `RENDER_CMD_TILEMAP_LAYER` from the first pivot is gone (already
removed).

---

## 2. Architecture

Three layers, unchanged by either pivot — which is the strongest evidence available that DR-07 and
DR-31 were the right calls:

- **Platform layer** (`main.exe`) — SDL3, window, input, DLL loading, framebuffer ownership,
  texture ownership, final upscale blit, frame pacing. Owns every OS resource.
- **App layer** (`app.dll`) — all game logic. Makes **zero** OS calls. Emits render commands.
- **Renderer** (`renderer.dll`, planned; currently `#include`d into the platform exe) — consumes
  render commands, writes pixels. The software rasterizer lives here.

`src/shared.h` is the only header crossing the DLL boundary. It will pull in `src/math2d.h`
(`Vec2`, `Mat3`) once M1 starts, because both sides need the types: the app builds transforms for
its sprites, the renderer composes and inverts them.

**Engine/content split:** code implements systems; maps, crew, weapons and tables are data.

---

## 3. The 2D Rasterizer (deep system — the project's centre of gravity)

This is the primary deliverable, not a means to an end. The 3D triangle pipeline of the previous
plan is retired (DR-41); what replaces it is **not** a thin blitter, because two design choices
made elsewhere force real depth into it:

1. **Unit positions are continuous floats** (DR-46). At 384×216 a sprite lands *between* pixels, so
   correct rendering means inverse-mapped source sampling — not an integer memcpy per row.
2. **Facing matters** (vision cones, the DK2 verb set). Rotated sprites mean a per-pixel affine
   inverse transform over the destination bounding box. That is the triangle rasterizer's inner
   loop wearing a different hat: same bounding-box walk, same per-pixel interpolation, same
   edge/bounds tests.

### Pipeline shape

```
app builds:  unit quad + Mat3 modelToWorld (position · rotation · scale)  ──┐
             Mat3 worldToScreen (camera)                                   ─┤ render commands
                                                                            ▼
renderer:  modelToScreen = worldToScreen · modelToWorld
           transform the quad's 4 corners → screen space
           → destination bounding box, clamped to the framebuffer
           → invert: screenToModel = inverse(modelToScreen)
           → per-pixel: map screen pixel → model space → source texel
           → bounds-reject, sample, blend
```

**The transform lives in the renderer, not the app** (DR-42). The app never computes a screen
coordinate. This is what keeps a future GPU backend a backend task rather than a rewrite, and it
puts the transform and sampling — the interesting parts — on the deep side of the seam.

### The depth roadmap (scheduled across milestones, not all at once)

| Capability | Milestone |
|---|---|
| Mat3 transform chain, composed in the renderer | M1 |
| Textured blit with correct clipping | M1 |
| Alpha blending (source-over) | M1 |
| Inverse-mapped affine sampling (rotation, scale) | M1 |
| Sub-pixel positioning | M1 |
| Golden framebuffer tests | M1 |
| Additive blend (muzzle flash, plasma) | later |
| Visibility-polygon rasterization + fog mask | with vision |
| Span/dirty-rect optimization | when fill rate binds |
| SIMD fill and blend pass | when fill rate binds |
| Bilinear filtering | probably never — nearest is correct for pixel art |

What we give up relative to the retired 3D plan: the z-buffer, near-plane clipping, and
perspective-correct interpolation. Accepted knowingly (DR-41).

### Render-command alignment contract (carried forward, load-bearing)

`PushSize` aligns every allocation to 8 bytes; `FlushRenderCommands` walks the stream with
`at += sizeof(cmd)` and does **no** re-alignment. Writer and reader stay in lockstep only if every
`RenderCmd*` struct's `sizeof` is a multiple of 8. The `LIST_RENDER_COMMANDS` X-macro +
`static_assert` guards this at compile time. **Add every new command to that list** — the sprite
command carries a `Mat3` and is exactly the kind of struct where field reordering happens.

---

## 4. Level Representation (DR-44)

**A flat tile grid.** No height channel — a station floorplan is walls and floor, and Door Kickers
handles multi-story buildings by showing one floor at a time. So do we.

```
Tile      { uint8_t type; uint8_t flags; }   // type indexes a TileType table
LevelMap  { int32_t w, h; Tile* tiles; }     // one floor, w × h tiles
```

`flags` carries the bits that rules code reads directly — blocks-movement, blocks-sight, is-door.
Both fields are `uint8_t`; a floorplan will never need more, and the pair packs to 2 bytes so a
128×128 map is 32 KB.

- One floor loaded at a time. Between-floor movement is a map transition, not a render concern.
- The grid is the **navigation and query structure**: pathfinding, line of sight, cover, and noise
  propagation all read it. Unit *positions* are continuous and live outside it (DR-46).
- Built once into permanent storage when a map loads.

Hand-authored and compiled into the app for M1 (DR-31 — no file I/O in the app). Procedural
floorplan generation is deliberately undesigned; see §7.

---

## 5. Simulation Model (DR-45)

Real-time with pause, on a **fixed 60 Hz logical tick**. Real-time does not mean variable
timestep — and the fixed tick is what makes the rest work:

- **Determinism.** A plan that replays identically is what makes DK2-style planning legible, and
  it is the record/replay payoff DR-31 was built for.
- **No `dt` crosses the seam.** `AppUpdate` remains a pure fixed tick.
- **Pause and time-scale are app-side.** The platform keeps calling `AppUpdate` at a fixed rate;
  the app decides how many simulation steps that call advances — zero when paused, two at double
  speed. The seam does not move, and the platform never learns what "paused" means.

Current implementation: the platform nails the frame to 60 Hz with sleep + spinlock and calls
`AppUpdate` once per frame, so tick == frame. **Exit path if frames ever drop:** the platform gains
a fixed-step accumulator and calls `AppUpdate` 0..N times per rendered frame. Nothing in the app
changes. Not building that until a dropped frame is observed.

---

## 6. Core Fantasy (DR-48)

Door Kickers' tension is **information** — what is behind the door, which angle is uncovered. A
swarm's tension is **attrition and geometry** — chokepoints, ammo, arcs of fire. The thing that
reconciles them is **noise as the information currency**.

A loud breach draws the swarm. Welding a door buys time but closes a route. A motion ping tells you
something is coming down the corridor, not what. The crew keep Door Kickers' verbs — stack up,
breach, cover an angle, overwatch — but what they are managing is a flow, not a patrol.

This is a settled *direction*. None of its mechanics are designed; see §7.

---

## 7. Not Designed Yet — the open register

This section exists so that "we haven't decided" is recorded rather than implied. Nothing here is
committed to; each gets its own design discussion when its milestone arrives.

**Near-ish (M2–M3 territory):**
- The order/plan interface — issuing paths and action markers, the pause/plan/execute loop. This is
  Door Kickers' *defining* feature and it is classified **good-enough** (§8), which is a real
  tension worth naming out loud: the thing that makes the reference game great is the thing this
  project is not supposed to gold-plate.
- Doors and the verb set (open quietly / breach / weld / peek), and door state representation.
- Continuous-position collision and unit separation.

**Middle distance:**
- Noise: propagation model, what generates it, how the swarm consumes it.
- Swarm AI. Flow-field pathfinding is the likely shape — one Dijkstra pass over the grid per
  target, every agent reads the gradient, O(map) instead of O(agents × path). Not committed.
- Whether the swarm simulation is a **deep** system. It is the first place real performance
  pressure will appear, which is an argument for it. Decide when it arrives, not now.
- Vision cones, fog of war, and how visibility is rasterized.
- Combat resolution: aim time, accuracy, reaction time, damage.
- Target swarm scale. Design for a few hundred concurrent agents; architect so that scaling to a
  thousand is a data-oriented exercise rather than a rewrite.

**Far out:**
- Crew: composition, loadouts, individual skills, permadeath, replacement.
- Run structure: how maps sequence, extraction, what carries between maps and between runs.
- Procedural floorplan generation.
- Script VM (deep system, purpose still undefined — see DR-04).
- Audio command buffer + mixer (deep system — see DR-27).
- Save/load of run state.
- Data pipeline consolidation, placed after its first real consumers exist.
- The `renderer.dll` split (currently `#include`d into the platform exe).

---

## 8. Systems Classification

**Deep** (tinker freely — this is why the project exists):
2D software rasterizer · memory · script VM · audio mixer/threading
*(swarm simulation is a candidate — decide when it arrives)*

**Good-enough** (exists only to make the deep systems reachable):
order/planning UI · menu/widget glue · level generation plumbing · UI panels · data importers

---

## 9. Milestones (rolling wave)

### M1 — The 2D rasterizer becomes real ← **current**

**Exit criterion:** a hand-authored station floor drawn from a texture atlas, plus one sprite drawn
rotated and sub-pixel positioned, clipped and alpha-blended — all through a `Mat3` transform chain
composed in the renderer, verified by golden framebuffer tests.

*Locked:* Y-down world space, `Mat3` column-vector column-major as plain `float[9]` (DR-43) ·
transform composed in the renderer (DR-42) · textures owned by the platform, referenced by handle
(DR-47) · tile grid with `type` + `flags`, map compiled in (DR-44, DR-31) · internal resolution
384×216 (DR-33) · fixed 60 Hz tick (DR-34, DR-45) · nearest-neighbour sampling.

**Systems touched**

| System | Class | M1 role |
|---|---|---|
| Rasterizer | **Deep** (M1's core) | Solid rect fill → textured affine sprite pipeline |
| Math (`math2d.h`) | Pure core | `Vec2`, `Mat3`, compose + invert |
| Platform | Minimal | Texture table, atlas load at startup |
| Level/tile grid | Good-enough | Hand-authored floor, pushed as sprites |

**Rasterizer — acceptance bar for M1**
- `modelToScreen = worldToScreen · modelToWorld`, composed in the renderer from the camera command
  and the per-sprite transform.
- Textured blit with correct clipping against framebuffer bounds — **no seams and no overlap
  between adjacent tiles.** Test with adjacent tiles early; an off-by-one in the clip or the
  bounding box shows as a one-pixel gap or double-drawn column, and it shows up on frame one
  because the whole floor is a grid of touching quads.
- Source-over alpha blending, per pixel.
- General affine path: invert `modelToScreen`, walk the destination bounding box, map each screen
  pixel back to model space, reject out-of-quad, sample the atlas.
- **Sub-pixel positioning:** a fractional world position must produce a correct sample coordinate
  computed from the inverse transform — not a rounded destination rectangle.
- **Golden framebuffer tests:** known atlas + known transforms → exact pixels.

*Deferred (scheduled, not cut):* additive and other blend modes · bilinear filtering · visibility
polygons and fog masks · span/dirty-rect optimization · SIMD fill and blend · sprite animation ·
an atlas packing tool · texture-by-handle becoming a real asset system · the `renderer.dll` split.

**Designated slip:** rotation. If the affine path drags, ship M1 on the axis-aligned textured blit
(clipping + alpha + sub-pixel) and carry rotation into M2. The axis-aligned path is what the exit
criterion depends on; rotation is where the depth is. Slip it only if it is genuinely stuck, not
merely hard — see Standing Rule 1.

**New `shared.h` render commands** (both must go into `LIST_RENDER_COMMANDS`)

```
RENDER_CMD_SET_CAMERA   — replaces the {x, y, zoom} form
  { type; pad; Mat3 worldToScreen; }

RENDER_CMD_SPRITE
  { type; textureId; srcX, srcY, srcW, srcH; Mat3 modelToWorld; }
```

Camera stays its own command, held in a local by `FlushRenderCommands` while it walks the stream —
that is how GPUs work, and it avoids duplicating the matrix per sprite. `textureId` is a value from
an enum declared in `shared.h`, agreed by both sides — never a pointer, never a path (DR-47,
Standing Rule 5).

**Implementation order** (user writes `src/**`; Claude writes `tests/**`)

Order matters: debugging a transform chain against a full tile map is misery, against one quad it
is tractable. **Get one textured quad on screen before the tile map pushes sprites.**

1. **`src/math2d.h`** — `Vec2`, `Mat3`; `Mat3Identity`, `Mat3Mul`, `Mat3Translate`, `Mat3Rotate`,
   `Mat3Scale`, `Mat3Inverse`, `Mat3TransformPoint`. Include from `shared.h`.
   → *Claude writes `tests/test_math2d.cpp`* — pure core, highest ROI: multiply order, that
   `Mat3Inverse(m)` composed with `m` is identity, that a known point round-trips.
2. **`shared.h`** — the two command structs, push helpers, X-macro entries, the texture-ID enum.
3. **Platform** — vendor `stb_image.h` into `lib/` (SDL's internal copy under
   `lib/SDL/src/video/` is not ours to use), add a small fixed texture table, load the atlas at
   startup from a compiled-in list of paths.
4. **Rasterizer** — axis-aligned textured blit with clipping. **First pixels.** Then alpha. Then
   the general inverse-mapped affine path. *The deep work.*
   - *Debugging tips:* get the transform right before the sampling — plot the quad's four
     transformed corners as single pixels first, and check they land where you expect. Then verify
     the inverse separately (forward-then-back is a unit test, not a visual one). A quad that
     samples garbage and a quad that is transformed wrong look identical on screen; separating the
     two is what saves the evening.
5. → *Claude writes the golden framebuffer test.*
6. **App** — `Tile` / `LevelMap` structs, a small hand-authored floor, push camera + one sprite per
   visible tile, plus one test sprite rotating in place.
7. **Verify the exit:** run it, look at the floor.

### M2 — The grid becomes interactive (lightly defined)

Mouse picking screen → tile (the inverse camera `Mat3` from M1 gives this almost for free) · tile
highlight · one crew member takes a move order and walks a grid path with continuous interpolated
position · the pause/plan/execute split in its simplest possible form: issue an order while paused,
unpause, watch it run.

This is where the mode stack (DR-01/02/03) gets its first real use, and where the first honest
design discussion about the order/plan interface has to happen.

### Beyond M2

Sketch only — see §7's open register. Nothing there is designed, and nothing there should be
designed before its milestone.

---

## 10. Decision Records

Format: **Decision — options considered — why.** New decisions append here. Numbering is continuous
across all three project incarnations; surviving DRs keep their original numbers so references
never rot. Full original rationale for carried-over DRs is in `old-jrpg-project-plan.md`.

### Carried over unchanged

**DR-01: Modes as enum + tagged union with switch dispatch.** Explicit, debuggable, no hidden
allocation, fits no-RTTI C-style. Entire control flow visible in one place.

**DR-02: Overlay/replace via a per-entry `renders_below` flag; lower modes never update; input to
top only.** Eliminates a whole class of double-input bugs.

**DR-03: Mode communication via one result slot + push params.** No callbacks (hidden control
flow), no queue (only one pop can happen per frame — enforced invariant). Each mode is "a function
call stretched over many frames".

**DR-04: Scripted events via a tiny bytecode VM in shared state.** *Considered:* coroutines (fights
the C-style grain, opaque save state); hand-rolled state machines (misery past ~3 events). *Why:*
VM state is trivially serializable. **Its role in this game is undefined and deferred** — it
remains a deep-system deliverable, not a scheduled feature.

**DR-07: Render command buffer between game and backend.** *Considered:* immediate-mode direct
calls. *Why:* a backend swap becomes an isolated task instead of a rewrite; free bonuses are render
order decoupled from logic order, frame capture, and headless testing. **Two pivots have now proved
this one:** neither the 2D→3D change nor the 3D→2D change cost the seam anything.

**DR-08: Fixed low internal resolution + integer upscale.** ~16× fewer pixels filled per frame and
simple inner loops. Still the reason a per-pixel software sampler is affordable at all.

**DR-09: Accepted steppy scrolling, with pre-built exit paths.** Tier 1: oversized buffer +
fractional camera at upscale. Tier 2: native-res backend, bundled with a GPU swap.

**DR-10: 32-bit RGBA; no palette format.** Palette mode complicates a GPU swap and alpha effects;
palette *aesthetics* come from authored assets anyway.

**DR-11: Aspect ratios — variable internal width, fixed internal height, clamped range; UI
edge/center-anchored.** Internal size stays a startup variable, never a `#define`.

**DR-19: Tabular data as hand-authored text with an owned ~200-line parser + hot reload.**

**DR-22: Load source formats directly now; design structs bake-ready; no asset packer yet.**

**DR-23: Symbolic names in source, integer indices at runtime, two-phase load with a resolution
pass.** Numbers fail silently; names fail loudly. Ordering and circularity become non-issues.

**DR-24: Flags declared explicitly in one registry file.** A typo'd flag must be a build error,
never a silent always-false.

**DR-25: Saves store names (or name-hashes), not indices.** Immune to content reordering.

**DR-27: Audio behind a command buffer, mirroring the renderer.** The buffer doubles as the
audio-thread boundary. Fire-and-forget SFX; named channels for persistent sounds; a reverse event
buffer for completion notifications; symbolic asset IDs, never pointers.

**DR-31: The app↔platform seam is absolute — the app makes zero OS calls; everything crosses as
data buffers.** The project's central experiment. Payoffs are load-bearing, not aesthetic:
hot-reload safety (no OS handle ever lives in app memory), input record/replay determinism,
headless testability, backend-swap portability. Two directions only: **forward = commands/
requests**, **reverse = events/results**. Concrete I/O request/result structs deferred until a save
system exists. Binding constraint until then: **no direct OS call may leak into the app, even a
temporary one** — M1's map and texture atlas are compiled in and platform-loaded respectively,
never `fopen`ed by the app.

**DR-32: Test the contract at three seam-defined surfaces; "include the source" linkage; no static
test lib until forced.** Pure cores → unit tests. The app as a pure function → drive `AppUpdate`,
assert on emitted command buffers + `AppMemory`. The rasterizer → golden framebuffer tests.
Platform/SDL glue → not unit-tested, verified by running. Tests assert on **outputs, not
implementation**, so a deep system can be rewritten (a SIMD blend pass, a span-based blitter) and
the same golden test proves identical output. One test TU per `.cpp`.

**DR-33: Internal resolution is 384×216.** 16:9, consistent with DR-11's fixed-height/
variable-width model; ×5 → 1080p and ×10 → 2160p are exact.

**DR-34: Movement uses a fixed 60 Hz logical tick; no `dt` crosses the seam.** Keeps `AppUpdate` a
pure fixed tick, which is the record/replay payoff of DR-31. **Promoted by DR-41 from convenience
to load-bearing:** a real-time game needs determinism more than a turn-based one did, not less.
Extended by DR-45.

### Surviving in spirit

**DR-17: Uniform action model** (actor + action type + target(s) + resolution fn, shared between
player and AI). Fits a real-time order queue *better* than it fit turns — a crew member's plan is a
queue of these, consumed over continuous time instead of resolved on a turn boundary. Carried into
the tactics sketch rather than kept as a settled DR.

**DR-36: The transform lives in the renderer, not the app.** The 3D form is retired with the 3D
pipeline; the principle is re-recorded for 2D as **DR-42**, with the same rationale.

**DR-38: Math conventions settled once, never revisited.** The specific 3D conventions (Y-up
right-handed, grid on XZ, CCW front faces) are retired. The two rules that were about *engineering*
rather than about 3D survive verbatim into **DR-43**: column-vector column-major storage, and
matrices as plain `float[N]` with no SIMD members.

### Retired

- **DR-12–DR-15** (2D field: tile-to-tile movement, per-tileset tile properties, one map at a time,
  warps as built-in triggers). Retired at the first pivot. The *spirit* survives — properties per
  `TileType`, one map loaded at a time — but grid-locked movement is explicitly rejected by DR-46.
- **DR-16, DR-18** (JRPG battle sequencer, weighted-list enemy AI, small status list).
- **DR-20** (Tiled for maps). Maps are procedural; there is no hand-authored map pipeline to import.
- **DR-21** (script DSL assembled by a Python tool). Deferred with the VM's purpose.
- **DR-26** (saving only in field mode). Save semantics for a roguelike run are a different problem.
- **DR-28–DR-30** (milestone sequencing, level curves as tables). Tied to the retired M1–M8 chain.
- **DR-37** (orthographic projection). Retired with the 3D pipeline — there is no projection matrix
  at all now, only an affine 2D camera.
- **DR-39** (heightfield grid with discrete floors). Retired; replaced by DR-44. The migration
  analysis it contained (span columns, voxels, and *when* the decision point falls) is moot — the
  game no longer has a height axis.
- **DR-40** (M1 uses a hard-coded camera matrix). Retired as moot: a real camera struct already
  exists in `app.cpp` and predates M1.

### New — the second pivot

**DR-41: Pivot to *Left Stranded* — a real-time-with-pause tactical roguelike rendered by a 2D
sprite rasterizer.**
*Considered:* (a) keep the 3D turn-based tactical plan; (b) keep the top-down concept but render it
with a 3D orthographic triangle pipeline — level geometry as real 3D walls, units as camera-facing
textured quads; (c) 2D sprite rasterizer — chosen.
*Why:* the game direction is user-driven and settled — Door Kickers 2's plan/execute loop against a
swarm, on a small crew. The *rendering* choice was genuinely open, and (b) was recommended on the
grounds that it preserves a richer deep system (transform chain, culling, clipping, z-buffer) while
producing the same top-down look. The user chose (c). The decisive counter-argument is that (c) is
where the code had already gone, and that a 2D rasterizer is not shallow *given the other two
decisions in this pivot*: continuous positions (DR-46) force inverse-mapped sub-pixel sampling, and
facing forces per-pixel affine inverse transforms — which is the triangle rasterizer's inner loop
over a different primitive. What is knowingly given up: the z-buffer, near-plane clipping, and
perspective-correct interpolation. What is gained back: visibility-polygon rasterization becomes a
first-class problem rather than an afterthought, and fill-rate pressure from a few hundred swarm
sprites arrives early enough to justify the SIMD work honestly.
*Cost of the pivot:* the plan document, plus four retired DRs. No 3D code had been written — the
previous plan's M1 never started — so, as with DR-35, the pivot cost is the plan and nothing else.

**DR-42: The 2D transform lives in the renderer, not the app.** *(successor to DR-36)*
*Considered:* (a) app pushes a unit quad + `Mat3`, renderer composes and inverts — chosen; (b) app
computes screen-space rectangles and pushes those; (c) app pushes semantic commands ("draw the
floor") and the renderer builds geometry.
*Why:* (b) kills any future GPU backend outright — it would receive already-projected geometry and
have nothing to do — violating DR-07's whole purpose, and it drags matrix math into gameplay code.
(c) bakes game concepts into the renderer, the wrong direction for a system meant to stay general.
(a) mirrors the GPU pipeline, so what is learned transfers. **Already true in the existing code:**
`app.cpp` pushes world coordinates and `rasterizer.cpp` applies the camera. M1 generalizes that
from `{x, y, zoom}` to a `Mat3`; it does not change direction.

**DR-43: 2D math conventions — Y-down world space; `Mat3` column-vector column-major, plain
`float[9]`.** *(successor to DR-38)*
*Considered:* Y-up world space with a flip at the camera (conventional in maths and in most 3D
work); row-vector row-major matrices (Handmade Hero style, arguably more readable in C).
*Why Y-down:* the framebuffer is Y-down, the mouse is Y-down, and the existing camera code is
already Y-down. A flip somewhere in the chain is a bug generator that buys nothing here — there is
no 3D reference material to agree with any more, which was DR-38's entire reason for choosing Y-up.
*Why column-vector column-major:* unchanged from DR-38 — it matches GLSL, so matrices survive a
future GL swap untransposed. Cost: `m[col*3 + row]` indexing, and `Mat3Mul(a, b)` means *apply `b`,
then `a`*.
*Why plain `float[9]` with no `__m128` members:* unchanged from DR-38, and still defensive. A
16-byte-aligned member inside a render command silently raises the command-stream alignment
contract from 8 to 16, and the failure mode is a desynced command walk — not worth it for a SIMD
pass that does not exist yet.

**DR-44: The level is a flat tile grid with `type` + `flags`; no height channel.**
*(successor to DR-39)*
*Considered:* (a) keep the heightfield and use height for walls; (b) flat grid with flags — chosen;
(c) wall segments on tile edges rather than wall tiles.
*Why:* the game has no elevation gameplay — Door Kickers has none within a floor, and multi-story
is handled as separate floors. Height would be a channel that only ever holds 0 or "wall", which
is a flag. (c) is what a floorplan editor would do and gives thinner, prettier walls, but it doubles
the adjacency logic in every consumer (pathfinding, LoS, noise) for a rendering benefit — rejected
on the good-enough principle. `flags` (blocks-movement / blocks-sight / is-door) is what rules code
actually queries, and keeping those as bits rather than deriving them from `type` means a rule
never needs the `TileType` table in its inner loop.

**DR-45: The simulation advances on a fixed 60 Hz tick; pause and time-scale are app-side.**
*(extends DR-34)*
*Considered:* (a) variable timestep with `dt` crossing the seam; (b) fixed tick, platform owns
pause; (c) fixed tick, app owns pause and time-scale — chosen.
*Why:* (a) is the obvious thing to reach for in a real-time game and is exactly wrong here — it
destroys DR-31's record/replay determinism, which a plan/execute game needs *more* than a
turn-based one did. (b) would make the platform learn a gameplay concept, which is the one thing
the seam exists to prevent. (c) keeps `AppUpdate` a pure fixed tick: the platform calls it at a
fixed rate and the app decides how many simulation steps that call advances — zero when paused, two
at double speed.
*Current implementation:* the platform nails 60 Hz with sleep + spinlock and calls `AppUpdate` once
per frame, so tick == frame. *Exit path when frames drop:* the platform gains a fixed-step
accumulator and calls `AppUpdate` 0..N times per rendered frame; nothing in the app changes. Not
building it until a dropped frame is observed.

**DR-46: Navigation is grid-based; unit positions are continuous.**
*Considered:* (a) fully grid-locked positions (the DR-12 simplification — spatial state is two
integers, collision is a lookup); (b) grid navigation with continuous interpolated positions —
chosen; (c) fully continuous with nav-mesh or raw geometry collision, no grid.
*Why:* (a) makes a swarm read as a conga line — dozens of agents stepping in lockstep through a
corridor is exactly the wrong visual for the core fantasy — and it fits Door Kickers' continuous
aim/reaction timing badly. (c) is the most faithful to the reference but discards the grid, and
with it the 2D algorithms that make procedural floorplan generation and flow-field pathfinding
tractable. (b) keeps every *query* on the grid (pathfinding, LoS, cover, noise) while positions
interpolate along the resulting path.
*Accepted costs:* unit-vs-unit separation becomes a continuous problem (boids-style steering rather
than an occupancy lookup); positions are floats, so serialization is floats; and rendering must be
sub-pixel correct, which is a direct input to DR-41's argument that the 2D rasterizer stays deep.

**DR-47: Textures are owned by the platform and referenced by handle.**
*Considered:* (a) app loads PNGs — impossible, violates DR-31; (b) platform loads at startup, app
references by an ID from an enum in `shared.h` — chosen; (c) build the full I/O request/result
mechanism now so the app can request assets as data.
*Why:* (c) is the eventual right answer and is already deferred (DR-31) until a save system forces
it; building it for M1 would be designing three milestones ahead. (b) is the minimum that respects
the seam: the app names a texture, never a path, never a pointer — which is DR-23's "names in
source, indices at runtime" applied to assets. The texture table is a small fixed array in the
platform, populated at startup from a compiled-in list.
*Requires:* vendoring `stb_image.h` into `lib/` — SDL's internal copy at `lib/SDL/src/video/` is
SDL's, not ours to call.

**DR-48: Noise is the information currency.**
*Considered:* (a) Door Kickers-faithful — individual enemies with facing and reaction time, tension
from angles and clean room-clearing; (b) *Aliens*-faithful — waves, chokepoints, ammo and position
management; (c) a noise-driven hybrid — chosen.
*Why:* (a) and (b) each discard half of what makes the concept interesting — (a) leaves the swarm
as flavour with no mechanical role, (b) leaves the planning layer with little to plan. In (c) the
crew keep Door Kickers' verbs, but what those verbs *cost* is attention from a flow that responds
to sound: a loud breach draws it, welding a door buys time at the price of a route, a motion ping
tells you something is coming but not what. It also gives the swarm a mechanical reason to exist,
which is what justifies flow-field pathfinding and the performance work downstream.
*Status:* a settled direction, not a mechanic. Noise propagation, the verb set, and swarm response
are all undesigned — see §7.

---

## 11. Standing Rules

1. **The milestone exit criterion is the tiebreaker — against fascination, not impatience.** When a
   system clears its acceptance bar, ship the exit; the deferred depth is already scheduled.
2. Game code never thinks in physical pixels, and never computes a screen coordinate.
3. Everything that persists lives in flat, serializable state. Integer indices, not pointers.
4. **The app makes zero OS calls.** All outside data enters as inputs or delivered results; all
   outside effects leave as command/request buffers. (DR-31.)
5. Names in source, indices at runtime, names in saves.
6. Every new `RenderCmd*` struct goes into `LIST_RENDER_COMMANDS`.
7. Only the current milestone is designed to build depth. Do not design past the next one — and
   when something past that horizon needs deciding, it goes in §7's open register, not into a
   speculative design.
