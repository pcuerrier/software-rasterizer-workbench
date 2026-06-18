#include "platform_image.h"

#pragma warning(push, 0)
#include <SDL3/SDL.h>

// Redirect QOI's internal heap allocations to SDL so we stay consistent
// with the rest of the platform layer.
#define QOI_MALLOC(sz) SDL_malloc(sz)
#define QOI_FREE(p)    SDL_free(p)
#define QOI_IMPLEMENTATION
#include "../../lib/qoi/qoi.h"
#pragma warning(pop)

// Internal helper — not exposed to the game layer.
static LoadedBitmap LoadQOIFile(const char* filename, MemoryArena* arena)
{
    PLATFORM_LOG_INFO("Loading image: %s", filename);
    LoadedBitmap result = {};

    size_t fileSize = 0;
    void* fileData = SDL_LoadFile(filename, &fileSize);
    if (!fileData)
    {
        PLATFORM_LOG_ERROR("Failed to load image file: %s", SDL_GetError());
        return result;
    }

    // Decode into a temporary heap buffer (freed below).
    // We always request 4 channels so the output is always RGBA.
    qoi_desc desc;
    void* decoded = qoi_decode(fileData, (int)fileSize, &desc, 4);
    SDL_free(fileData);

    if (!decoded)
    {
        PLATFORM_LOG_ERROR("Failed to decode QOI image: %s", filename);
        return result;
    }

    int32_t totalPixels = (int32_t)(desc.width * desc.height);
    result.width  = (int32_t)desc.width;
    result.height = (int32_t)desc.height;
    result.pixels = PushArray(arena, totalPixels, uint32_t);

    // QOI outputs [R, G, B, A] byte order.
    // DrawBitmap expects 0xAARRGGBB (ARGB as a 32-bit LE integer).
    // Swizzle into the arena — no extra allocation needed after this.
    uint8_t* src = (uint8_t*)decoded;
    for (int32_t i = 0; i < totalPixels; ++i)
    {
        uint8_t r = src[0];
        uint8_t g = src[1];
        uint8_t b = src[2];
        uint8_t a = src[3];
        result.pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                           ((uint32_t)g << 8)  |  (uint32_t)b;
        src += 4;
    }
    PLATFORM_LOG_INFO("Loaded image: %s (%dx%d)", filename, result.width, result.height);

    QOI_FREE(decoded);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
//  Asset loading — file paths live here, never in game code.
//  To add a new asset: add a field to GameAssets in shared.h,
//  then load it here.
// ---------------------------------------------------------------------------------------------------------------------
void Platform_LoadAssets(GameAssets* assets, MemoryArena* arena)
{
    assets->heroBitmap    = LoadQOIFile("assets/Male_01-1.qoi",  arena);
    assets->tilesetBitmap = LoadQOIFile("assets/tileset_01.qoi", arena);
}
