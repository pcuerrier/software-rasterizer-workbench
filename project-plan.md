# Tactical Roguelike — Project Plan

**Project:** Turn-based tactical roguelike — a party of adventurers descending through dungeons
**Stack:** C-style C++ (no STL/exceptions/RTTI), SDL3, custom software **3D** rasterizer, custom memory management
**Cadence:** ~1 hour/day solo development
**Canonical exceptions to no-external-code:** stb_image (PNG loading), stb_vorbis (OGG streaming)

> **Scope pivot (2026-07-27).** This project was previously a classic 2D JRPG. The prior plan
> is preserved verbatim in `old-jrpg-project-plan.md`. No game code had been written, so the
> pivot cost was limited to the plan itself plus one dead render command. See **DR-35**.

---

## 1. Architecture Overview

Three layers, unchanged by the pivot:

- **Platform layer** (`main.exe`) — SDL3 init, window, input polling, DLL loading, framebuffer +
  depth buffer ownership, final upscale blit. Owns every OS resource.
- **App layer** (`app.dll`) — all game logic. Makes **zero** OS calls. Emits render commands.
- **Renderer** (`renderer.dll`, planned; currently `#include`d into the platform exe) — consumes
  render commands, writes pixels. The software 3D rasterizer lives here.

`src/shared.h` is the only header crossing the DLL boundary. It now includes `src/math.h`
(Vec3/Vec4/Mat4/Vertex), because both sides need the types: the app builds matrices and meshes,
the renderer transforms and rasterizes them.

**Engine/content split:** code implements systems; dungeons, abilities, and tables are data.

---

## 2. The Rasterizer (deep system — the project's centre of gravity)

The pivot moved this from a 2D blitter to a **software 3D triangle pipeline**. This is the
primary deliverable, not a means to an end.

### Pipeline shape

```
app builds:  Vertex[] (model space) + Mat4 modelToWorld  ──┐
             Mat4 worldToView + Mat4 viewToClip  ──────────┤ render commands
                                                            ▼
renderer:  modelToClip = viewToClip · worldToView · modelToWorld
           transform vertices → clip space (w == 1 under ortho)
           → NDC → screen coords
           → backface cull (screen-space signed area)
           → per-triangle: bbox clamp to framebuffer, edge-function fill
           → per-pixel: interpolate depth, z-test, write colour
```

**The transform lives in the renderer, not the app** (DR-36). The app never computes a screen
coordinate. This is what keeps the OpenGL swap a backend task rather than a rewrite, and it puts
transform + clipping — the interesting parts — on the deep side of the seam.

### Projection: orthographic (DR-37)

Chosen for the isometric look. It removes three things from the first pipeline version:

| Perspective would need | Ortho |
|---|---|
| `w`-divide per vertex | `w == 1`, no divide |
| Near-plane polygon clipping (mandatory — a triangle crossing the eye plane projects to infinity) | Nothing goes to infinity; bbox clamp suffices |
| Perspective-correct interpolation (`u/w, v/w, 1/w` per pixel) | Affine interpolation is **exact** |

Bonus: post-projection depth is **linear** in view depth, so a `float` z-buffer has uniform
precision throughout the frustum (no 1/z crunch near the far plane).

Isometric camera angles: yaw 45°, pitch `atan(1/√2)` ≈ 35.264° — the angle at which the three
cube axes project to equal lengths, 120° apart. That is what makes isometric look *true*.

### Conventions (DR-38 — settle once, never revisit)

- **Y-up, right-handed. The dungeon grid lies on the XZ plane.** Matches OpenGL and every 3D
  reference you will read.
- **Column-vector convention** (`v' = M·v`), **stored column-major**. Matches GLSL exactly, so
  matrices pass through the eventual GL swap untransposed. Cost: `m[col*4 + row]` indexing, and
  `Mat4Mul(a, b)` means *apply `b`, then `a`*.
- **Counter-clockwise winding is front-facing** (OpenGL default).
- **`Mat4` is plain `float[16]`. No `__m128` members.** A 16-byte-aligned member in a render
  command raises the command-stream alignment contract from 8 to 16 — don't take that on for a
  SIMD pass that doesn't exist yet.
- One cell spans 1.0 × 1.0 world units in XZ. One height step = 0.5 world units in Y.

### Render-command alignment contract (carried forward, now load-bearing)

`PushSize` aligns every allocation to 8 bytes; `FlushRenderCommands` walks the stream with
`at += sizeof(cmd)` and does **no** re-alignment. Writer and reader stay in lockstep only if
every `RenderCmd*` struct's `sizeof` is a multiple of 8. The `LIST_RENDER_COMMANDS` X-macro +
`static_assert` guards this at compile time. **Add every new command to that list** — the 3D
commands are the first ones where field reordering is likely, which is exactly when the guard
earns its keep.

---

## 3. Dungeon Representation (DR-39)

**Heightfield grid, with discrete floors.**

```
Cell         { uint8_t height; uint8_t type; }   // type indexes a TileType table
DungeonFloor { int32_t w, d; Cell* cells; }      // one level, w × d cells
```

- One floor loaded and rendered at a time. Stairs move between floors.
- `height` is discrete steps; `uint8_t` gives 0–255, which will never bind.
- Tactical rules read `height` directly: move cost from height delta, line of sight from height
  comparison along a ray, elevation bonus from the difference. All integer, all per-column, all
  trivially serializable.

### Mesh build

For each cell `(x, z)` at height `h`:
1. Emit the **top quad** — 4 vertices at `y = h * HEIGHT_STEP`, spanning `[x, x+1] × [z, z+1]`.
2. For each of the 4 neighbours with `neighbour.height < h`, emit a **side quad** from the
   neighbour's height up to `h` on that edge. Out-of-bounds neighbours count as height 0, which
   closes the map's outer edge automatically.

Worst case 5 quads = 10 triangles per cell; typical dungeons are mostly flat and emit close to
one quad per cell. A 32×32 floor lands around 1000–3000 triangles. At 384×216 that is
comfortable — but **triangle setup, not fill, is the first bottleneck** in a low-resolution
software rasterizer. That is the number to watch.

Built once into permanent storage when a floor loads; rebuilt only when terrain changes.

**Known limit (accepted, with a known fix):** `uint16_t` indices cap a mesh at 65 535 vertices.
Worst-case geometry (5 quads/cell, no vertex sharing) hits that around a 57×57 floor; typical
geometry has far more headroom. When it binds, **widen the index type to `uint32_t`** — do not
split the mesh.

### Migration escape hatches (analysed, deliberately not taken)

Overhangs (walk over *and* under a bridge) are the one thing a heightfield cannot express. Two
exits exist, in increasing cost:

1. **Span-based columns** — each column holds a few `{bottom, top}` pairs. Keeps 2D generation,
   modest mesh-builder change. The likely real migration path if overhangs are ever wanted.
2. **Full voxel occupancy** — buys arbitrary 3D, costs a redesign of dungeon generation (2D
   algorithms have no off-the-shelf 3D equivalent), a derived "standable surface" layer, 6-face
   mesh culling, and 3D DDA mouse picking.

**Migration cost is time-dependent, and the deadline is not where you'd guess.** The rasterizer,
render commands, math, and camera cost **zero** in any migration — the command seam fully
insulates the deep system. The cost lives entirely in the tactics layer. Switching during M1 is
roughly a week; switching after movement, LoS, targeting and AI exist is 3–6 weeks, most of it
design. **Therefore: the decision point is before movement and line of sight, not before the
mesh builder.** M1 does not commit us.

**Insulation to build when the first gameplay rule appears** (not before): a narrow accessor set
(`CellHeightAt`, `IsWalkable`, `NeighborsOf`) that rules code uses instead of touching `cells[]`.
It converts a diffuse rewrite into a localized one — but note it cannot hide the semantic change:
a heightfield guarantees *one standable surface per column*, and spans/voxels do not. Any rule
that assumes uniqueness ("the cell in front of me", occupancy keyed by `(x, z)`, pathfinding node
identity) needs revisiting on migration regardless.

---

## 4. Tactics Layer (sketch — do not design ahead)

Deliberately thin at this horizon. What is known:

- Grid-locked positions and turn-based resolution — the genre's defining simplification, and the
  same reason DR-12 was right for the old plan: spatial state is a couple of integers, collision
  is a lookup, everything is serializable.
- Height is tactically meaningful (move cost, line of sight, elevation advantage) — that is the
  whole reason for the heightfield.
- The **uniform action model** from DR-17 survives well: actor + action type + target(s) +
  resolution fn, shared between player and AI.

Everything else — turn order, ability schema, AI, the run loop, permadeath and meta-progression —
stays a sketch until its milestone arrives.

---

## 5. Systems Classification

**Deep** (tinker freely — this is why the project exists):
software 3D rasterizer · memory · script VM · audio mixer/threading

**Good-enough** (exists only to make the deep systems reachable):
menu/widget glue · dungeon generation plumbing · UI panels · data importers

---

## 6. Milestones (rolling wave)

Only M1 is defined to build depth. M2 is defined just enough that M1 cannot box it in.
Everything past M2 is a sketch and will sharpen just-in-time.

### M1 — A dungeon on screen ← **current**

**Exit criterion:** an orthographic 3D view of a hand-authored heightfield dungeon floor,
z-buffered and flat-shaded, rendered by our own triangle rasterizer.

*Locked:* orthographic projection (DR-37) · conventions per DR-38 · heightfield geometry (DR-39) ·
hard-coded camera matrix, no `Camera` struct yet (DR-40) · flat shading, no textures · internal
resolution 384×216 (DR-33) · fixed 60 Hz tick, no `dt` (DR-34) · floor compiled in, no file I/O
(DR-31).

**Systems touched**

| System | Class | M1 role |
|---|---|---|
| Rasterizer | **Deep** (M1's core) | 2D blitter → 3D triangle pipeline with z-buffer |
| Math (`math.h`) | Pure core | Vec3/Vec4/Mat4, ortho + look-at construction |
| Dungeon mesh builder | Good-enough | Heightfield → vertex/index buffers |
| Platform | Minimal | Depth buffer allocation + clear |

**Rasterizer — acceptance bar for M1**
- Full transform chain `modelToWorld → worldToView → viewToClip`, column-vector column-major.
- Backface culling from screen-space signed area, CCW front-facing.
- Triangle fill via edge functions, bounding box clamped to the framebuffer.
- **No gaps between adjacent triangles.** Use inclusive edge tests (`>= 0`), which produces
  double-coverage on shared edges rather than gaps. Double-coverage is invisible under opaque
  z-buffered flat shading; gaps are immediately visible — and every quad in the mesh is two
  triangles sharing an edge, so this shows up on frame one.
- Float z-buffer, per-pixel interpolated depth, `<` test.
- Flat shading from the provoking (first) vertex.
- **Golden framebuffer test:** known mesh + known matrices → exact pixels.

*Deferred (scheduled, not cut):* the **top-left fill rule** (the correct fix for shared-edge
double-coverage — needed once blending exists) · Gouraud interpolation · texturing (affine, exact
under ortho) · near/far plane clipping · perspective projection · sub-pixel precision via
fixed-point edge functions · lighting · SIMD · mesh-by-handle instead of pointer (at the
`renderer.dll` split).

**New `shared.h` render commands**

```
RENDER_CMD_SET_CAMERA
  { type; pad; Mat4 worldToView; Mat4 viewToClip; }              // 136 bytes

RENDER_CMD_MESH
  { type; vertexCount; indexCount; pad;
    Vertex* vertices; uint16_t* indices; Mat4 modelToWorld; }    // 96 bytes
```

Camera is its own command, held in a local by `FlushRenderCommands` while it walks the stream —
that is how GPUs work, and it avoids duplicating 128 bytes of matrix per mesh. Pointer-in-command
is fine while the renderer is `#include`d into the platform exe; it becomes a handle at the
`renderer.dll` split (deferred). **Both must be added to `LIST_RENDER_COMMANDS`.**

**Implementation order** (user writes `src/**`; Claude writes `tests/**`)

The order matters more than usual here: debugging a 3D pipeline against a 2000-triangle mesh is
misery, against one triangle it is tractable. **Get pixels from a single hard-coded triangle
before the mesh builder exists.**

1. **`src/math.h`** — `Vec3`, `Vec4`, `Mat4`, `Vertex`; `Mat4Identity`, `Mat4Mul`, `Mat4Ortho`,
   `Mat4LookAt`, basic Vec3 ops. Include it from `shared.h`.
   → *Claude writes `tests/test_math.cpp`* (pure core, highest ROI: multiply order, ortho mapping
   of known points, look-at basis orthonormality).
2. **`shared.h`** — the two command structs, their push helpers, X-macro entries.
3. **Platform** — allocate the depth buffer alongside the render buffer in `main.cpp`; clear it
   each frame (simplest: have `RENDER_CMD_CLEAR` clear both).
4. **Rasterizer** — transform, cull, fill, z-test, driven by **one hard-coded triangle** pushed
   from `AppUpdate`. *The deep work.* **First pixels.**
   - *Debugging tips:* get the transform right before the fill — log or plot projected vertex
     positions as single pixels first. Then **disable backface culling until geometry appears**;
     a mesh that is mirrored or inside-out renders as nothing at all with culling on, and that
     failure looks identical to "the transform is broken".
5. → *Claude writes the golden framebuffer test.*
6. **App** — heightfield structs, a small hand-authored floor (compiled in), the mesh builder in
   `AppInit` writing into permanent storage; `AppUpdate` pushes clear + set-camera + mesh.
7. **Verify the exit:** run it, look at the dungeon.

### M2 — The grid becomes interactive (lightly defined)

Orbit camera as a real struct (fixed pitch, yaw snapping to 4 compass views with smooth
interpolation, adjustable zoom) · mouse picking screen → cell · cell highlight · one unit standing
on the grid. The heightfield/voxel decision point (DR-39) falls at the M1→M2 boundary.

*Claude owes the user a full explanation of orbit-camera construction — basis vectors, look-at,
why yaw/pitch/distance is stored instead of the matrix, how 90° snapping interpolates — when M2
sharpens.*

### Beyond M2 — sketch only

Tactics core (turn order, move range via height-cost flood fill, line of sight, attack
resolution) · procedural dungeon generation · party, abilities, progression · the roguelike run
loop and permadeath · script VM (deep; purpose to be defined — ability effects and/or dungeon
events) · audio command buffer + mixer (deep) · save/load of run state · data pipeline
consolidation, placed after its first real consumers exist.

---

## 7. Decision Records

Format: **Decision — options considered — why.** New decisions append here.
Numbering is continuous with the old plan: surviving DRs keep their original numbers so
references never rot. Full original rationale for carried-over DRs is in
`old-jrpg-project-plan.md`.

### Carried over unchanged

**DR-01: Modes as enum + tagged union with switch dispatch.** Explicit, debuggable, no hidden
allocation, fits no-RTTI C-style. Entire control flow visible in one place.

**DR-02: Overlay/replace via a per-entry `renders_below` flag; lower modes never update; input to
top only.** Eliminates a whole class of double-input bugs.

**DR-03: Mode communication via one result slot + push params.** No callbacks (hidden control
flow), no queue (only one pop can happen per frame — enforced invariant). Each mode is "a
function call stretched over many frames".

**DR-04: Scripted events via a tiny bytecode VM in shared state.** *Considered:* coroutines
(fights the C-style grain, opaque save state); hand-rolled state machines (misery past ~3 events).
*Why:* VM state is trivially serializable. **Its role in a tactical roguelike is undefined and
deferred** — it remains a deep-system deliverable, not a scheduled feature.

**DR-07: Render command buffer between game and backend.** *Considered:* immediate-mode direct
calls. *Why:* the software→OpenGL swap becomes an isolated backend task instead of a rewrite;
free bonuses are render order decoupled from logic order, frame capture, and headless testing.
**The pivot proved this one:** the 2D→3D change cost the seam nothing, and it is why any future
geometry-representation change costs the rasterizer nothing (DR-39).

**DR-08: Fixed low internal resolution + integer upscale.** ~16× fewer pixels filled per frame
and simple inner loops. **More valuable after the pivot, not less** — 384×216 ≈ 83k pixels is
what makes a z-buffered software triangle pipeline affordable at all.

**DR-09: Accepted steppy scrolling, with pre-built exit paths.** Tier 1: oversized buffer +
fractional camera at upscale. Tier 2: native-res backend, bundled with the OpenGL swap.

**DR-10: 32-bit RGBA; no palette format.** Palette mode complicates the GPU swap and alpha
effects; palette *aesthetics* come from authored assets anyway.

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
requests**, **reverse = events/results**. Concrete I/O request/result structs deferred until a
save system exists. Binding constraint until then: **no direct OS call may leak into the app,
even a temporary one** — M1's dungeon floor is compiled in, not `fopen`ed.

**DR-32: Test the contract at three seam-defined surfaces; "include the source" linkage; no
static test lib until forced.** Pure cores → unit tests. The app as a pure function → drive
`AppUpdate`, assert on emitted command buffers + `AppMemory`. The rasterizer → golden framebuffer
tests. Platform/SDL glue → not unit-tested, verified by running. Tests assert on **outputs, not
implementation**, so a deep system can be rewritten (a SIMD blit pass, a fixed-point rasterizer)
and the same golden test proves identical output. One test TU per `.cpp`.

**DR-33: Internal resolution is 384×216.** 16:9, consistent with DR-11's fixed-height/
variable-width model; ×5 → 1080p and ×10 → 2160p are exact.

**DR-34: Movement uses a fixed 60 Hz logical tick; no `dt` crosses the seam.** Keeps `AppUpdate`
a pure fixed tick, which is the record/replay payoff of DR-31. Decoupling sim from render later
means *adding interpolation*, not switching to a variable timestep.

### Retired by the pivot

- **DR-12–DR-15** (2D field: tile-to-tile movement, per-tileset tile properties, one map at a
  time, warps as built-in triggers). The *spirit* survives — grid-locked positions, properties per
  `TileType`, one floor loaded at a time, stairs instead of warps — but the 2D specifics are gone.
- **DR-16, DR-18** (JRPG battle sequencer, weighted-list enemy AI, small status list). A tactical
  system has a different shape; these will be re-decided in their own milestone.
- **DR-17** (uniform action model, data-driven effects) — **survives in spirit**, carried into the
  tactics-layer sketch rather than kept as a settled DR.
- **DR-20** (Tiled for maps). Dungeons are procedural; there is no hand-authored map pipeline to
  import.
- **DR-21** (script DSL assembled by a Python tool). Deferred with the VM's purpose.
- **DR-26** (saving only in field mode). Save semantics for a roguelike run are a different
  problem; to be re-decided.
- **DR-28–DR-30** (milestone sequencing, level curves as tables). Tied to the retired M1–M8 chain.

### New — the pivot

**DR-35: Pivot from classic 2D JRPG to 3D-rendered tactical roguelike.**
*Considered:* (a) finish the JRPG as planned; (b) pivot now; (c) pivot but keep 2D tilemap
rendering.
*Why:* no game code existed yet — only the platform layer, the seam, the arena, and a partial 2D
blitter — so the pivot cost was the plan document plus one dead render command
(`RENDER_CMD_TILEMAP_LAYER`, left in place while it still compiles, deleted when the mesh path
lands). Rejected (c) because rendering the dungeon in 3D is precisely where the rasterizer gets
interesting: a triangle pipeline with transform, culling, clipping and depth is a far richer deep
system than a tile blitter, which matches the project's stated primary goal of depth and craft in
low-level systems over time-to-fun. Everything below the app layer survived untouched — which is
itself the strongest available evidence that DR-07 and DR-31 were correct.

**DR-36: The 3D transform lives in the renderer, not the app.**
*Considered:* (a) app pushes model-space vertices + matrices, renderer transforms — chosen;
(b) app transforms to screen space and pushes 2D triangles; (c) app pushes semantic commands
("draw dungeon floor") and the renderer builds geometry.
*Why:* (b) kills the OpenGL swap outright — a GPU backend would receive already-projected
vertices and have nothing to do — violating DR-07's entire purpose, and it drags matrix math into
gameplay code. (c) bakes game concepts into the renderer, which is the wrong direction for a
system meant to stay general. (a) mirrors the GPU pipeline exactly, so what is learned transfers,
and it puts transform + clipping — the interesting parts — on the deep side of the seam. The app
still owns **mesh building**, because that is game data becoming geometry, not rendering.

**DR-37: Orthographic projection.**
*Considered:* perspective (more pipeline to write: `w`-divide, mandatory near-plane clipping,
perspective-correct interpolation — genuinely more learning); orthographic (chosen).
*Why (user-driven):* the game is meant to read as isometric, and ortho is what produces that look
honestly. Technical dividends are real: no `w`-divide, no near-plane clipping (nothing projects to
infinity, so a bbox clamp suffices), affine interpolation is *exact* rather than an approximation,
and post-projection depth is linear so `float` z-precision is uniform. Accepted cost: less
pipeline surface in version one. *Exit path:* the pipeline is otherwise identical — switching to
perspective is one matrix constructor plus the near-clip and perspective-correct interpolation
work, both already listed as deferred rasterizer items. Nothing about M1 forecloses it.

**DR-38: Math conventions — Y-up right-handed, grid on XZ; column-vector column-major; CCW front
faces; `Mat4` as plain `float[16]`.**
*Considered:* Z-up (common in tactics/CAD tooling); row-vector row-major (DirectX / Handmade Hero
style, arguably more readable in C).
*Why:* every convention here is chosen to match OpenGL, because the GL swap is a live plan and
because every 3D reference the user reads will agree with the code. Row-vector would read a little
better (`m[row][col]`) but would require transposing at the swap and disagrees with GLSL
documentation. The `float[16]`/no-SIMD rule is defensive: a 16-byte-aligned member inside a render
command raises the command-stream alignment contract from 8 to 16, silently, and the failure mode
is a desynced command walk — not worth it for a SIMD pass that does not exist.

**DR-39: Dungeon geometry is a heightfield grid with discrete floors.**
*Considered:* (a) flat floor + wall segments — cheapest, but no verticality, so no tactical
height; (b) heightfield — chosen; (c) heightfield + discrete stacked floors — chosen, as the
multi-level story; (d) full 3D voxel occupancy.
*Why:* the decisive axis is that a roguelike generates dungeons procedurally, and every
well-understood generation algorithm (BSP, rooms-and-corridors, cellular automata) is natively
2D. A heightfield is exactly "2D grid + a height channel", so generation stays on solved ground
while height still carries real tactical meaning (move cost, line of sight, elevation advantage).
Stacked floors supply the classic descend-the-dungeon structure without 3D occupancy.
Rejected (d): voxels buy **exactly one thing** the alternatives cannot fake — overhangs — and
cost a 3D generation redesign with no off-the-shelf answer, a derived "standable surface" layer
(which, absent overhangs, *is* a heightfield), 6-face mesh culling, and 3D DDA mouse picking in
place of a near-analytic inverse projection. The user confirmed overhangs are not wanted.
*Deliberately kept cheap to reverse:* the rasterizer, render commands, math and camera cost zero
in any migration — the seam insulates them completely — so the entire cost sits in the tactics
layer, and **the real deadline is before movement and line of sight, not before the mesh builder**
(≈1 week during M1; 3–6 weeks after the tactics layer exists). Escape hatches in cost order:
span-based columns first, full voxels only if that is insufficient.

**DR-40: M1 uses a hard-coded camera matrix; the orbit camera is deferred to M2.**
*Considered:* building the `Camera` struct (focus/yaw/pitch/distance + view-matrix derivation) as
part of M1.
*Why (user-driven):* the render command carries a `Mat4` either way, so whether that matrix came
from a struct or from constants is invisible to the renderer — the camera is therefore *not* on
M1's critical path, and adding it would mean debugging camera control and the triangle pipeline
simultaneously, with each failure looking like the other. M1 pins the camera so that anything
wrong on screen is the rasterizer's fault. The M2 form is settled in advance (orbit: fixed pitch,
yaw snapping to 4 compass views with smooth interpolation, adjustable zoom — FFT-style, chosen
because constrained yaw keeps occlusion problems bounded), so M1 cannot box it in.

---

## 8. Standing Rules

1. **The milestone exit criterion is the tiebreaker — against fascination, not impatience.** When
   a system clears its acceptance bar, ship the exit; the deferred depth is already scheduled.
2. Game code never thinks in physical pixels, and never computes a screen coordinate.
3. Everything that persists lives in flat, serializable state. Integer indices, not pointers.
4. **The app makes zero OS calls.** All outside data enters as inputs or delivered results; all
   outside effects leave as command/request buffers. (DR-31.)
5. Names in source, indices at runtime, names in saves.
6. Every new `RenderCmd*` struct goes into `LIST_RENDER_COMMANDS`.
7. Only the current milestone is designed to build depth. Do not design past the next one.
