#include "renderer.h"

static void ResizeRenderBuffer(
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
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        newWidth, newHeight
    );
    SDL_SetTextureScaleMode(*outTexture, SDL_SCALEMODE_NEAREST);

    PLATFORM_LOG_INFO("Render buffer resized to %dx%d", newWidth, newHeight);
}

static void Render(
    SDL_Renderer* renderer,
    SDL_Texture*  texture,
    void*         pixelBuffer,
    int           width,
    int           height,
    int           pitch,
    int           windowWidth,
    int           windowHeight)
{
    void* texPixels = nullptr;
    int   texPitch  = 0;
    if (SDL_LockTexture(texture, NULL, &texPixels, &texPitch))
    {
        uint8_t* src = (uint8_t*)pixelBuffer;
        uint8_t* dst = (uint8_t*)texPixels;
        for (int y = 0; y < height; ++y)
        {
            SDL_memcpy(dst, src, (size_t)(width * 4));
            src += pitch;
            dst += texPitch;
        }
        SDL_UnlockTexture(texture);
    }
    int scale_x = windowWidth / width;      // integer division
    int scale_y = windowHeight / height;
    int scale   = std::min(scale_x, scale_y);
    if (scale < 1) scale = 1;                 // window smaller than internal res
    SDL_FRect dst = {
        (float)(windowWidth - width * scale) / 2.0f,
        (float)(windowHeight - height * scale) / 2.0f,
        (float)(width * scale),
        (float)(height * scale)
    };
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}