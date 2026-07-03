#pragma once

#pragma warning(disable : 4100) // unreferenced formal parameter
#pragma warning(disable : 4189) // local variable is initialized but not referenced
#pragma warning(disable : 4191) // unsafe cast
#pragma warning(disable : 4201) // nonstandard extension used : nameless struct/union
#pragma warning(disable : 4505) // unreferenced local function has been removed
#pragma warning(disable : 4514) // 'function' : unreferenced inline function has been removed
#pragma warning(disable : 4820) // structure was padded due to alignment specifier
#pragma warning(disable : 5045) // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified

#include "debug.h"

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------------
//  Hot Reload — The OS layer owns the DLL handle and function pointers.
//
//  The OS checks the source DLL's write-time each
//  frame and rebuilds this struct whenever a new compile drops.
// ---------------------------------------------------------------------------------------------------------------------

struct AppOffscreenBuffer
{
    void*   memory;
    int32_t width;
    int32_t height;
    int32_t pitch; // bytes per row
};

// Function pointer signature for the one-time game initialization (called before the main loop)
#define APP_INIT(name) void name()
typedef APP_INIT(AppInitFn);

// Function pointer signature for the main game loop
#define APP_UPDATE(name) \
    void name(AppOffscreenBuffer* buffer)
typedef APP_UPDATE(AppUpdateFn);

// ---------------------------------------------------------------------------------------------------------------------
//  Logging — OS owns spdlog; DLL communicates through a function pointer.
//
//  The DLL never links spdlog. It formats its own message with snprintf,
//  then hands a plain C string across the DLL boundary via GameMemory->Log.
//  The OS side maps the level integer onto spdlog::level::level_enum and
//  preserves the original source location in the log output.
// ---------------------------------------------------------------------------------------------------------------------

// Mirror of spdlog::level so shared.h stays free of spdlog headers.
enum PlatformLogLevel
{
    PLATFORM_LOG_TRACE    = 0,
    PLATFORM_LOG_DEBUG    = 1,
    PLATFORM_LOG_INFO     = 2,
    PLATFORM_LOG_WARN     = 3,
    PLATFORM_LOG_ERROR    = 4,
    PLATFORM_LOG_CRITICAL = 5,
};

// The function signature the OS exposes to the DLL.
// `message` is a pre-formatted, null-terminated string.
typedef void (*PlatformLogFn)(int level, const char* file, int line, const char* message);

// ---------------------------------------------------------------------------------------------------------------------
//  DLL-side convenience macros
//  The DLL calls these; they format into a stack buffer then
//  invoke the OS function pointer — zero spdlog dependency in game code.
// ---------------------------------------------------------------------------------------------------------------------
#define LOG_GAME(memory, lvl, fmt, ...)                         \
    do {                                                         \
        char _log_buf[512];                                      \
        snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__);\
        if ((memory)->Log) {                                     \
            (memory)->Log((lvl), __FILE__, __LINE__, _log_buf);  \
        }                                                        \
    } while(0)

#define LOG_TRACE(memory, fmt, ...)    LOG_GAME(memory, PLATFORM_LOG_TRACE,    fmt, ##__VA_ARGS__)
#define LOG_DEBUG(memory, fmt, ...)    LOG_GAME(memory, PLATFORM_LOG_DEBUG,    fmt, ##__VA_ARGS__)
#define LOG_INFO(memory, fmt, ...)     LOG_GAME(memory, PLATFORM_LOG_INFO,     fmt, ##__VA_ARGS__)
#define LOG_WARN(memory, fmt, ...)     LOG_GAME(memory, PLATFORM_LOG_WARN,     fmt, ##__VA_ARGS__)
#define LOG_ERROR(memory, fmt, ...)    LOG_GAME(memory, PLATFORM_LOG_ERROR,    fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(memory, fmt, ...) LOG_GAME(memory, PLATFORM_LOG_CRITICAL, fmt, ##__VA_ARGS__)