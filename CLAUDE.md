# CLAUDE.md

## What this project is

A classic Dragon Quest–style JRPG in C-style C++ with SDL3 — but the **primary goal is
learning and tinkering with low-level systems.** The custom software rasterizer, the
hand-rolled arena memory, the bytecode VM, and the audio mixer are *deliverables in
themselves*, not just means to ship a game. When advising, optimize for depth and
craft in these systems, not time-to-fun. Cadence is ~1 hour/day, solo.

The full living design doc is **`project-plan.md`** — architecture, milestones, and the
Decision Records (DR-01 … DR-30). This file is the thin spine; that file is the detail.
Don't duplicate it here.

## Hard constraints

- **C-style C++**: no STL, no exceptions, no RTTI (`-GR- -EHsc-`). Plain structs, flat
  arrays, integer indices — not pointers — as cross-references.
- **Custom software rasterizer.** Do not reach for SDL's GPU/2D renderer to shortcut it;
  writing the rasterizer is the point. OpenGL swap is a *later, opportunistic* option
  behind the render-command seam — not something to pull forward.
- **Arena / bump allocation only** (`src/arena.h`). No `malloc`/`new` in game code.
- **Three-layer hot-reload split**: platform `.exe` (owns SDL, window, DLL loading,
  final blit) → `app.dll` (`AppUpdate`, fills a render-command buffer, never touches
  pixels) → `renderer.dll` (planned; consumes commands, writes the framebuffer).
- **`src/shared.h` is the only header crossing the DLL boundary.** Keep it free of
  platform-specific includes. (Older notes call this `platform.h` — the real file is
  `shared.h`.) The DLLs never link spdlog; logging crosses the boundary via the
  `PlatformLogFn` function pointer and the `LOG_*` macros.
- **The app↔platform seam is absolute (the project's central experiment).** The `app.dll`
  makes **zero** OS calls — no audio, no file I/O, no clock query, no OS allocation.
  Everything the app needs from the outside arrives as *data* (frame `UserInput`, delivered
  I/O / asset results, a time delta + RNG seed); everything it wants done leaves as *data*
  (render commands, audio commands, I/O requests). The platform is the sole owner of every
  OS resource. This is not aesthetic: it's what makes hot-reload safe (no OS handle ever
  lives in app memory) and input record/replay possible. Never let a direct OS call leak
  into the app, even a temporary one. See DR-31; the I/O request/result mechanism itself is
  deferred to M7.

## Build / run / test

Run from the **repo root** (scripts create and `pushd` into `.\build`):

```
scripts\build.bat            REM Debug   -> build\main_debug.exe, app.dll
scripts\build.bat Release    REM Release -> build\main.exe
build\main_debug.exe         REM run (SDL3.dll is copied into build\)
```

`build.bat` builds app DLL → platform exe → tests, and **runs the test suite**
(`build\tests\run_tests.exe`) as its last step. Tests are GoogleTest from `tests\*.cpp`.
Debug defines `DEBUG`; test builds also define `ENGINE_DEBUG`. MSVC env auto-bootstraps
via `vcvarsall.bat` (hardcoded VS path in `scripts\shared_vars.bat` — update if it moves).

## Code conventions (match the existing code)

- Functions/types `PascalCase` (`PushRenderCmdRect`, `AppUpdate`, `MemoryArena`);
  fields `camelCase` (`transitionCount`, `colorARGB`); enum constants `SCREAMING_SNAKE`.
- Fixed-width integer types (`int32_t`, `uint32_t`), not `int`/`long`.
- DLL entry points: `extern "C" __declspec(dllexport)` via the `APP_*` signature macros
  in `shared.h`; keep exports in sync with the `-EXPORT:` flags in `build_app.bat`.
- The warning-disable block at the top of `shared.h` is deliberate; builds are `-WX`
  (warnings are errors) and `-Wall`. Don't introduce new warnings.
- Comments match the surrounding density and explain *why*, not *what*.

## How we work together

**Roles: the user writes the code; Claude guides and project-manages.** Do **not** make
changes to `src/**` or the build scripts unless the user explicitly asks. When implementation
is needed, guide step-by-step instead — which file, what to add, the reasoning, the gotchas —
and let the user write it. **Exception:** test files (`tests/**`), which Claude may write.
Claude continues to own the planning/design layer: `project-plan.md`, `CLAUDE.md`, and memory.
The point is that the user is here to learn the low-level systems by building them.

**Rolling-wave planning.** Only the **current** milestone is defined to build-depth; the
**next** milestone is lightly defined (just enough that the current one can't box it in);
everything further out stays as the sketch in `project-plan.md`. Do not design ahead of
that horizon. Milestones sharpen just-in-time as we reach them.

**Deep vs. good-enough systems.** Every system is one or the other:
- *Deep* (tinker freely — this is why the project exists): software rasterizer, memory,
  the script VM, the audio mixer/threading.
- *Good-enough* (exists only to make the deep systems reachable): menu widget, Tiled
  importer, shop UI, and similar glue. Hold the line hard here — resist gold-plating.

**Acceptance state per deep system.** When a milestone touches a deep system, record its
**acceptance bar for this milestone + what is explicitly deferred** (e.g. "M1 rasterizer:
correct clipped opaque tile blit; deferred: sprite alpha → M2, SIMD → later"). Deferring
is scheduling, not cutting a corner.

**The exit criterion is the tiebreaker — against *fascination*, not impatience.** The
real risk on this project is rabbit-holing forever, not losing motivation. When a system
clears its acceptance bar, ship the milestone exit and move on; the deferred depth is
already scheduled.

**Decisions get recorded.** New settled decisions are appended to `project-plan.md` as
Decision Records (DR-xx) with options considered + rationale.

## Testing

The seam gives three testable surfaces, each with its own technique:
- **Pure cores** (arena, later the VM, damage math, name resolver, save serialization) →
  ordinary unit tests. Highest ROI.
- **The app as a pure function** → drive `AppUpdate` over frames with synthetic `UserInput`
  + delivered results, then assert on the **emitted command buffers** and the resulting
  `AppMemory`. Tests game *features* headless — no SDL, no window, no clock.
- **The rasterizer** → golden framebuffer tests: feed a known command buffer into a raw
  pixel buffer, assert exact pixels (or a checksum). Deterministic rendering makes it exact.
- **Platform / SDL glue** (window, DLL loading, the presenter) → *not* unit-tested; verify
  by running (`/run`, `/verify`).

Principles:
- **Test the contract (outputs), not the implementation.** Deep systems get rewritten (the
  blit will get a SIMD pass); contract tests survive the rewrite and prove identical output.
- **A deep system's acceptance bar for a milestone includes its tests.** Good-enough glue
  gets a test only when a real bug appears — never for the sake of it.

Mechanics:
- **"Include the source" linkage:** test TUs `#include` the unit directly (header-only like
  `arena.h`, or a single `.cpp`). **One test TU per `.cpp`** (else the linker sees duplicate
  symbols); header-only units are immune. Switch to a static lib only if forced (see DR-32).
- `build.bat` builds `tests/*.cpp` (auto-globbed) and runs `run_tests.exe` as its last step.
  GoogleTest; `gtest_main` supplies `main()`. Running `build_tests.bat` alone fails with
  "`cl` not recognized" — only `build.bat` bootstraps MSVC.
- Debug-only `ASSERT` guards are tested with `#if DEBUG` death tests (`EXPECT_DEATH`), since
  `ASSERT` compiles to nothing in release. Pattern-setter suite: `tests/test_arena.cpp`.

## Skills: split knowledge into the seams as we grow

Our context should isolate the same way the code does. As systems mature, extract their
deep context (relevant DRs, current acceptance state, local conventions, how to exercise
them in isolation) into **dedicated skills, one per architectural seam** — so a session
working on the rasterizer loads rasterizer context and nothing else. A skill's
description is the router, the way an interface is for a function call.

**Not yet.** The project is currently small enough that `project-plan.md` + this file
hold everything; premature skills are premature abstraction. Create a skill for a seam
only when (a) the seam is real and stable, and (b) its context is large enough that
loading it every session is wasteful. Candidate future seams: **platform · renderer/
rasterizer · engine (mode-stack + VM) · field · battle · data pipeline · audio.** Until
then, the milestone/cross-cutting knowledge stays here in the spine, not in any one skill.
