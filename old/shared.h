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

// Struct to hold the loaded file data
struct DebugReadFileResult
{
    size_t contentsSize;
    void* contents;
};

// A decoded image. Pixels are stored as 0xAARRGGBB (ARGB, 32-bit little-endian).
// pixels == nullptr means the asset failed to load; the game should treat it as absent.
struct LoadedBitmap
{
    int32_t   width;
    int32_t   height;
    uint32_t* pixels;
};

// A tilemap with runtime-allocated layer data.
// All layers are stored in one contiguous arena block: [layer-0][layer-1][layer-2].
struct Map
{
    int32_t  width;
    int32_t  height;
    uint8_t* layers;   // Allocated via PushArray into permanentStorage
};

// Access tile (x, y) in layer L.  x is the column, y is the row.
#define MAP_TILE(mapPtr, layer, x, y) \
    (mapPtr)->layers[(layer) * (mapPtr)->width * (mapPtr)->height + (y) * (mapPtr)->width + (x)]

// ---------------------------------------------------------------------------------------------------------------------
//  Game Assets
//  Pre-loaded by the platform layer before the first game frame.
//  The game receives a pointer to this struct and reads from it —
//  no file paths, no loaders, no allocations inside the DLL.
//  To add a new asset: add a field here, load it in Platform_LoadAssets.
// ---------------------------------------------------------------------------------------------------------------------
struct GameAssets
{
    LoadedBitmap heroBitmap;
    LoadedBitmap tilesetBitmap;   // 6x4 grid, 32x32 px per tile
};

// --- Function Pointer Signatures ---
#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) DebugReadFileResult name(const char* filename)
typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void* memory)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);

#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) bool name(const char* filename, size_t memorySize, void* memory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);

struct GameButtonState
{
    int halfTransitionCount; // How many times the button changed state this frame
    bool endedDown;          // Is the button currently held down at the end of the frame?
};

struct GameControllerInput
{
    // A Union allows us to access the exact same block of memory in two different ways.
    // We can loop over the 'buttons' array, OR we can access them by name!
    union
    {
        GameButtonState buttons[9];
        struct
        {
            // Indices 0-3: directional movement (order must match buttons[])
            GameButtonState moveUp;
            GameButtonState moveDown;
            GameButtonState moveLeft;
            GameButtonState moveRight;
            // Indices 4-8: action buttons
            GameButtonState confirm;
            GameButtonState cancel;
            GameButtonState attack;
            GameButtonState openInventory;
            GameButtonState openMenu;
        };
    };
};

// Guard: named fields must cover exactly the same storage as buttons[].
static_assert(
    sizeof(GameControllerInput) == 9 * sizeof(GameButtonState),
    "GameControllerInput union size mismatch: buttons[] count does not match named fields"
);

struct GameMouseInput
{
    float x;           // Cursor X in framebuffer pixels (0 = left edge)
    float y;           // Cursor Y in framebuffer pixels (0 = top edge)
    float wheelDelta;  // Scroll wheel movement this frame (+ = up, - = down)
    GameButtonState left;
    GameButtonState right;
    GameButtonState middle;
};

// 3. The unified Input struct passed to the DLL
struct GameInput
{
    GameControllerInput controllers[1];
    GameMouseInput      mouse;
    float deltaTime;
    float frameTimeMs;  // Actual measured frame duration in milliseconds
};

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

#include "arena.h"

// The memory block passed to the game every frame
struct GameMemory
{
    MemoryArena permanentStorage; // Survives hot-reloads (Player stats, world data)
    MemoryArena transientStorage; // Cleared every frame (Frame-specific calculations)
    bool isInitialized;

    // The OS gives the Game Layer these function pointers!
    debug_platform_read_entire_file* DEBUG_ReadEntireFile;
    debug_platform_free_file_memory* DEBUG_FreeFileMemory;
    debug_platform_write_entire_file* DEBUG_WriteEntireFile;

    // Logging: OS owns the spdlog logger; DLL calls through this pointer.
    // Never NULL after OS initialisation — the OS sets a safe stub if needed.
    PlatformLogFn Log;
};

// --- The Audio Buffer Struct ---
struct GameAudioBuffer
{
    int samplesPerSecond; // Usually 48000 (48kHz)
    int sampleCount;      // How many samples we need to fill this frame
    int16_t* samples;     // The raw memory array we will write to
};

struct GameOffscreenBuffer
{
    void* memory; // The raw pixel data
    int width;
    int height;
    int pitch;    // The number of bytes in a single row of pixels
};

// Function pointer signature for the one-time game initialization (called before the main loop)
#define GAME_INIT(name) void name(GameMemory* memory)
typedef GAME_INIT(GameInitFn);

// Function pointer signature for the main game loop
#define GAME_RUN_FRAME(name) \
    void name(GameMemory* memory, GameInput* input, \
              GameOffscreenBuffer* buffer, GameAudioBuffer* audioBuffer, GameAssets* assets)
typedef GAME_RUN_FRAME(GameRunFrame);

// Allocates a single struct of a specific type
#define PushStruct(arena, type) (type*)PushSize(arena, sizeof(type))

// Allocates an array of a specific type
#define PushArray(arena, count, type) (type*)PushSize(arena, (count) * sizeof(type))

// Tell the compiler to pack this struct tightly (1-byte alignment)
#pragma pack(push, 1) 
struct BitmapHeader
{
    uint16_t fileType;     // Always 'BM' (0x4D42)
    uint32_t fileSize;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t bitmapOffset; // The byte index where the actual pixels start!
    uint32_t size;         // Size of this header
    int32_t width;         // Image width in pixels
    int32_t height;        // Image height in pixels
    uint16_t planes;
    uint16_t bitsPerPixel; // We want 32 (8 bits for A, R, G, B)
    uint32_t compression;  // We want 0 (Uncompressed)
    uint32_t sizeOfBitmap;
    int32_t horzResolution;
    int32_t vertResolution;
    uint32_t colorsUsed;
    uint32_t colorsImportant;
};
#pragma pack(pop) // Return to normal compiler packing rules
