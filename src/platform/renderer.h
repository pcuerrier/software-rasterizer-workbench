#pragma once

#include "shared.h"

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#pragma warning(pop)

// ============================================================
//  Platform Renderer
//  Manages the offscreen pixel buffer and its paired streaming
//  texture. Call ResizeRenderBuffer once at startup and again
//  on every SDL_EVENT_WINDOW_RESIZED.
// ============================================================

static void ResizeRenderBuffer(
    SDL_Renderer*  renderer,
    int            newWidth,
    int            newHeight,
    SDL_Texture**  outTexture,
    void**         outPixels,
    int*           outWidth,
    int*           outHeight,
    int*           outPitch
);

static void Render(
    SDL_Renderer* renderer,
    SDL_Texture*  texture,
    void*         pixelBuffer,
    int           width,
    int           height,
    int           pitch,
    int           windowWidth,
    int           windowHeight
);
