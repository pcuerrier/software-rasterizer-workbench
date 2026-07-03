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
#include "arena.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/*struct AppOffscreenBuffer
{
    void*   memory;
    int32_t width;
    int32_t height;
    int32_t pitch; // bytes per row
};*/

// The memory block passed to the application every frame
struct AppMemory
{
    MemoryArena permanentStorage;      // Survives hot-reloads (Player stats, world data)
    MemoryArena transientStorage;      // Cleared every frame (Frame-specific calculations)
    MemoryArena renderCommandStorage;  // Render commands — platform resets used=0 before each frame
    bool isInitialized;
};

// ---------------------------------------------------------------------------------------------------------------------
//  User Input — The OS owns the input state and passes it to the DLL each frame.
// ---------------------------------------------------------------------------------------------------------------------

struct ControllerButtonState
{
    int transitionCount; // How many times the button changed state this frame
    bool endedDown;      // Is the button currently held down at the end of the frame?
};

struct ControllerInput
{
    union
    {
        ControllerButtonState buttons[9];
        struct
        {
            // Indices 0-3: directional movement (order must match buttons[])
            ControllerButtonState moveUp;
            ControllerButtonState moveDown;
            ControllerButtonState moveLeft;
            ControllerButtonState moveRight;
            // Indices 4-8: action buttons
            ControllerButtonState confirm;
            ControllerButtonState cancel;
            ControllerButtonState attack;
            ControllerButtonState openInventory;
            ControllerButtonState openMenu;
        };
    };
};

// Guard: named fields must cover exactly the same storage as buttons[].
static_assert(
    sizeof(ControllerInput) == 9 * sizeof(ControllerButtonState),
    "ControllerInput union size mismatch: buttons[] count does not match named fields"
);

struct MouseInput
{
    float x;           // Cursor X in framebuffer pixels (0 = left edge)
    float y;           // Cursor Y in framebuffer pixels (0 = top edge)
    float wheelDelta;  // Scroll wheel movement this frame (+ = up, - = down)
    ControllerButtonState left;
    ControllerButtonState right;
    ControllerButtonState middle;
};

// The unified Input struct passed to the DLL
struct UserInput
{
    ControllerInput controllers[1];
    MouseInput      mouse;
};

// ---------------------------------------------------------------------------------------------------------------------
//  Render Commands — The OS owns the render buffer and executes commands from the DLL.
// ---------------------------------------------------------------------------------------------------------------------

enum RenderCommandType : uint32_t
{
    RENDER_CMD_CLEAR,
    RENDER_CMD_RECT,
};

struct RenderCmdClear
{
    RenderCommandType type;
    uint32_t          colorARGB;
};

struct RenderCmdRect
{
    RenderCommandType type;
    int32_t           x, y, w, h;
    uint32_t          colorARGB;
};

struct RenderCommandBuffer
{
    MemoryArena* storage;
    int32_t      width;
    int32_t      height;
};

static inline void PushRenderCmdClear(RenderCommandBuffer* cmds, uint32_t colorARGB)
{
    RenderCmdClear* cmd = (RenderCmdClear*)PushSize(cmds->storage, sizeof(RenderCmdClear));
    cmd->type      = RENDER_CMD_CLEAR;
    cmd->colorARGB = colorARGB;
}

static inline void PushRenderCmdRect(RenderCommandBuffer* cmds,
                                     int32_t x, int32_t y, int32_t w, int32_t h,
                                     uint32_t colorARGB)
{
    RenderCmdRect* cmd = (RenderCmdRect*)PushSize(cmds->storage, sizeof(RenderCmdRect));
    cmd->type      = RENDER_CMD_RECT;
    cmd->x         = x;
    cmd->y         = y;
    cmd->w         = w;
    cmd->h         = h;
    cmd->colorARGB = colorARGB;
}

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
#define LOG_GAME(logFn, lvl, fmt, ...)                         \
    do {                                                         \
        char _log_buf[512];                                      \
        snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__);\
        if (logFn) {                                     \
            logFn((lvl), __FILE__, __LINE__, _log_buf);  \
        }                                                        \
    } while(0)

#define LOG_TRACE(logFn, fmt, ...)    LOG_GAME(logFn, PLATFORM_LOG_TRACE,    fmt, ##__VA_ARGS__)
#define LOG_DEBUG(logFn, fmt, ...)    LOG_GAME(logFn, PLATFORM_LOG_DEBUG,    fmt, ##__VA_ARGS__)
#define LOG_INFO(logFn, fmt, ...)     LOG_GAME(logFn, PLATFORM_LOG_INFO,     fmt, ##__VA_ARGS__)
#define LOG_WARN(logFn, fmt, ...)     LOG_GAME(logFn, PLATFORM_LOG_WARN,     fmt, ##__VA_ARGS__)
#define LOG_ERROR(logFn, fmt, ...)    LOG_GAME(logFn, PLATFORM_LOG_ERROR,    fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(logFn, fmt, ...) LOG_GAME(logFn, PLATFORM_LOG_CRITICAL, fmt, ##__VA_ARGS__)

// ---------------------------------------------------------------------------------------------------------------------
//  Hot Reload — The OS layer owns the DLL handle and function pointers.
//
//  The OS checks the source DLL's write-time each
//  frame and rebuilds this struct whenever a new compile drops.
// ---------------------------------------------------------------------------------------------------------------------

// Function pointer signature for the one-time game initialization (called before the main loop)
#define APP_INIT(name) void name(PlatformLogFn logFn, AppMemory& memory)
typedef APP_INIT(AppInitFn);

// Function pointer signature for the main game loop
#define APP_UPDATE(name) \
    void name(PlatformLogFn logFn, AppMemory& memory, UserInput& userInput, RenderCommandBuffer* render_cmds)
typedef APP_UPDATE(AppUpdateFn);