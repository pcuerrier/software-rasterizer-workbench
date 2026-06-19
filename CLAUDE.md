# hot-reload-app-template

A C++ application template built on a hot-reloadable platform/app/renderer split. The old attempt lives in `old/` and can be referenced for code to port forward.

## Architecture

### Philosophy

The platform is the stable host. The app and renderer are **services** it loads — both are hot-reloadable DLLs with no knowledge of each other. All communication between them flows through types defined in `src/platform.h`, which is the only file shared across the DLL boundary.

```
Platform (main.exe)
  ├── hot-loads app.dll      → AppUpdate(input*, RenderCommands*)
  ├── hot-loads renderer.dll → RendererDraw(RenderCommands*, RenderBuffer*)
  └── blits RenderBuffer to screen via SDL
```

### Layers

| Layer | Source | Output | Role |
|---|---|---|---|
| Platform | `src/main.cpp` | `build/main.exe` | Window, SDL3, hot-loads both DLLs, blits to screen |
| App | `src/app.cpp` | `build/app.dll` | Updates logic, fills a `RenderCommands` buffer |
| Renderer | `src/renderer.cpp` | `build/renderer.dll` | Consumes `RenderCommands`, writes pixels to `RenderBuffer` |

### Data flow

1. Platform calls `AppUpdate` — app reads input, updates state, writes draw commands into `RenderCommands`
2. Platform calls `RendererDraw` — renderer reads `RenderCommands`, rasterizes into `RenderBuffer`
3. Platform blits `RenderBuffer` to the SDL window

The app never touches pixels. The renderer never touches input or game state. The platform owns the window and the memory that both services write into.

### Hot-reload

The platform watches each DLL's write time every frame. When a DLL changes on disk, it unloads the old one and loads a fresh copy via a temp DLL (so the compiler can overwrite the source file while the app is running). App and renderer can be recompiled and reloaded independently.

## Building

Requires MSVC (`cl.exe`) on PATH (run from a Visual Studio developer command prompt). SDL3, spdlog, and GoogleTest must be built first (see Dependencies below).

All scripts accept an optional `Debug` (default) or `Release` argument:

```bat
scripts\build.bat                  # Debug — all targets
scripts\build.bat Release          # Release — all targets
scripts\build_app.bat              # app DLL only (use during hot-reload dev)
scripts\build_platform.bat Release  # platform exe, release
scripts\build_tests.bat            # compiles and runs GoogleTest suite
```

Config differences (`scripts\shared_vars.bat`):

| | Debug | Release |
|---|---|---|
| Optimization | `-Od` | `-O2` |
| Debug info | `-Z7` | — |
| Defines | `-DENGINE_DEBUG -DDEBUG` | — |
| CRT (app DLL) | `-MTd` | `-MT` |
| CRT (tests) | `-MDd` | `-MD` |
| Lib paths | `*/Debug/` | `*/Release/` |

Output lands in `build/`. Run `build\main.exe` from the `build\` directory so it can find `app.dll`.

## Dependencies

Git submodules in `lib/`. After cloning, register them with:
```
git submodule update --init --recursive
```

| Library | Path | Used by |
|---|---|---|
| SDL3 | `lib/SDL` | Platform layer only |
| spdlog | `lib/spdlog` | Platform layer only |
| GoogleTest | `lib/googletest` | Test builds only |

Each must be built with CMake before the batch scripts will link. Build the config(s) you need (`Debug`, `Release`, or both):

```bat
cmake -B lib/SDL/build -S lib/SDL
cmake --build lib/SDL/build --config Debug
cmake --build lib/SDL/build --config Release

cmake -B lib/spdlog/build -S lib/spdlog
cmake --build lib/spdlog/build --config Debug
cmake --build lib/spdlog/build --config Release

cmake -B lib/googletest/build -S lib/googletest -G "Visual Studio 17 2022" -A x64
cmake --build lib/googletest/build --config Debug
cmake --build lib/googletest/build --config Release
```

## Key Design Rules

- `src/platform.h` is the only file shared across DLL boundaries. Keep it free of platform-specific headers.
- The app layer never writes pixels. The renderer layer never reads input or game state. All cross-DLL communication goes through structs in `platform.h`.
- The renderer is intentionally swappable — the first implementation is a software rasterizer, but any renderer that consumes `RenderCommands` and writes to `RenderBuffer` can replace it.
- Neither DLL may link spdlog. Logging in DLLs goes through a function pointer passed via `platform.h` (see `old/shared.h` `PlatformLogFn` for the pattern).
- Tests live in `tests/*.cpp` and are compiled with exceptions enabled (`-EHsc`) to satisfy GoogleTest. The main build uses `-EHsc-`.
