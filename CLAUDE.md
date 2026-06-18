# Software Rasterizer Workbench

A C++ software rasterizer built on a hot-reloadable engine/app split. The old attempt lives in `old/` and can be referenced for code to port forward.

## Architecture

Two compilation units with a clean boundary defined in `src/platform.h`:

| Layer | Source | Output | Role |
|---|---|---|---|
| Platform | `src/main.cpp` | `build/main.exe` | Window, SDL3, DLL loading, blit to screen |
| App | `src/app.cpp` | `build/app.dll` | Logic + fills the offscreen pixel buffer |

The app exports a single function: `AppUpdate(AppOffscreenBuffer* buffer)`. It updates logic and writes pixels — it does not call any rendering API. The platform layer blits the buffer to the screen each frame.

Hot-reload: the platform layer watches `app.dll`'s write time each frame. When it changes, it unloads the old DLL and loads a fresh copy (via a temp DLL so the compiler can overwrite the source).

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

- The app layer (`app.cpp`) must not link spdlog. If logging is needed in the app, it goes through a function pointer passed via `platform.h` (see `old/shared.h` `PlatformLogFn` for the pattern).
- `src/platform.h` is the only file shared across the DLL boundary. Keep it free of platform-specific headers.
- Tests live in `tests/*.cpp` and are compiled with exceptions enabled (`-EHsc`) to satisfy GoogleTest. The main build uses `-EHsc-`.
