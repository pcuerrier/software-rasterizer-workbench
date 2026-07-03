#include "renderer.h"

void ResizeRenderBuffer(
    SDL_Renderer*  renderer,
    int            newWidth,
    int            newHeight,
    SDL_Texture**  outTexture,
    void**         outPixels,
    int*           outWidth,
    int*           outHeight,
    int*           outPitch)
{
    // Tear down old resources (safe on first call when both are null)
    if (*outTexture)
    {
        SDL_DestroyTexture(*outTexture);
        *outTexture = nullptr;
    }
    if (*outPixels)
    {
        SDL_free(*outPixels);
        *outPixels = nullptr;
    }

    *outWidth  = newWidth;
    *outHeight = newHeight;
    *outPitch  = newWidth * 4; // 4 bytes per pixel (ARGB8888)

    *outPixels  = SDL_malloc((size_t)(newWidth * newHeight * 4));
    *outTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        newWidth, newHeight
    );

    PLATFORM_LOG_INFO("Render buffer resized to %dx%d", newWidth, newHeight);
}

void Render(
    SDL_Renderer*             renderer,
    SDL_Texture*              texture,
    const AppOffscreenBuffer& buffer)
{
    void* texPixels = nullptr;
    int   texPitch  = 0;
    if (SDL_LockTexture(texture, NULL, &texPixels, &texPitch))
    {
        uint8_t* src = (uint8_t*)buffer.memory;
        uint8_t* dst = (uint8_t*)texPixels;
        for (int y = 0; y < buffer.height; ++y)
        {
            SDL_memcpy(dst, src, (size_t)(buffer.width * 4));
            src += buffer.pitch;
            dst += texPitch;
        }
        SDL_UnlockTexture(texture);
    }
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}